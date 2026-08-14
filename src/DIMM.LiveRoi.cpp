#include "DIMM.h"

#include "CameraManager.h"
#include "CanvasWidgets.h"
#include "DimmRuntimeHelpers.h"
#include "FullFrameStarDetector.h"
#include "ImageProcessor.h"
#include "LivePreviewPolicy.h"
#include "PathUtils.h"
#include "PolarisDetectionPipeline.h"
#include "PulseGeneratorManager.h"
#include "SettingsDialog.h"

#include <algorithm>
#include <cmath>

#include <QDateTime>
#include <QLabel>
#include <QPointF>

using PolarisDetectionPipeline::InitialStarCandidate;
using PolarisDetectionPipeline::InitialStarSelection;

namespace {

LivePreviewPolicy::StartupPhase livePreviewStartupPhase(DIMM::LiveStartupPhase phase)
{
    switch (phase) {
    case DIMM::LiveStartupPhase::LocatePair:
        return LivePreviewPolicy::StartupPhase::LocatePair;
    case DIMM::LiveStartupPhase::Tracking:
        return LivePreviewPolicy::StartupPhase::Tracking;
    case DIMM::LiveStartupPhase::None:
    default:
        return LivePreviewPolicy::StartupPhase::None;
    }
}

} // namespace

QVector<InitialStarCandidate> DIMM::stabilizeInitialCandidates(
    int cameraIndex,
    const QVector<InitialStarCandidate>& candidates)
{
    if (!isValidCameraIndex(cameraIndex)) {
        return candidates;
    }
    return m_stableCandidateTrackers[cameraIndex].update(candidates);
}

void DIMM::clearStableCandidateTrackers()
{
    for (int cameraIndex = 0; cameraIndex < kCameraCount; ++cameraIndex) {
        m_stableCandidateTrackers[cameraIndex].clear();
    }
}

bool DIMM::isCentroidNearCurrentRoiEdge(int cameraIndex, double x, double y) const
{
    if (!m_imageProcessor || cameraIndex < 0 || cameraIndex >= 2) {
        return false;
    }

    const RoiRect roi = m_imageProcessor->getCurrentRoi(cameraIndex);
    const double localX = x - static_cast<double>(roi.x);
    const double localY = y - static_cast<double>(roi.y);
    return localX <= static_cast<double>(kRoiEdgeUpdateMarginPx) ||
           localY <= static_cast<double>(kRoiEdgeUpdateMarginPx) ||
           localX >= static_cast<double>(roi.w - 1 - kRoiEdgeUpdateMarginPx) ||
           localY >= static_cast<double>(roi.h - 1 - kRoiEdgeUpdateMarginPx);
}

bool DIMM::isCentroidTooFarFromCurrentRoiCenter(int cameraIndex) const
{
    if (!m_imageProcessor || cameraIndex < 0 || cameraIndex >= 2) {
        return false;
    }

    const auto& runtime = activeRuntime();
    if (!runtime.hasValidCentroid[cameraIndex]) {
        return false;
    }

    const RoiRect roi = m_imageProcessor->getCurrentRoi(cameraIndex);
    const double localX = runtime.centroidX[cameraIndex] - static_cast<double>(roi.x);
    const double localY = runtime.centroidY[cameraIndex] - static_cast<double>(roi.y);
    return localX <= m_roiRecenteringThresholdPx ||
           localY <= m_roiRecenteringThresholdPx ||
           localX >= static_cast<double>(roi.w - 1) - m_roiRecenteringThresholdPx ||
           localY >= static_cast<double>(roi.h - 1) - m_roiRecenteringThresholdPx;
}

bool DIMM::shouldUpdateRoiForRecentering()
{
    auto& runtime = activeRuntime();
    if (!hasValidCentroidsForRoiUpdate()) {
        runtime.roiRecenteringCandidateFrameCount = 0;
        return false;
    }

    const bool needsRecentering = isCentroidTooFarFromCurrentRoiCenter(0) ||
                                  isCentroidTooFarFromCurrentRoiCenter(1);
    if (!needsRecentering) {
        runtime.roiRecenteringCandidateFrameCount = 0;
        return false;
    }

    double maximumRoiRecenteringShift = 0.0;
    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        const RoiRect currentRoi = m_imageProcessor->getCurrentRoi(cameraIndex);
        const RoiRect targetRoi = buildCameraCentroidRoi(cameraIndex);
        maximumRoiRecenteringShift =
            std::max(maximumRoiRecenteringShift,
                     std::hypot(static_cast<double>(targetRoi.x - currentRoi.x),
                                static_cast<double>(targetRoi.y - currentRoi.y)));
    }
    if (maximumRoiRecenteringShift < m_roiRecenteringMinimumShiftPx) {
        runtime.roiRecenteringCandidateFrameCount = 0;
        return false;
    }

    ++runtime.roiRecenteringCandidateFrameCount;
    if (runtime.roiRecenteringCandidateFrameCount < m_roiRecenteringRequiredFrames) {
        return false;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_lastRoiUpdateMs >= 0 && (nowMs - m_lastRoiUpdateMs) < m_roiRecenteringCooldownMs) {
        return false;
    }

    return true;
}

void DIMM::requestLiveFullFrameRelocalization(const QString& reason)
{
    if (m_captureState != CaptureState::Live || !m_cameraManager) {
        return;
    }

    auto& runtime = activeRuntime();
    runtime.liveRelocalizationStartedMs = QDateTime::currentMSecsSinceEpoch();
    resetLiveFrameAcceptanceGates();
    QString switchReason;
    const bool fullFrameReady = applyLiveFullFrameForRelocalization(&switchReason);

    if (!fullFrameReady &&
        m_configTriggerMode != 0) {
        const QString detail =
            switchReason.isEmpty()
                ? QStringLiteral(
                      "切回全画幅重定位失败")
                : switchReason;

        handleHardwareTriggerStartupFailure(
            detail);

        return;
    }

    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        runtime.hasValidCentroid[cameraIndex] = false;
        runtime.lostCentroidFrameCount[cameraIndex] = 0;
        runtime.lostCentroidSinceMs[cameraIndex] = -1;
        runtime.initialRoiConfirmed[cameraIndex] = false;
        runtime.pendingInitialRoi[cameraIndex] = RoiRect();
        runtime.pendingInitialRoiReady[cameraIndex] = false;
        runtime.lastTargetPosition[cameraIndex] = QPointF();
        runtime.hasLastTargetPosition[cameraIndex] = false;
        runtime.lastLivePreviewUpdateMs[cameraIndex] = -1;
        runtime.liveRelocalizationPreviewFrame[cameraIndex].release();
    }
    if (m_cam1RoiCanvas) {
        m_cam1RoiCanvas->clear();
    }
    if (m_cam2RoiCanvas) {
        m_cam2RoiCanvas->clear();
    }
    if (ui->lblCam1ROICoord) {
        ui->lblCam1ROICoord->setText(QStringLiteral("(0.0, 0.0)"));
    }
    if (ui->lblCam2ROICoord) {
        ui->lblCam2ROICoord->setText(QStringLiteral("(0.0, 0.0)"));
    }
    ui->lblROITimeCurrent->setText(QStringLiteral("全画幅重定位"));
    ui->lblROITimeNext->setText(QStringLiteral("等待两路重新锁定 ROI"));

    m_liveHardwareRoiActive = false;
    m_liveStartupPhase = LiveStartupPhase::LocatePair;
    if (!fullFrameReady) {
        setStatusMessage(switchReason.isEmpty()
                             ? QStringLiteral("状态: 回全画幅重新定位失败")
                             : switchReason,
                         UiStatusLevel::Error);
    } else {
        setStatusMessage(reason.isEmpty()
                             ? QStringLiteral("状态: 已回到全画幅重新定位")
                             : reason,
                         UiStatusLevel::Warning);
    }
}

