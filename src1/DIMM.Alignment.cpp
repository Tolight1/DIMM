#include "DIMM.h"

#include "AlignmentCameraCoordinator.h"
#include "AlignmentCoarseController.h"
#include "AlignmentController.h"
#include "AlignmentFrameCoordinator.h"
#include "AlignmentLocalTracker.h"
#include "AlignmentSession.h"
#include "AlignmentTaskManager.h"
#include "AlignmentUiPresenter.h"
#include "CameraManager.h"
#include "CanvasWidgets.h"
#include "DimmRuntimeHelpers.h"
#include "FullFrameStarDetector.h"
#include "ImageUtils.h"
#include "InitialStarDetectionConfig.h"
#include "PathUtils.h"
#include "PolarisDetectionPipeline.h"
#include "PolarisSolver.h"
#include "PolarisTracker.h"
#include "SettingsDialog.h"

#include <algorithm>
#include <cmath>

#include <QDateTime>
#include <QInputDialog>
#include <QMessageBox>
#include <QPointF>
#include <QSize>
#include <QStringList>

using PolarisDetectionPipeline::InitialStarCandidate;
using PolarisDetectionPipeline::InitialStarSelection;


namespace {

struct AlignmentStartReadiness {
    bool canStart = false;
    bool alreadyActive = false;
    bool shouldStopPausedCapture = false;
    QString reason;
};

AlignmentStartReadiness validateAlignmentStartReadiness(bool hasCameraManager,
                                                        bool alignmentActive,
                                                        bool idleOrPaused,
                                                        bool paused,
                                                        int openCameraCount)
{
    AlignmentStartReadiness readiness;
    if (!hasCameraManager) {
        readiness.reason = QStringLiteral("相机管理器未初始化。");
        return readiness;
    }

    if (alignmentActive) {
        readiness.canStart = true;
        readiness.alreadyActive = true;
        return readiness;
    }

    if (!idleOrPaused) {
        readiness.reason = QStringLiteral("请先停止当前采集或模拟采集，再进入对准模式。");
        return readiness;
    }

    if (openCameraCount < 2) {
        readiness.reason = QStringLiteral("对准模式需要两台相机均已连接。");
        return readiness;
    }

    readiness.canStart = true;
    readiness.shouldStopPausedCapture = paused;
    return readiness;
}
}

void DIMM::onToggleAlignmentMode()
{
    if (m_captureState == CaptureState::Alignment) {
        stopAlignmentMode();
        return;
    }

    QString reason;
    if (!startAlignmentMode(&reason)) {
        const QString message = reason.isEmpty()
                                    ? QStringLiteral("无法进入对准模式。")
                                    : reason;
        QMessageBox::warning(this, QStringLiteral("对准模式"), message);
        setStatusMessage(message, UiStatusLevel::Warning);
    }
}

void DIMM::onConfirmCamera1PolarisCandidate()
{
    requestAlignmentPolarisSelection(0);
}

void DIMM::onConfirmCamera2PolarisCandidate()
{
    requestAlignmentPolarisSelection(1);
}

void DIMM::requestAlignmentPolarisSelection(int cameraIndex)
{
    if (m_captureState != CaptureState::Alignment) {
        setStatusMessage(QStringLiteral("状态: 请先进入对准模式，再确认北极星"), UiStatusLevel::Warning);
        return;
    }
    if (!isValidCameraIndex(cameraIndex)) {
        return;
    }

    auto& runtime = m_liveRuntime;
    auto& cameraState = m_alignmentSession.camera(cameraIndex);
    cameraState.selectionRequested = true;
    cameraState.lastPreviewMs = -1;
    runtime.selectedInitialCandidateIndex[cameraIndex] = -1;
    runtime.lastInitialCandidatePromptMs[cameraIndex] = -1;
    setStatusMessage(QStringLiteral("状态: 已请求确认相机1的北极星，下一张全画幅候选列表将弹出编号确认")
                         .arg(cameraIndex + 1),
                     UiStatusLevel::Info);
    if (!m_alignmentCachedCandidates[cameraIndex].isEmpty()) {
        int chosenCandidateIndex = -1;
        cameraState.selectionRequested = false;
        if (!promptAlignmentCandidateSelection(cameraIndex,
                                               m_alignmentCachedCandidates[cameraIndex],
                                               &chosenCandidateIndex)) {
            AlignmentSession::recordCandidatePromptCancelled(
                &runtime.lastInitialCandidatePromptMs[cameraIndex],
                QDateTime::currentMSecsSinceEpoch());
            setStatusMessage(QStringLiteral("状态: 相机%1对准候选星点选择已取消，保留候选框等待确认")
                                 .arg(cameraIndex + 1),
                             UiStatusLevel::Warning);
            return;
        }

        AlignmentSession::recordCandidatePromptAccepted(
            &runtime.selectedInitialCandidateIndex[cameraIndex],
            &runtime.lastInitialCandidatePromptMs[cameraIndex],
            chosenCandidateIndex);
        InitialStarSelection selection =
            PolarisDetectionPipeline::selectInitialStarCandidate(
                m_alignmentCachedCandidates[cameraIndex],
                false,
                QPointF(),
                chosenCandidateIndex);
        FullFrameCanvas* targetCanvas =
            cameraIndex == 0 ? m_fullFrameCanvas1 : m_fullFrameCanvas2;
        if (selection.selected) {
            applyAlignmentSelectedCandidate(cameraIndex,
                                            targetCanvas,
                                            m_alignmentCachedCandidates[cameraIndex],
                                            selection,
                                            true,
                                            nullptr);
        }
    }
}

