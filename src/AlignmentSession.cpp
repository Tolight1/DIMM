#include "AlignmentSession.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

AlignmentSessionState& AlignmentSession::state()
{
    return m_state;
}

const AlignmentSessionState& AlignmentSession::state() const
{
    return m_state;
}

AlignmentCameraFrameState& AlignmentSession::camera(int cameraIndex)
{
    return m_state.cameras[static_cast<size_t>(std::clamp(cameraIndex, 0, kCameraCount - 1))];
}

const AlignmentCameraFrameState& AlignmentSession::camera(int cameraIndex) const
{
    return m_state.cameras[static_cast<size_t>(std::clamp(cameraIndex, 0, kCameraCount - 1))];
}

quint64 AlignmentSession::solveGeneration() const
{
    return m_state.solveGeneration;
}

quint64 AlignmentSession::advanceSolveGeneration()
{
    return ++m_state.solveGeneration;
}

void AlignmentSession::resetForStart(bool autoSolveEnabled)
{
    advanceSolveGeneration();
    for (AlignmentCameraFrameState& cameraState : m_state.cameras) {
        cameraState.lastFrame.release();
        cameraState.lastFrameId = 0;
        cameraState.lastPreviewMs = -1;
        cameraState.selectionRequested = false;
        cameraState.solveResult = PolarisSolveResult();
        cameraState.solveRuntime = AlignmentCameraSolveRuntime();
        cameraState.solveRuntime.state =
            autoSolveEnabled ? AlignmentSolveState::WaitingFrame : AlignmentSolveState::Disabled;
    }
}

void AlignmentSession::resetForStop()
{
    advanceSolveGeneration();
    for (AlignmentCameraFrameState& cameraState : m_state.cameras) {
        cameraState.lastFrame.release();
        cameraState.lastFrameId = 0;
        cameraState.lastPreviewMs = -1;
        cameraState.selectionRequested = false;
        cameraState.solveRuntime = AlignmentCameraSolveRuntime();
        cameraState.solveRuntime.state = AlignmentSolveState::Disabled;
    }
}

void AlignmentSession::resetCameraForStart(int cameraIndex,
                                           AlignmentLiveRuntimeAccess runtime,
                                           bool autoSolveEnabled)
{
    if (runtime.confirmedPolarisPosition) {
        *runtime.confirmedPolarisPosition = QPointF();
    }
    if (runtime.hasConfirmedPolarisPosition) {
        *runtime.hasConfirmedPolarisPosition = false;
    }
    if (runtime.lastTargetPosition) {
        *runtime.lastTargetPosition = QPointF();
    }
    if (runtime.hasLastTargetPosition) {
        *runtime.hasLastTargetPosition = false;
    }
    if (runtime.selectedInitialCandidateIndex) {
        *runtime.selectedInitialCandidateIndex = -1;
    }
    if (runtime.pendingInitialCandidateSelectionRequired) {
        *runtime.pendingInitialCandidateSelectionRequired = false;
    }
    if (runtime.lastInitialCandidatePromptMs) {
        *runtime.lastInitialCandidatePromptMs = -1;
    }

    AlignmentCameraFrameState& cameraState = camera(cameraIndex);
    cameraState.lastFrame.release();
    cameraState.lastFrameId = 0;
    cameraState.solveResult = PolarisSolveResult();
    cameraState.solveRuntime = AlignmentCameraSolveRuntime();
    cameraState.solveRuntime.state =
        autoSolveEnabled ? AlignmentSolveState::WaitingFrame : AlignmentSolveState::Disabled;
}

void AlignmentSession::resetCameraForStop(int cameraIndex)
{
    AlignmentCameraFrameState& cameraState = camera(cameraIndex);
    cameraState.lastFrame.release();
    cameraState.lastFrameId = 0;
    cameraState.solveRuntime = AlignmentCameraSolveRuntime();
    cameraState.solveRuntime.state = AlignmentSolveState::Disabled;
}

void AlignmentSession::applyManualConfirmation(int cameraIndex, const QPointF& selectedPosition)
{
    AlignmentCameraSolveRuntime& runtime = camera(cameraIndex).solveRuntime;
    runtime.state = AlignmentSolveState::ManualOnly;
    runtime.nextRetryMs = -1;
    runtime.consecutiveTrackFailures = 0;
    runtime.consecutiveLowConfidenceResults = 0;
    runtime.lastPolarisPosition = selectedPosition;
    runtime.hasLastPolarisPosition = true;
}

QVector<AlignmentSession::InitialStarCandidate> AlignmentSession::collectCandidates(
    const AlignmentCandidateCollectionInput& input,
    cv::Mat* mono8,
    double* peakValue)
{
    if (!input.frame || !input.solved) {
        return {};
    }

    const PolarisSolveResult& solved = *input.solved;
    const bool canReuseSolverDetections =
        input.autoSolveEnabled &&
        input.hasCurrentSolverResult &&
        solved.frameId == input.lastFrameId &&
        !solved.detections.isEmpty();
    if (canReuseSolverDetections) {
        return PolarisDetectionPipeline::initialCandidatesFromPolarisDetections(solved.detections);
    }
    if (!input.allowGuiCandidateDetection || !input.candidateDetector) {
        return {};
    }

    cv::Mat grayscale;
    if (input.frame->channels() == 1) {
        grayscale = *input.frame;
    } else {
        cv::cvtColor(*input.frame, grayscale, cv::COLOR_BGR2GRAY);
    }
    double ignoredPeak = 0.0;
    return input.candidateDetector(grayscale, peakValue ? peakValue : &ignoredPeak);
}

