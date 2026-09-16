#pragma once

#include "AutoFocusController.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

// Collects the same valid-frame groups used by autofocus, but remains active
// for every Tracking session so the main acquisition CSV can show live HFR/RMS.
class AutoFocusRealtimeMetricsSampler final {
public:
    AutoFocusRealtimeMetricsSampler() = default;

    void configure(int framesPerGroup,
                   AutoFocusStatisticsMode statisticsMode,
                   double trimRatio = 0.0);
    void reset();

    void submitSample(int cameraIndex,
                      const AutoFocusSample& sample,
                      std::int64_t timestampMs);
    std::optional<AutoFocusMetrics> takeCompletedMetrics(int cameraIndex,
                                                         std::int64_t nowMs);

private:
    struct CameraState {
        std::vector<AutoFocusSample> samples;
        std::optional<AutoFocusMetrics> completedMetrics;
        std::int64_t groupStartedMs = -1;
        std::int64_t lastEmitMs = -1;
    };

    static bool isUsableSample(const AutoFocusSample& sample);

    int m_framesPerGroup = 0;
    AutoFocusStatisticsMode m_statisticsMode = AutoFocusStatisticsMode::Mean;
    double m_trimRatio = 0.0;
    std::array<CameraState, 2> m_camera;
};
