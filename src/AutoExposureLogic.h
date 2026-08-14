#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

enum class AutoExposurePeakQuality {
    ValidSpotPeak,
    RejectedIsolatedPeak,
    WeakOrNoSignal,
    SpotSaturated
};

enum class AutoExposureAdjustDirection {
    Hold,
    Increase,
    Decrease
};

struct AutoExposureSpotConfig {
    double thresholdDn = 16.0;
    double hardSaturationDn = 4090.0;
    int saturatedPixelCount = 1;
    int supportRadiusPx = 2;
    double supportFraction = 0.50;
    int minSupportPixelCount = 3;
    double minNeighborPeakRatio = 0.35;
    int maxCandidateCount = 8;
    double supportedPeakPercentile = 0.95;
};

struct AutoExposureSpotResult {
    AutoExposurePeakQuality quality = AutoExposurePeakQuality::WeakOrNoSignal;
    bool decisionSample = true;
    bool validSpotPeak = false;
    bool spotHardSaturated = false;
    double peakDn = 0.0;
    double supportedPeakDn = 0.0;
    double rejectedPeakDn = 0.0;
    int peakX = -1;
    int peakY = -1;
    int rejectedPeakX = -1;
    int rejectedPeakY = -1;
    int rejectedCandidateCount = 0;
    int supportPixelCount = 0;
    int spotSaturatedPixelCount = 0;
};

struct AutoExposureDecisionConfig {
    double targetPeakLowDn = 500.0;
    double targetPeakHighDn = 3600.0;
    double exposureHysteresisDn = 300.0;
    double hardSaturationDn = 4090.0;
    double darkFrameRatioThreshold = 0.60;
    double brightFrameRatioThreshold = 0.60;
    double stableFrameRatioThreshold = 0.70;
    double hardSaturationFrameRatioThreshold = 0.05;
    int minDecisionSampleCount = 500;
    double autoExposureStepUs = 200.0;
    double minExposureUs = 500.0;
    double maxExposureUs = 20000.0;
    double maxExposureChangeRatioUp = 1.30;
    double maxExposureChangeRatioDown = 0.70;
    double minExposureDeltaUs = 10.0;
    double minExposureChangeRatio = 0.02;
};

inline bool shouldApplyAutoExposureInitialExposure(bool autoAcquisitionEnabled,
                                                    bool autoExposureEnabled)
{
    return autoAcquisitionEnabled && autoExposureEnabled;
}

struct AutoExposureWindowStats {
    int decisionSampleCount = 0;
    int validPeakSampleCount = 0;
    int invalidPeakSampleCount = 0;
    int rejectedIsolatedPeakCount = 0;
    int weakOrNoSignalCount = 0;
    int darkEvidenceCount = 0;
    int brightFrameCount = 0;
    int stableFrameCount = 0;
    int spotHardSaturationFrameCount = 0;
    double peakP50Dn = 0.0;
    double peakP90Dn = 0.0;
    double peakP95Dn = 0.0;
    double latestPeakDn = 0.0;
    double darkRatio = 0.0;
    double brightRatio = 0.0;
    double stableRatio = 0.0;
    double hardSaturationRatio = 0.0;
    double invalidPeakRatio = 0.0;
};

struct AutoExposureControlAction {
    AutoExposureAdjustDirection direction = AutoExposureAdjustDirection::Hold;
    int targetExposureUs = 0;
    std::string reason = "HOLD";
};

namespace AutoExposureLogicDetail {

struct PeakCandidate {
    int x = 0;
    int y = 0;
    double value = 0.0;
};

inline bool inBounds(int x, int y, int width, int height)
{
    return x >= 0 && y >= 0 && x < width && y < height;
}

inline double pixelAt(const double* pixels, int width, int x, int y)
{
    return pixels[static_cast<std::size_t>(y * width + x)];
}

inline double percentile(std::vector<double> values, double fraction)
{
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const double clamped = std::clamp(fraction, 0.0, 1.0);
    const auto index = static_cast<std::size_t>(
        std::floor(clamped * static_cast<double>(values.size() - 1)));
    return values[index];
}

inline bool isLocalMaximum(const double* pixels, int width, int height, int x, int y)
{
    const double value = pixelAt(pixels, width, x, y);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            const int nx = x + dx;
            const int ny = y + dy;
            if (!inBounds(nx, ny, width, height)) {
                continue;
            }
            if (pixelAt(pixels, width, nx, ny) > value) {
                return false;
            }
        }
    }
    return true;
}