void DIMM::handleLiveRoiCentroidLoss(int cameraIndex)
{
    if (m_captureState != CaptureState::Live ||
        m_liveStartupPhase != LiveStartupPhase::Tracking ||
        !m_liveHardwareRoiActive ||
        cameraIndex < 0 ||
        cameraIndex >= 2) {
        return;
    }

    auto& runtime = activeRuntime();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (isAutoExposureRoiRelocalizationGraceActive(nowMs)) {
        if (cameraIndex >= 0 && cameraIndex < 2) {
            runtime.lostCentroidFrameCount[cameraIndex] = 0;
            runtime.lostCentroidSinceMs[cameraIndex] = -1;
        }
        setStatusMessage(QStringLiteral("自动曝光调整后等待 ROI 亮度稳定，暂缓全画幅重定位"),
                         UiStatusLevel::Warning);
        return;
    }
    if (runtime.lostCentroidFrameCount[cameraIndex] == 0 ||
        runtime.lostCentroidSinceMs[cameraIndex] < 0) {
        runtime.lostCentroidSinceMs[cameraIndex] = nowMs;
    }
    ++runtime.lostCentroidFrameCount[cameraIndex];
    if ((nowMs - runtime.lostCentroidSinceMs[cameraIndex]) < kLostCentroidRelocalizeTimeoutMs) {
        return;
    }

    if (m_imageProcessor) {
        m_imageProcessor->finalizeActiveAtmosphereWindow(
            QStringLiteral("相机%1星点丢失，未达到完整 r0 计算窗口").arg(cameraIndex + 1));
    }
    if (m_liveStartupOrigin == LiveStartupOrigin::AutoAcquisition) {
        m_autoAcquisitionRecovery.noteStarLost(nowMs);
    }
    requestLiveFullFrameRelocalization(
        QStringLiteral("状态: 相机%1星点离开 ROI，已切回全画幅重新定位")
            .arg(cameraIndex + 1));
}

bool DIMM::isUsableCentroidSample(int cameraIndex,
                                  double x,
                                  double y,
                                  double peakValue,
                                  double totalFlux,
                                  double background,
                                  double threshold,
                                  quint64 signalPixelCount,
                                  bool requireCentered) const
{
    if (cameraIndex < 0 || cameraIndex >= 2) {
        return false;
    }

    const auto& runtime = activeRuntime();
    const QSize frameSize = runtime.frameSize[cameraIndex].isValid() ? runtime.frameSize[cameraIndex] : QSize(5120, 5120);
    if (frameSize.width() <= 0 || frameSize.height() <= 0) {
        return false;
    }

    if (!(peakValue > threshold && peakValue > background + 4.0)) {
        return false;
    }
    if (signalPixelCount < 2 || signalPixelCount > 900) {
        return false;
    }
    if (totalFlux <= 80.0) {
        return false;
    }
    if (x < 0.0 || y < 0.0 || x >= frameSize.width() || y >= frameSize.height()) {
        return false;
    }

    if (requireCentered) {
        const int margin = kFixedRoiSize / 2;
        if (x < margin || y < margin ||
            x > static_cast<double>(frameSize.width() - margin) ||
            y > static_cast<double>(frameSize.height() - margin)) {
            return false;
        }
    }

    return true;
}

RoiRect DIMM::sanitizeRoi(const RoiRect& roi, int cameraIndex) const
{
    const int safeIndex = qBound(0, cameraIndex, 1);
    const auto& runtime = activeRuntime();
    const QSize frameSize = runtime.frameSize[safeIndex].isValid() ? runtime.frameSize[safeIndex] : QSize(5120, 5120);

    RoiRect clean = roi;
    clean.w = kFixedRoiSize;
    clean.h = kFixedRoiSize;

    const int frameWidth = qMax(clean.w, frameSize.width());
    const int frameHeight = qMax(clean.h, frameSize.height());
    const int maxX = qMax(0, frameWidth - clean.w);
    const int maxY = qMax(0, frameHeight - clean.h);

    clean.x = qBound(0, clean.x, maxX);
    clean.y = qBound(0, clean.y, maxY);
    return clean;
}

RoiRect DIMM::buildCameraCentroidRoi(int cameraIndex) const
{
    const int safeIndex = qBound(0, cameraIndex, 1);
    const auto& runtime = activeRuntime();
    RoiRect roi;
    roi.x = qRound(runtime.centroidX[safeIndex]) - kFixedRoiSize / 2;
    roi.y = qRound(runtime.centroidY[safeIndex]) - kFixedRoiSize / 2;
    roi.w = kFixedRoiSize;
    roi.h = kFixedRoiSize;
    return sanitizeRoi(roi, safeIndex);
}

void DIMM::applyRoiSummary(const RoiRect& roi, const QString& cameraLabel)
{
    ui->lblROIXValue->setText(QString::number(roi.x));
    ui->lblROIYValue->setText(QString::number(roi.y));
    ui->lblROIWValue->setText(QString::number(roi.w));
    ui->lblROIHValue->setText(QString::number(roi.h));
    m_lblStatusROI->setText(QStringLiteral("当前ROI(%1): (%2, %3) %4x%5")
                                .arg(cameraLabel)
                                .arg(roi.x)
                                .arg(roi.y)
                                .arg(roi.w)
                                .arg(roi.h));
}

QString DIMM::roiRuleDescription() const
{
    return QStringLiteral("ROI 固定为 64x64；启动后两台相机分别全画幅定位，并切换到各自独立 ROI 跟踪");
}

bool DIMM::validateAndCacheLiveRoiCapabilities(QString* reason)
{
    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        if (!m_cameraManager->prepareFixedRoi(cameraIndex,
                                              kFixedRoiSize,
                                              kFixedRoiSize,
                                              &m_liveRoiCapabilities[cameraIndex])) {
            if (reason) {
                *reason = QStringLiteral("相机%1固定 ROI 能力探测失败").arg(cameraIndex + 1);
            }
            m_liveRoiCapabilitiesValid = false;
            return false;
        }
    }

    m_liveRoiCapabilitiesValid = true;
    return true;
}

bool DIMM::readLivePairRoiPosition(RoiPosition positions[2], QString* reason)
{
    if (!positions) {
        return false;
    }

    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        if (!m_cameraManager->readRoiPosition(cameraIndex, &positions[cameraIndex])) {
            if (reason) {
                *reason = QStringLiteral("读取相机%1当前 ROI 位置失败").arg(cameraIndex + 1);
            }
            return false;
        }
    }
    return true;
}

