#pragma once

#include "AlignmentTypes.h"

#include <QPointF>
#include <QRect>
#include <QSize>

namespace PolarisTracker {
QRect trackingWindowForPosition(const QPointF& position,
                                const QSize& frameSize,
                                int halfWindowPx = 96);
bool isUsableTrackingWindow(const QRect& trackingWindow, int minSidePx = 16);
void recordTrackSuccess(AlignmentCameraSolveRuntime* runtime,
                        const QPointF& trackedPosition);
bool recordTrackFailure(AlignmentCameraSolveRuntime* runtime,
                        int lostTrackRetryCount,
                        qint64 nowMs,
                        qint64 retryIntervalMs);
bool shouldHoldFullSolveRequest(const AlignmentCameraSolveRuntime& runtime,
                                bool force,
                                qint64 nowMs);
void markFullSolveSubmitted(AlignmentCameraSolveRuntime* runtime, qint64 nowMs);
}