bool DIMM::startAlignmentMode(QString* reason)
{
    const AlignmentStartReadiness readiness =
        validateAlignmentStartReadiness(
            m_cameraManager != nullptr,
            m_captureState == CaptureState::Alignment,
            m_captureState == CaptureState::Idle || m_captureState == CaptureState::Paused,
            m_captureState == CaptureState::Paused,
            openCameraCount());
    if (readiness.alreadyActive) {
        return true;
    }
    if (!readiness.canStart) {
        if (reason) {
            *reason = readiness.reason;
        }
        return false;
    }

    if (readiness.shouldStopPausedCapture) {
        stopLiveCapture();
        updateCaptureState(CaptureState::Idle);
    }

    if (!prepareAlignmentCamerasForPreview(reason)) {
        return false;
    }

    resetAlignmentRuntimeForStart();
    clearAlignmentCanvasesForStart();

    if (!m_cameraManager->startAll()) {
        if (reason) {
            *reason = QStringLiteral("对准模式启动相机连续取图失败。");
        }
        return false;
    }

    showAlignmentModeStarted();
    return true;
}

void DIMM::stopAlignmentMode()
{
    if (m_captureState != CaptureState::Alignment) {
        return;
    }

    restoreCamerasAfterAlignment();

    resetAlignmentRuntimeForStop();
    clearAlignmentCanvasesForStop();

    showAlignmentModeStopped();
}

bool DIMM::prepareAlignmentCamerasForPreview(QString* reason)
{
    return AlignmentCameraCoordinator::preparePreview(m_cameraManager,
                                                      m_alignmentPreviewRateHz,
                                                      reason);
}

void DIMM::restoreCamerasAfterAlignment()
{
    AlignmentCameraCoordinator::restoreAfterAlignment(m_cameraManager,
                                                      m_configTriggerMode);
}

void DIMM::showAlignmentModeStarted()
{
    setDetailViewMode(DetailViewMode::None);
    updateCaptureState(CaptureState::Alignment);
    const QString solveLabel = m_alignmentAutoSolveEnabled
                                   ? AlignmentUiPresenter::waitingAlignmentLabelText()
                                   : AlignmentUiPresenter::solveStateText(AlignmentSolveState::Disabled);
    const UiStatusLevel solveLevel = m_alignmentAutoSolveEnabled ? UiStatusLevel::Info
                                                                 : UiStatusLevel::Warning;
    setAlignmentSolveLabel(0, solveLabel, solveLevel);
    setAlignmentSolveLabel(1, solveLabel, solveLevel);
    setStatusMessage(AlignmentUiPresenter::startedAlignmentStatusText(m_alignmentPreviewRateHz),
                     UiStatusLevel::Info);
}

void DIMM::showAlignmentModeStopped()
{
    updateCaptureState(CaptureState::Idle);
    if (m_lblAlignmentSolveCam1) {
        m_lblAlignmentSolveCam1->setVisible(false);
    }
    if (m_lblAlignmentSolveCam2) {
        m_lblAlignmentSolveCam2->setVisible(false);
    }
    setDetailViewMode(DetailViewMode::RoiOnly);
    setStatusMessage(AlignmentUiPresenter::stoppedAlignmentStatusText(), UiStatusLevel::Warning);
}

void DIMM::resetAlignmentRuntimeForStart()
{
    m_alignmentCoarseActive = false;
    resetCoarseAlignmentRuntime();
    m_alignmentSession.camera(0).lastPreviewMs = -1;
    m_alignmentSession.camera(1).lastPreviewMs = -1;
    m_alignmentSession.camera(0).selectionRequested = false;
    m_alignmentSession.camera(1).selectionRequested = false;
    m_alignmentCachedCandidates[0].clear();
    m_alignmentCachedCandidates[1].clear();
    m_alignmentLastCandidateDetectionMs[0] = -1;
    m_alignmentLastCandidateDetectionMs[1] = -1;
    const quint64 solveGeneration = m_alignmentSession.advanceSolveGeneration();
    if (m_polarisSolverController) {
        m_polarisSolverController->cancelAll(solveGeneration);
    }
    auto& runtime = m_liveRuntime;
    for (int i = 0; i < kCameraCount; ++i) {
        AlignmentLiveRuntimeAccess access;
        access.confirmedPolarisPosition = &runtime.confirmedPolarisPosition[i];
        access.hasConfirmedPolarisPosition = &runtime.hasConfirmedPolarisPosition[i];
        access.lastTargetPosition = &runtime.lastTargetPosition[i];
        access.hasLastTargetPosition = &runtime.hasLastTargetPosition[i];
        access.selectedInitialCandidateIndex = &runtime.selectedInitialCandidateIndex[i];
        access.pendingInitialCandidateSelectionRequired =
            &runtime.pendingInitialCandidateSelectionRequired[i];
        access.lastInitialCandidatePromptMs = &runtime.lastInitialCandidatePromptMs[i];
        m_alignmentSession.resetCameraForStart(i, access, m_alignmentAutoSolveEnabled);
        const QString solveLabel = m_alignmentAutoSolveEnabled
                                       ? AlignmentUiPresenter::waitingAlignmentLabelText()
                                       : AlignmentUiPresenter::solveStateText(AlignmentSolveState::Disabled);
        const UiStatusLevel solveLevel = m_alignmentAutoSolveEnabled ? UiStatusLevel::Info
                                                                     : UiStatusLevel::Warning;
        setAlignmentSolveLabel(i, solveLabel, solveLevel);
    }
}