RoiRect DIMM::buildLiveCameraRoi(int cameraIndex, const RoiRect& desiredRoi) const
{
    const int safeIndex = qBound(0, cameraIndex, 1);
    if (!m_liveRoiCapabilitiesValid) {
        return sanitizeRoi(desiredRoi, safeIndex);
    }

    const RoiCapability& capability = m_liveRoiCapabilities[safeIndex];
    const double sensorCenterX = static_cast<double>(desiredRoi.x) + static_cast<double>(desiredRoi.w) / 2.0;
    const double sensorCenterY = static_cast<double>(desiredRoi.y) + static_cast<double>(desiredRoi.h) / 2.0;
    const qint64 requestedX = static_cast<qint64>(
        std::llround(sensorCenterX - static_cast<double>(capability.width) / 2.0));
    const qint64 requestedY = static_cast<qint64>(
        std::llround(sensorCenterY - static_cast<double>(capability.height) / 2.0));

    RoiRect liveRoi;
    liveRoi.x = static_cast<int>(alignRoiValue(requestedX, capability.offsetX));
    liveRoi.y = static_cast<int>(alignRoiValue(requestedY, capability.offsetY));
    liveRoi.w = static_cast<int>(capability.width);
    liveRoi.h = static_cast<int>(capability.height);
    return liveRoi;
}

bool DIMM::configureLiveCameras(QString* reason)
{
    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        if (!m_cameraManager->isOpen(cameraIndex)) {
            if (reason) {
                *reason = QStringLiteral("相机%1未连接，无法开始实时采集").arg(cameraIndex + 1);
            }
            return false;
        }

        if (!m_cameraManager->prepareFullFrame(cameraIndex)) {
            if (reason) {
                *reason = QStringLiteral("相机%1切换到全画幅失败").arg(cameraIndex + 1);
            }
            return false;
        }

        if (!m_cameraManager->setExposure(cameraIndex, m_cameraExposureUs[cameraIndex]) ||
            !m_cameraManager->setGain(cameraIndex, m_configGainDb)) {
            if (reason) {
                *reason = QStringLiteral("相机%1曝光或增益设置失败").arg(cameraIndex + 1);
            }
            return false;
        }
    }

    if (!validateAndCacheLiveRoiCapabilities(reason)) {
        return false;
    }

    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        if (!m_cameraManager->prepareFullFrame(cameraIndex)) {
            if (reason) {
                *reason = QStringLiteral("相机%1校验独立 ROI 后恢复全画幅失败").arg(cameraIndex + 1);
            }
            return false;
        }
    }

    // Configure the trigger path last. In hardware-trigger mode we should avoid
    // touching ROI/full-frame geometry after the camera has been armed, otherwise
    // one camera can end up missing the trigger-wait state while the other keeps it.
    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        const bool triggerConfigured =
            m_configTriggerMode == 0 ? m_cameraManager->setTriggerMode(cameraIndex, TriggerMode::Continuous)
                                     : m_cameraManager->configureExternalTrigger(cameraIndex);
        if (!triggerConfigured) {
            if (reason) {
                *reason = QStringLiteral("相机%1触发模式配置失败").arg(cameraIndex + 1);
            }
            return false;
        }
    }

    if (!applyContinuousCameraFrameRate(reason)) {
        return false;
    }

    return true;
}

bool DIMM::applyContinuousCameraFrameRate(QString* reason)
{
    if (!m_cameraManager || m_configTriggerMode != 0) {
        return true;
    }

    const bool restartLiveContinuousCapture = m_captureState == CaptureState::Live;
    bool liveCaptureStopped = false;
    if (restartLiveContinuousCapture) {
        if (!m_cameraManager->stopAll()) {
            if (reason) {
                *reason = QStringLiteral("暂停连续采集以设置帧率失败");
            }
            return false;
        }
        liveCaptureStopped = true;
        resetLiveFrameAcceptanceGates();
    }

    const auto restartLiveCapture = [&]() {
        if (!liveCaptureStopped) {
            return true;
        }
        liveCaptureStopped = false;
        return m_cameraManager->startAll();
    };

    const auto failWithRestart = [&](const QString& message) {
        QString restartReason;
        if (!restartLiveCapture()) {
            restartReason = QStringLiteral("；恢复连续采集失败");
        }
        if (reason) {
            *reason = message + restartReason;
        }
        return false;
    };

    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        if (!m_cameraManager->isOpen(cameraIndex)) {
            continue;
        }
        if (!m_cameraManager->setFrameRate(cameraIndex, m_configContinuousFrameRateHz)) {
            return failWithRestart(QStringLiteral("相机%1连续采集帧率设置失败").arg(cameraIndex + 1));
        }

        const double actualFrameRate = m_cameraManager->getFrameRate(cameraIndex);
        m_lastContinuousFrameRateReadback[cameraIndex] = actualFrameRate;
        const double tolerance = std::max(0.05, m_configContinuousFrameRateHz * 0.05);
        if (actualFrameRate <= 0.0 ||
            std::abs(actualFrameRate - m_configContinuousFrameRateHz) > tolerance) {
            return failWithRestart(QStringLiteral("相机%1连续采集帧率读回异常: 目标 %2 fps，实际 %3 fps。")
                                       .arg(cameraIndex + 1)
                                       .arg(m_configContinuousFrameRateHz, 0, 'f', 2)
                                       .arg(actualFrameRate, 0, 'f', 2));
        }
    }

    if (!restartLiveCapture()) {
        if (reason) {
            *reason = QStringLiteral("设置连续采集帧率后恢复采集失败");
        }
        return false;
    }
    if (restartLiveContinuousCapture) {
        advanceLiveAcquisitionGeneration();
    }

    return true;
}

void DIMM::advanceLiveAcquisitionGeneration()
{
    ++m_liveAcquisitionGeneration;
    if (m_imageProcessor) {
        m_imageProcessor->advanceAcquisitionGeneration();
    }
    resetLiveFrameAcceptanceGates();
}

void DIMM::resetLiveFrameAcceptanceGates()
{
    m_liveFrameAcceptAfterMs = QDateTime::currentMSecsSinceEpoch();
    m_lastAcceptedLiveFrameId[0] = 0;
    m_lastAcceptedLiveFrameId[1] = 0;
    m_lastAcceptedLiveFrameMs[0] = -1;
    m_lastAcceptedLiveFrameMs[1] = -1;
    m_lastAcceptedContinuousFrameMs[0] = -1;
    m_lastAcceptedContinuousFrameMs[1] = -1;
}

bool DIMM::startDualCameraLocalization(QString* reason)
{
    if (!m_cameraManager) {
        if (reason) {
            *reason = QStringLiteral("相机管理器未初始化");
        }
        return false;
    }

    if (!m_cameraManager->startAll()) {
        if (reason) {
            *reason = QStringLiteral("双相机全画幅定位启动失败");
        }
        return false;
    }

    m_liveStartupPhase = LiveStartupPhase::LocatePair;
    return true;
}

