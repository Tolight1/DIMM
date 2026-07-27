#pragma once

#include <opencv2/opencv.hpp>

namespace ImageUtils {

double normalizeThresholdToMono8(double value);
cv::Mat grayscaleDetectionFrame(const cv::Mat& frame);
cv::Mat normalizeMono8Frame(const cv::Mat& grayscale);
cv::Mat normalizeDetectionFrame(const cv::Mat& frame);

} // namespace ImageUtils