void DIMM::resetAlignmentRuntimeForStop()
{
    m_alignmentCoarseActive = false;
    resetCoarseAlignmentRuntime();
    m_alignmentSession.camera(0).selectionRequested = false;
    m_alignmentSession.camera(1).selectionRequested = false;
    m_alignmentCachedCandidates[0].clear();
    m_alignmentCachedCandidates[1].clear();
    m_alignmentLastCandidateDetectionMs[0] = -1;
    m_alignmentLastCandidateDetectionMs[1] = -1;
    const quint64 solveGeneration = m_alignmentSession.advanceSolveGeneration();
    if (m_polarisSolverController) {
        m_polarisSolverController->cancelAll(solveGeneration);
    }
    for (int i = 0; i < kCameraCount; ++i) {
        m_alignmentSession.resetCameraForStop(i);
        setAlignmentSolveLabel(i, AlignmentUiPresenter::stoppedAlignmentLabelText(), UiStatusLevel::Muted);
    }
}

void DIMM::clearAlignmentCanvasesForStart()
{
    clearCoarseAlignmentOverlays();
    if (m_fullFrameCanvas1) {
        m_fullFrameCanvas1->clearAlignmentOverlay();
        m_fullFrameCanvas1->clearStarCandidateOverlays();
        m_fullFrameCanvas1->setRoiList({});
        m_fullFrameCanvas1->setCurrentRoi(-1);
    }
    if (m_fullFrameCanvas2) {
        m_fullFrameCanvas2->clearAlignmentOverlay();
        m_fullFrameCanvas2->clearStarCandidateOverlays();
        m_fullFrameCanvas2->setRoiList({});
        m_fullFrameCanvas2->setCurrentRoi(-1);
    }
}

void DIMM::clearAlignmentCanvasesForStop()
{
    clearCoarseAlignmentOverlays();
    if (m_fullFrameCanvas1) {
        m_fullFrameCanvas1->clearAlignmentOverlay();
    }
    if (m_fullFrameCanvas2) {
        m_fullFrameCanvas2->clearAlignmentOverlay();
    }
}

void DIMM::onToggleCoarseAlignment()
{
    if (m_captureState != CaptureState::Alignment) {
        setStatusMessage(QStringLiteral("状态: 请先进入对准模式，再启动粗对准"), UiStatusLevel::Warning);
        return;
    }

    if (m_alignmentCoarseActive) {
        m_alignmentCoarseActive = false;
        resetCoarseAlignmentRuntime();
        clearCoarseAlignmentOverlays();
        setStatusMessage(QStringLiteral("状态: 粗对准已停止，可继续自动识别或人工确认"), UiStatusLevel::Info);
        refreshActionStates();
        return;
    }

    m_alignmentCoarseActive = true;
    resetCoarseAlignmentRuntime();
    if (m_polarisSolverController) {
        m_polarisSolverController->cancelAll(m_alignmentSession.solveGeneration());
    }
    setAlignmentSolveLabel(0, QStringLiteral("粗对准: 等待星点漂移"), UiStatusLevel::Info);
    setAlignmentSolveLabel(1, QStringLiteral("粗对准: 等待星点漂移"), UiStatusLevel::Info);
    setStatusMessage(QStringLiteral("状态: 粗对准已启动，请关闭恒星跟踪并等待 15-30 秒"), UiStatusLevel::Info);
    refreshActionStates();
}

void DIMM::resetCoarseAlignmentRuntime()
{
    m_alignmentLastCoarseSubmitMs[0] = -1;
    m_alignmentLastCoarseSubmitMs[1] = -1;
    m_alignmentCoarseEstimates[0] = CoarseAlignmentEstimate();
    m_alignmentCoarseEstimates[1] = CoarseAlignmentEstimate();
    if (m_alignmentCoarseController) {
        m_alignmentCoarseController->resetAll();
    }
}

void DIMM::clearCoarseAlignmentOverlays()
{
    if (m_fullFrameCanvas1) {
        m_fullFrameCanvas1->clearCoarseDriftOverlay();
    }
    if (m_fullFrameCanvas2) {
        m_fullFrameCanvas2->clearCoarseDriftOverlay();
    }
}

CoarseAlignmentConfig DIMM::buildCoarseAlignmentConfig() const
{
    CoarseAlignmentConfig config;
    config.maxCandidates = 80;
    config.maxAssociationDistancePx = 25.0;
    config.maxStaleTrackSec = 5.0;
    config.maxTrackPoints = 90;
    config.minTrackPoints = 5;
    config.minTrackDurationSec = 15.0;
    config.minTrackDisplacementPx = 2.0;
    config.maxTrackFitRmsPx = 3.5;
    config.minTrackSpeedPxSec = 0.005;
    config.maxCenterResidualRmsPx = 80.0;
    config.plateScaleArcsecPx =
        206265.0 * std::max(0.001, m_alignmentPixelSizeUm / 1000.0) /
        std::max(1.0, m_alignmentFocalLengthMm);
    config.siderealArcsecSec = 15.041;
    config.minTracksForCenter = 2;
    return config;
}

void DIMM::submitCoarseAlignmentFrame(int cameraIndex, const CameraFrame& packet, qint64 nowMs)
{
    if (!m_alignmentCoarseController || !isValidCameraIndex(cameraIndex)) {
        return;
    }
    if (m_alignmentLastCoarseSubmitMs[cameraIndex] >= 0 &&
        nowMs - m_alignmentLastCoarseSubmitMs[cameraIndex] < m_alignmentCoarseSubmitIntervalMs) {
        return;
    }
    m_alignmentLastCoarseSubmitMs[cameraIndex] = nowMs;

    m_alignmentCoarseController->submitFrame(cameraIndex,
                                             packet.image,
                                             currentInitialStarDetectionConfig(),
                                             buildCoarseAlignmentConfig(),
                                             m_alignmentSession.solveGeneration(),
                                             packet.frameId,
                                             nowMs);
}

