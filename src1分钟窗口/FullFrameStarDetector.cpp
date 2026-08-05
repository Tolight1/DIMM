#include "FullFrameStarDetector.h"

#include "ImageProcessor.h"
#include "ImageUtils.h"
#include "InitialStarDetectionConfig.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <QRect>
#include <QtGlobal>

#include <opencv2/opencv.hpp>

using PolarisDetectionPipeline::InitialStarCandidate;

cv::Mat cropFrameForRoiProcessing(const cv::Mat& frame, const RoiRect& roi)
{
    if (frame.empty() || roi.w <= 0 || roi.h <= 0) {
        return cv::Mat();
    }

    if (frame.cols <= roi.w && frame.rows <= roi.h) {
        return frame;
    }

    const int x = std::clamp(roi.x, 0, std::max(0, frame.cols - 1));
    const int y = std::clamp(roi.y, 0, std::max(0, frame.rows - 1));
    const int w = std::min(roi.w, frame.cols - x);
    const int h = std::min(roi.h, frame.rows - y);
    if (w <= 0 || h <= 0) {
        return cv::Mat();
    }

    return frame(cv::Rect(x, y, w, h));
}

namespace {

InitialStarDetectionConfig scaledInitialStarDetectionConfigForMono8()
{
    InitialStarDetectionConfig config = currentInitialStarDetectionConfig();
    config.thresholdAbsolute =
        config.thresholdAbsolute >= 0.0 ? ImageUtils::normalizeThresholdToMono8(config.thresholdAbsolute) : -1.0;
    config.minimumIntensity = std::max(0.0, config.minimumIntensity);
    config.minimumIntensity = ImageUtils::normalizeThresholdToMono8(config.minimumIntensity);
    return config;
}

double rawPixelValueAt(const cv::Mat& image, int y, int x)
{
    switch (image.depth()) {
    case CV_8U:
        return static_cast<double>(image.ptr<uchar>(y)[x]);
    case CV_16U:
        return static_cast<double>(image.ptr<quint16>(y)[x]);
    case CV_32F:
        return static_cast<double>(image.ptr<float>(y)[x]);
    case CV_64F:
        return image.ptr<double>(y)[x];
    default:
        return 0.0;
    }
}

} // namespace

QVector<InitialStarCandidate> detectInitialStarCandidates(const cv::Mat& grayscale,
                                                          double* peakValue,
                                                          double* thresholdValue)
{
    QVector<InitialStarCandidate> candidates;
    if (grayscale.empty() || grayscale.channels() != 1) {
        return candidates;
    }

    cv::Scalar mean;
    cv::Scalar stddev;
    cv::meanStdDev(grayscale, mean, stddev);

    double minValue = 0.0;
    double maxValue = 0.0;
    cv::minMaxLoc(grayscale, &minValue, &maxValue);
    if (peakValue) {
        *peakValue = maxValue;
    }

    InitialStarDetectionConfig config = currentInitialStarDetectionConfig();
    const double dynamicThreshold = std::max({config.minimumIntensity,
                                              mean[0] + config.sigmaThreshold * stddev[0],
                                              mean[0] + (maxValue - mean[0]) * config.peakFraction});
    const double threshold = config.thresholdAbsolute >= 0.0
                                 ? std::min(config.thresholdAbsolute, dynamicThreshold)
                                 : dynamicThreshold;
    if (thresholdValue) {
        *thresholdValue = threshold;
    }
    if (maxValue <= threshold) {
        return candidates;
    }

    cv::Mat binary;
    cv::compare(grayscale, cv::Scalar(threshold), binary, cv::CMP_GT);

    cv::Mat labels;
    cv::Mat stats;
    cv::Mat centroids;
    const int componentCount =
        cv::connectedComponentsWithStats(binary, labels, stats, centroids, 8, CV_32S);

    std::vector<double> componentSignal(static_cast<size_t>(componentCount), 0.0);
    std::vector<double> componentPeak(static_cast<size_t>(componentCount), 0.0);
    for (int y = 0; y < labels.rows; ++y) {
        const int* labelRow = labels.ptr<int>(y);
        for (int x = 0; x < labels.cols; ++x) {
            const int label = labelRow[x];
            if (label > 0 && label < componentCount) {
                const double value = rawPixelValueAt(grayscale, y, x);
                componentSignal[static_cast<size_t>(label)] += value;
                componentPeak[static_cast<size_t>(label)] =
                    std::max(componentPeak[static_cast<size_t>(label)], value);
            }
        }
    }

    for (int label = 1; label < componentCount; ++label) {
        const int area = stats.at<int>(label, cv::CC_STAT_AREA);
        const int width = stats.at<int>(label, cv::CC_STAT_WIDTH);
        const int height = stats.at<int>(label, cv::CC_STAT_HEIGHT);
        if (area < config.minArea || area > config.maxArea) {
            continue;
        }

        InitialStarCandidate candidate;
        candidate.center = QPointF(centroids.at<double>(label, 0), centroids.at<double>(label, 1));
        candidate.area = area;
        candidate.peak = componentPeak[static_cast<size_t>(label)];
        candidate.signal = componentSignal[static_cast<size_t>(label)];
        candidate.bbox = QRect(stats.at<int>(label, cv::CC_STAT_LEFT),
                               stats.at<int>(label, cv::CC_STAT_TOP),
                               width,
                               height);
        candidates.append(candidate);
    }

    std::sort(candidates.begin(), candidates.end(), [](const InitialStarCandidate& a,
                                                       const InitialStarCandidate& b) {
        return a.signal > b.signal;
    });
    for (int i = 0; i < candidates.size(); ++i) {
        candidates[i].index = i + 1;
    }

    return candidates;
}

