#pragma once

#include <QtGlobal>

#include <opencv2/opencv.hpp>

#include "AlignmentTypes.h"

namespace AlignmentFrameCoordinator {
enum class FrameAction {
    Disabled,
    ManualTrack,
    AutomaticTrack,
    WaitRetry,
    RequestSolve
};

struct FrameGateInput {
    bool alignmentActive = false;
    int cameraIndex = -1;
    qint64 nowMs = 0;
    qint64 lastPreviewMs = -1;
    int previewIntervalMs = 1000;
    const cv::Mat* frame = nullptr;
};

int previewIntervalMs(double previewRateHz);

bool shouldAcceptAlignmentFrame(const FrameGateInput& input);

FrameAction nextFrameAction(bool autoSolveEnabled,
                            const AlignmentCameraSolveRuntime& runtime,
                            qint64 nowMs);
}
