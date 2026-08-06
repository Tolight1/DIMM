#include "DIMM.h"

#include "CameraManager.h"
#include "DimmRuntimeHelpers.h"
#include "ImageProcessor.h"
#include "PathUtils.h"
#include "SettingsDialog.h"

#include <algorithm>
#include <cmath>

#include <QDateTime>
#include <QPointF>
#include <QSize>
#include <QVector>

void DIMM::onStartSimulation()
{
    if (m_captureState == CaptureState::Simulation) {
        stopSimulationCapture();
        m_reporting = false;
        if (m_reportTimer) {
            m_reportTimer->stop();
        }
        updateCaptureState(CaptureState::Idle);
        setStatusMessage(QStringLiteral("状态: 模拟采集已停止"), UiStatusLevel::Warning);
        return;
    }

    if (m_captureState == CaptureState::Live) {
        noteManualAutoAcquisitionStopIfNeeded();
        stopLiveCapture();
    }

    m_reporting = false;
    if (m_reportTimer) {
        m_reportTimer->stop();
    }

    if (startSimulationCapture()) {
        updateCaptureState(CaptureState::Simulation);
        setDetailViewMode(DetailViewMode::RoiOnly);
        setStatusMessage(QStringLiteral("状态: 模拟采集"), UiStatusLevel::Info);
        return;
    }

    updateCaptureState(CaptureState::Idle);
    setStatusMessage(QStringLiteral("状态: 启动模拟采集失败"), UiStatusLevel::Error);
}

void DIMM::stopSimulationCapture()
{
    if (m_simulationTimer) {
        m_simulationTimer->stop();
    }
}

bool DIMM::startSimulationCapture()
{
    stopSimulationCapture();
    closeResultFile();
    resetMeasurementState();
    auto& runtime = runtimeForState(CaptureState::Simulation);
    runtime.simulationFrameIndex = 0;
    runtime.lastSimulationPreviewFrame = -1;
    runtime.frameSize[0] = QSize(kSimulationFrameSize, kSimulationFrameSize);
    runtime.frameSize[1] = QSize(kSimulationFrameSize, kSimulationFrameSize);
    onUpdateSimulation();
    if (m_simulationTimer) {
        m_simulationTimer->start();
    }
    return true;
}

cv::Mat DIMM::buildSimulationFrame(int cameraIndex) const
{
    cv::Mat frame(kSimulationFrameSize, kSimulationFrameSize, CV_8UC1, cv::Scalar(6));

    const double timeSeconds =
        (static_cast<double>(runtimeForState(CaptureState::Simulation).simulationFrameIndex) *
         kSimulationFrameIntervalMs) /
        1000.0;
    const double baseX = kSimulationFrameSize * 0.5;
    const double baseY = kSimulationFrameSize * 0.5;

    constexpr double kBeijingLatitudeDeg = 39.9042;
    constexpr double kPolarisDeclinationDeg = 89.366;
    constexpr double kSiderealDaySeconds = 86164.0905;
    constexpr double kSimulationPixelSizeM = 2.5e-6;
    constexpr double kSimulationFocalLengthM = 2.69;
    const double pixelPerRadian = kSimulationFocalLengthM / kSimulationPixelSizeM;
    const double latitudeRad = kBeijingLatitudeDeg * kPi / 180.0;
    const double declinationRad = kPolarisDeclinationDeg * kPi / 180.0;
    const double startHourAngle = -0.35;
    const double hourAngle = startHourAngle + (2.0 * kPi * timeSeconds / kSiderealDaySeconds);

    const auto projectedPolaris = [&](double h) {
        const double east = std::cos(declinationRad) * std::sin(h);
        const double north =
            std::cos(latitudeRad) * std::sin(declinationRad) -
            std::sin(latitudeRad) * std::cos(declinationRad) * std::cos(h);
        return QPointF(east, north);
    };

    const QPointF startProjection = projectedPolaris(startHourAngle);
    const QPointF currentProjection = projectedPolaris(hourAngle);
    const double skyMotionX = (currentProjection.x() - startProjection.x()) * pixelPerRadian;
    const double skyMotionY = -(currentProjection.y() - startProjection.y()) * pixelPerRadian;

    const double commonJitterX = 0.35 * std::sin((2.0 * kPi / 6.0) * timeSeconds + 0.2);
    const double commonJitterY = 0.30 * std::cos((2.0 * kPi / 7.5) * timeSeconds + 0.5);

    const int frameIndex = runtimeForState(CaptureState::Simulation).simulationFrameIndex;
    const double slowDifferentialX = 0.35 * std::sin((2.0 * kPi / 5.5) * timeSeconds + 0.8);
    const double slowDifferentialY = 0.35 * std::cos((2.0 * kPi / 6.3) * timeSeconds + 1.1);
    const double seeingNoiseX =
        0.95 * deterministicUnitNoise(frameIndex, 11) +
        0.45 * deterministicUnitNoise(frameIndex / 3, 17) +
        slowDifferentialX;
    const double seeingNoiseY =
        1.10 * deterministicUnitNoise(frameIndex, 23) +
        0.50 * deterministicUnitNoise(frameIndex / 3, 29) +
        slowDifferentialY;
    const double differentialSign = cameraIndex == 0 ? -0.5 : 0.5;
    const double differentialX = differentialSign * seeingNoiseX;
    const double differentialY = differentialSign * seeingNoiseY;

    const double centerX = baseX + skyMotionX + commonJitterX + differentialX;
    const double centerY = baseY + skyMotionY + commonJitterY + differentialY;
    const double amplitude = 220.0 + 10.0 * std::sin((2.0 * kPi / 20.0) * timeSeconds + cameraIndex * 0.4);

    auto stampSpot = [&frame](double cx, double cy, double peak, double sigma) {
        const int minX = qMax(0, static_cast<int>(std::floor(cx - 4.0 * sigma)));
        const int maxX = qMin(frame.cols - 1, static_cast<int>(std::ceil(cx + 4.0 * sigma)));
        const int minY = qMax(0, static_cast<int>(std::floor(cy - 4.0 * sigma)));
        const int maxY = qMin(frame.rows - 1, static_cast<int>(std::ceil(cy + 4.0 * sigma)));

        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                const double dx = x - cx;
                const double dy = y - cy;
                const double value = peak * std::exp(-(dx * dx + dy * dy) / (2.0 * sigma * sigma));
                const int blended = qBound(0, static_cast<int>(frame.at<uchar>(y, x) + value), 255);
                frame.at<uchar>(y, x) = static_cast<uchar>(blended);
            }
        }
    };

    // Match the real sensor view better: a compact star core with a soft halo.
    stampSpot(centerX, centerY, amplitude, 2.4);
    stampSpot(centerX, centerY, amplitude * 0.18, 5.4);

    return frame;
}