void DIMM::onCoarseAlignmentEstimateReady(CoarseAlignmentEstimate estimate)
{
    if (estimate.generation != m_alignmentSession.solveGeneration() ||
        !isValidCameraIndex(estimate.cameraIndex) ||
        m_captureState != CaptureState::Alignment ||
        !m_alignmentCoarseActive) {
        return;
    }

    m_alignmentCoarseEstimates[estimate.cameraIndex] = estimate;
    updateCoarseAlignmentOverlay(estimate.cameraIndex);

    UiStatusLevel level = UiStatusLevel::Info;
    if (estimate.valid) {
        level = UiStatusLevel::Success;
    } else if (estimate.centerIllConditioned) {
        level = UiStatusLevel::Warning;
    }

    setAlignmentSolveLabel(estimate.cameraIndex,
                           estimate.statusText,
                           level);
    setStatusMessage(QStringLiteral("状态: 相机%1 %2")
                         .arg(estimate.cameraIndex + 1)
                         .arg(estimate.statusText),
                     level);
}

void DIMM::updateCoarseAlignmentOverlay(int cameraIndex)
{
    FullFrameCanvas* canvas = cameraIndex == 0 ? m_fullFrameCanvas1 : m_fullFrameCanvas2;
    if (!canvas || !isValidCameraIndex(cameraIndex)) {
        return;
    }

    const CoarseAlignmentEstimate& estimate = m_alignmentCoarseEstimates[cameraIndex];
    FullFrameCanvas::CoarseDriftOverlay overlay;
    overlay.enabled = m_alignmentCoarseActive;
    overlay.valid = estimate.valid;
    overlay.northCelestialPolePx = estimate.northCelestialPolePx;
    overlay.frameCenterPx = estimate.frameCenterPx;
    overlay.adjustmentVectorPx = estimate.adjustmentVectorPx;
    overlay.offsetPx = estimate.offsetPx;
    overlay.offsetDeg = estimate.offsetDeg;
    overlay.medianSpeedPxSec = estimate.medianSpeedPxSec;
    overlay.medianFittedSpeedPxSec =
        estimate.medianFittedSpeedPxSec;
    overlay.centerResidualRmsPx = estimate.centerResidualRmsPx;
    overlay.detectedCandidateCount =
        estimate.detectedCandidateCount;
    overlay.activeTrackCount = estimate.activeTrackCount;
    overlay.fittedTrackCount = estimate.fittedTrackCount;
    overlay.usableTrackCount = estimate.usableTrackCount;
    overlay.requiredTrackCount = estimate.requiredTrackCount;
    overlay.statusText = estimate.statusText;
    overlay.diagnosticText = estimate.diagnosticText;

    for (const CoarseAlignmentTrackOverlay& track : estimate.tracks) {
        FullFrameCanvas::CoarseDriftTrackOverlay drawTrack;
        drawTrack.pointCount = track.pointCount;
        drawTrack.startPx = track.startPx;
        drawTrack.endPx = track.endPx;
        drawTrack.velocityPxSec = track.velocityPxSec;
        drawTrack.speedPxSec = track.speedPxSec;
        drawTrack.durationSec = track.durationSec;
        drawTrack.displacementPx = track.displacementPx;
        drawTrack.fitRmsPx = track.fitRmsPx;
        drawTrack.velocityFitValid = track.velocityFitValid;
        drawTrack.usedForSolve = track.usedForSolve;
        drawTrack.rejectionReason = track.rejectionReason;
        overlay.tracks.append(drawTrack);
    }

    canvas->setCoarseDriftOverlay(overlay);
}

double DIMM::fallbackAlignmentOrbitRadiusPx() const
{
    const double focalLengthMm = std::max(1.0, m_alignmentFocalLengthMm);
    const double pixelSizeMm = std::max(0.001, m_alignmentPixelSizeUm / 1000.0);
    const double plateScaleArcsecPerPx = 206265.0 * pixelSizeMm / focalLengthMm;
    const double polarDistanceArcsec = std::max(0.0, m_alignmentPolarisPolarDistanceArcmin) * 60.0;
    const double autoRadius = plateScaleArcsecPerPx > 0.0
                                  ? polarDistanceArcsec / plateScaleArcsecPerPx
                                  : 0.0;
    return std::max(1.0, autoRadius + m_alignmentRadiusAdjustPx);
}

double DIMM::alignmentOrbitRadiusPx() const
{
    return fallbackAlignmentOrbitRadiusPx();
}

