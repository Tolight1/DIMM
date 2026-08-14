#include "DIMM.h"

#include "CameraManager.h"
#include "CanvasWidgets.h"
#include "CommManager.h"
#include "DimmRuntimeHelpers.h"
#include "FullFrameStarDetector.h"
#include "ImageProcessor.h"
#include "LivePreviewPolicy.h"
#include "PulseGeneratorManager.h"
#include "SettingsDialog.h"

#include <algorithm>
#include <cmath>

#include <QDateTime>
#include <QMessageBox>
#include <QSize>
#include <QTimer>
#include <QVector>

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
    if (m_captureState == CaptureState::Live &&
        m_configTriggerMode != 0 &&
        !m_liveStartupConfirmed) {
        handleHardwareTriggerStartupFailure(
            QStringLiteral(
                "硬件触发启动期间相机%1断开")
                .arg(index + 1));
        return;
    }
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

    if (cameraIndex >= 0 && cameraIndex < 2) {
        m_lastAcceptedLiveFrameMs[cameraIndex] = frameReceivedMs;
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
    if (m_configTriggerMode != 0) {
        recordHardwareTriggerStartupFrame(
            cameraIndex,
            frameLooksLikeHardwareRoi);
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
            (nowMs - runtime.lastLivePreviewUpdateMs[cameraIndex]) >= kLiveFullFramePreviewIntervalMs;
        FullFrameCanvas* targetCanvas = cameraIndex == 0 ? m_fullFrameCanvas1 : m_fullFrameCanvas2;
        const bool canUpdateFullFramePreview =
            m_captureState == CaptureState::Live
                ? LivePreviewPolicy::shouldUpdateLiveFullFramePreview(
                      livePreviewStartupPhase(m_liveStartupPhase),
                      frameLooksLikeHardwareRoi)
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

void DIMM::beginHardwareTriggerStartupStage(
    HardwareTriggerStartupStage stage)
{
    m_hardwareTriggerStartupStage = stage;

    if (stage ==
            HardwareTriggerStartupStage::
                WaitingFullFramePair ||
        stage ==
            HardwareTriggerStartupStage::
                WaitingRoiTrackingPair) {
        m_liveStartupConfirmed = false;
    }

    const auto& runtime = activeRuntime();

    for (int cameraIndex = 0;
         cameraIndex < 2;
         ++cameraIndex) {
        m_hardwareTriggerStageBaselineFrameCount[cameraIndex] =
            runtime.frameCountPerCamera[cameraIndex];

        m_hardwareTriggerStageFrameSeen[cameraIndex] =
            false;
    }

    scheduleHardwareTriggerStartupCheck();
}

void DIMM::recordHardwareTriggerStartupFrame(
    int cameraIndex,
    bool frameLooksLikeHardwareRoi)
{
    if (cameraIndex < 0 ||
        cameraIndex >= 2 ||
        m_captureState != CaptureState::Live ||
        m_configTriggerMode == 0) {
        return;
    }

    const auto stage =
        m_hardwareTriggerStartupStage;

    if (stage != HardwareTriggerStartupStage::WaitingFullFramePair &&
        stage != HardwareTriggerStartupStage::WaitingRoiTrackingPair) {
        return;
    }

    const auto& runtime = activeRuntime();

    const bool isNewStageFrame =
        runtime.frameCountPerCamera[cameraIndex] >
        m_hardwareTriggerStageBaselineFrameCount[cameraIndex];

    if (!isNewStageFrame) {
        return;
    }

    if (stage ==
        HardwareTriggerStartupStage::WaitingFullFramePair) {
        // 全画幅阶段不能使用 64×64 ROI 帧完成确认。
        if (frameLooksLikeHardwareRoi) {
            return;
        }

        m_hardwareTriggerStageFrameSeen[cameraIndex] = true;
    } else {
        // ROI 高频阶段必须收到 64×64 或更小的硬件 ROI 图像。
        if (!frameLooksLikeHardwareRoi) {
            return;
        }

        m_hardwareTriggerStageFrameSeen[cameraIndex] = true;
    }

    confirmHardwareTriggerStartupIfReady();
}

void DIMM::scheduleHardwareTriggerStartupCheck()
{
    if (!m_hardwareTriggerStartupTimer) {
        return;
    }

    int timeoutMs =
        kHardwareTriggerFirstFrameTimeoutMs;

    if (m_hardwareTriggerStartupStage ==
        HardwareTriggerStartupStage::WaitingRoiTrackingPair) {
        timeoutMs =
            kRoiTrackingFirstFrameTimeoutMs;
    }

    m_hardwareTriggerStartupTimer->start(timeoutMs);
}

void DIMM::confirmHardwareTriggerStartupIfReady()
{
    if (m_captureState != CaptureState::Live ||
        m_configTriggerMode == 0) {
        return;
    }

    const auto stage =
        m_hardwareTriggerStartupStage;

    if (stage != HardwareTriggerStartupStage::WaitingFullFramePair &&
        stage != HardwareTriggerStartupStage::WaitingRoiTrackingPair) {
        return;
    }

    const bool bothReady =
        m_hardwareTriggerStageFrameSeen[0] &&
        m_hardwareTriggerStageFrameSeen[1];

    if (!bothReady) {
        return;
    }

    if (m_hardwareTriggerStartupTimer) {
        m_hardwareTriggerStartupTimer->stop();
    }

    const bool hadPulseBoardTimeout =
        m_pulseBoardResponseTimedOut;

    m_pulseBoardResponseTimedOut = false;
    m_lastPulseBoardTimeoutStatusMs = -1;

    if (stage ==
        HardwareTriggerStartupStage::WaitingFullFramePair) {
        /*
         * 这里只确认低频全画幅触发有效。
         * 不能将整个采集标记为最终启动成功。
         */
        m_hardwareTriggerStartupStage =
            HardwareTriggerStartupStage::None;

        if (hadPulseBoardTimeout) {
            setStatusMessage(
                QStringLiteral(
                    "状态: 脉冲板未返回全画幅触发应答，但双相机已收到新的全画幅图像，继续定位"),
                UiStatusLevel::Warning);
        } else {
            setStatusMessage(
                QStringLiteral(
                    "状态: 双相机全画幅低频触发已确认，继续进行星点定位"),
                UiStatusLevel::Success);
        }

        return;
    }

    /*
     * 只有 ROI 高频阶段双相机都收到新 ROI 帧，
     * 才算完整启动成功。
     */
    m_hardwareTriggerStartupStage =
        HardwareTriggerStartupStage::Running;

    m_liveStartupConfirmed = true;
    m_liveStartupRecoveryInProgress = false;
    m_liveStartupRetryCount = 0;

    if (m_liveStartupRetryTimer) {
        m_liveStartupRetryTimer->stop();
    }

    if (m_liveStartupOrigin ==
        LiveStartupOrigin::AutoAcquisition) {
        setAutoAcquisitionStatus(
            QStringLiteral(
                "自动采集启动成功，双相机 ROI 高频触发已确认"),
            UiStatusLevel::Success,
            QStringLiteral("auto-start-confirmed"));
    }

    if (hadPulseBoardTimeout) {
        setStatusMessage(
            QStringLiteral(
                "状态: 脉冲板未返回 ROI 高频切换应答，但双相机已收到新的 ROI 图像，继续采集"),
            UiStatusLevel::Warning);
    } else {
        setStatusMessage(
            QStringLiteral(
                "状态: 双相机 ROI 高频触发确认成功，实时采集已稳定运行"),
            UiStatusLevel::Success);
    }
}

void DIMM::checkHardwareTriggerStartup()
{
    if (m_captureState != CaptureState::Live ||
        m_configTriggerMode == 0) {
        return;
    }

    const auto stage =
        m_hardwareTriggerStartupStage;

    if (stage != HardwareTriggerStartupStage::WaitingFullFramePair &&
        stage != HardwareTriggerStartupStage::WaitingRoiTrackingPair) {
        return;
    }

    if (m_hardwareTriggerStageFrameSeen[0] &&
        m_hardwareTriggerStageFrameSeen[1]) {
        confirmHardwareTriggerStartupIfReady();
        return;
    }

    QString detail;

    if (stage ==
        HardwareTriggerStartupStage::WaitingFullFramePair) {
        if (!m_hardwareTriggerStageFrameSeen[0] &&
            !m_hardwareTriggerStageFrameSeen[1]) {
            detail = QStringLiteral(
                "全画幅低频触发后，两台相机均未收到新的全画幅图像");
        } else if (!m_hardwareTriggerStageFrameSeen[0]) {
            detail = QStringLiteral(
                "全画幅低频触发后，只有相机2收到新图像，相机1未触发");
        } else {
            detail = QStringLiteral(
                "全画幅低频触发后，只有相机1收到新图像，相机2未触发");
        }
    } else {
        if (!m_hardwareTriggerStageFrameSeen[0] &&
            !m_hardwareTriggerStageFrameSeen[1]) {
            detail = QStringLiteral(
                "ROI 高频触发切换后，两台相机均未收到新的 64×64 ROI 图像");
        } else if (!m_hardwareTriggerStageFrameSeen[0]) {
            detail = QStringLiteral(
                "ROI 高频触发切换后，只有相机2收到新的 ROI 图像，相机1未触发");
        } else {
            detail = QStringLiteral(
                "ROI 高频触发切换后，只有相机1收到新的 ROI 图像，相机2未触发");
        }
    }

    handleHardwareTriggerStartupFailure(detail);
}

void DIMM::onCommCommand(uint8_t cmd)
{
    Q_UNUSED(cmd);
}
