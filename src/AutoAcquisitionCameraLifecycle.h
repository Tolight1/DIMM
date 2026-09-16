#pragma once

struct AutoAcquisitionCameraLifecycleDecision {
    bool needsOpenAll = false;
    bool closeAllOnEnd = false;
};

AutoAcquisitionCameraLifecycleDecision decideAutoAcquisitionCameraLifecycle(
    int openCameraCount,
    int requiredCameraCount = 2);