void DIMM::handleAlignmentFramePacket(int cameraIndex, const CameraFrame& packet)
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    AlignmentFrameCoordinator::FrameGateInput frameGate;
    frameGate.alignmentActive = m_captureState == CaptureState::Alignment;
    frameGate.cameraIndex = cameraIndex;
    frameGate.nowMs = nowMs;
    frameGate.lastPreviewMs = isValidCameraIndex(cameraIndex)
                                  ? m_alignmentSession.camera(cameraIndex).lastPreviewMs
                                  : -1;
    frameGate.previewIntervalMs =
        AlignmentFrameCoordinator::previewIntervalMs(m_alignmentPreviewRateHz);
    frameGate.frame = &packet.image;
    if (!AlignmentFrameCoordinator::shouldAcceptAlignmentFrame(frameGate)) {
        return;
    }

    if (!prepareAlignmentFramePreview(cameraIndex, packet)) {
        return;
    }

    if (m_alignmentCoarseActive) {
        submitCoarseAlignmentFrame(cameraIndex, packet, nowMs);
        finishAlignmentFramePreview(cameraIndex, packet, nowMs);
        return;
    }

    auto& solveRuntime = m_alignmentSession.camera(cameraIndex).solveRuntime;
    switch (AlignmentFrameCoordinator::nextFrameAction(m_alignmentAutoSolveEnabled,
                                                       solveRuntime,
                                                       nowMs)) {
    case AlignmentFrameCoordinator::FrameAction::Disabled:
        solveRuntime.state = AlignmentSolveState::Disabled;
        finishAlignmentFramePreview(cameraIndex, packet, nowMs);
        return;
    case AlignmentFrameCoordinator::FrameAction::ManualTrack:
        handleManualAlignmentFrameTracking(cameraIndex, packet.image);
        finishAlignmentFramePreview(cameraIndex, packet, nowMs);
        return;
    case AlignmentFrameCoordinator::FrameAction::AutomaticTrack:
        if (handleAutomaticAlignmentFrameTracking(cameraIndex, packet.image, nowMs)) {
            finishAlignmentFramePreview(cameraIndex, packet, nowMs);
            return;
        }
        [[fallthrough]];
    case AlignmentFrameCoordinator::FrameAction::WaitRetry:
        setAlignmentSolveLabel(cameraIndex,
                               AlignmentUiPresenter::formatRetryWaitingSolveLabel(
                                   solveRuntime.nextRetryMs - nowMs),
                               UiStatusLevel::Warning);
        finishAlignmentFramePreview(cameraIndex, packet, nowMs);
        return;
    case AlignmentFrameCoordinator::FrameAction::RequestSolve:
        break;
    }
    requestAutomaticPolarisSolve(cameraIndex, false);
    finishAlignmentFramePreview(cameraIndex, packet, nowMs);
}

bool DIMM::handleManualAlignmentFrameTracking(int cameraIndex, const cv::Mat& frame)
{
    auto& solveRuntime = m_alignmentSession.camera(cameraIndex).solveRuntime;
    auto& runtime = m_liveRuntime;
    QPointF trackedPosition;
    double trackedPeak = 0.0;
    if (trackAlignmentPolarisLocally(cameraIndex, frame, &trackedPosition, &trackedPeak)) {
        AlignmentController::applyManualTrackingSuccess(&solveRuntime, trackedPosition);
        AlignmentController::syncTrackedPolarisPosition(cameraIndex,
                                                        trackedPosition,
                                                        runtime.confirmedPolarisPosition,
                                                        runtime.hasConfirmedPolarisPosition,
                                                        runtime.lastTargetPosition,
                                                        runtime.hasLastTargetPosition);
        setAlignmentSolveLabel(cameraIndex,
                               AlignmentUiPresenter::formatManualTrackingSolveLabel(trackedPosition),
                               UiStatusLevel::Success);
    } else {
        AlignmentController::applyManualTrackingFailure(&solveRuntime);
        setAlignmentSolveLabel(cameraIndex,
                               AlignmentUiPresenter::formatManualTrackingLostSolveLabel(),
                               UiStatusLevel::Warning);
    }
    return true;
}

bool DIMM::handleAutomaticAlignmentFrameTracking(int cameraIndex, const cv::Mat& frame, qint64 nowMs)
{
    auto& solveRuntime = m_alignmentSession.camera(cameraIndex).solveRuntime;
    auto& runtime = m_liveRuntime;
    QPointF trackedPosition;
    double trackedPeak = 0.0;
    if (trackAlignmentPolarisLocally(cameraIndex, frame, &trackedPosition, &trackedPeak)) {
        PolarisTracker::recordTrackSuccess(&solveRuntime, trackedPosition);
        AlignmentController::syncTrackedPolarisPosition(cameraIndex,
                                                        trackedPosition,
                                                        runtime.confirmedPolarisPosition,
                                                        runtime.hasConfirmedPolarisPosition,
                                                        runtime.lastTargetPosition,
                                                        runtime.hasLastTargetPosition);
        setAlignmentSolveLabel(cameraIndex,
                               AlignmentUiPresenter::formatTrackingSolveLabel(trackedPosition),
                               UiStatusLevel::Success);
        return true;
    }

    const int lostTrackRetryCount = buildPolarisSolverConfig().lostTrackRetryCount;
    if (PolarisTracker::recordTrackFailure(&solveRuntime,
                                           lostTrackRetryCount,
                                           nowMs,
                                           m_alignmentRetryIntervalMs)) {
        return false;
    }

    setAlignmentSolveLabel(cameraIndex,
                           AlignmentUiPresenter::formatTrackingLostSolveLabel(
                               solveRuntime.consecutiveTrackFailures,
                               lostTrackRetryCount),
                           UiStatusLevel::Warning);
    return true;
}

bool DIMM::prepareAlignmentFramePreview(int cameraIndex, const CameraFrame& packet)
{
    FullFrameCanvas* targetCanvas = cameraIndex == 0 ? m_fullFrameCanvas1 : m_fullFrameCanvas2;
    if (!targetCanvas) {
        return false;
    }

    targetCanvas->setImage(packet.image);
    targetCanvas->setRoiList({});
    targetCanvas->setCurrentRoi(-1);
    auto& cameraState = m_alignmentSession.camera(cameraIndex);
    cameraState.lastFrame = packet.image.clone();
    cameraState.lastFrameId = packet.frameId;
    return true;
}

void DIMM::finishAlignmentFramePreview(int cameraIndex, const CameraFrame& packet, qint64 nowMs)
{
    updateAlignmentOverlay(cameraIndex, packet);
    m_alignmentSession.camera(cameraIndex).lastPreviewMs = nowMs;
}