bool DIMM::applyLiveHardwareRois(const RoiRect rois[2], QString* reason, RoiRect appliedRois[2])
{
    if (!m_liveRoiCapabilitiesValid) {
        if (reason) {
            *reason = QStringLiteral("独立 ROI 能力尚未准备完成");
        }
        return false;
    }
    if (!rois) {
        if (reason) {
            *reason = QStringLiteral("独立 ROI 参数无效");
        }
        return false;
    }

    RoiPosition currentPositions[2];
    if (!readLivePairRoiPosition(currentPositions, reason)) {
        return false;
    }

    RoiRect liveRois[2] = {
        buildLiveCameraRoi(0, rois[0]),
        buildLiveCameraRoi(1, rois[1]),
    };
    RoiPosition targetPositions[2] = {
        RoiPosition{liveRois[0].x, liveRois[0].y},
        RoiPosition{liveRois[1].x, liveRois[1].y},
    };

    if (currentPositions[0].x == targetPositions[0].x &&
        currentPositions[0].y == targetPositions[0].y &&
        currentPositions[1].x == targetPositions[1].x &&
        currentPositions[1].y == targetPositions[1].y) {
        if (appliedRois) {
            appliedRois[0] = liveRois[0];
            appliedRois[1] = liveRois[1];
        }
        const bool rateReady = applyContinuousCameraFrameRate(reason);
        if (rateReady) {
            advanceLiveAcquisitionGeneration();
        }
        return rateReady;
    }

    const bool hardwareTriggerMode = m_configTriggerMode != 0;
    bool triggerGated = false;
    if (hardwareTriggerMode) {
        for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
            if (!m_cameraManager->prepareTriggerInputLine(cameraIndex, QString::fromLatin1(kRoiUpdateGateLine))) {
                if (reason) {
                    *reason = QStringLiteral("准备相机%1 ROI 更新门控触发线失败").arg(cameraIndex + 1);
                }
                return false;
            }
        }
        if (!m_cameraManager->setPairTriggerSource(QString::fromLatin1(kRoiUpdateGateLine))) {
            if (reason) {
                *reason = QStringLiteral("切换到 ROI 更新门控触发线失败");
            }
            return false;
        }
        triggerGated = true;
    }

    RoiUpdatePauseState pauseState[2];
    if (!m_cameraManager->pausePairForRoiUpdate(pauseState)) {
        if (reason) {
            *reason = QStringLiteral("暂停采集以更新硬件 ROI 失败");
        }
        if (triggerGated) {
            m_cameraManager->setPairTriggerSource(QString::fromLatin1(kHardwareTriggerLine));
        }
        return false;
    }

    bool success = true;
    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        success = m_cameraManager->prepareFixedRoi(cameraIndex, liveRois[cameraIndex].w, liveRois[cameraIndex].h) &&
                  m_cameraManager->moveRoi(cameraIndex, targetPositions[cameraIndex]);
        if (!success) {
            if (reason) {
                *reason = QStringLiteral("相机%1硬件 ROI 更新失败").arg(cameraIndex + 1);
            }
            break;
        }
    }

    const bool resumed = m_cameraManager->resumePairAfterRoiUpdate(pauseState);
    if (!resumed && reason && success) {
        *reason = QStringLiteral("硬件 ROI 更新后恢复采集失败");
    }

    if (resumed) {
        m_cameraManager->flushPairQueues();
    }

    if (triggerGated &&
        !m_cameraManager->setPairTriggerSource(QString::fromLatin1(kHardwareTriggerLine)) &&
        reason && success && resumed) {
        *reason = QStringLiteral("硬件 ROI 更新后恢复 Line0 触发源失败");
        success = false;
    }

    if (!success || !resumed) {
        if (triggerGated) {
            m_cameraManager->setPairTriggerSource(QString::fromLatin1(kHardwareTriggerLine));
        }
        return false;
    }

    RoiPosition verifiedPositions[2];
    if (!readLivePairRoiPosition(verifiedPositions, reason)) {
        return false;
    }
    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        if (verifiedPositions[cameraIndex].x != targetPositions[cameraIndex].x ||
            verifiedPositions[cameraIndex].y != targetPositions[cameraIndex].y) {
            if (reason) {
                *reason = QStringLiteral("相机%1硬件 ROI 更新后偏移校验失败").arg(cameraIndex + 1);
            }
            return false;
        }
    }

    if (appliedRois) {
        appliedRois[0] = liveRois[0];
        appliedRois[1] = liveRois[1];
    }
    const bool rateReady = applyContinuousCameraFrameRate(reason);
    if (rateReady) {
        advanceLiveAcquisitionGeneration();
    }
    return rateReady;
}

