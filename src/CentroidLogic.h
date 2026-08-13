#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace CentroidLogic {

struct PeakKernelConfig {
    int radiusPx = 3;
    double strongHotPixelExcessDn = 100.0;
};

struct PeakKernelResult {
    bool valid = false;
    bool containsStrongHotPixel = false;
    double x = 0.0;
    double y = 0.0;
    double peakValue = 0.0;
    double totalFlux = 0.0;
    int usedPixelCount = 0;
};

namespace Detail {

inline bool inBounds(int x, int y, int width, int height)
{
    return x >= 0 && y >= 0 && x < width && y < height;
}

inline std::size_t indexOf(int x, int y, int width)
{
    return static_cast<std::size_t>(y * width + x);
}

inline bool isStrongHotPixel(const unsigned char* hotMask,
                             const std::uint16_t* hotExcess,
                             int width,
                             int x,
                             int y,
                             double thresholdDn)
{
    if (!hotMask || !hotExcess) {
        return false;
    }
    const std::size_t index = indexOf(x, y, width);
    return hotMask[index] != 0 && static_cast<double>(hotExcess[index]) > thresholdDn;
}

} // namespace Detail

inline PeakKernelResult computeIntensityCog(const double* pixels,
                                            int width,
                                            int height,
                                            int peakX,
                                            int peakY,
                                            const unsigned char* hotMask,
                                            const std::uint16_t* hotExcess,
                                            const PeakKernelConfig& config)
{
    PeakKernelResult result;
    const int radius = std::clamp(config.radiusPx, 1, 20);
    long double sumX = 0.0;
    long double sumY = 0.0;
    long double sumW = 0.0;

    for (int y = peakY - radius; y <= peakY + radius; ++y) {
        for (int x = peakX - radius; x <= peakX + radius; ++x) {
            if (!Detail::inBounds(x, y, width, height)) {
                continue;
            }
            if (Detail::isStrongHotPixel(hotMask,
                                         hotExcess,
                                         width,
                                         x,
                                         y,
                                         config.strongHotPixelExcessDn)) {
                result.containsStrongHotPixel = true;
                continue;
            }
            const double value = pixels[Detail::indexOf(x, y, width)];
            if (!std::isfinite(value) || value <= 0.0) {
                continue;
            }
            sumX += static_cast<long double>(x) * value;
            sumY += static_cast<long double>(y) * value;
            sumW += value;
            result.peakValue = std::max(result.peakValue, value);
            ++result.usedPixelCount;
        }
    }

    if (sumW <= 0.0 || result.usedPixelCount < 1) {
        return result;
    }

    result.x = static_cast<double>(sumX / sumW);
    result.y = static_cast<double>(sumY / sumW);
    result.totalFlux = static_cast<double>(sumW);
    result.valid = true;
    return result;
}

inline PeakKernelResult computePeakKernelCentroid(const double* pixels,
                                                  int width,
                                                  int height,
                                                  int peakX,
                                                  int peakY,
                                                  const unsigned char* hotMask,
                                                  const std::uint16_t* hotExcess,
                                                  const PeakKernelConfig& config)
{
    if (!pixels || width <= 0 || height <= 0 ||
        !Detail::inBounds(peakX, peakY, width, height)) {
        return PeakKernelResult();
    }

    return computeIntensityCog(pixels, width, height, peakX, peakY, hotMask, hotExcess, config);
}

} // namespace CentroidLogic
