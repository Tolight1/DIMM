#include "AcquisitionImagePolicy.h"

#include <algorithm>
#include <cmath>

namespace AcquisitionImagePolicy {

namespace {

constexpr std::int64_t kPeriodicTrackingRoiImageIntervalMs = 30 * 60 * 1000;

} // namespace

std::int64_t intervalMs(Phase phase,
                        int searchIntervalMinutes,
                        std::int64_t trackingIntervalMs)
{
    if (phase == Phase::Searching) {
        const std::int64_t minutes = std::clamp<std::int64_t>(searchIntervalMinutes, 1, 120);
        return minutes * 60 * 1000;
    }
    return std::max<std::int64_t>(1, trackingIntervalMs);
}

std::int64_t trackingIntervalMsForExposureUs(double exposureAUs,
                                              double exposureBUs,
                                              double frameRateHz,
                                              int minimumIntervalMs)
{
    const double maxExposureUs = std::max(exposureAUs, exposureBUs);
    const std::int64_t exposureDurationMs =
        std::isfinite(maxExposureUs) && maxExposureUs > 0.0
            ? std::max<std::int64_t>(1,
                                     static_cast<std::int64_t>(std::ceil(maxExposureUs / 1000.0)))
            : 1;
    const std::int64_t framePeriodMs =
        std::isfinite(frameRateHz) && frameRateHz > 0.0
            ? std::max<std::int64_t>(1,
                                     static_cast<std::int64_t>(std::ceil(1000.0 / frameRateHz)))
            : 1;
    return std::max<std::int64_t>(
        std::max<std::int64_t>(1, minimumIntervalMs),
        exposureDurationMs + framePeriodMs);
}

bool isEligibleFrameForPhase(Phase phase,
                             int width,
                             int height,
                             int fixedRoiSize)
{
    if (width <= 0 || height <= 0 || fixedRoiSize <= 0) {
        return false;
    }
    if (phase == Phase::Searching) {
        return width > fixedRoiSize && height > fixedRoiSize;
    }
    return width == fixedRoiSize && height == fixedRoiSize;
}

bool isDue(std::int64_t lastSuccessfulSaveMs,
           std::int64_t nowMs,
           std::int64_t selectedIntervalMs)
{
    if (lastSuccessfulSaveMs < 0) {
        return true;
    }
    if (nowMs < lastSuccessfulSaveMs) {
        return false;
    }
    const std::int64_t interval = std::max<std::int64_t>(1, selectedIntervalMs);
    return nowMs - lastSuccessfulSaveMs >= interval;
}

bool shouldSavePeriodicTrackingRoiImage(bool autoExposureEnabled,
                                        std::int64_t lastSuccessfulSaveMs,
                                        std::int64_t nowMs)
{
    return !autoExposureEnabled &&
           isDue(lastSuccessfulSaveMs, nowMs, kPeriodicTrackingRoiImageIntervalMs);
}

} // namespace AcquisitionImagePolicy
