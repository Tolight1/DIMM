#include "AutoFocusRealtimeMetricsSampler.h"

#include <cmath>
#include <utility>

namespace {

constexpr std::int64_t kMinimumEmitIntervalMs = 1000;

}  // namespace

void AutoFocusRealtimeMetricsSampler::configure(int framesPerGroup,
                                                 AutoFocusStatisticsMode statisticsMode,
                                                 double trimRatio)
{
    m_framesPerGroup = framesPerGroup;
    m_statisticsMode = statisticsMode;
    m_trimRatio = trimRatio;
    reset();
}

void AutoFocusRealtimeMetricsSampler::reset()
{
    for (CameraState& camera : m_camera) {
        camera = CameraState{};
    }
}

bool AutoFocusRealtimeMetricsSampler::isUsableSample(const AutoFocusSample& sample)
{
    return sample.valid && std::isfinite(sample.hfr) && sample.hfr > 0.0 &&
           std::isfinite(sample.rms) && sample.rms > 0.0;
}

void AutoFocusRealtimeMetricsSampler::submitSample(int cameraIndex,
                                                   const AutoFocusSample& sample,
                                                   std::int64_t timestampMs)
{
    if (cameraIndex < 0 || cameraIndex >= static_cast<int>(m_camera.size()) ||
        m_framesPerGroup <= 0 || !isUsableSample(sample)) {
        return;
    }

    CameraState& camera = m_camera[static_cast<std::size_t>(cameraIndex)];
    // Keep the completed group intact until the main CSV consumes it. Samples
    // after completion belong to the next one-second slot, not this group.
    if (camera.completedMetrics.has_value()) {
        return;
    }

    if (camera.samples.empty()) {
        camera.groupStartedMs = timestampMs;
        camera.samples.reserve(static_cast<std::size_t>(m_framesPerGroup));
    }
    camera.samples.push_back(sample);
    if (static_cast<int>(camera.samples.size()) < m_framesPerGroup) {
        return;
    }

    const AutoFocusMetrics metrics =
        AutoFocusController::aggregate(camera.samples, m_statisticsMode, m_trimRatio);
    camera.samples.clear();
    if (metrics.valid && metrics.validFrameCount == static_cast<std::size_t>(m_framesPerGroup)) {
        camera.completedMetrics = metrics;
    } else {
        camera.groupStartedMs = -1;
    }
}

std::optional<AutoFocusMetrics>
AutoFocusRealtimeMetricsSampler::takeCompletedMetrics(int cameraIndex,
                                                      std::int64_t nowMs)
{
    if (cameraIndex < 0 || cameraIndex >= static_cast<int>(m_camera.size())) {
        return std::nullopt;
    }

    CameraState& camera = m_camera[static_cast<std::size_t>(cameraIndex)];
    if (!camera.completedMetrics.has_value() || camera.groupStartedMs < 0) {
        return std::nullopt;
    }

    const std::int64_t referenceMs = camera.lastEmitMs >= 0
                                         ? camera.lastEmitMs
                                         : camera.groupStartedMs;
    if (nowMs < referenceMs || nowMs - referenceMs < kMinimumEmitIntervalMs) {
        return std::nullopt;
    }

    std::optional<AutoFocusMetrics> result = std::move(camera.completedMetrics);
    camera.completedMetrics.reset();
    camera.samples.clear();
    camera.groupStartedMs = -1;
    camera.lastEmitMs = nowMs;
    return result;
}
