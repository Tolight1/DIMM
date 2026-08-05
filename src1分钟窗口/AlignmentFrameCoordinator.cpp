#include "AlignmentFrameCoordinator.h"

#include <algorithm>

int AlignmentFrameCoordinator::previewIntervalMs(double previewRateHz)
{
    return std::max(100, static_cast<int>(1000.0 / std::max(0.1, previewRateHz)));
}

bool AlignmentFrameCoordinator::shouldAcceptAlignmentFrame(const FrameGateInput& input)
{
    if (!input.alignmentActive ||
        input.cameraIndex < 0 ||
        input.cameraIndex >= 2 ||
        !input.frame ||
        input.frame->empty()) {
        return false;
    }

    return input.lastPreviewMs < 0 || input.nowMs - input.lastPreviewMs >= input.previewIntervalMs;
}

AlignmentFrameCoordinator::FrameAction AlignmentFrameCoordinator::nextFrameAction(
    bool autoSolveEnabled,
    const AlignmentCameraSolveRuntime& runtime,
    qint64 nowMs)
{
    if (runtime.state == AlignmentSolveState::ManualOnly) {
        return FrameAction::ManualTrack;
    }
    if (!autoSolveEnabled) {
        return FrameAction::Disabled;
    }
    if (runtime.state == AlignmentSolveState::Tracking) {
        return FrameAction::AutomaticTrack;
    }
    if (runtime.state == AlignmentSolveState::RetryWaiting &&
        runtime.nextRetryMs > nowMs) {
        return FrameAction::WaitRetry;
    }
    return FrameAction::RequestSolve;
}
