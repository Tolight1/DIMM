#include "AlignmentController.h"

bool AlignmentController::shouldIgnoreSolverResult(const AlignmentCameraSolveRuntime& runtime)
{
    return runtime.state == AlignmentSolveState::ManualOnly;
}

void AlignmentController::applyManualTrackingSuccess(AlignmentCameraSolveRuntime* runtime,
                                                     const QPointF& trackedPosition)
{
    if (!runtime) {
        return;
    }

    runtime->state = AlignmentSolveState::ManualOnly;
    runtime->lastPolarisPosition = trackedPosition;
    runtime->hasLastPolarisPosition = true;
    runtime->consecutiveTrackFailures = 0;
}

void AlignmentController::applyManualTrackingFailure(AlignmentCameraSolveRuntime* runtime)
{
    if (!runtime) {
        return;
    }

    runtime->state = AlignmentSolveState::ManualOnly;
    ++runtime->consecutiveTrackFailures;
}

void AlignmentController::syncTrackedPolarisPosition(int cameraIndex,
                                                     const QPointF& trackedPosition,
                                                     QPointF confirmedPolarisPosition[2],
                                                     bool hasConfirmedPolarisPosition[2],
                                                     QPointF lastTargetPosition[2],
                                                     bool hasLastTargetPosition[2])
{
    if (cameraIndex < 0 || cameraIndex >= 2) {
        return;
    }

    confirmedPolarisPosition[cameraIndex] = trackedPosition;
    hasConfirmedPolarisPosition[cameraIndex] = true;
    lastTargetPosition[cameraIndex] = trackedPosition;
    hasLastTargetPosition[cameraIndex] = true;
}

void AlignmentController::applyDetectedPolarisSolve(AlignmentCameraSolveRuntime* runtime,
                                                    const PolarisSolveResult& result)
{
    if (!runtime) {
        return;
    }

    runtime->state = AlignmentSolveState::Tracking;
    runtime->consecutiveTrackFailures = 0;
    runtime->consecutiveLowConfidenceResults = 0;
    runtime->nextRetryMs = -1;
    runtime->lastPolarisPosition = result.detectedPolarisPixel;
    runtime->hasLastPolarisPosition = true;
}

void AlignmentController::applyPredictedOnlyRetry(AlignmentCameraSolveRuntime* runtime,
                                                  qint64 nowMs,
                                                  qint64 retryIntervalMs)
{
    if (!runtime) {
        return;
    }

    runtime->state = AlignmentSolveState::RetryWaiting;
    runtime->nextRetryMs = nowMs + retryIntervalMs;
}

void AlignmentController::applySolveError(AlignmentCameraSolveRuntime* runtime)
{
    if (!runtime) {
        return;
    }

    runtime->state = AlignmentSolveState::Error;
}