bool detectRawInitialStarPeakCandidate(const cv::Mat& grayscale,
                                       InitialStarCandidate* candidate,
                                       double* peakValue)
{
    if (grayscale.empty() || grayscale.channels() != 1 || !candidate) {
        return false;
    }

    cv::Scalar mean;
    cv::Scalar stddev;
    cv::meanStdDev(grayscale, mean, stddev);

    double minValue = 0.0;
    double maxValue = 0.0;
    cv::Point maxLoc;
    cv::minMaxLoc(grayscale, &minValue, &maxValue, nullptr, &maxLoc);
    if (peakValue) {
        *peakValue = maxValue;
    }

    const InitialStarDetectionConfig config = currentInitialStarDetectionConfig();
    const double dynamicThreshold = std::max({config.minimumIntensity,
                                              mean[0] + config.sigmaThreshold * stddev[0],
                                              mean[0] + (maxValue - mean[0]) * config.peakFraction});
    const double threshold = config.thresholdAbsolute >= 0.0
                                 ? config.thresholdAbsolute
                                 : dynamicThreshold;
    if (maxValue <= threshold) {
        return false;
    }

    constexpr int kSearchRadius = 16;
    const int x0 = std::max(0, maxLoc.x - kSearchRadius);
    const int y0 = std::max(0, maxLoc.y - kSearchRadius);
    const int x1 = std::min(grayscale.cols - 1, maxLoc.x + kSearchRadius);
    const int y1 = std::min(grayscale.rows - 1, maxLoc.y + kSearchRadius);
    double weightedX = 0.0;
    double weightedY = 0.0;
    double weightSum = 0.0;
    int supportCount = 0;
    int minX = maxLoc.x;
    int maxX = maxLoc.x;
    int minY = maxLoc.y;
    int maxY = maxLoc.y;
    const double supportThreshold = std::max(threshold, mean[0] + std::max(1.0, stddev[0]));
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const double value = rawPixelValueAt(grayscale, y, x);
            if (value < supportThreshold) {
                continue;
            }
            ++supportCount;
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
            weightedX += static_cast<double>(x) * value;
            weightedY += static_cast<double>(y) * value;
            weightSum += value;
        }
    }
    if (weightSum <= 0.0 || supportCount < config.minArea || supportCount > config.maxArea) {
        return false;
    }

    candidate->index = 1;
    candidate->center = QPointF(weightedX / weightSum, weightedY / weightSum);
    candidate->area = supportCount;
    candidate->peak = maxValue;
    candidate->signal = weightSum;
    candidate->bbox = QRect(minX, minY, maxX - minX + 1, maxY - minY + 1);
    return true;
}

