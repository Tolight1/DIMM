#include "AutoAcquisitionCameraLifecycle.h"

#include <algorithm>

AutoAcquisitionCameraLifecycleDecision decideAutoAcquisitionCameraLifecycle(
    int openCameraCount,
    int requiredCameraCount)
{
    const int required = std::max(0, requiredCameraCount);
    AutoAcquisitionCameraLifecycleDecision decision;
    decision.needsOpenAll = openCameraCount < required;
    decision.closeAllOnEnd = true;
    return decision;
}