inline std::vector<PeakCandidate> collectPeakCandidates(const double* pixels,
                                                        int width,
                                                        int height,
                                                        double thresholdDn)
{
    std::vector<PeakCandidate> candidates;
    if (!pixels || width <= 0 || height <= 0) {
        return candidates;
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double value = pixelAt(pixels, width, x, y);
            if (value <= thresholdDn || !isLocalMaximum(pixels, width, height, x, y)) {
                continue;
            }
            candidates.push_back(PeakCandidate{x, y, value});
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const PeakCandidate& a, const PeakCandidate& b) {
        return a.value > b.value;
    });
    return candidates;
}

inline std::vector<double> connectedSupportValues(const double* pixels,
                                                  int width,
                                                  int height,
                                                  const PeakCandidate& candidate,
                                                  const AutoExposureSpotConfig& config)
{
    const int radius = std::max(1, config.supportRadiusPx);
    const double supportThreshold =
        std::max(config.thresholdDn, candidate.value * std::clamp(config.supportFraction, 0.0, 1.0));
    std::vector<double> values;
    std::vector<std::pair<int, int>> stack;
    std::vector<unsigned char> visited(static_cast<std::size_t>(width * height), 0);
    auto markIndex = [width](int x, int y) {
        return static_cast<std::size_t>(y * width + x);
    };

    stack.push_back({candidate.x, candidate.y});
    visited[markIndex(candidate.x, candidate.y)] = 1;
    while (!stack.empty()) {
        const auto current = stack.back();
        stack.pop_back();
        const int x = current.first;
        const int y = current.second;
        const double value = pixelAt(pixels, width, x, y);
        if (value < supportThreshold) {
            continue;
        }
        values.push_back(value);
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) {
                    continue;
                }
                const int nx = x + dx;
                const int ny = y + dy;
                if (!inBounds(nx, ny, width, height)) {
                    continue;
                }
                if (std::abs(nx - candidate.x) > radius || std::abs(ny - candidate.y) > radius) {
                    continue;
                }
                const std::size_t index = markIndex(nx, ny);
                if (visited[index]) {
                    continue;
                }
                visited[index] = 1;
                stack.push_back({nx, ny});
            }
        }
    }
    return values;
}

inline double strongestNeighborRatio(const double* pixels,
                                     int width,
                                     int height,
                                     const PeakCandidate& candidate)
{
    double strongestNeighbor = 0.0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            const int nx = candidate.x + dx;
            const int ny = candidate.y + dy;
            if (!inBounds(nx, ny, width, height)) {
                continue;
            }
            strongestNeighbor = std::max(strongestNeighbor, pixelAt(pixels, width, nx, ny));
        }
    }
    return candidate.value > 0.0 ? strongestNeighbor / candidate.value : 0.0;
}

inline int countSaturated(const std::vector<double>& values, double hardSaturationDn)
{
    return static_cast<int>(std::count_if(values.begin(), values.end(), [hardSaturationDn](double value) {
        return value >= hardSaturationDn;
    }));
}

inline int roundedExposure(double value)
{
    return static_cast<int>(std::lround(std::max(1.0, value)));
}

inline AutoExposureControlAction holdAction(double currentExposureUs, const std::string& reason)
{
    AutoExposureControlAction action;
    action.direction = AutoExposureAdjustDirection::Hold;
    action.targetExposureUs = roundedExposure(currentExposureUs);
    action.reason = reason;
    return action;
}

inline AutoExposureControlAction exposureAction(AutoExposureAdjustDirection direction,
                                                double currentExposureUs,
                                                double targetExposureUs,
                                                const AutoExposureDecisionConfig& config,
                                                const std::string& reason)
{
    const double clampedTarget = std::clamp(targetExposureUs, config.minExposureUs, config.maxExposureUs);
    const double absoluteDelta = std::abs(clampedTarget - currentExposureUs);
    if (absoluteDelta <= 0.0) {
        return holdAction(currentExposureUs, "SMALL_DELTA_HOLD");
    }
    AutoExposureControlAction action;
    action.direction = direction;
    action.targetExposureUs = roundedExposure(clampedTarget);
    action.reason = reason;
    return action;
}

inline double darkAdjustmentTargetDn(const AutoExposureDecisionConfig& config)
{
    return config.targetPeakLowDn + std::max(0.0, config.exposureHysteresisDn);
}