bool DIMM::applyLiveFullFrameForRelocalization(QString* reason)
{
    if (!m_cameraManager) {
        if (reason) {
            *reason = QStringLiteral("相机管理器未初始化");
        }
        return false;
    }

    const bool hardwareTriggerMode = m_configTriggerMode != 0;
    bool triggerGated = false;
    if (hardwareTriggerMode) {
        for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
            if (!m_cameraManager->prepareTriggerInputLine(cameraIndex, QString::fromLatin1(kRoiUpdateGateLine))) {
                if (reason) {
                    *reason = QStringLiteral("准备相机%1全画幅重定位门控触发线失败").arg(cameraIndex + 1);
                }
                return false;
            }
        }
        if (!m_cameraManager->setPairTriggerSource(QString::fromLatin1(kRoiUpdateGateLine))) {
            if (reason) {
                *reason = QStringLiteral("切换到全画幅重定位门控触发线失败");
            }
            return false;
        }
        triggerGated = true;
    }

    RoiUpdatePauseState pauseState[2];
    if (!m_cameraManager->pausePairForRoiUpdate(pauseState)) {
        if (reason) {
            *reason = QStringLiteral("暂停采集以切换全画幅失败");
        }
        if (triggerGated) {
            m_cameraManager->setPairTriggerSource(QString::fromLatin1(kHardwareTriggerLine));
        }
        return false;
    }

    bool success = true;
    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        if (!m_cameraManager->prepareFullFrame(cameraIndex)) {
            success = false;
            if (reason) {
                *reason = QStringLiteral("相机%1切换全画幅失败").arg(cameraIndex + 1);
            }
            break;
        }
    }

    const bool resumed = m_cameraManager->resumePairAfterRoiUpdate(pauseState);
    if (!resumed && reason && success) {
        *reason = QStringLiteral("切换全画幅后恢复采集失败");
    }
    if (resumed) {
        m_cameraManager->flushPairQueues();
    }

    if (triggerGated &&
        !m_cameraManager->setPairTriggerSource(QString::fromLatin1(kHardwareTriggerLine)) &&
        reason && success && resumed) {
        *reason = QStringLiteral("全画幅重定位后恢复 Line0 触发源失败");
        success = false;
    }

    if (!success || !resumed) {
        if (triggerGated) {
            m_cameraManager->setPairTriggerSource(QString::fromLatin1(kHardwareTriggerLine));
        }
        return false;
    }

    if (m_configTriggerMode != 0) {
        QString pulseReason;

        const bool pulseStarted =
            startFullFrameLocalizationPulse(&pulseReason);

        if (!pulseStarted) {
            if (!isPulseBoardResponseTimeout(pulseReason)) {
                if (reason) {
                    *reason =
                        pulseReason.isEmpty()
                            ? QStringLiteral(
                                  "全画幅重定位低频触发启动失败。")
                            : pulseReason;
                }

                return false;
            }

            m_pulseBoardResponseTimedOut = true;

            setPulseBoardResponseTimeoutStatus(
                QStringLiteral(
                    "状态: 全画幅重定位脉冲板应答超时，继续等待双相机新的全画幅图像确认触发是否生效"));
        }

        beginHardwareTriggerStartupStage(
            HardwareTriggerStartupStage::WaitingFullFramePair);

        if (reason) {
            reason->clear();
        }

        return true;
    }
    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        if (m_cameraManager->isOpen(cameraIndex) &&
            !m_cameraManager->setFrameRate(cameraIndex, kFullFrameLocalizationPulseHz)) {
            if (reason) {
                *reason = QStringLiteral("相机%1全画幅重定位帧率设置失败").arg(cameraIndex + 1);
            }
            return false;
        }
    }
    return true;
}
bool DIMM::selectLiveRelocalizationCentroid(
    int cameraIndex,
    const cv::Mat& fullFrame,
    QPointF* centroid,
    double* peakValue,
    QString* selectionSource,
    QString* failureReason)
{
    if (selectionSource) {
        selectionSource->clear();
    }
    if (failureReason) {
        failureReason->clear();
    }

    if (cameraIndex < 0 ||
        cameraIndex >= 2 ||
        fullFrame.empty() ||
        !centroid) {
        if (failureReason) {
            *failureReason =
                QStringLiteral("全画幅目标选择参数无效");
        }
        return false;
    }

    double actualThreshold = 0.0;
    double otsuThreshold = 0.0;
    QVector<InitialStarCandidate> candidates =
        detectInitialStarCandidates(fullFrame, peakValue, &actualThreshold, &otsuThreshold);
    candidates = stabilizeInitialCandidates(cameraIndex, candidates);
    setFullFrameThresholdDisplay(cameraIndex, otsuThreshold, actualThreshold);
    if (candidates.isEmpty()) {
        if (failureReason) {
            *failureReason =
                QStringLiteral("相机%1全画幅未检测到有效候选星")
                    .arg(cameraIndex + 1);
        }
        return false;
    }

    auto& runtime = activeRuntime();
    FullFrameCanvas* targetCanvas =
        cameraIndex == 0 ? m_fullFrameCanvas1 : m_fullFrameCanvas2;
    const int previousSelectedIndex = runtime.selectedInitialCandidateIndex[cameraIndex];
    InitialStarSelection selection =
        PolarisDetectionPipeline::selectFullFrameStarCandidate(
            candidates,
            previousSelectedIndex,
            previousSelectedIndex > 0);

    if (!selection.selected && selection.requiresUserSelection) {
        runtime.pendingInitialCandidateSelectionRequired[cameraIndex] = true;
        if (targetCanvas) {
            targetCanvas->setStarCandidateOverlays(
                PolarisDetectionPipeline::buildCandidateOverlays(
                    candidates,
                    previousSelectedIndex));
        }
    }

    if (!selection.selected) {
        if (failureReason) {
            *failureReason = selection.requiresUserSelection
                                 ? QStringLiteral("相机%1检测到多个候选星，请在对准模式中选择星点")
                                       .arg(cameraIndex + 1)
                                 : selection.reason;
        }
        return false;
    }

    *centroid = selection.candidate.center;
    if (peakValue) {
        *peakValue = selection.candidate.peak;
    }
    if (selectionSource) {
        *selectionSource = previousSelectedIndex > 0
                               ? QStringLiteral("人工确认全画幅候选星")
                               : (candidates.size() == 1
                                      ? QStringLiteral("全画幅单候选星")
                                      : QStringLiteral("人工确认全画幅候选星"));
    }
    if (targetCanvas) {
        targetCanvas->setStarCandidateOverlays(
            PolarisDetectionPipeline::buildCandidateOverlays(
                candidates,
                selection.candidate.index));
    }
    runtime.pendingInitialCandidateSelectionRequired[cameraIndex] = false;
    runtime.selectedInitialCandidateIndex[cameraIndex] = -1;
    runtime.lastInitialCandidatePromptMs[cameraIndex] = -1;
    return true;
}

