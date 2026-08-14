#include "StarSegmentation.h"

#include <algorithm>
#include <cmath>

namespace {

bool nativeOtsuSupported(const cv::Mat& image)
{
    return image.depth() == CV_8U || image.depth() == CV_16U;
}

} // namespace

namespace StarSegmentation {

ForegroundSegmentation segmentForegroundOtsu(const cv::Mat& image,
                                             double sigmaThreshold,
                                             double peakFraction)
{
    ForegroundSegmentation result;
    if (image.empty() || image.channels() != 1 || !nativeOtsuSupported(image)) {
        return result;
    }

    // Keep the reference implementation's exact operation: calculate the raw
    // Otsu value directly from the native Mono8/Mono12 image. A low raw Otsu
    // split can still include isolated noise, so callers may provide the
    // existing background-sigma and peak-fraction floors for the actual mask.
    cv::Mat nativeMask;
    const double otsuThreshold =
        cv::threshold(image,
                      nativeMask,
                      0.0,
                      255.0,
                      cv::THRESH_BINARY | cv::THRESH_OTSU);
    result.valid = true;
    result.otsuThreshold = otsuThreshold;

    double actualThreshold = otsuThreshold;
    if (sigmaThreshold > 0.0 || peakFraction > 0.0) {
        cv::Scalar mean;
        cv::Scalar stddev;
        double minValue = 0.0;
        double maxValue = 0.0;
        cv::meanStdDev(image, mean, stddev);
        cv::minMaxLoc(image, &minValue, &maxValue);

        const double sigmaFloor = mean[0] + std::max(0.0, sigmaThreshold) * stddev[0];
        const double peakFloor = mean[0] +
                                 std::clamp(peakFraction, 0.0, 1.0) * (maxValue - mean[0]);
        actualThreshold = std::max({actualThreshold, sigmaFloor, peakFloor});
        if (maxValue > minValue && actualThreshold >= maxValue) {
            // Keep the brightest integer-valued pixels while preventing the
            // floor from making the entire foreground empty.
            actualThreshold = std::nextafter(maxValue, minValue);
        }
    }

    result.actualThreshold = actualThreshold;
    if (std::abs(actualThreshold - otsuThreshold) <= 1e-12) {
        if (nativeMask.type() == CV_8UC1) {
            result.mask = nativeMask;
        } else {
            nativeMask.convertTo(result.mask, CV_8U);
        }
    } else {
        cv::Mat actualMask;
        cv::threshold(image,
                      actualMask,
                      actualThreshold,
                      255.0,
                      cv::THRESH_BINARY);
        if (actualMask.type() == CV_8UC1) {
            result.mask = actualMask;
        } else {
            actualMask.convertTo(result.mask, CV_8U);
        }
    }
    return result;
}

} // namespace StarSegmentation