inline double brightAdjustmentTargetDn(const AutoExposureDecisionConfig& config)
{
    return config.targetPeakHighDn - std::max(0.0, config.exposureHysteresisDn);
}

} // namespace AutoExposureLogicDetail

inline AutoExposureSpotResult analyzeAutoExposureSpot(const double* pixels,
                                                      int width,
                                                      int height,
                                                      const AutoExposureSpotConfig& config)
{
    AutoExposureSpotResult result;
    result.decisionSample = true;
    if (!pixels || width <= 0 || height <= 0) {
        return result;
    }

    std::vector<AutoExposureLogicDetail::PeakCandidate> candidates =
        AutoExposureLogicDetail::collectPeakCandidates(pixels, width, height, config.thresholdDn);
    if (candidates.empty()) {
        result.quality = AutoExposurePeakQuality::WeakOrNoSignal;
        return result;
    }

    const int maxCandidates = std::max(1, config.maxCandidateCount);
    const int candidateCount = std::min<int>(maxCandidates, static_cast<int>(candidates.size()));
    for (int i = 0; i < candidateCount; ++i) {
        const auto& candidate = candidates[static_cast<std::size_t>(i)];
        std::vector<double> supportValues =
            AutoExposureLogicDetail::connectedSupportValues(pixels, width, height, candidate, config);
        const double neighborRatio =
            AutoExposureLogicDetail::strongestNeighborRatio(pixels, width, height, candidate);
        if (static_cast<int>(supportValues.size()) < std::max(1, config.minSupportPixelCount) ||
            neighborRatio < config.minNeighborPeakRatio) {
            ++result.rejectedCandidateCount;
            if (candidate.value >= result.rejectedPeakDn) {
                result.rejectedPeakDn = candidate.value;
                result.rejectedPeakX = candidate.x;
                result.rejectedPeakY = candidate.y;
            }
            continue;
        }

        const int saturatedCount =
            AutoExposureLogicDetail::countSaturated(supportValues, config.hardSaturationDn);
        result.validSpotPeak = true;
        result.peakDn = candidate.value;
        result.peakX = candidate.x;
        result.peakY = candidate.y;
        result.supportPixelCount = static_cast<int>(supportValues.size());
        result.spotSaturatedPixelCount = saturatedCount;
        result.spotHardSaturated =
            saturatedCount >= std::max(1, config.saturatedPixelCount) ||
            candidate.value >= config.hardSaturationDn;
        result.quality = result.spotHardSaturated
                             ? AutoExposurePeakQuality::SpotSaturated
                             : AutoExposurePeakQuality::ValidSpotPeak;
        result.supportedPeakDn = AutoExposureLogicDetail::percentile(
            supportValues, config.supportedPeakPercentile);
        return result;
    }

    result.quality = result.rejectedCandidateCount > 0
                         ? AutoExposurePeakQuality::RejectedIsolatedPeak
                         : AutoExposurePeakQuality::WeakOrNoSignal;
    return result;
}

inline AutoExposureWindowStats summarizeAutoExposureWindow(
    const std::vector<AutoExposureSpotResult>& samples,
    const AutoExposureDecisionConfig& config)
{
    AutoExposureWindowStats stats;
    std::vector<double> validPeaks;
    for (const AutoExposureSpotResult& sample : samples) {
        if (!sample.decisionSample) {
            continue;
        }
        ++stats.decisionSampleCount;
        switch (sample.quality) {
        case AutoExposurePeakQuality::ValidSpotPeak:
        case AutoExposurePeakQuality::SpotSaturated: {
            ++stats.validPeakSampleCount;
            const double peak = sample.supportedPeakDn > 0.0 ? sample.supportedPeakDn : sample.peakDn;
            validPeaks.push_back(peak);
            stats.latestPeakDn = peak;
            if (sample.quality == AutoExposurePeakQuality::SpotSaturated ||
                sample.spotHardSaturated ||
                peak >= config.hardSaturationDn) {
                ++stats.spotHardSaturationFrameCount;
            }
            if (peak < config.targetPeakLowDn) {
                ++stats.darkEvidenceCount;
            } else if (peak > config.targetPeakHighDn) {
                ++stats.brightFrameCount;
            } else {
                ++stats.stableFrameCount;
            }
            break;
        }
        case AutoExposurePeakQuality::WeakOrNoSignal:
            ++stats.invalidPeakSampleCount;
            ++stats.weakOrNoSignalCount;
            break;
        case AutoExposurePeakQuality::RejectedIsolatedPeak:
            ++stats.invalidPeakSampleCount;
            ++stats.rejectedIsolatedPeakCount;
            break;
        }
    }

    stats.peakP50Dn = AutoExposureLogicDetail::percentile(validPeaks, 0.50);
    stats.peakP90Dn = AutoExposureLogicDetail::percentile(validPeaks, 0.90);
    stats.peakP95Dn = AutoExposureLogicDetail::percentile(validPeaks, 0.95);
    if (stats.decisionSampleCount > 0) {
        const double decisionCount = static_cast<double>(stats.decisionSampleCount);
        stats.darkRatio = static_cast<double>(stats.darkEvidenceCount) / decisionCount;
        stats.hardSaturationRatio =
            static_cast<double>(stats.spotHardSaturationFrameCount) / decisionCount;
        stats.invalidPeakRatio =
            static_cast<double>(stats.invalidPeakSampleCount) / decisionCount;
    }
    if (stats.validPeakSampleCount > 0) {
        const double validCount = static_cast<double>(stats.validPeakSampleCount);
        stats.brightRatio = static_cast<double>(stats.brightFrameCount) / validCount;
        stats.stableRatio = static_cast<double>(stats.stableFrameCount) / validCount;
    }
    return stats;
}