bool DIMM::maybeSeedRoiFromFrame(int cameraIndex, const cv::Mat& frame)
{
    if (!m_imageProcessor || frame.empty() || cameraIndex < 0 || cameraIndex >= 2) {
        return false;
    }

    const bool liveLocatePhase =
        m_captureState == CaptureState::Live && m_liveStartupPhase != LiveStartupPhase::Tracking;
    if (m_captureState == CaptureState::Live) {
        const bool frameLooksLikeHardwareRoi =
            frame.cols <= kFixedRoiSize && frame.rows <= kFixedRoiSize;
        // Live ROI seeding must use real full-frame images. Stale 64x64 frames can still be
        // delivered while the camera stream is switching back from hardware ROI.
        if (!liveLocatePhase || frameLooksLikeHardwareRoi) {
            return false;
        }
    }

    auto& runtime = activeRuntime();
    if (!liveLocatePhase && runtime.hasValidCentroid[cameraIndex]) {
        return false;
    }
    if (runtime.pendingInitialRoiReady[cameraIndex]) {
        return false;
    }

    cv::Mat grayscale;
    if (frame.channels() == 1) {
        grayscale = frame;
    } else {
        cv::cvtColor(frame, grayscale, cv::COLOR_BGR2GRAY);
    }

    FullFrameCanvas* targetCanvas = cameraIndex == 0 ? m_fullFrameCanvas1 : m_fullFrameCanvas2;
    if (targetCanvas &&
        LivePreviewPolicy::shouldShowLocalizationFrameBeforeStarSelection(
            livePreviewStartupPhase(m_liveStartupPhase),
            false)) {
        targetCanvas->setImage(grayscale);
        runtime.lastLivePreviewUpdateMs[cameraIndex] = QDateTime::currentMSecsSinceEpoch();
        updateFullFrameRoiOverlay(cameraIndex);
    }

    QPointF centroid;
    double peakValue = 0.0;
    if (liveLocatePhase) {
        QString selectionSource;
        QString selectionFailureReason;

        if (!selectLiveRelocalizationCentroid(
                cameraIndex,
                grayscale,
                &centroid,
                &peakValue,
                &selectionSource,
                &selectionFailureReason)) {
            if (targetCanvas &&
                !runtime.pendingInitialCandidateSelectionRequired[cameraIndex]) {
                targetCanvas->clearStarCandidateOverlays();
            }

            setStatusMessage(
                selectionFailureReason.isEmpty()
                    ? QStringLiteral(
                          "状态: 相机%1全画幅找星未找到有效目标，"
                          "等待下一帧。")
                          .arg(cameraIndex + 1)
                    : QStringLiteral("状态: %1")
                          .arg(selectionFailureReason),
                UiStatusLevel::Warning);
            return false;
        }

        runtime.lastTargetPosition[cameraIndex] = centroid;
        runtime.hasLastTargetPosition[cameraIndex] = true;

        if (runtime.hasConfirmedPolarisPosition[cameraIndex]) {
            runtime.confirmedPolarisPosition[cameraIndex] =
                centroid;
        }

        runtime.liveRelocalizationPreviewFrame[cameraIndex] =
            frame.clone();
        runtime.pendingInitialCandidateSelectionRequired[cameraIndex] =
            false;

        setStatusMessage(
            QStringLiteral(
                "状态: 相机%1全画幅定位找到星点 "
                "(%2, %3)，峰值 %4，来源 %5，第 %6 帧")
                .arg(cameraIndex + 1)
                .arg(centroid.x(), 0, 'f', 1)
                .arg(centroid.y(), 0, 'f', 1)
                .arg(peakValue, 0, 'f', 1)
                .arg(selectionSource.isEmpty()
                         ? QStringLiteral("未知")
                         : selectionSource)
                .arg(runtime.frameCountPerCamera[cameraIndex]),
            UiStatusLevel::Info);
    } else {
        double actualThreshold = 0.0;
        double otsuThreshold = 0.0;
        QVector<InitialStarCandidate> candidates =
            detectInitialStarCandidates(grayscale, &peakValue, &actualThreshold, &otsuThreshold);
        candidates = stabilizeInitialCandidates(cameraIndex, candidates);
        setFullFrameThresholdDisplay(cameraIndex, otsuThreshold, actualThreshold);
        if (candidates.isEmpty()) {
            if (runtime.pendingInitialCandidateSelectionRequired[cameraIndex]) {
                if (targetCanvas) {
                    targetCanvas->clearStarCandidateOverlays();
                }
                setStatusMessage(QStringLiteral("状态: 相机%1 正在等待有效的全画幅 SDK 连通域候选列表")
                                     .arg(cameraIndex + 1),
                                 UiStatusLevel::Warning);
                return false;
            }
            if (targetCanvas) {
                targetCanvas->clearStarCandidateOverlays();
            }
            runtime.pendingInitialCandidateSelectionRequired[cameraIndex] = false;
            setStatusMessage(QStringLiteral("状态: 相机%1 全画幅 SDK 连通域找星未找到有效星点，未初始化 ROI")
                                 .arg(cameraIndex + 1),
                             UiStatusLevel::Warning);
            return false;
        } else {
            if (targetCanvas) {
                targetCanvas->setStarCandidateOverlays(
                    PolarisDetectionPipeline::buildCandidateOverlays(
                        candidates,
                        runtime.selectedInitialCandidateIndex[cameraIndex]));
            }

            InitialStarSelection selection =
                PolarisDetectionPipeline::selectFullFrameStarCandidate(
                    candidates,
                    runtime.selectedInitialCandidateIndex[cameraIndex],
                    false);
            runtime.pendingInitialCandidateSelectionRequired[cameraIndex] =
                selection.requiresUserSelection;
            if (!selection.selected) {
                setStatusMessage(QStringLiteral("状态: 相机%1检测到多个候选星，请完成人工选星")
                                     .arg(cameraIndex + 1),
                                 UiStatusLevel::Warning);
                return false;
            }
            centroid = selection.candidate.center;
            if (targetCanvas) {
                targetCanvas->setStarCandidateOverlays(
                    PolarisDetectionPipeline::buildCandidateOverlays(
                        candidates, selection.candidate.index));
            }
            if (runtime.selectedInitialCandidateIndex[cameraIndex] > 0) {
                runtime.selectedInitialCandidateIndex[cameraIndex] = -1;
            }
            runtime.lastInitialCandidatePromptMs[cameraIndex] = -1;
        }
    }

    const RoiRect seeded = sanitizeRoi(
        RoiRect{qRound(centroid.x()) - kFixedRoiSize / 2,
                qRound(centroid.y()) - kFixedRoiSize / 2,
                kFixedRoiSize,
                kFixedRoiSize},
        cameraIndex);
    m_imageProcessor->setCurrentRoi(cameraIndex, seeded);
    runtime.pendingInitialRoi[cameraIndex] = seeded;
    runtime.pendingInitialRoiReady[cameraIndex] = true;
    applyRoiSummary(seeded, QStringLiteral("相机%1").arg(cameraIndex + 1));
    if (m_captureState == CaptureState::Live &&
        (!runtime.pendingInitialRoiReady[0] || !runtime.pendingInitialRoiReady[1])) {
        const int waitingCamera = runtime.pendingInitialRoiReady[0] ? 2 : 1;
        setStatusMessage(QStringLiteral("状态: 相机%1全画幅已找到星点，等待相机%2全画幅定位")
                             .arg(cameraIndex + 1)
                             .arg(waitingCamera),
                         UiStatusLevel::Info);
    }
    return commitPairedInitialRoisIfReady();
}

void DIMM::handleLiveRelocalizationWatchdog(qint64 nowMs)
{
    if (m_captureState != CaptureState::Live) {
        return;
    }

    auto& runtime = activeRuntime();
    const bool relocalizationActive =
        runtime.liveRelocalizationStartedMs >= 0 ||
        m_liveStartupPhase == LiveStartupPhase::LocatePair ||
        !m_liveHardwareRoiActive ||
        !runtime.initialRoiConfirmed[0] ||
        !runtime.initialRoiConfirmed[1];
    if (!relocalizationActive) {
        return;
    }

    if (runtime.liveRelocalizationStartedMs < 0) {
        runtime.liveRelocalizationStartedMs = nowMs;
        return;
    }
    if ((nowMs - runtime.liveRelocalizationStartedMs) < kLiveRelocalizationMaxDurationMs) {
        return;
    }

    if (m_liveStartupOrigin == LiveStartupOrigin::AutoAcquisition) {
        const bool noCameraHasPendingRoi =
            !runtime.pendingInitialRoiReady[0] &&
            !runtime.pendingInitialRoiReady[1];
        const bool manualSelectionRequired =
            runtime.pendingInitialCandidateSelectionRequired[0] ||
            runtime.pendingInitialCandidateSelectionRequired[1];
        if (noCameraHasPendingRoi || manualSelectionRequired) {
            clearPendingLiveRelocalizationRois();
            stopAutoAcquisitionScanUntilNextInterval(
                manualSelectionRequired
                    ? QStringLiteral("自动采集检测到多个候选星，等待人工选择")
                    : QStringLiteral("自动采集全画幅未找到星点，等待下一次扫描"),
                manualSelectionRequired);
            return;
        }
    }

    clearPendingLiveRelocalizationRois();
    runtime.liveRelocalizationStartedMs = nowMs;
    m_liveStartupPhase = LiveStartupPhase::LocatePair;
    m_liveHardwareRoiActive = false;
    resetLiveFrameAcceptanceGates();
    QString switchReason;
    const bool fullFrameReady = applyLiveFullFrameForRelocalization(&switchReason);

    if (!fullFrameReady &&
        m_configTriggerMode != 0) {
        const QString detail =
            switchReason.isEmpty()
                ? QStringLiteral(
                      "全画幅重定位超时后重新切换失败。")
                : switchReason;

        handleHardwareTriggerStartupFailure(
            detail);

        return;
    }

    if (ui->lblROITimeCurrent) {
        ui->lblROITimeCurrent->setText(fullFrameReady
                                           ? QStringLiteral("全画幅重定位重试中")
                                           : QStringLiteral("全画幅重定位重试失败"));
    }
    if (ui->lblROITimeNext) {
        ui->lblROITimeNext->setText(QStringLiteral("已清空本轮候选，等待下一对全画幅"));
    }
    setStatusMessage(fullFrameReady
                         ? QStringLiteral("状态: 全画幅重定位超时，已重新切换全画幅并重新开始检测")
                         : (switchReason.isEmpty()
                                ? QStringLiteral("状态: 全画幅重定位超时，重新切换全画幅失败")
                                : switchReason),
                     fullFrameReady ? UiStatusLevel::Warning : UiStatusLevel::Error);
}