AlignmentSession::InitialStarSelection AlignmentSession::selectInitialCandidate(
    const AlignmentCandidateRuntimeAccess& runtime,
    const QVector<InitialStarCandidate>& candidates,
    bool manualSelectionRequested)
{
    if (!manualSelectionRequested) {
        if (runtime.selectedInitialCandidateIndex &&
            *runtime.selectedInitialCandidateIndex > 0) {
            const int selectedIndex = *runtime.selectedInitialCandidateIndex;
            for (const InitialStarCandidate& candidate : candidates) {
                if (candidate.index == selectedIndex) {
                    InitialStarSelection selection;
                    selection.selected = true;
                    selection.candidate = candidate;
                    return selection;
                }
            }
        }

    }

    return PolarisDetectionPipeline::selectFullFrameStarCandidate(
        candidates,
        -1,
        false);
}

void AlignmentSession::recordSelectedCandidate(AlignmentCandidateRuntimeAccess runtime,
                                               const QPointF& star,
                                               int selectedCandidateIndex)
{
    if (runtime.confirmedPolarisPosition) {
        *runtime.confirmedPolarisPosition = star;
    }
    if (runtime.hasConfirmedPolarisPosition) {
        *runtime.hasConfirmedPolarisPosition = true;
    }
    if (runtime.lastTargetPosition) {
        *runtime.lastTargetPosition = star;
    }
    if (runtime.hasLastTargetPosition) {
        *runtime.hasLastTargetPosition = true;
    }
    if (runtime.pendingInitialCandidateSelectionRequired) {
        *runtime.pendingInitialCandidateSelectionRequired = false;
    }
    if (runtime.selectedInitialCandidateIndex) {
        *runtime.selectedInitialCandidateIndex = selectedCandidateIndex;
    }
    if (runtime.selectionRequested) {
        *runtime.selectionRequested = false;
    }
}

void AlignmentSession::updateFromFallbackCentroid(
    AlignmentCandidateRuntimeAccess runtime,
    const cv::Mat& frame,
    bool allowGuiCandidateDetection,
    cv::Mat* mono8,
    QPointF* star,
    double* peakValue,
    const CentroidDetector& centroidDetector)
{
    if (!allowGuiCandidateDetection ||
        !runtime.hasConfirmedPolarisPosition ||
        !*runtime.hasConfirmedPolarisPosition ||
        !mono8 ||
        !star ||
        !centroidDetector) {
        return;
    }

    if (mono8->empty()) {
        cv::Mat grayscale;
        if (frame.channels() == 1) {
            grayscale = frame;
        } else {
            cv::cvtColor(frame, grayscale, cv::COLOR_BGR2GRAY);
        }
        *mono8 = grayscale;
    }
    if (mono8->empty() || !centroidDetector(*mono8, star, peakValue)) {
        return;
    }

    recordSelectedCandidate(runtime, *star,
                            runtime.selectedInitialCandidateIndex
                                ? *runtime.selectedInitialCandidateIndex
                                : -1);
}

bool AlignmentSession::canApplyCandidateSelection(bool manualSelectionRequested,
                                                  bool hadConfirmedPolarisBeforeSelection)
{
    return manualSelectionRequested || hadConfirmedPolarisBeforeSelection;
}

bool AlignmentSession::shouldShowCandidatePrompt(qint64 lastPromptMs,
                                                 qint64 nowMs,
                                                 qint64 promptIntervalMs)
{
    return lastPromptMs < 0 || nowMs - lastPromptMs >= promptIntervalMs;
}

void AlignmentSession::recordCandidatePromptCancelled(qint64* lastPromptMs, qint64 nowMs)
{
    if (lastPromptMs) {
        *lastPromptMs = nowMs;
    }
}

void AlignmentSession::recordCandidatePromptAccepted(int* selectedCandidateIndex,
                                                     qint64* lastPromptMs,
                                                     int chosenCandidateIndex)
{
    if (selectedCandidateIndex) {
        *selectedCandidateIndex = chosenCandidateIndex;
    }
    if (lastPromptMs) {
        *lastPromptMs = -1;
    }
}

QString AlignmentSession::manualConfirmedMessage(const QPointF& selectedPosition)
{
    return QStringLiteral("(%1, %2)")
        .arg(selectedPosition.x(), 0, 'f', 1)
        .arg(selectedPosition.y(), 0, 'f', 1);
}

QStringList AlignmentSession::candidatePromptLines(const QVector<InitialStarCandidate>& candidates)
{
    QStringList candidateLines;
    candidateLines.reserve(candidates.size());
    for (const InitialStarCandidate& candidate : candidates) {
        candidateLines << QStringLiteral("候选 #%1：中心=(%2, %3)，面积=%4，峰值=%5，信号总和=%6")
                              .arg(candidate.index)
                              .arg(candidate.center.x(), 0, 'f', 1)
                              .arg(candidate.center.y(), 0, 'f', 1)
                              .arg(candidate.area)
                              .arg(candidate.peak, 0, 'f', 1)
                              .arg(candidate.signal, 0, 'f', 1);
    }
    return candidateLines;
}
