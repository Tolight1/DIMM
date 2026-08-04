#include "DIMM.h"

#include "CameraManager.h"
#include "CanvasWidgets.h"
#include "CommManager.h"
#include "DimmRuntimeHelpers.h"
#include "FullFrameStarDetector.h"
#include "ImageProcessor.h"
#include "PulseGeneratorManager.h"
#include "SettingsDialog.h"

#include <algorithm>
#include <cmath>

#include <QDateTime>
#include <QMessageBox>
#include <QSize>
#include <QTimer>
#include <QVector>

void DIMM::onConnectAll()
{
    QString reason;
    if (!canConnectOrDisconnectCameras(&reason)) {
        QMessageBox::warning(this, QStringLiteral("连接相机"), reason);
        return;
    }

    m_connectingCameras = true;
    refreshActionStates();
    setStatusMessage(QStringLiteral("正在扫描相机设备..."), UiStatusLevel::Warning);
    const auto devices = m_cameraManager->enumerateDevices();
    if (devices.isEmpty()) {
        m_connectingCameras = false;
        setStatusMessage(QStringLiteral("未发现相机"), UiStatusLevel::Error);
        refreshCameraUi();
        refreshActionStates();
        return;
    }

    const bool success = m_cameraManager->openAll();
    m_connectingCameras = false;
    refreshUi();

    if (success) {
        QString message = QStringLiteral("已连接设备\n");
        for (int i = 0; i < devices.size(); ++i) {
            message += QStringLiteral("\n相机%1: %2 (%3) [%4]")
                           .arg(i + 1)
                           .arg(devices[i].serialNumber)
                           .arg(devices[i].modelName)
                           .arg(devices[i].ipAddress);
        }
        setStatusMessage(QStringLiteral("已连接%1 台相机").arg(devices.size()), UiStatusLevel::Success);
        qInfo().noquote() << message;
    } else {
        setStatusMessage(QStringLiteral("部分相机连接失败"), UiStatusLevel::Error);
    }
}

void DIMM::onDisconnectAll()
{
    QString reason;
    if (!canConnectOrDisconnectCameras(&reason)) {
        QMessageBox::warning(this, QStringLiteral("断开相机"), reason);
        return;
    }

    m_connectingCameras = true;
    refreshActionStates();
    m_cameraManager->closeAll();
    m_connectingCameras = false;
    refreshUi();
    setStatusMessage(QStringLiteral("相机已断开"), UiStatusLevel::Warning);
}

void DIMM::onCameraConnected(int index, QString serial, QString model)
{
    Q_UNUSED(index);
    Q_UNUSED(model);
    m_connectingCameras = false;
    refreshCameraUi();
    refreshActionStates();
    setStatusMessage(QStringLiteral("相机已连接 %1").arg(serial), UiStatusLevel::Success);
}

void DIMM::onCameraDisconnected(int index)
{
    Q_UNUSED(index);
    m_connectingCameras = false;
    refreshCameraUi();
    refreshActionStates();
    if (!hasAnyOpenCamera() && m_captureState == CaptureState::Live) {
        updateCaptureState(CaptureState::Paused);
        setStatusMessage(QStringLiteral("相机断开，采集已暂停"), UiStatusLevel::Warning);
    } else {
        setStatusMessage(QStringLiteral("相机已断开"), UiStatusLevel::Warning);
    }
}

void DIMM::onCameraError(int index, int errorCode, QString message)
{
    Q_UNUSED(errorCode);
    m_connectingCameras = false;
    refreshActionStates();
    setStatusMessage(QStringLiteral("相机%1错误: %2").arg(index + 1).arg(message), UiStatusLevel::Error);
    QTimer::singleShot(5000, this, [this]() {
        if (!hasActiveCapture()) {
            setStatusMessage(QStringLiteral("状态: 就绪"), UiStatusLevel::Muted);
        }
    });
}

void DIMM::onFrameReady(int cameraIndex)
{
    const CameraFrame packet = m_cameraManager ? m_cameraManager->takeLatestFramePacket(cameraIndex) : CameraFrame();
    if (m_captureState == CaptureState::Alignment) {
        handleAlignmentFramePacket(cameraIndex, packet);
        return;
    }
    if (m_captureState == CaptureState::Live && m_configTriggerMode != 0) {
        return;
    }
    handleLiveFramePacket(cameraIndex, packet);
}