void DIMM::updateFullFrameRoiOverlay(int cameraIndex)
{
    if (!m_imageProcessor || cameraIndex < 0 || cameraIndex >= 2) {
        return;
    }

    auto& runtime = activeRuntime();
    const bool showConfirmedRoiOverlay =
        m_captureState != CaptureState::Live || runtime.initialRoiConfirmed[cameraIndex];

    QVector<RoiRect> rois;
    if (showConfirmedRoiOverlay) {
        rois.append(m_imageProcessor->getCurrentRoi(cameraIndex));
    }

    FullFrameCanvas* targetCanvas = cameraIndex == 0 ? m_fullFrameCanvas1 : m_fullFrameCanvas2;
    if (!targetCanvas) {
        return;
    }

    targetCanvas->setRoiList(rois);
    targetCanvas->setCurrentRoi(rois.isEmpty() ? -1 : 0);
    updateActualRoiTrackOverlay(cameraIndex);
}

void DIMM::appendActualRoiTrackPoint(int cameraIndex, const RoiRect& roi)
{
    if (cameraIndex < 0 || cameraIndex >= 2) {
        return;
    }

    m_actualRoiTracks[cameraIndex].append(
        QPointF(roi.x + roi.w * 0.5, roi.y + roi.h * 0.5));
    updateActualRoiTrackOverlay(cameraIndex);
}

void DIMM::updateActualRoiTrackOverlay(int cameraIndex)
{
    if (cameraIndex < 0 || cameraIndex >= 2) {
        return;
    }

    FullFrameCanvas* targetCanvas = cameraIndex == 0 ? m_fullFrameCanvas1 : m_fullFrameCanvas2;
    if (!targetCanvas) {
        return;
    }

    FullFrameCanvas::RoiTrajectoryOverlay overlay;
    overlay.enabled = true;
    overlay.points = m_actualRoiTracks[cameraIndex].points();
    const PolarisTrajectory::RoiTrackFit fit =
        m_actualRoiTracks[cameraIndex].fitCircle();
    overlay.hasFittedCircle = fit.valid;
    overlay.fittedCenter = fit.center;
    overlay.fittedRadiusPx = fit.radiusPx;
    overlay.fittedRmsPx = fit.rmsPx;
    targetCanvas->setRoiTrajectoryOverlay(overlay);
}

void DIMM::clearActualRoiTracks()
{
    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        m_actualRoiTracks[cameraIndex].clear();
    }
    if (m_fullFrameCanvas1) {
        m_fullFrameCanvas1->clearRoiTrajectoryOverlay();
    }
    if (m_fullFrameCanvas2) {
        m_fullFrameCanvas2->clearRoiTrajectoryOverlay();
    }
}

void DIMM::showDeferredLiveRelocalizationPreview()
{
    auto& runtime = activeRuntime();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        FullFrameCanvas* targetCanvas = cameraIndex == 0 ? m_fullFrameCanvas1 : m_fullFrameCanvas2;
        if (targetCanvas && !runtime.liveRelocalizationPreviewFrame[cameraIndex].empty()) {
            targetCanvas->setImage(runtime.liveRelocalizationPreviewFrame[cameraIndex]);
        }
        runtime.liveRelocalizationPreviewFrame[cameraIndex].release();
        runtime.lastLivePreviewUpdateMs[cameraIndex] = nowMs;
        updateFullFrameRoiOverlay(cameraIndex);
    }
}

void DIMM::clearPendingLiveRelocalizationRois()
{
    auto& runtime = activeRuntime();
    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        runtime.pendingInitialRoi[cameraIndex] = RoiRect();
        runtime.pendingInitialRoiReady[cameraIndex] = false;
        runtime.liveRelocalizationPreviewFrame[cameraIndex].release();
    }
}

bool DIMM::isFullFrameLocalizationPulseRunning() const
{
    if (m_configTriggerMode == 0 ||
        !m_pulseGeneratorEnabled ||
        !m_pulseGenerator ||
        !m_pulseGenerator->isRunning()) {
        return false;
    }

    PulseGeneratorManager::Config pulseConfig;
    pulseConfig.enabled = true;
    pulseConfig.portName = m_pulseGeneratorPort;
    pulseConfig.baudRate = m_pulseGeneratorBaudRate;
    pulseConfig.terminalId = m_pulseGeneratorTerminalId;
    pulseConfig.frequencyHz = kFullFrameLocalizationPulseHz;
    pulseConfig.pulseCount = m_pulseGeneratorPulseCount;
    pulseConfig.dutyPercent = m_pulseGeneratorDutyPercent;
    pulseConfig.remoteControl = m_pulseGeneratorRemoteControl;

    return pulseConfigsMatch(m_pulseGenerator->config(), pulseConfig);
}

bool DIMM::commitPairedInitialRoisIfReady()
{
    if (!m_imageProcessor) {
        return false;
    }

    auto& runtime = activeRuntime();
    if (!runtime.pendingInitialRoiReady[0] || !runtime.pendingInitialRoiReady[1]) {
        return false;
    }

    const RoiRect pairedRois[2] = {
        runtime.pendingInitialRoi[0],
        runtime.pendingInitialRoi[1],
    };
    RoiRect actualRois[2] = {
        pairedRois[0],
        pairedRois[1],
    };

    QString reason;
    if (m_captureState == CaptureState::Live && !applyLiveHardwareRois(pairedRois, &reason, actualRois)) {
        m_liveHardwareRoiActive = false;
        clearPendingLiveRelocalizationRois();
        setStatusMessage(reason.isEmpty()
                             ? QStringLiteral("状态: 双相机初始 ROI 写入失败")
                             : reason,
                         UiStatusLevel::Warning);
        return false;
    }

    bool roiPulseResponseTimeout = false;

    if (m_captureState == CaptureState::Live) {
        if (!switchToRoiTrackingPulse(&reason)) {
            if (!isPulseBoardResponseTimeout(reason)) {
                m_liveHardwareRoiActive = false;
                clearPendingLiveRelocalizationRois();

                m_hardwareTriggerStartupStage =
                    HardwareTriggerStartupStage::None;

                if (m_hardwareTriggerStartupTimer) {
                    m_hardwareTriggerStartupTimer->stop();
                }

                handleHardwareTriggerStartupFailure(
                    reason.isEmpty()
                        ? QStringLiteral("ROI 高频触发切换失败")
                        : reason);

                return false;
            }

            roiPulseResponseTimeout = true;
            m_pulseBoardResponseTimedOut = true;
        }

        /*
         * switchToRoiTrackingPulse() 已经返回。
         * Qt 当前线程中的图像回调尚未处理，因此此时记录帧数基线。
         */
        beginHardwareTriggerStartupStage(
            HardwareTriggerStartupStage::WaitingRoiTrackingPair);
    }

    m_imageProcessor->setPairRois(actualRois);
    appendActualRoiTrackPoint(0, actualRois[0]);
    appendActualRoiTrackPoint(1, actualRois[1]);
    ++m_roiUpdateCount;
    m_lastRoiUpdateMs = QDateTime::currentMSecsSinceEpoch();
    m_lastRoiUpdateReason = runtime.liveRelocalizationStartedMs >= 0
                                ? QStringLiteral("full_frame_relocalization")
                                : QStringLiteral("initial_lock");
    runtime.initialRoiConfirmed[0] = true;
    runtime.initialRoiConfirmed[1] = true;
    runtime.pendingInitialRoiReady[0] = false;
    runtime.pendingInitialRoiReady[1] = false;
    runtime.liveRelocalizationStartedMs = -1;
    if (m_fullFrameCanvas1) {
        m_fullFrameCanvas1->clearStarCandidateOverlays();
    }
    if (m_fullFrameCanvas2) {
        m_fullFrameCanvas2->clearStarCandidateOverlays();
    }
    runtime.pendingInitialCandidateSelectionRequired[0] = false;
    runtime.pendingInitialCandidateSelectionRequired[1] = false;
    m_liveHardwareRoiActive = m_captureState == CaptureState::Live;
    m_liveStartupPhase = LiveStartupPhase::Tracking;
    applyRoiSummary(actualRois[0], QStringLiteral("相机1"));
    showDeferredLiveRelocalizationPreview();
    if (roiPulseResponseTimeout) {
        setPulseBoardResponseTimeoutStatus(
            QStringLiteral(
                "状态: ROI 已写入，脉冲板未返回高频切换应答；正在等待双相机新的 ROI 图像确认触发是否生效"));
    } else {
        setStatusMessage(
            QStringLiteral(
                "状态: ROI 已写入并已发起高频触发，等待双相机新的 ROI 图像确认"),
            UiStatusLevel::Warning);
    }
    return true;
}

