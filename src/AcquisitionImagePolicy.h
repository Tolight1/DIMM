#pragma once

#include <cstdint>

namespace AcquisitionImagePolicy {

enum class Phase {
    Searching,
    Tracking
};

std::int64_t intervalMs(Phase phase,
                        int searchIntervalMinutes,
                        std::int64_t trackingIntervalMs);
std::int64_t trackingIntervalMsForExposureUs(double exposureAUs,
                                              double exposureBUs,
                                              double frameRateHz,
                                              int minimumIntervalMs);
bool isEligibleFrameForPhase(Phase phase,
                             int width,
                             int height,
                             int fixedRoiSize);
bool isDue(std::int64_t lastSuccessfulSaveMs,
           std::int64_t nowMs,
           std::int64_t intervalMs);
bool shouldSavePeriodicTrackingRoiImage(bool autoExposureEnabled,
                                        std::int64_t lastSuccessfulSaveMs,
                                        std::int64_t nowMs);

} // namespace AcquisitionImagePolicy