void DIMM::onCapturedFramePacket(int cameraIndex, CameraFrame packet)
{
    if (m_captureState != CaptureState::Live || m_configTriggerMode == 0) {
        return;
    }
    recordSyncDiagnosticEvent(QStringLiteral("capture"), cameraIndex, packet);
    handleLiveFramePacket(cameraIndex, packet);
}

void DIMM::handleLiveFramePacket(int cameraIndex, const CameraFrame& packet)
{
    const cv::Mat frame = packet.image;
    if (frame.empty() || m_captureState != CaptureState::Live) {
        return;
    }
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    handleLiveRelocalizationWatchdog(nowMs);
    const qint64 frameReceivedMs =
        packet.receivedMs > 0 ? packet.receivedMs : nowMs;
    if (packet.receivedMs > 0 && packet.receivedMs < m_liveFrameAcceptAfterMs) {
        return;
    }
    if (cameraIndex >= 0 && cameraIndex < 2 &&
        packet.frameId > 0 && packet.frameId <= m_lastAcceptedLiveFrameId[cameraIndex]) {
        return;
    }
    if (cameraIndex >= 0 && cameraIndex < 2 && m_configTriggerMode == 0) {
        const qint64 continuousFrameIntervalMs =
            qMax<qint64>(1, static_cast<qint64>(std::llround(1000.0 / std::max(0.1, m_configContinuousFrameRateHz))));
        const qint64 lastAcceptedMs = m_lastAcceptedContinuousFrameMs[cameraIndex];
        if (lastAcceptedMs >= 0 && (frameReceivedMs - lastAcceptedMs) < continuousFrameIntervalMs) {
            return;
        }
        m_lastAcceptedContinuousFrameMs[cameraIndex] = frameReceivedMs;
    }

    auto& runtime = activeRuntime();
    const bool frameLooksLikeHardwareRoi =
        frame.cols <= kFixedRoiSize && frame.rows <= kFixedRoiSize;
    if (cameraIndex >= 0 && cameraIndex < 2) {
        // Keep the full-frame geometry once live hardware ROI tracking starts. The centroid
        // pipeline reports absolute coordinates, so shrinking the runtime frame size to 64x64
        // would incorrectly reject otherwise valid centroids as "out of bounds".
        if (!(m_liveHardwareRoiActive && frameLooksLikeHardwareRoi)) {
            runtime.frameSize[cameraIndex] = QSize(frame.cols, frame.rows);
        }
        if (packet.frameId > 0) {
            m_lastAcceptedLiveFrameId[cameraIndex] = packet.frameId;
        }
        ++runtime.frameCountPerCamera[cameraIndex];
    }

    ++runtime.frameCount;
    if (m_configTriggerMode != 0 &&
        (m_statusText.contains(QStringLiteral("Timed out waiting for pulse-board response."),
                               Qt::CaseInsensitive) ||
         m_statusText.contains(QStringLiteral("脉冲板应答超时")))) {
        setStatusMessage(QStringLiteral("状态: 已收到硬件触发图像帧，脉冲板未返回串口应答但采集继续"),
                         UiStatusLevel::Warning);
    }
    if (runtime.frameCount == 1 && m_liveStartupPhase == LiveStartupPhase::Tracking) {
        setStatusMessage(QStringLiteral("状态: 实时采集中，已收到图像帧，预览按30秒刷新"),
                         UiStatusLevel::Success);
    }
    maybeSeedRoiFromFrame(cameraIndex, frame);

    if (cameraIndex >= 0 && cameraIndex < 2) {
        QVector<RoiRect> rois;
        const bool showConfirmedRoiOverlay =
            m_captureState != CaptureState::Live || runtime.initialRoiConfirmed[cameraIndex];
        if (showConfirmedRoiOverlay) {
            rois.append(m_imageProcessor->getCurrentRoi(cameraIndex));
        } else {
            rois.clear();
        }
        const bool shouldRefreshPreview =
            runtime.lastLivePreviewUpdateMs[cameraIndex] < 0 ||
            (nowMs - runtime.lastLivePreviewUpdateMs[cameraIndex]) >= kSimulationPreviewIntervalMs;
        FullFrameCanvas* targetCanvas = cameraIndex == 0 ? m_fullFrameCanvas1 : m_fullFrameCanvas2;
        const bool canUpdateFullFramePreview =
            m_captureState == CaptureState::Live
                ? (!frameLooksLikeHardwareRoi &&
                   m_liveStartupPhase == LiveStartupPhase::Tracking)
                : true;
        if (targetCanvas && canUpdateFullFramePreview && shouldRefreshPreview) {
            targetCanvas->setImage(frame);
            runtime.lastLivePreviewUpdateMs[cameraIndex] = nowMs;
            updateFullFrameRoiOverlay(cameraIndex);
        }
    }

    const bool roiConfirmed =
        cameraIndex >= 0 && cameraIndex < 2 && runtime.initialRoiConfirmed[cameraIndex];
    const bool roiAvailableForThisCamera =
        cameraIndex >= 0 && cameraIndex < 2 &&
        (roiConfirmed || (m_liveHardwareRoiActive && frameLooksLikeHardwareRoi));
    if (roiAvailableForThisCamera) {
        const RoiRect processingRoi = m_imageProcessor->getCurrentRoi(cameraIndex);
        const cv::Mat processingFrame = cropFrameForRoiProcessing(frame, processingRoi);
        if (!processingFrame.empty()) {
            if (m_configTriggerMode != 0) {
                recordSyncDiagnosticEvent(QStringLiteral("submit"), cameraIndex, packet);
            }
            m_imageProcessor->processFrame(cameraIndex,
                                           processingFrame,
                                           packet.frameId,
                                           packet.cameraTimestamp,
                                           m_liveAcquisitionGeneration);
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

void DIMM::scheduleHardwareTriggerStartupCheck()
{
    if (!m_hardwareTriggerStartupTimer) {
        return;
    }
    m_hardwareTriggerStartupTimer->start(2500);
}

void DIMM::checkHardwareTriggerStartup()
{
    if (m_captureState != CaptureState::Live || m_configTriggerMode == 0) {
        return;
    }

    const auto& runtime = activeRuntime();
    const bool cam1Ready = runtime.frameCountPerCamera[0] > 0;
    const bool cam2Ready = runtime.frameCountPerCamera[1] > 0;
    if (cam1Ready && cam2Ready) {
        return;
    }

    QString detail;
    if (!cam1Ready && !cam2Ready) {
        detail = QStringLiteral("两台相机在启动后 2.5 秒内都没有收到首帧。请优先检查触发线、TriggerSource(Line0)、脉冲是否已实际输出，以及脉冲是否发生在相机进入等待态之后");
    } else if (!cam1Ready) {
        detail = QStringLiteral("只有相机2收到首帧，相机仍未触发。请检查相机对应的触发接线、网口带宽和硬件触发输入");
    } else {
        detail = QStringLiteral("只有相机1收到首帧，相机仍未触发。请检查相机对应的触发接线、网口带宽和硬件触发输入");
    }

    setStatusMessage(QStringLiteral("硬件触发首帧超时: %1").arg(detail), UiStatusLevel::Warning);
}

void DIMM::onCommCommand(uint8_t cmd)
{
    using namespace CommProtocol;

    switch (cmd) {
    case CMD_START_REPORT:
        if (!isLiveCaptureActive()) {
            m_commManager->sendAck(CMD_START_REPORT, 1);
            m_reporting = false;
            if (m_reportTimer) {
                m_reportTimer->stop();
            }
            setStatusMessage(QStringLiteral("当前为模拟空闲模式，已拒绝上报请求"), UiStatusLevel::Warning);
            refreshStatusUi();
            return;
        }
        m_commManager->sendAck(CMD_START_REPORT, 0);
        m_reporting = true;
        m_reportTimer->start();
        setStatusMessage(QStringLiteral("上位机请求开始上报"), UiStatusLevel::Success);
        break;
    case CMD_STOP_REPORT:
        m_commManager->sendAck(CMD_STOP_REPORT, 0);
        m_reporting = false;
        m_reportTimer->stop();
        setStatusMessage(QStringLiteral("上位机请求停止上报"), UiStatusLevel::Warning);
        break;
    case CMD_QUERY_STATUS:
        reportDeviceStatus();
        break;
    default:
        qDebug() << "[DIMM] Unknown command:" << QString::number(cmd, 16);
        break;
    }
    refreshStatusUi();
}
