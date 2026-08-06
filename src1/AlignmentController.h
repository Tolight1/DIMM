#pragma once

#include "AlignmentTypes.h"

class AlignmentController final {
public:
    static bool shouldIgnoreSolverResult(const AlignmentCameraSolveRuntime& runtime);
    static void applyManualTrackingSuccess(AlignmentCameraSolveRuntime* runtime,
                                           const QPointF& trackedPosition);
    static void applyManualTrackingFailure(AlignmentCameraSolveRuntime* runtime);
    static void syncTrackedPolarisPosition(int cameraIndex,
                                           const QPointF& trackedPosition,
                                           QPointF confirmedPolarisPosition[2],
                                           bool hasConfirmedPolarisPosition[2],
                                           QPointF lastTargetPosition[2],
                                           bool hasLastTargetPosition[2]);
    static void applyDetectedPolarisSolve(AlignmentCameraSolveRuntime* runtime,
                                          const PolarisSolveResult& result);
    static void applyPredictedOnlyRetry(AlignmentCameraSolveRuntime* runtime,
                                        qint64 nowMs,
                                        qint64 retryIntervalMs);
    static void applySolveError(AlignmentCameraSolveRuntime* runtime);
};
