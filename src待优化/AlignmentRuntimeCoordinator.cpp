#include "AlignmentRuntimeCoordinator.h"

#include "AlignmentFlowCoordinator.h"

void AlignmentRuntimeCoordinator::resetCameraForStart(
    LiveRuntimeAccess runtime,
    cv::Mat* lastFrame,
    quint64* lastFrameId,
    PolarisSolveResult* solveResult,
    AlignmentCameraSolveRuntime* solveRuntime,
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
    if (lastFrame) {
        lastFrame->release();
    }
    if (lastFrameId) {
        *lastFrameId = 0;
    }
    if (solveResult) {
        *solveResult = PolarisSolveResult();
    }
    if (solveRuntime) {
        *solveRuntime = AlignmentCameraSolveRuntime();
        solveRuntime->state = AlignmentFlowCoordinator::initialSolveState(autoSolveEnabled);
    }
}

void AlignmentRuntimeCoordinator::resetCameraForStop(
    cv::Mat* lastFrame,
    quint64* lastFrameId,
    AlignmentCameraSolveRuntime* solveRuntime)
{
    if (lastFrame) {
        lastFrame->release();
    }
    if (lastFrameId) {
        *lastFrameId = 0;
    }
    if (solveRuntime) {
        *solveRuntime = AlignmentCameraSolveRuntime();
        solveRuntime->state = AlignmentSolveState::Disabled;
    }
}
