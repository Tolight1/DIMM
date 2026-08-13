#pragma once

#include "CameraTypes.h"
#include "PolarisSolver.h"

#include <array>
#include <QPointF>
#include <QRect>

#include <opencv2/core/mat.hpp>

enum class AlignmentSolveState {
    Disabled,
    WaitingFrame,
    FullSolving,
    Tracking,
    RetryWaiting,
    ManualOnly,
    Error
};

struct AlignmentCameraSolveRuntime {
    AlignmentSolveState state = AlignmentSolveState::WaitingFrame;
    qint64 lastFullSolveMs = -1;
    qint64 nextRetryMs = -1;
    int consecutiveTrackFailures = 0;
    int consecutiveLowConfidenceResults = 0;
    QRect trackingWindow;
    QPointF lastPolarisPosition;
    bool hasLastPolarisPosition = false;
    PolarisSolveResult lastFullSolve;
};

struct AlignmentCameraFrameState {
    cv::Mat lastFrame;
    quint64 lastFrameId = 0;
    qint64 lastPreviewMs = -1;
    bool selectionRequested = false;
    PolarisSolveResult solveResult;
    AlignmentCameraSolveRuntime solveRuntime;
};

struct AlignmentSessionState {
    std::array<AlignmentCameraFrameState, kCameraCount> cameras;
    quint64 solveGeneration = 0;
};
