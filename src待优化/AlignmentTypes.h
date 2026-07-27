#pragma once

#include "PolarisSolver.h"

#include <QPointF>
#include <QRect>

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
