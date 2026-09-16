#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace BackgroundNoiseThresholdEstimator {

struct Config {
    int clipIterations = 3;
    double clipSigma = 3.0;
    double thresholdSigmaMultiplier = 1.0;
};

struct Estimate {
    bool valid = false;
    double background = 0.0;
    double noiseSigma = 0.0;
    double threshold = 0.0;
    int sampleCount = 0;
};

inline double medianInPlace(std::vector<double>* values)
{
    if (!values || values->empty()) {
        return 0.0;
    }

    const std::size_t middle = values->size() / 2;
    std::nth_element(values->begin(), values->begin() + static_cast<std::ptrdiff_t>(middle), values->end());
    const double upper = (*values)[middle];
    if (values->size() % 2 != 0) {
        return upper;
    }

    const double lower = *std::max_element(values->begin(), values->begin() + static_cast<std::ptrdiff_t>(middle));
    return (lower + upper) * 0.5;
}

inline Estimate estimate(const double* pixels, std::size_t pixelCount, Config config)
{
    Estimate result;
    if (!pixels || pixelCount == 0 || !std::isfinite(config.clipSigma) || config.clipSigma <= 0.0 ||
        !std::isfinite(config.thresholdSigmaMultiplier)) {
        return result;
    }

    std::vector<double> samples;
    samples.reserve(pixelCount);
    for (std::size_t index = 0; index < pixelCount; ++index) {
        if (std::isfinite(pixels[index])) {
            samples.push_back(pixels[index]);
        }
    }
    if (samples.empty()) {
        return result;
    }

    const int iterations = std::clamp(config.clipIterations, 0, 20);
    std::vector<double> deviations;
    deviations.reserve(samples.size());
    for (int iteration = 0; iteration <= iterations; ++iteration) {
        const double background = medianInPlace(&samples);
        deviations.clear();
        for (const double sample : samples) {
            deviations.push_back(std::abs(sample - background));
        }
        const double noiseSigma = 1.4826 * medianInPlace(&deviations);
        if (!std::isfinite(background) || !std::isfinite(noiseSigma)) {
            return result;
        }

        if (iteration == iterations) {
            result.valid = true;
            result.background = background;
            result.noiseSigma = noiseSigma;
            result.threshold = background +
                               std::max(0.0, config.thresholdSigmaMultiplier) * noiseSigma;
            result.sampleCount = static_cast<int>(samples.size());
            return std::isfinite(result.threshold) ? result : Estimate{};
        }

        const double limit = config.clipSigma * noiseSigma;
        std::vector<double> retained;
        retained.reserve(samples.size());
        for (const double sample : samples) {
            if (std::abs(sample - background) <= limit) {
                retained.push_back(sample);
            }
        }
        if (retained.empty()) {
            return result;
        }
        samples.swap(retained);
    }

    return result;
}

} // namespace BackgroundNoiseThresholdEstimator