void DIMM::requestAutomaticPolarisSolve(int cameraIndex, bool force)
{
    if (m_captureState != CaptureState::Alignment ||
        !isValidCameraIndex(cameraIndex) ||
        !m_polarisSolverController) {
        return;
    }
    auto& cameraState = m_alignmentSession.camera(cameraIndex);
    if (cameraState.lastFrame.empty()) {
        showMissingAlignmentFrameForSolve(cameraIndex);
        return;
    }

    auto& solveRuntime = cameraState.solveRuntime;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (AlignmentTaskManager::prepareFullSolveRequest(&solveRuntime, force, nowMs) !=
        AlignmentFullSolveRequestAction::Submit) {
        return;
    }

    PolarisSolverConfig solverConfig = buildPolarisSolverConfig();
    solverConfig.cameraIndex = cameraIndex;
    AlignmentTaskManager::submitFullSolve(m_polarisSolverController,
                                          cameraIndex,
                                          cameraState.lastFrame,
                                          solverConfig,
                                          m_alignmentSession.solveGeneration(),
                                          cameraState.lastFrameId,
                                          force);
    showSubmittedAlignmentSolve(cameraIndex, force);
}

void DIMM::requestAutomaticPolarisSolveBoth()
{
    requestAutomaticPolarisSolve(0, true);
    requestAutomaticPolarisSolve(1, true);
}

PolarisSolverConfig DIMM::buildPolarisSolverConfig() const
{
    PolarisSolverConfig config;
    config.enabled = m_alignmentAutoSolveEnabled;
    config.maxDetectedStars = m_alignmentMaxDetectedStars;
    config.minMatchedStars = m_alignmentMinMatchedStars;
    config.maxRmsPx = m_alignmentMaxRmsPx;
    config.nominalPlateScaleArcsecPx =
        206265.0 * std::max(0.001, m_alignmentPixelSizeUm / 1000.0) /
        std::max(1.0, m_alignmentFocalLengthMm);
    config.minPlateScaleArcsecPx = config.nominalPlateScaleArcsecPx * 0.88;
    config.maxPlateScaleArcsecPx = config.nominalPlateScaleArcsecPx * 1.12;
    config.initialMatchTolerancePx = 8.0;
    config.refinedMatchTolerancePx = 4.0;
    config.minScoreMargin = 5.0;
    config.minMatchedSpatialSpreadPx = m_alignmentMinMatchedSpatialSpreadPx;
    config.minPolarisSnr = m_alignmentMinPolarisSnr;
    config.allowSaturatedPolarisConfirmation = m_alignmentAllowSaturatedPolarisConfirmation;
    const InitialStarDetectionConfig starConfig = currentInitialStarDetectionConfig();
    config.starThresholdAbsolute = starConfig.thresholdAbsolute >= 0.0
                                       ? starConfig.thresholdAbsolute
                                       : -1.0;
    config.starThresholdSigma = starConfig.sigmaThreshold;
    config.starPeakFraction = starConfig.peakFraction;
    config.starMinimumIntensity = starConfig.minimumIntensity;
    config.minStarAreaPx = starConfig.minArea;
    config.maxStarAreaPx = starConfig.maxArea;
    config.observationEpochYear = decimalYearFromUtc(QDateTime::currentDateTimeUtc());
    config.retryIntervalMs = m_alignmentRetryIntervalMs;
    config.lostTrackRetryCount = 3;
    config.showMatchedCatalogStars = m_alignmentShowMatchedCatalogStars;
    config.hotPixelTemplates[0].enabled = m_hotPixelTemplatesEnabled;
    config.hotPixelTemplates[0].maskPath = PathUtils::resolvePathFromAppDir(m_hotPixelCamera0MaskPath);
    config.hotPixelTemplates[0].excessPath = PathUtils::resolvePathFromAppDir(m_hotPixelCamera0ExcessPath);
    config.hotPixelTemplates[0].templateWidth = m_hotPixelTemplateWidth;
    config.hotPixelTemplates[0].templateHeight = m_hotPixelTemplateHeight;
    config.hotPixelTemplates[1].enabled = m_hotPixelTemplatesEnabled;
    config.hotPixelTemplates[1].maskPath = PathUtils::resolvePathFromAppDir(m_hotPixelCamera1MaskPath);
    config.hotPixelTemplates[1].excessPath = PathUtils::resolvePathFromAppDir(m_hotPixelCamera1ExcessPath);
    config.hotPixelTemplates[1].templateWidth = m_hotPixelTemplateWidth;
    config.hotPixelTemplates[1].templateHeight = m_hotPixelTemplateHeight;
    return config;
}

