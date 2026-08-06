#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace CentroidLogic {

enum class PeakKernelMethod {
    IntensityCog = 0,
    GaussianFit = 1
};

struct PeakKernelConfig {
    int radiusPx = 3;
    PeakKernelMethod method = PeakKernelMethod::GaussianFit;
    double strongHotPixelExcessDn = 100.0;
    int gaussianMinTrustedPixels = 5;
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

inline bool solve4x4(double a[4][5], double out[4])
{
    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        for (int row = col + 1; row < 4; ++row) {
            if (std::abs(a[row][col]) > std::abs(a[pivot][col])) {
                pivot = row;
            }
        }
        if (std::abs(a[pivot][col]) < 1e-12) {
            return false;
        }
        if (pivot != col) {
            for (int k = col; k < 5; ++k) {
                std::swap(a[col][k], a[pivot][k]);
            }
        }
        const double div = a[col][col];
        for (int k = col; k < 5; ++k) {
            a[col][k] /= div;
        }
        for (int row = 0; row < 4; ++row) {
            if (row == col) {
                continue;
            }
            const double factor = a[row][col];
            for (int k = col; k < 5; ++k) {
                a[row][k] -= factor * a[col][k];
            }
        }
    }

    for (int i = 0; i < 4; ++i) {
        out[i] = a[i][4];
    }
    return true;
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
                return result;
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

inline PeakKernelResult computeGaussianPeak(const double* pixels,
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
    struct Sample {
        double x = 0.0;
        double y = 0.0;
        double value = 0.0;
    };

    std::vector<Sample> samples;
    samples.reserve(static_cast<std::size_t>((2 * radius + 1) * (2 * radius + 1)));
    double minValue = std::numeric_limits<double>::max();

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
            if (!std::isfinite(value)) {
                continue;
            }
            samples.push_back(Sample{static_cast<double>(x), static_cast<double>(y), value});
            minValue = std::min(minValue, value);
            result.peakValue = std::max(result.peakValue, value);
            result.totalFlux += value;
        }
    }

    result.usedPixelCount = static_cast<int>(samples.size());
    if (result.usedPixelCount < std::max(5, config.gaussianMinTrustedPixels)) {
        return result;
    }

    const double background = std::max(0.0, minValue - 1e-6);
    double normal[4][5] = {};
    for (const Sample& sample : samples) {
        const double signal = std::max(1e-6, sample.value - background);
        const double row[4] = {1.0,
                               sample.x,
                               sample.y,
                               sample.x * sample.x + sample.y * sample.y};
        const double rhs = std::log(signal);
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                normal[r][c] += row[r] * row[c];
            }
            normal[r][4] += row[r] * rhs;
        }
    }

    double coeff[4] = {};
    if (!Detail::solve4x4(normal, coeff) || coeff[3] >= -1e-12) {
        return result;
    }

    const double x0 = -coeff[1] / (2.0 * coeff[3]);
    const double y0 = -coeff[2] / (2.0 * coeff[3]);
    const double sigma = std::sqrt(-1.0 / (2.0 * coeff[3]));
    if (!std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(sigma) || sigma <= 0.0) {
        return result;
    }
    if (x0 < static_cast<double>(peakX - radius) - 0.5 ||
        x0 > static_cast<double>(peakX + radius) + 0.5 ||
        y0 < static_cast<double>(peakY - radius) - 0.5 ||
        y0 > static_cast<double>(peakY + radius) + 0.5) {
        return result;
    }

    result.x = x0;
    result.y = y0;
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

    if (config.method == PeakKernelMethod::IntensityCog) {
        return computeIntensityCog(pixels, width, height, peakX, peakY, hotMask, hotExcess, config);
    }
    return computeGaussianPeak(pixels, width, height, peakX, peakY, hotMask, hotExcess, config);
}

} // namespace CentroidLogic
