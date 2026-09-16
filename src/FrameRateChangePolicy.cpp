#include "FrameRateChangePolicy.h"

#include <algorithm>
#include <cmath>

bool needsFrameRateChange(double oldRateHz, double newRateHz)
{
    if (!std::isfinite(oldRateHz) || !std::isfinite(newRateHz)) {
        return true;
    }
    const double tolerance = std::max(0.05, std::abs(oldRateHz) * 0.005);
    return std::abs(oldRateHz - newRateHz) > tolerance;
}
