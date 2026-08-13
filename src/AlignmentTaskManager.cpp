#include "AlignmentTaskManager.h"

#include "PolarisTracker.h"

#include <algorithm>

AlignmentFullSolveRequestAction AlignmentTaskManager::prepareFullSolveRequest(
    AlignmentCameraSolveRuntime* runtime,
    bool force,
    qint64 nowMs)
{
    if (!runtime) {
        return AlignmentFullSolveRequestAction::Hold;
    }
    if (runtime->state == AlignmentSolveState::ManualOnly && !force) {
        return AlignmentFullSolveRequestAction::Hold;
    }
    if (force && runtime->state == AlignmentSolveState::ManualOnly) {
        runtime->state = AlignmentSolveState::WaitingFrame;
        runtime->nextRetryMs = -1;
        runtime->consecutiveTrackFailures = 0;
        runtime->consecutiveLowConfidenceResults = 0;
    }
    if (PolarisTracker::shouldHoldFullSolveRequest(*runtime, force, nowMs)) {
        return AlignmentFullSolveRequestAction::Hold;
    }

    PolarisTracker::markFullSolveSubmitted(runtime, nowMs);
    return AlignmentFullSolveRequestAction::Submit;
}

AlignmentRetryDecision AlignmentTaskManager::applySolveFailureRetry(
    AlignmentCameraSolveRuntime* runtime,
    PolarisSolveStatus status,
    qint64 nowMs,
    qint64 baseRetryIntervalMs)
{
    AlignmentRetryDecision decision;
    if (!runtime) {
        return decision;
    }

    runtime->state = AlignmentSolveState::RetryWaiting;
    int lowConfidenceRetryMultiplier = 1;
    if (status == PolarisSolveStatus::LowConfidence) {
        ++runtime->consecutiveLowConfidenceResults;
        lowConfidenceRetryMultiplier =
            std::min(4, std::max(1, runtime->consecutiveLowConfidenceResults));
        if (runtime->consecutiveLowConfidenceResults >= 2) {
            decision.diagnosticHint = QStringLiteral("；连续低置信度，请检查焦距、星点数量和阈值");
        }
    } else {
        runtime->consecutiveLowConfidenceResults = 0;
    }

    decision.retryDelayMs = baseRetryIntervalMs * lowConfidenceRetryMultiplier;
    runtime->nextRetryMs = nowMs + decision.retryDelayMs;
    return decision;
}

void AlignmentTaskManager::submitFullSolve(PolarisSolverController* solverController,
                                           int cameraIndex,
                                           const cv::Mat& frame,
                                           const PolarisSolverConfig& config,
                                           quint64 generation,
                                           quint64 frameId,
                                           bool force)
{
    if (!solverController) {
        return;
    }

    solverController->submitFrame(cameraIndex,
                                  frame,
                                  config,
                                  generation,
                                  frameId,
                                  force);
}