void DIMM::onPolarisSolveFinished(PolarisSolveResult result)
{
    if (result.generation != m_alignmentSession.solveGeneration() ||
        !isValidCameraIndex(result.cameraIndex) ||
        m_captureState != CaptureState::Alignment) {
        return;
    }

    auto& cameraState = m_alignmentSession.camera(result.cameraIndex);
    auto& solveRuntime = cameraState.solveRuntime;
    if (AlignmentController::shouldIgnoreSolverResult(solveRuntime)) {
        logPolarisSolveResult(result);
        return;
    }

    logPolarisSolveResult(result);
    cameraState.solveResult = result;
    solveRuntime.lastFullSolve = result;
    auto& runtime = m_liveRuntime;
    if (result.valid && result.hasDetectedPolarisPixel) {
        AlignmentController::applyDetectedPolarisSolve(&solveRuntime, result);
        runtime.confirmedPolarisPosition[result.cameraIndex] = result.detectedPolarisPixel;
        runtime.hasConfirmedPolarisPosition[result.cameraIndex] = true;
        runtime.lastTargetPosition[result.cameraIndex] = result.detectedPolarisPixel;
        runtime.hasLastTargetPosition[result.cameraIndex] = true;
        runtime.pendingInitialCandidateSelectionRequired[result.cameraIndex] = false;
        runtime.selectedInitialCandidateIndex[result.cameraIndex] = -1;
        refreshActionStates();
    }
    if (result.valid && !result.hasDetectedPolarisPixel) {
        AlignmentController::applyPredictedOnlyRetry(&solveRuntime,
                                                     QDateTime::currentMSecsSinceEpoch(),
                                                     m_alignmentRetryIntervalMs);
        setAlignmentSolveLabel(result.cameraIndex,
                               AlignmentUiPresenter::formatPredictedOnlySolveLabel(result),
                               UiStatusLevel::Warning);
        setStatusMessage(AlignmentUiPresenter::formatPredictedOnlyStatusMessage(result),
                         UiStatusLevel::Warning);
    } else if (result.valid) {
        setAlignmentSolveLabel(result.cameraIndex,
                               AlignmentUiPresenter::formatSolvedSolveLabel(result),
                               UiStatusLevel::Success);
        setStatusMessage(AlignmentUiPresenter::formatSolvedStatusMessage(result),
                         UiStatusLevel::Success);
    } else if (result.status == PolarisSolveStatus::InsufficientStars ||
               result.status == PolarisSolveStatus::NoCatalogMatch ||
               result.status == PolarisSolveStatus::LowConfidence) {
        const AlignmentRetryDecision retryDecision =
            AlignmentTaskManager::applySolveFailureRetry(&solveRuntime,
                                                         result.status,
                                                         QDateTime::currentMSecsSinceEpoch(),
                                                         m_alignmentRetryIntervalMs);
        setAlignmentSolveLabel(result.cameraIndex,
                               AlignmentUiPresenter::formatRetrySolveLabel(result),
                               UiStatusLevel::Warning);
        setStatusMessage(AlignmentUiPresenter::formatRetryStatusMessage(
                             result,
                             retryDecision.diagnosticHint),
                         UiStatusLevel::Warning);
    } else if (result.status == PolarisSolveStatus::Error) {
        AlignmentController::applySolveError(&solveRuntime);
        setAlignmentSolveLabel(result.cameraIndex,
                               AlignmentUiPresenter::formatErrorSolveLabel(result),
                               UiStatusLevel::Error);
        setStatusMessage(AlignmentUiPresenter::formatErrorStatusMessage(result),
                         UiStatusLevel::Error);
    }
}

QVector<InitialStarCandidate> DIMM::collectAlignmentStarCandidates(
    int cameraIndex,
    const cv::Mat& frame,
    const PolarisSolveResult& solved,
    bool hasCurrentSolverResult,
    bool allowGuiCandidateDetection,
    cv::Mat* mono8,
    double* peakValue) const
{
    AlignmentCandidateCollectionInput input;
    input.cameraIndex = cameraIndex;
    input.frame = &frame;
    input.solved = &solved;
    input.autoSolveEnabled = m_alignmentAutoSolveEnabled;
    input.hasCurrentSolverResult = hasCurrentSolverResult;
    input.lastFrameId = m_alignmentSession.camera(cameraIndex).lastFrameId;
    input.allowGuiCandidateDetection = allowGuiCandidateDetection;
    input.candidateDetector = [mono8](const cv::Mat& grayscale, double* peak) {
        if (mono8) {
            *mono8 = ImageUtils::normalizeMono8Frame(grayscale);
        }
        if (grayscale.empty()) {
            return QVector<InitialStarCandidate>();
        }
        QVector<InitialStarCandidate> candidates = detectInitialStarCandidates(grayscale, peak);
        if (!candidates.isEmpty()) {
            return candidates;
        }

        InitialStarCandidate peakCandidate;
        double peakValue = 0.0;
        if (detectRawInitialStarPeakCandidate(grayscale, &peakCandidate, &peakValue)) {
            if (peak) {
                *peak = peakValue;
            }
            candidates.append(peakCandidate);
        }
        return candidates;
    };
    return AlignmentSession::collectCandidates(input, mono8, peakValue);
}

bool DIMM::handleAlignmentCandidateSelection(
    int cameraIndex,
    FullFrameCanvas* targetCanvas,
    FullFrameCanvas::AlignmentOverlay* overlay,
    const QVector<InitialStarCandidate>& candidates,
    QPointF* selectedStar)
{
    if (!targetCanvas || !overlay || candidates.isEmpty()) {
        return true;
    }

    auto& runtime = m_liveRuntime;
    targetCanvas->setStarCandidateOverlays(
        PolarisDetectionPipeline::buildCandidateOverlays(
            candidates, runtime.selectedInitialCandidateIndex[cameraIndex]));

    const bool manualSelectionRequested = m_alignmentSession.camera(cameraIndex).selectionRequested;
    const bool hadConfirmedPolarisBeforeSelection =
        runtime.hasConfirmedPolarisPosition[cameraIndex];
    QPointF preferredTarget;
    InitialStarSelection selection = selectAlignmentInitialCandidate(cameraIndex,
                                                                    candidates,
                                                                    manualSelectionRequested,
                                                                    &preferredTarget);
    runtime.pendingInitialCandidateSelectionRequired[cameraIndex] =
        selection.requiresUserSelection;
    if (!selection.selected && selection.requiresUserSelection && !manualSelectionRequested) {
        setStatusMessage(QStringLiteral("状态: 相机%1显示到多个候选星点，请点击“确认相机1的北极星”后选择编号")
                             .arg(cameraIndex + 1),
                         UiStatusLevel::Info);
    }
    if (manualSelectionRequested &&
        !handleManualAlignmentCandidatePrompt(cameraIndex,
                                              targetCanvas,
                                              overlay,
                                              candidates,
                                              preferredTarget,
                                              &selection)) {
        return false;
    }

    const bool canApplyAlignmentSelection =
        AlignmentSession::canApplyCandidateSelection(
            manualSelectionRequested,
            hadConfirmedPolarisBeforeSelection);
    if (selection.selected && canApplyAlignmentSelection) {
        applyAlignmentSelectedCandidate(cameraIndex,
                                        targetCanvas,
                                        candidates,
                                        selection,
                                        manualSelectionRequested,
                                        selectedStar);
    }
    return true;
}

