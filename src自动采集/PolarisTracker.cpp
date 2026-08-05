#include "PolarisTracker.h"

#include <algorithm>
#include <cmath>

QRect PolarisTracker::trackingWindowForPosition(const QPointF& position,
                                                const QSize& frameSize,
                                                int halfWindowPx)
{
    const int safeHalfWindowPx = std::max(1, halfWindowPx);
    const QRect imageRect(0, 0, frameSize.width(), frameSize.height());
    QRect trackingWindow(static_cast<int>(std::floor(position.x())) - safeHalfWindowPx,
                         static_cast<int>(std::floor(position.y())) - safeHalfWindowPx,
                         safeHalfWindowPx * 2 + 1,
                         safeHalfWindowPx * 2 + 1);
    return trackingWindow.intersected(imageRect);
}

bool PolarisTracker::isUsableTrackingWindow(const QRect& trackingWindow, int minSidePx)
{
    const int safeMinSidePx = std::max(1, minSidePx);
    return trackingWindow.width() >= safeMinSidePx &&
           trackingWindow.height() >= safeMinSidePx;
}

void PolarisTracker::recordTrackSuccess(AlignmentCameraSolveRuntime* runtime,
                                        const QPointF& trackedPosition)
{
    if (!runtime) {
        return;
    }
    runtime->state = AlignmentSolveState::Tracking;
    runtime->consecutiveTrackFailures = 0;
    runtime->lastPolarisPosition = trackedPosition;
    runtime->hasLastPolarisPosition = true;
}

bool PolarisTracker::recordTrackFailure(AlignmentCameraSolveRuntime* runtime,
                                        int lostTrackRetryCount,
                                        qint64 nowMs,
                                        qint64 retryIntervalMs)
{
    if (!runtime) {
        return false;
    }
    ++runtime->consecutiveTrackFailures;
    if (runtime->consecutiveTrackFailures < std::max(1, lostTrackRetryCount)) {
        return false;
    }
    runtime->state = AlignmentSolveState::RetryWaiting;
    runtime->nextRetryMs = nowMs + std::max<qint64>(0, retryIntervalMs);
    return true;
}

bool PolarisTracker::shouldHoldFullSolveRequest(const AlignmentCameraSolveRuntime& runtime,
                                                bool force,
                                                qint64 nowMs)
{
    if (force) {
        return false;
    }
    return runtime.state == AlignmentSolveState::FullSolving ||
           runtime.state == AlignmentSolveState::Tracking ||
           (runtime.state == AlignmentSolveState::RetryWaiting &&
            runtime.nextRetryMs > nowMs);
}

void PolarisTracker::markFullSolveSubmitted(AlignmentCameraSolveRuntime* runtime, qint64 nowMs)
{
    if (!runtime) {
        return;
    }
    runtime->state = AlignmentSolveState::FullSolving;
    runtime->lastFullSolveMs = nowMs;
    runtime->nextRetryMs = -1;
}