bool DIMM::startHardwarePulseStage(double frequencyHz, const QString& stageLabel, QString* reason)
{
    if (m_configTriggerMode == 0 || !m_pulseGeneratorEnabled) {
        return true;
    }
    if (!m_pulseGenerator) {
        if (reason) {
            *reason = QStringLiteral("脉冲板控制器未初始化");
        }
        return false;
    }

    PulseGeneratorManager::Config pulseConfig;
    pulseConfig.enabled = true;
    pulseConfig.portName = m_pulseGeneratorPort;
    pulseConfig.baudRate = m_pulseGeneratorBaudRate;
    pulseConfig.terminalId = m_pulseGeneratorTerminalId;
    pulseConfig.frequencyHz = frequencyHz;
    pulseConfig.pulseCount = m_pulseGeneratorPulseCount;
    pulseConfig.dutyPercent = m_pulseGeneratorDutyPercent;
    pulseConfig.remoteControl = m_pulseGeneratorRemoteControl;

    if (m_pulseGenerator->isRunning() && pulseConfigsMatch(m_pulseGenerator->config(), pulseConfig)) {
        setStatusMessage(QStringLiteral("状态: 复用当前脉冲输出: %1 @ %2 Hz")
                             .arg(m_pulseGeneratorPort)
                             .arg(frequencyHz, 0, 'f', 1),
                         UiStatusLevel::Success);
        return true;
    }

    QString errorMessage;
    if (!m_pulseGenerator->configureAndStart(pulseConfig, &errorMessage)) {
        if (reason) {
            *reason = errorMessage.isEmpty()
                          ? QStringLiteral("%1触发启动失败").arg(stageLabel)
                          : errorMessage;
        }
        return false;
    }

    setStatusMessage(QStringLiteral("状态: %1触发已启用: %2 @ %3 Hz")
                         .arg(stageLabel, m_pulseGeneratorPort)
                         .arg(frequencyHz, 0, 'f', 1),
                     UiStatusLevel::Success);
    return true;
}

bool DIMM::startFullFrameLocalizationPulse(QString* reason)
{
    return startHardwarePulseStage(kFullFrameLocalizationPulseHz,
                                   QStringLiteral("全画幅低频定位"),
                                   reason);
}

bool DIMM::switchToRoiTrackingPulse(QString* reason)
{
    return startHardwarePulseStage(m_pulseGeneratorFrequencyHz,
                                   QStringLiteral("ROI 高频跟踪"),
                                   reason);
}

void DIMM::updateMinuteRoi(bool force)
{
    if (!m_imageProcessor) {
        return;
    }

    Q_UNUSED(force);
    auto& runtime = activeRuntime();
    if (!hasValidCentroidsForRoiUpdate()) {
        return;
    }

    RoiRect roi0 = buildCameraCentroidRoi(0);
    RoiRect roi1 = buildCameraCentroidRoi(1);

    RoiRect actualRoi0 = roi0;
    RoiRect actualRoi1 = roi1;

    if (m_captureState == CaptureState::Live) {
        QString reason;
        RoiRect actualRois[2] = {actualRoi0, actualRoi1};
        const RoiRect liveRois[2] = {roi0, roi1};
        if (applyLiveHardwareRois(liveRois, &reason, actualRois)) {
            actualRoi0 = actualRois[0];
            actualRoi1 = actualRois[1];
            m_liveHardwareRoiActive = true;
            m_imageProcessor->setPairRoisPreservingAtmosphereWindow(actualRois);
            appendActualRoiTrackPoint(0, actualRois[0]);
            appendActualRoiTrackPoint(1, actualRois[1]);
            ++m_roiUpdateCount;
            m_lastRoiUpdateMs = QDateTime::currentMSecsSinceEpoch();
            m_lastRoiUpdateReason = QStringLiteral("centroid_recenter");
            runtime.roiRecenteringCandidateFrameCount = 0;
            applyRoiSummary(actualRoi0, QStringLiteral("相机1"));
        } else {
            m_liveHardwareRoiActive = false;
            setStatusMessage(reason, UiStatusLevel::Warning);
            return;
        }
    } else {
        m_imageProcessor->setCurrentRoi(0, actualRoi0);
        m_imageProcessor->setCurrentRoi(1, actualRoi1);
        applyRoiSummary(actualRoi0, QStringLiteral("相机1"));
    }

    ui->lblROITimeCurrent->setText(hasValidCentroidsForRoiUpdate()
                                       ? QStringLiteral("已锁定双相机独立 ROI")
                                       : QStringLiteral("等待两路有效质心"));
    ui->lblROITimeNext->setText(QStringLiteral("ROI 固定尺寸: 64 x 64"));
}

void DIMM::hideLegacyRoiScheduleUi()
{
    ui->roiTablePanel->hide();
    ui->btnAddROI->hide();
    ui->btnDeleteROI->hide();
    ui->actionROISchedule->setVisible(false);
    ui->actionViewROI->setVisible(false);
    ui->btnROI->setVisible(false);
    ui->lblROIMapLabel->setText(roiRuleDescription());
    ui->lblROITimeLabel->setText(QStringLiteral("ROI 规则"));
    ui->lblROITimeCurrent->setText(QStringLiteral("等待两路有效质心"));
    ui->lblROITimeNext->setText(QStringLiteral("ROI 固定尺寸: 64 x 64"));
}