bool DIMM::promptAlignmentCandidateSelection(
    int cameraIndex,
    const QVector<InitialStarCandidate>& candidates,
    int* chosenCandidateIndex)
{
    if (!chosenCandidateIndex || candidates.isEmpty()) {
        return false;
    }

    const QStringList candidateLines =
        AlignmentSession::candidatePromptLines(candidates);
    bool ok = false;
    const int candidateIndex =
        QInputDialog::getInt(this,
                             QStringLiteral("相机%1北极星候选选择")
                                 .arg(cameraIndex + 1),
                             QStringLiteral("相机%1候选列表\n%2\n\n请选择北极星候选编号")
                                 .arg(cameraIndex + 1)
                                 .arg(candidateLines.join(QLatin1Char('\n'))),
                             1,
                             1,
                             candidates.size(),
                             1,
                             &ok);
    if (ok) {
        *chosenCandidateIndex = candidateIndex;
    }
    return ok;
}

void DIMM::updateAlignmentOverlay(int cameraIndex, const CameraFrame& packet)
{
    const cv::Mat& frame = packet.image;
    if (!isValidCameraIndex(cameraIndex) || frame.empty()) {
        return;
    }

    FullFrameCanvas* targetCanvas = cameraIndex == 0 ? m_fullFrameCanvas1 : m_fullFrameCanvas2;
    if (!targetCanvas) {
        return;
    }

    auto& cameraState = m_alignmentSession.camera(cameraIndex);
    const PolarisSolveResult& solved = cameraState.solveResult;
    auto& solveRuntime = cameraState.solveRuntime;
    const bool hasCurrentSolverResult = solved.generation == m_alignmentSession.solveGeneration();
    AlignmentUiPresenter::OverlayBuildInput overlayInput;
    overlayInput.frameSize = QSize(frame.cols, frame.rows);
    overlayInput.fallbackOrbitRadiusPx = alignmentOrbitRadiusPx();
    overlayInput.fallbackPlateScaleArcsecPx =
        206265.0 * std::max(0.001, m_alignmentPixelSizeUm / 1000.0) /
        std::max(1.0, m_alignmentFocalLengthMm);
    overlayInput.useSolvedOrbit = m_alignmentAutoRadius;
    overlayInput.radiusAdjustPx = m_alignmentRadiusAdjustPx;
    overlayInput.solveState = solveRuntime.state;
    overlayInput.solved = &solved;
    overlayInput.hasCurrentSolverResult = hasCurrentSolverResult;
    FullFrameCanvas::AlignmentOverlay overlay =
        AlignmentUiPresenter::buildAlignmentOverlay(overlayInput);

    auto& runtime = m_liveRuntime;
    QPointF star;
    double peakValue = 0.0;
    cv::Mat mono8;
    const bool manualSelectionRequested =
        m_alignmentSession.camera(cameraIndex).selectionRequested;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool shouldRefreshCandidateDetection =
        manualSelectionRequested ||
        m_alignmentCachedCandidates[cameraIndex].isEmpty() ||
        m_alignmentLastCandidateDetectionMs[cameraIndex] < 0 ||
        nowMs - m_alignmentLastCandidateDetectionMs[cameraIndex] >=
            kAlignmentCandidateDetectionRefreshMs;
    const bool allowGuiCandidateDetection = shouldRefreshCandidateDetection;
    QVector<InitialStarCandidate> candidates;
    if (allowGuiCandidateDetection) {
        candidates = collectAlignmentStarCandidates(cameraIndex,
                                                    frame,
                                                    solved,
                                                    hasCurrentSolverResult,
                                                    allowGuiCandidateDetection,
                                                    &mono8,
                                                    &peakValue);
        m_alignmentLastCandidateDetectionMs[cameraIndex] = nowMs;
    } else {
        candidates = m_alignmentCachedCandidates[cameraIndex];
    }
    if (!candidates.isEmpty()) {
        m_alignmentCachedCandidates[cameraIndex] = candidates;
        if (!handleAlignmentCandidateSelection(cameraIndex,
                                               targetCanvas,
                                               &overlay,
                                               candidates,
                                               &star)) {
            return;
        }
    } else {
        m_alignmentCachedCandidates[cameraIndex].clear();
        targetCanvas->clearStarCandidateOverlays();
        runtime.pendingInitialCandidateSelectionRequired[cameraIndex] = false;
        if (manualSelectionRequested &&
            AlignmentSession::shouldShowCandidatePrompt(
                runtime.lastInitialCandidatePromptMs[cameraIndex],
                nowMs)) {
            runtime.lastInitialCandidatePromptMs[cameraIndex] = nowMs;
            setStatusMessage(QStringLiteral("状态: 相机%1未检测到候选星点，请降低全画幅找星阈值或确认当前帧为 Mono12 原始帧")
                                 .arg(cameraIndex + 1),
                             UiStatusLevel::Warning);
        }
        updateConfirmedPolarisFromFallbackCentroid(cameraIndex,
                                                   frame,
                                                   allowGuiCandidateDetection,
                                                   &mono8,
                                                   &star,
                                                   &peakValue);
    }

    AlignmentUiPresenter::applyConfirmedPolarisToOverlay(
        runtime.hasConfirmedPolarisPosition[cameraIndex],
        runtime.confirmedPolarisPosition[cameraIndex],
        &overlay);

    targetCanvas->setAlignmentOverlay(overlay);
}
