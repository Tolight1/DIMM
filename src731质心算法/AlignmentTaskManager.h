#pragma once

#include "AlignmentTypes.h"

#include <QString>

enum class AlignmentFullSolveRequestAction {
    Hold,
    Submit
};

struct AlignmentRetryDecision {
    qint64 retryDelayMs = 0;
    QString diagnosticHint;
};

class AlignmentTaskManager final {
public:
    static AlignmentFullSolveRequestAction prepareFullSolveRequest(AlignmentCameraSolveRuntime* runtime,
                                                                    bool force,
                                                                    qint64 nowMs);
    static AlignmentRetryDecision applySolveFailureRetry(AlignmentCameraSolveRuntime* runtime,
                                                         PolarisSolveStatus status,
                                                         qint64 nowMs,
                                                         qint64 baseRetryIntervalMs);
    static void submitFullSolve(PolarisSolverController* solverController,
                                int cameraIndex,
                                const cv::Mat& frame,
                                const PolarisSolverConfig& config,
                                quint64 generation,
                                quint64 frameId,
                                bool force);
};
