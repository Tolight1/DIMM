#pragma once

#include "AlignmentTypes.h"

#include <QPointF>

#include <opencv2/opencv.hpp>

namespace AlignmentRuntimeCoordinator {
struct LiveRuntimeAccess {
    QPointF* confirmedPolarisPosition = nullptr;
    bool* hasConfirmedPolarisPosition = nullptr;
    QPointF* lastTargetPosition = nullptr;
    bool* hasLastTargetPosition = nullptr;
    int* selectedInitialCandidateIndex = nullptr;
    bool* pendingInitialCandidateSelectionRequired = nullptr;
    qint64* lastInitialCandidatePromptMs = nullptr;
};

void resetCameraForStart(LiveRuntimeAccess runtime,
                         cv::Mat* lastFrame,
                         quint64* lastFrameId,
                         PolarisSolveResult* solveResult,
                         AlignmentCameraSolveRuntime* solveRuntime,
                         bool autoSolveEnabled);

void resetCameraForStop(cv::Mat* lastFrame,
                        quint64* lastFrameId,
                        AlignmentCameraSolveRuntime* solveRuntime);
}