bool detectInitialStarCentroid(const cv::Mat& grayscale, QPointF* centroid, double* peakValue)
{
    if (grayscale.empty() || grayscale.channels() != 1 || !centroid) {
        return false;
    }

    double detectedPeak = 0.0;
    const QVector<InitialStarCandidate> candidates =
        detectInitialStarCandidates(grayscale, &detectedPeak);
    if (!candidates.isEmpty()) {
        *centroid = candidates.first().center;
        if (peakValue) {
            *peakValue = detectedPeak;
        }
        return true;
    }

    cv::Mat mono8;
    if (grayscale.type() == CV_8UC1) {
        mono8 = grayscale;
    } else {
        grayscale.convertTo(mono8, CV_8UC1);
    }

    cv::Scalar mean;
    cv::Scalar stddev;
    cv::meanStdDev(mono8, mean, stddev);

    double minValue = 0.0;
    double maxValue = 0.0;
    cv::Point maxLoc;
    cv::minMaxLoc(mono8, &minValue, &maxValue, nullptr, &maxLoc);
    if (peakValue) {
        *peakValue = maxValue;
    }

    InitialStarDetectionConfig config = scaledInitialStarDetectionConfigForMono8();
    const double dynamicThreshold = std::max({config.minimumIntensity,
                                              mean[0] + config.sigmaThreshold * stddev[0],
                                              mean[0] + (maxValue - mean[0]) * config.peakFraction});
    const double threshold = config.thresholdAbsolute >= 0.0
                                 ? config.thresholdAbsolute
                                 : dynamicThreshold;
    if (maxValue <= threshold) {
        return false;
    }

    if (maxLoc.x < 8 || maxLoc.y < 8 || maxLoc.x >= mono8.cols - 8 || maxLoc.y >= mono8.rows - 8) {
        return false;
    }

    const int x0 = std::max(0, maxLoc.x - 8);
    const int y0 = std::max(0, maxLoc.y - 8);
    const int x1 = std::min(mono8.cols - 1, maxLoc.x + 8);
    const int y1 = std::min(mono8.rows - 1, maxLoc.y + 8);
    double weightedX = 0.0;
    double weightedY = 0.0;
    double weightSum = 0.0;
    int brightPixelCount = 0;
    int minBrightX = x1;
    int maxBrightX = x0;
    int minBrightY = y1;
    int maxBrightY = y0;
    int supportPixelCount = 0;
    double center3x3Sum = 0.0;
    int center3x3Count = 0;
    double ringSum = 0.0;
    int ringCount = 0;
    const double supportThreshold = std::max(mean[0] + 2.0 * stddev[0], threshold * 0.75);
    for (int y = y0; y <= y1; ++y) {
        const uchar* row = mono8.ptr<uchar>(y);
        for (int x = x0; x <= x1; ++x) {
            const double value = static_cast<double>(row[x]);
            const int dx = std::abs(x - maxLoc.x);
            const int dy = std::abs(y - maxLoc.y);
            if (dx <= 1 && dy <= 1) {
                center3x3Sum += value;
                ++center3x3Count;
            } else if (dx <= 4 && dy <= 4) {
                ringSum += value;
                ++ringCount;
            }
            if (value >= supportThreshold) {
                ++supportPixelCount;
            }
            if (value < threshold) {
                continue;
            }
            ++brightPixelCount;
            minBrightX = std::min(minBrightX, x);
            maxBrightX = std::max(maxBrightX, x);
            minBrightY = std::min(minBrightY, y);
            maxBrightY = std::max(maxBrightY, y);
            weightedX += static_cast<double>(x) * value;
            weightedY += static_cast<double>(y) * value;
            weightSum += value;
        }
    }
    if (weightSum <= 0.0 || brightPixelCount < 2 || supportPixelCount < 3) {
        return false;
    }

    const int brightWidth = maxBrightX - minBrightX + 1;
    const int brightHeight = maxBrightY - minBrightY + 1;
    if (brightWidth > 32 || brightHeight > 32) {
        return false;
    }
    const double centerMean = center3x3Count > 0 ? center3x3Sum / static_cast<double>(center3x3Count) : 0.0;
    const double ringMean = ringCount > 0 ? ringSum / static_cast<double>(ringCount) : mean[0];
    if (centerMean < ringMean + std::max(6.0, stddev[0] * 1.5)) {
        return false;
    }
    *centroid = QPointF(weightedX / weightSum, weightedY / weightSum);
    return true;
}

