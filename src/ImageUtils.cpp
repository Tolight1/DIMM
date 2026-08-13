#include "ImageUtils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ImageUtils {

double normalizeThresholdToMono8(double value)
{
    if (!std::isfinite(value) || value < 0.0) {
        return value;
    }
    if (value > 255.0 && value <= 4095.0) {
        value = value * 255.0 / 4095.0;
    } else if (value > 4095.0) {
        value = value * 255.0 / 65535.0;
    }
    return std::clamp(value, 0.0, 255.0);
}

cv::Mat grayscaleDetectionFrame(const cv::Mat& frame)
{
    if (frame.empty()) {
        return cv::Mat();
    }

    cv::Mat grayscale;
    if (frame.channels() == 1) {
        grayscale = frame;
    } else if (frame.channels() == 3) {
        cv::cvtColor(frame, grayscale, cv::COLOR_BGR2GRAY);
    } else if (frame.channels() == 4) {
        cv::cvtColor(frame, grayscale, cv::COLOR_BGRA2GRAY);
    } else {
        return cv::Mat();
    }
    return grayscale;
}

cv::Mat fullFrameMono8Preview(const cv::Mat& grayscale)
{
    if (grayscale.empty() || grayscale.channels() != 1) {
        return cv::Mat();
    }
    if (grayscale.type() == CV_8UC1) {
        return grayscale;
    }
    if (grayscale.type() != CV_16UC1) {
        return cv::Mat();
    }

    // The camera delivers Mono12 values in the low 12 bits of a 16-bit word.
    // Keep the proportional mapping integer-only so the endpoints remain
    // exact without a floating-point rescale or a second min/max pass.
    cv::Mat mono8(grayscale.size(), CV_8UC1);
    for (int y = 0; y < grayscale.rows; ++y) {
        const std::uint16_t* source = grayscale.ptr<std::uint16_t>(y);
        uchar* destination = mono8.ptr<uchar>(y);
        for (int x = 0; x < grayscale.cols; ++x) {
            const std::uint32_t mono12 = std::min<std::uint32_t>(source[x], 4095U);
            const std::uint32_t scaled = (mono12 * 255U + 2047U) / 4095U;
            destination[x] = static_cast<uchar>(scaled);
        }
    }
    return mono8;
}

cv::Mat normalizeMono8Frame(const cv::Mat& grayscale)
{
    if (grayscale.empty() || grayscale.channels() != 1) {
        return cv::Mat();
    }
    if (grayscale.type() == CV_8UC1) {
        return grayscale;
    }

    double minValue = 0.0;
    double maxValue = 0.0;
    cv::minMaxLoc(grayscale, &minValue, &maxValue);
    cv::Mat mono8;
    if (maxValue > minValue) {
        const double scale = 255.0 / (maxValue - minValue);
        grayscale.convertTo(mono8, CV_8UC1, scale, -minValue * scale);
    } else {
        grayscale.convertTo(mono8, CV_8UC1);
    }
    return mono8;
}

cv::Mat normalizeDetectionFrame(const cv::Mat& frame)
{
    const cv::Mat grayscale = grayscaleDetectionFrame(frame);
    return normalizeMono8Frame(grayscale);
}

} // namespace ImageUtils