inline AutoExposureControlAction chooseAutoExposureAction(const AutoExposureWindowStats& stats,
                                                          double currentExposureUs,
                                                          const AutoExposureDecisionConfig& config,
                                                          AutoExposureAdjustDirection activeAdjustmentDirection =
                                                              AutoExposureAdjustDirection::Hold)
{
    if (stats.decisionSampleCount < std::max(1, config.minDecisionSampleCount)) {
        return AutoExposureLogicDetail::holdAction(currentExposureUs, "WAIT_SAMPLES");
    }

    if (stats.brightRatio >= config.brightFrameRatioThreshold) {
        return AutoExposureLogicDetail::exposureAction(AutoExposureAdjustDirection::Decrease,
                                                       currentExposureUs,
                                                       currentExposureUs - std::max(0.0, config.autoExposureStepUs),
                                                       config,
                                                       "BRIGHT_DECREASE");
    }

    if (stats.darkRatio >= config.darkFrameRatioThreshold) {
        if (currentExposureUs >= config.maxExposureUs) {
            return AutoExposureLogicDetail::holdAction(currentExposureUs, "MAX_EXPOSURE_DARK_HOLD");
        }
        return AutoExposureLogicDetail::exposureAction(AutoExposureAdjustDirection::Increase,
                                                       currentExposureUs,
                                                       currentExposureUs + std::max(0.0, config.autoExposureStepUs),
                                                       config,
                                                       "DARK_INCREASE");
    }

    if (activeAdjustmentDirection == AutoExposureAdjustDirection::Decrease &&
        stats.validPeakSampleCount > 0 &&
        stats.peakP90Dn > AutoExposureLogicDetail::brightAdjustmentTargetDn(config)) {
        return AutoExposureLogicDetail::exposureAction(AutoExposureAdjustDirection::Decrease,
                                                       currentExposureUs,
                                                       currentExposureUs - std::max(0.0, config.autoExposureStepUs),
                                                       config,
                                                       "BRIGHT_RECOVERY_DECREASE");
    }

    if (activeAdjustmentDirection == AutoExposureAdjustDirection::Increase &&
        stats.peakP50Dn > 0.0 &&
        stats.peakP50Dn < AutoExposureLogicDetail::darkAdjustmentTargetDn(config)) {
        if (currentExposureUs >= config.maxExposureUs) {
            return AutoExposureLogicDetail::holdAction(currentExposureUs, "MAX_EXPOSURE_DARK_HOLD");
        }
        return AutoExposureLogicDetail::exposureAction(AutoExposureAdjustDirection::Increase,
                                                       currentExposureUs,
                                                       currentExposureUs + std::max(0.0, config.autoExposureStepUs),
                                                       config,
                                                       "DARK_RECOVERY_INCREASE");
    }

    if (stats.stableRatio >= config.stableFrameRatioThreshold) {
        return AutoExposureLogicDetail::holdAction(currentExposureUs, "STABLE_HOLD");
    }

    return AutoExposureLogicDetail::holdAction(currentExposureUs, "UNSTABLE_HOLD");
}