void DIMM::onUpdateSimulation()
{
    if (m_captureState != CaptureState::Simulation) {
        return;
    }

    auto& runtime = activeRuntime();
    ++runtime.simulationFrameIndex;
    ++runtime.frameCount;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const int previewIntervalFrames = std::max(1, kSimulationPreviewIntervalMs / kSimulationFrameIntervalMs);
    const bool shouldRefreshPreview =
        runtime.lastSimulationPreviewFrame < 0 ||
        (runtime.simulationFrameIndex - runtime.lastSimulationPreviewFrame) >= previewIntervalFrames;
    cv::Mat previewFrame0;
    cv::Mat previewFrame1;
    if (shouldRefreshPreview) {
        previewFrame0 = buildSimulationFrame(0);
        previewFrame1 = buildSimulationFrame(1);
        runtime.frameSize[0] = QSize(previewFrame0.cols, previewFrame0.rows);
        runtime.frameSize[1] = QSize(previewFrame1.cols, previewFrame1.rows);

        if (m_fullFrameCanvas1) {
            QVector<RoiRect> rois0;
            if (m_imageProcessor) {
                rois0.append(m_imageProcessor->getCurrentRoi(0));
            }
            m_fullFrameCanvas1->setImage(previewFrame0);
            m_fullFrameCanvas1->setRoiList(rois0);
            m_fullFrameCanvas1->setCurrentRoi(rois0.isEmpty() ? -1 : 0);
        }
        if (m_fullFrameCanvas2) {
            QVector<RoiRect> rois1;
            if (m_imageProcessor) {
                rois1.append(m_imageProcessor->getCurrentRoi(1));
            }
            m_fullFrameCanvas2->setImage(previewFrame1);
            m_fullFrameCanvas2->setRoiList(rois1);
            m_fullFrameCanvas2->setCurrentRoi(rois1.isEmpty() ? -1 : 0);
        }
        runtime.lastSimulationPreviewFrame = runtime.simulationFrameIndex;
    }

    if (m_imageProcessor) {
        for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
            cv::Mat simulationFrame =
                (cameraIndex == 0 && !previewFrame0.empty()) ? previewFrame0
                : (cameraIndex == 1 && !previewFrame1.empty()) ? previewFrame1
                : buildSimulationFrame(cameraIndex);
            runtime.frameSize[cameraIndex] =
                QSize(simulationFrame.cols, simulationFrame.rows);
            m_imageProcessor->processFrame(cameraIndex, simulationFrame);
        }
    }

    const bool shouldRefreshMeasurementUi =
        runtime.lastMeasurementUiUpdateMs < 0 ||
        (nowMs - runtime.lastMeasurementUiUpdateMs) >= kMeasurementUiIntervalMs;
    if (shouldRefreshMeasurementUi) {
        runtime.lastMeasurementUiUpdateMs = nowMs;
        refreshMeasurementUi();
    }
}