bool detectInitialStarCentroidFast(const cv::Mat& grayscale, QPointF* centroid, double* peakValue)
{
    if (grayscale.empty() || grayscale.channels() != 1 || !centroid) {
        return false;
    }

    cv::Mat mono8 = ImageUtils::normalizeMono8Frame(grayscale);
    if (mono8.empty()) {
        return false;
    }

    double minValue = 0.0;
    double maxValue = 0.0;
    cv::Point maxLoc;
    cv::minMaxLoc(mono8, &minValue, &maxValue, nullptr, &maxLoc);
    if (peakValue) {
        *peakValue = maxValue;
    }

    constexpr int kSearchRadius = 12;
    if (maxLoc.x < kSearchRadius || maxLoc.y < kSearchRadius ||
        maxLoc.x >= mono8.cols - kSearchRadius ||
        maxLoc.y >= mono8.rows - kSearchRadius) {
        return false;
    }

    const int x0 = std::max(0, maxLoc.x - kSearchRadius);
    const int y0 = std::max(0, maxLoc.y - kSearchRadius);
    const int x1 = std::min(mono8.cols - 1, maxLoc.x + kSearchRadius);
    const int y1 = std::min(mono8.rows - 1, maxLoc.y + kSearchRadius);

    double localBackgroundSum = 0.0;
    double localBackgroundSquareSum = 0.0;
    int localBackgroundCount = 0;
    for (int y = y0; y <= y1; ++y) {
        const uchar* row = mono8.ptr<uchar>(y);
        for (int x = x0; x <= x1; ++x) {
            const int dx = std::abs(x - maxLoc.x);
            const int dy = std::abs(y - maxLoc.y);
            if (dx > 5 || dy > 5) {
                const double value = static_cast<double>(row[x]);
                localBackgroundSum += value;
                localBackgroundSquareSum += value * value;
                ++localBackgroundCount;
            }
        }
    }

    const double localBackground =
        localBackgroundCount > 0 ? localBackgroundSum / static_cast<double>(localBackgroundCount) : minValue;
    const double localMeanSquare =
        localBackgroundCount > 0 ? localBackgroundSquareSum / static_cast<double>(localBackgroundCount)
                                 : localBackground * localBackground;
    const double localStd =
        std::sqrt(std::max(0.0, localMeanSquare - localBackground * localBackground));

    InitialStarDetectionConfig config = scaledInitialStarDetectionConfigForMono8();
    const double peakContrast = maxValue - localBackground;
    if (peakContrast < std::max(2.0, localStd * 2.0)) {
        return false;
    }

    // Live full-frame seeding must work on dim 2 Hz frames; use local contrast
    // instead of the ROI-stage absolute threshold from threshold.txt.
    const double adaptiveThreshold = std::max({localBackground + std::max(1.5, localStd * 2.0),
                                               localBackground + peakContrast * config.peakFraction,
                                               config.minimumIntensity * 0.25,
                                               3.0});
    const double threshold = config.thresholdAbsolute < 0.0
                                 ? adaptiveThreshold
                                 : std::min(config.thresholdAbsolute, adaptiveThreshold);
    if (maxValue <= threshold) {
        return false;
    }

    const double supportThreshold = std::max(localBackground + std::max(1.0, localStd),
                                             threshold * 0.50);
    double weightedX = 0.0;
    double weightedY = 0.0;
    double weightSum = 0.0;
    int brightPixelCount = 0;
    int supportPixelCount = 0;
    double center3x3Sum = 0.0;
    int center3x3Count = 0;
    double ringSum = 0.0;
    int ringCount = 0;
    int minBrightX = x1;
    int maxBrightX = x0;
    int minBrightY = y1;
    int maxBrightY = y0;

    for (int y = y0; y <= y1; ++y) {
        const uchar* row = mono8.ptr<uchar>(y);
        for (int x = x0; x <= x1; ++x) {
            const double value = static_cast<double>(row[x]);
            const int dx = std::abs(x - maxLoc.x);
            const int dy = std::abs(y - maxLoc.y);
            if (dx <= 1 && dy <= 1) {
                center3x3Sum += value;
                ++center3x3Count;
            } else if (dx <= 5 && dy <= 5) {
                ringSum += value;
                ++ringCount;
            }
            if (value >= supportThreshold) {
                ++supportPixelCount;
                const double weight = std::max(0.0, value - localBackground);
                weightedX += static_cast<double>(x) * weight;
                weightedY += static_cast<double>(y) * weight;
                weightSum += weight;
            }
            if (value < threshold) {
                continue;
            }
            ++brightPixelCount;
            minBrightX = std::min(minBrightX, x);
            maxBrightX = std::max(maxBrightX, x);
            minBrightY = std::min(minBrightY, y);
            maxBrightY = std::max(maxBrightY, y);
        }
    }

    if (weightSum <= 0.0 || brightPixelCount < 1 || supportPixelCount < 1) {
        return false;
    }

    const int brightWidth = maxBrightX - minBrightX + 1;
    const int brightHeight = maxBrightY - minBrightY + 1;
    if (brightWidth > 48 || brightHeight > 48) {
        return false;
    }

    const double centerMean = center3x3Count > 0 ? center3x3Sum / static_cast<double>(center3x3Count) : 0.0;
    const double ringMean = ringCount > 0 ? ringSum / static_cast<double>(ringCount) : localBackground;
    if (centerMean < ringMean + std::max(1.5, localStd * 0.50)) {
        return false;
    }

    *centroid = QPointF(weightedX / weightSum, weightedY / weightSum);
    return true;
}


