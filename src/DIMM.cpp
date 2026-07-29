#include "DIMM.h"

#include "AlignmentCameraCoordinator.h"
#include "AlignmentController.h"
#include "AlignmentFrameCoordinator.h"
#include "AlignmentLocalTracker.h"
#include "AlignmentTaskManager.h"
#include "AlignmentUiPresenter.h"
#include "CameraManager.h"
#include "CanvasWidgets.h"
#include "CommManager.h"
#include "ConfigTextUtils.h"
#include "EafFocuserManager.h"
#include "FocuserControlWidget.h"
#include "ImageUtils.h"
#include "ImageProcessor.h"
#include "InitialStarDetectionConfig.h"
#include "PathUtils.h"
#include "PolarisDetectionPipeline.h"
#include "PolarisSolver.h"
#include "SettingsDialog.h"
#include "PolarisTracker.h"
#include "PulseGeneratorManager.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <QAction>
#include <QApplication>
#include <QDate>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLatin1Char>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointF>
#include <QPushButton>
#include <QRadioButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QFrame>
#include <QSignalBlocker>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QShortcut>
#include <QStringList>
#include <QThread>
#include <QTabWidget>
#include <QTextStream>
#include <QTime>
#include <QVBoxLayout>

namespace {
constexpr int kFixedRoiSize = 64;
constexpr int kSimulationFrameSize = 5120;
constexpr int kSimulationTargetFps = 200;
constexpr int kSimulationFrameIntervalMs = 1000 / kSimulationTargetFps;
constexpr int kSimulationPreviewIntervalMs = 30000;
constexpr int kAlignmentPreviewIntervalMs = 1000;
constexpr int kAlignmentCandidateDetectionRefreshMs = 3000;
constexpr int kMeasurementUiIntervalMs = 100;
constexpr int kRoiEdgeUpdateMarginPx = 8;
constexpr qint64 kLostCentroidRelocalizeTimeoutMs = 1500;
constexpr qint64 kLiveRelocalizationMaxDurationMs = 15000;
constexpr double kFullFrameLocalizationPulseHz = 2.0;
constexpr double kAlignmentDefaultPolarisPolarDistanceArcmin = 37.6;
constexpr const char* kHardwareTriggerLine = "Line0";
constexpr const char* kRoiUpdateGateLine = "Line2";
constexpr double kPi = 3.14159265358979323846;

using PolarisDetectionPipeline::InitialStarCandidate;
using PolarisDetectionPipeline::InitialStarSelection;

struct HotPixelTemplateSettings {
    QString camera0Mask;
    QString camera0Excess;
    QString camera1Mask;
    QString camera1Excess;
    int width = 0;
    int height = 0;
};

struct AlignmentStartReadiness {
    bool canStart = false;
    bool alreadyActive = false;
    bool shouldStopPausedCapture = false;
    QString reason;
};

AlignmentStartReadiness validateAlignmentStartReadiness(bool hasCameraManager,
                                                        bool alignmentActive,
                                                        bool idleOrPaused,
                                                        bool paused,
                                                        int openCameraCount)
{
    AlignmentStartReadiness readiness;
    if (!hasCameraManager) {
        readiness.reason = QStringLiteral("相机管理器未初始化。");
        return readiness;
    }

    if (alignmentActive) {
        readiness.canStart = true;
        readiness.alreadyActive = true;
        return readiness;
    }

    if (!idleOrPaused) {
        readiness.reason = QStringLiteral("请先停止当前采集或模拟采集，再进入对准模式。");
        return readiness;
    }

    if (openCameraCount < 2) {
        readiness.reason = QStringLiteral("对准模式需要两台相机均已连接。");
        return readiness;
    }

    readiness.canStart = true;
    readiness.shouldStopPausedCapture = paused;
    return readiness;
}

bool loadHotPixelTemplateSettings(const QString& path, HotPixelTemplateSettings* settings)
{
    if (!settings) {
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    HotPixelTemplateSettings parsed;
    const QFileInfo configInfo(path);
    QTextStream input(&file);
    while (!input.atEnd()) {
        const QString line = ConfigTextUtils::stripInlineComment(input.readLine());
        if (line.isEmpty()) {
            continue;
        }

        const int equalPos = line.indexOf(QLatin1Char('='));
        if (equalPos <= 0) {
            continue;
        }

        const QString key = line.left(equalPos).trimmed().toLower();
        const QString valueText = line.mid(equalPos + 1).trimmed();
        auto resolveConfigPath = [&configInfo](const QString& rawPath) {
            QFileInfo candidate(rawPath);
            if (candidate.isAbsolute()) {
                return candidate.absoluteFilePath();
            }
            return QFileInfo(configInfo.absoluteDir(), rawPath).absoluteFilePath();
        };

        bool ok = false;
        const double number = valueText.toDouble(&ok);
        if ((key == QStringLiteral("hot_pixel_template_width") ||
             key == QStringLiteral("hot_template_width")) && ok) {
            parsed.width = std::max(0, static_cast<int>(std::lround(number)));
        } else if ((key == QStringLiteral("hot_pixel_template_height") ||
                    key == QStringLiteral("hot_template_height")) && ok) {
            parsed.height = std::max(0, static_cast<int>(std::lround(number)));
        } else if (key == QStringLiteral("camera_a_hot_pixel_mask") ||
                   key == QStringLiteral("camera0_hot_pixel_mask")) {
            parsed.camera0Mask = resolveConfigPath(valueText);
        } else if (key == QStringLiteral("camera_a_hot_pixel_excess") ||
                   key == QStringLiteral("camera0_hot_pixel_excess")) {
            parsed.camera0Excess = resolveConfigPath(valueText);
        } else if (key == QStringLiteral("camera_b_hot_pixel_mask") ||
                   key == QStringLiteral("camera1_hot_pixel_mask")) {
            parsed.camera1Mask = resolveConfigPath(valueText);
        } else if (key == QStringLiteral("camera_b_hot_pixel_excess") ||
                   key == QStringLiteral("camera1_hot_pixel_excess")) {
            parsed.camera1Excess = resolveConfigPath(valueText);
        }
    }

    const bool hasCompleteTemplate =
        parsed.width > 0 &&
        parsed.height > 0 &&
        !parsed.camera0Mask.isEmpty() &&
        !parsed.camera0Excess.isEmpty() &&
        !parsed.camera1Mask.isEmpty() &&
        !parsed.camera1Excess.isEmpty();
    if (!hasCompleteTemplate) {
        return false;
    }

    *settings = parsed;
    return true;
}
double medianOfSamples(QVector<double> samples)
{
    if (samples.isEmpty()) {
        return 0.0;
    }

    std::sort(samples.begin(), samples.end());
    const int middle = samples.size() / 2;
    if ((samples.size() % 2) == 1) {
        return samples[middle];
    }
    return (samples[middle - 1] + samples[middle]) * 0.5;
}

double deterministicUnitNoise(int frameIndex, int salt)
{
    quint32 x = static_cast<quint32>(frameIndex) * 1664525U +
               static_cast<quint32>(salt) * 1013904223U + 0x9e3779b9U;
    x ^= x >> 16;
    x *= 2246822519U;
    x ^= x >> 13;
    x *= 3266489917U;
    x ^= x >> 16;
    return (static_cast<double>(x) / static_cast<double>(std::numeric_limits<quint32>::max())) * 2.0 - 1.0;
}

double decimalYearFromUtc(const QDateTime& utcDateTime)
{
    const QDateTime utc = utcDateTime.toUTC();
    const QDate date = utc.date();
    const QTime time = utc.time();
    const int year = date.year();
    const QDate startDate(year, 1, 1);
    const QDate nextYearDate(year + 1, 1, 1);
    const double dayOffset = static_cast<double>(startDate.daysTo(date));
    const double secondsOfDay =
        static_cast<double>(QTime(0, 0).secsTo(time)) + static_cast<double>(time.msec()) / 1000.0;
    const double daysInYear = static_cast<double>(startDate.daysTo(nextYearDate));
    return static_cast<double>(year) + (dayOffset + secondsOfDay / 86400.0) / daysInYear;
}

qint64 safeRoiIncrement(qint64 increment)
{
    return increment > 0 ? increment : 1;
}

qint64 alignRoiValue(qint64 value, const RoiAxisRange& range)
{
    const qint64 increment = safeRoiIncrement(range.increment);
    const qint64 clamped = std::clamp(value, range.minValue, range.maxValue);
    const qint64 steps = (clamped - range.minValue) / increment;
    const qint64 aligned = range.minValue + steps * increment;
    return std::clamp(aligned, range.minValue, range.maxValue);
}

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

QVector<InitialStarCandidate> detectInitialStarCandidates(const cv::Mat& grayscale,
                                                          double* peakValue = nullptr,
                                                          double* thresholdValue = nullptr)
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
                                       double* peakValue = nullptr)
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

QString toggleButtonStyle(bool active)
{
    return active
               ? QStringLiteral("background-color: #20496b; border: 1px solid #56d4ff; color: #f8fcff;")
               : QString();
}

QString uiStatusColor(UiStatusLevel level)
{
    switch (level) {
    case UiStatusLevel::Info:
        return QStringLiteral("#56d4ff");
    case UiStatusLevel::Success:
        return QStringLiteral("#95dd6b");
    case UiStatusLevel::Warning:
        return QStringLiteral("#ffbe55");
    case UiStatusLevel::Error:
        return QStringLiteral("#ff5c57");
    case UiStatusLevel::Muted:
    default:
        return QStringLiteral("#8ea5bb");
    }
}

QString cameraStatusText(bool online)
{
    return online ? QStringLiteral("在线") : QStringLiteral("离线");
}

UiStatusLevel cameraStatusLevel(bool online)
{
    return online ? UiStatusLevel::Success : UiStatusLevel::Muted;
}

QString statusLabelStyle(const QString& color)
{
    return QStringLiteral("color: %1; background: transparent; padding: 0 12px 8px 12px;").arg(color);
}

QString statusLabelStyle(UiStatusLevel level)
{
    return statusLabelStyle(uiStatusColor(level));
}

bool pulseConfigsMatch(const PulseGeneratorManager::Config& lhs,
                       const PulseGeneratorManager::Config& rhs)
{
    return lhs.enabled == rhs.enabled &&
           lhs.portName.trimmed().compare(rhs.portName.trimmed(), Qt::CaseInsensitive) == 0 &&
           lhs.baudRate == rhs.baudRate &&
           lhs.terminalId == rhs.terminalId &&
           qFuzzyCompare(lhs.frequencyHz + 1.0, rhs.frequencyHz + 1.0) &&
           lhs.pulseCount == rhs.pulseCount &&
           qFuzzyCompare(lhs.dutyPercent + 1.0, rhs.dutyPercent + 1.0) &&
           lhs.remoteControl == rhs.remoteControl;
}
}

DIMM::DIMM(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui_DIMM)
{
    qRegisterMetaType<PolarisSolveResult>("PolarisSolveResult");
    qRegisterMetaType<TelescopeSlot>("TelescopeSlot");
    qRegisterMetaType<EafDeviceDescriptor>("EafDeviceDescriptor");
    qRegisterMetaType<EafDeviceState>("EafDeviceState");
    qRegisterMetaType<QVector<EafDeviceDescriptor>>("QVector<EafDeviceDescriptor>");

    ui->setupUi(this);

    m_autoExposureController.configure(m_autoExposureConfig);
    m_settingsDialog = new SettingsDialog(this);
    m_focuserManager = new EafFocuserManager(this);
    m_focuserControlWidget = new FocuserControlWidget(m_settingsDialog);
    m_focuserControlWidget->setManager(m_focuserManager);
    m_settingsDialog->addSettingsPage(m_focuserControlWidget, QStringLiteral("自动调焦"));
    m_focuserManager->initialize();

    m_lblStatusState = new QLabel(this);
    m_lblStatusState->setObjectName(QStringLiteral("lblStatusState"));
    ui->statusbar->addWidget(m_lblStatusState);

    m_lblStatusROI = new QLabel(this);
    m_lblStatusROI->setObjectName(QStringLiteral("lblStatusROI"));
    ui->statusbar->addWidget(m_lblStatusROI);

    m_lblStatusFrames = new QLabel(this);
    m_lblStatusFrames->setObjectName(QStringLiteral("lblStatusFrames"));
    ui->statusbar->addWidget(m_lblStatusFrames);

    ui->leftPanel->setMinimumWidth(248);
    ui->leftPanel->setMaximumWidth(300);
    ui->mainSplitter->setSizes({600, 340});
    ui->mainSplitter->setStretchFactor(0, 4);
    ui->mainSplitter->setStretchFactor(1, 3);
    ui->roiImagesArea->setMinimumHeight(220);
    ui->chartsArea->setMinimumHeight(320);
    ui->statsCard->setMinimumHeight(150);
    for (QLabel* label : {ui->lblStatFrames,
                          ui->lblStatValid,
                          ui->lblStatLatency,
                          ui->lblStatWindow,
                          ui->lblStatExposure,
                          ui->lblStatAutoExposure}) {
        label->setWordWrap(true);
        label->setMinimumHeight(30);
    }
    ui->stackedWidget->setCurrentIndex(0);
    ui->thetaCard->hide();
    ui->tauCard->hide();
    ui->lblThetaValue->setText(QStringLiteral("--"));
    ui->lblTauValue->setText(QStringLiteral("--"));
    m_statusText = QStringLiteral("状态: 就绪");

    m_simulationTimer = new QTimer(this);
    m_simulationTimer->setInterval(kSimulationFrameIntervalMs);
    connect(m_simulationTimer, &QTimer::timeout, this, &DIMM::onUpdateSimulation);

    m_actionStartSimulation = new QAction(QStringLiteral("模拟采集"), this);
    m_actionStartSimulation->setObjectName(QStringLiteral("btnStartSimulation"));
    if (ui->toolbar) {
        ui->toolbar->insertAction(ui->btnStop, m_actionStartSimulation);
    }
    if (ui->menuTools) {
        ui->menuTools->insertAction(ui->actionROISchedule, m_actionStartSimulation);
        ui->menuTools->insertSeparator(ui->actionROISchedule);
    }

    m_actionAlignmentMode = new QAction(QStringLiteral("对准模式"), this);
    m_actionAlignmentMode->setObjectName(QStringLiteral("btnAlignmentMode"));
    m_actionAlignmentMode->setCheckable(true);
    if (ui->toolbar) {
        ui->toolbar->insertAction(ui->btnSettings, m_actionAlignmentMode);
    }
    if (ui->menuTools) {
        ui->menuTools->insertAction(ui->actionROISchedule, m_actionAlignmentMode);
    }

    m_actionConfirmCamera1Polaris = new QAction(QStringLiteral("确认相机1的北极星"), this);
    m_actionConfirmCamera1Polaris->setObjectName(QStringLiteral("btnConfirmCamera1Polaris"));
    m_actionConfirmCamera2Polaris = new QAction(QStringLiteral("确认相机2的北极星"), this);
    m_actionConfirmCamera2Polaris->setObjectName(QStringLiteral("btnConfirmCamera2Polaris"));
    m_actionRetryCamera1PolarisSolve = new QAction(QStringLiteral("重新自动识别相机1"), this);
    m_actionRetryCamera1PolarisSolve->setObjectName(QStringLiteral("btnRetryCamera1PolarisSolve"));
    m_actionRetryCamera2PolarisSolve = new QAction(QStringLiteral("重新自动识别相机2"), this);
    m_actionRetryCamera2PolarisSolve->setObjectName(QStringLiteral("btnRetryCamera2PolarisSolve"));
    m_actionRetryBothPolarisSolve = new QAction(QStringLiteral("重新自动识别双相机"), this);
    m_actionRetryBothPolarisSolve->setObjectName(QStringLiteral("btnRetryBothPolarisSolve"));
    if (ui->menuTools) {
        ui->menuTools->insertAction(ui->actionROISchedule, m_actionConfirmCamera2Polaris);
        ui->menuTools->insertAction(m_actionConfirmCamera2Polaris, m_actionConfirmCamera1Polaris);
        ui->menuTools->insertAction(m_actionConfirmCamera1Polaris, m_actionRetryBothPolarisSolve);
        ui->menuTools->insertAction(m_actionRetryBothPolarisSolve, m_actionRetryCamera2PolarisSolve);
        ui->menuTools->insertAction(m_actionRetryCamera2PolarisSolve, m_actionRetryCamera1PolarisSolve);
    }

    hideLegacyRoiScheduleUi();

    m_cameraManager = &CameraManager::instance();
    m_cameraManager->init();
    m_imageProcessor = new ImageProcessor(this);
    m_imageProcessor->setTargetFrameRateHz(m_pulseGeneratorFrequencyHz);
    m_imageProcessor->setAutoExposureMetricConfig(m_autoExposureConfig.enabled,
                                                  m_autoExposureConfig.hardSaturationDn);
    m_polarisSolverController = new PolarisSolverController(this);
    connect(m_polarisSolverController,
            &PolarisSolverController::solveFinished,
            this,
            &DIMM::onPolarisSolveFinished);
    connect(m_polarisSolverController,
            &PolarisSolverController::solveStatusChanged,
            this,
            &DIMM::onPolarisSolveStatusChanged);
    {
        const QString appThresholdPath =
            QDir(QApplication::applicationDirPath()).filePath(QStringLiteral("threshold.txt"));
        const QString cwdThresholdPath =
            QDir::current().filePath(QStringLiteral("threshold.txt"));
        QString configMessage;
        QString loadedThresholdPath;
        if (QFileInfo::exists(appThresholdPath)) {
            m_imageProcessor->loadProcessingConfig(appThresholdPath, &configMessage);
            loadedThresholdPath = appThresholdPath;
        } else if (QFileInfo::exists(cwdThresholdPath)) {
            m_imageProcessor->loadProcessingConfig(cwdThresholdPath, &configMessage);
            loadedThresholdPath = cwdThresholdPath;
        }

        if (!loadedThresholdPath.isEmpty()) {
            HotPixelTemplateSettings hotSettings;
            if (loadHotPixelTemplateSettings(loadedThresholdPath, &hotSettings)) {
                m_hotPixelTemplatesEnabled = true;
                m_hotPixelCamera0MaskPath = PathUtils::relativizePathToAppDir(hotSettings.camera0Mask);
                m_hotPixelCamera0ExcessPath = PathUtils::relativizePathToAppDir(hotSettings.camera0Excess);
                m_hotPixelCamera1MaskPath = PathUtils::relativizePathToAppDir(hotSettings.camera1Mask);
                m_hotPixelCamera1ExcessPath = PathUtils::relativizePathToAppDir(hotSettings.camera1Excess);
                m_hotPixelTemplateWidth = hotSettings.width;
                m_hotPixelTemplateHeight = hotSettings.height;
                m_hotPixelTemplateExposureUs =
                    PathUtils::exposureUsFromTemplatePath(m_hotPixelCamera0MaskPath);
                m_cachedHotPixelTemplateExposures.clear();
                m_cachedHotPixelTemplateScanMs = -1;
                const int selectedTemplateExposureUs =
                    selectHotPixelTemplateExposureForCurrentExposure(m_configExposureUs);
                if (selectedTemplateExposureUs > 0 &&
                    selectedTemplateExposureUs != m_hotPixelTemplateExposureUs) {
                    QString resolvedCamera0Mask;
                    QString resolvedCamera0Excess;
                    QString resolvedCamera1Mask;
                    QString resolvedCamera1Excess;
                    if (resolveHotPixelTemplatePathsForExposure(selectedTemplateExposureUs,
                                                                &resolvedCamera0Mask,
                                                                &resolvedCamera0Excess,
                                                                &resolvedCamera1Mask,
                                                                &resolvedCamera1Excess)) {
                        m_hotPixelCamera0MaskPath = resolvedCamera0Mask;
                        m_hotPixelCamera0ExcessPath = resolvedCamera0Excess;
                        m_hotPixelCamera1MaskPath = resolvedCamera1Mask;
                        m_hotPixelCamera1ExcessPath = resolvedCamera1Excess;
                        m_hotPixelTemplateExposureUs = selectedTemplateExposureUs;
                    }
                }
                m_imageProcessor->configureHotPixelTemplates(
                    PathUtils::resolvePathFromAppDir(m_hotPixelCamera0MaskPath),
                    PathUtils::resolvePathFromAppDir(m_hotPixelCamera0ExcessPath),
                    PathUtils::resolvePathFromAppDir(m_hotPixelCamera1MaskPath),
                    PathUtils::resolvePathFromAppDir(m_hotPixelCamera1ExcessPath),
                    m_hotPixelTemplateWidth,
                    m_hotPixelTemplateHeight);
            }
        }
    }
    m_pulseGenerator = new PulseGeneratorManager();
    setupConnections();

    if (auto* previewLayout = ui->previewCanvas->layout()) {
        while (previewLayout->count() > 0) {
            auto* item = previewLayout->takeAt(0);
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }
        delete previewLayout;
    }

    auto* previewCanvasLayout = new QVBoxLayout(ui->previewCanvas);
    previewCanvasLayout->setContentsMargins(10, 10, 10, 10);
    previewCanvasLayout->setSpacing(10);
    ui->lblFullframeLabel = new QLabel(QStringLiteral("双相机全画幅预览"), ui->previewCanvas);
    ui->lblFullframeLabel->setAlignment(Qt::AlignCenter);
    previewCanvasLayout->addWidget(ui->lblFullframeLabel);

    auto* dualPreviewLayout = new QHBoxLayout();
    dualPreviewLayout->setContentsMargins(0, 0, 0, 0);
    dualPreviewLayout->setSpacing(10);

    auto* cam1Panel = new QFrame(ui->previewCanvas);
    cam1Panel->setObjectName(QStringLiteral("fullFrameCam1Panel"));
    auto* cam1PanelLayout = new QVBoxLayout(cam1Panel);
    cam1PanelLayout->setContentsMargins(0, 0, 0, 0);
    cam1PanelLayout->setSpacing(6);
    m_lblFullFrameCam1 = new QLabel(QStringLiteral("全画幅预览 - 相机1"), cam1Panel);
    m_lblFullFrameCam1->setAlignment(Qt::AlignCenter);
    m_lblAlignmentSolveCam1 = new QLabel(QStringLiteral("自动识别: 未启动"), cam1Panel);
    m_lblAlignmentSolveCam1->setAlignment(Qt::AlignCenter);
    m_lblAlignmentSolveCam1->setStyleSheet(QStringLiteral("color: %1").arg(uiStatusColor(UiStatusLevel::Muted)));
    m_lblAlignmentSolveCam1->setVisible(false);
    auto* cam1AlignmentControls = new QWidget(cam1Panel);
    auto* cam1AlignmentControlsLayout = new QHBoxLayout(cam1AlignmentControls);
    cam1AlignmentControlsLayout->setContentsMargins(0, 0, 0, 0);
    cam1AlignmentControlsLayout->setSpacing(6);
    m_btnRetryCamera1PolarisSolve = new QPushButton(QStringLiteral("重新识别"), cam1AlignmentControls);
    m_btnConfirmCamera1Polaris = new QPushButton(QStringLiteral("人工确认"), cam1AlignmentControls);
    m_btnRetryCamera1PolarisSolve->setVisible(false);
    m_btnConfirmCamera1Polaris->setVisible(false);
    cam1AlignmentControlsLayout->addWidget(m_btnRetryCamera1PolarisSolve);
    cam1AlignmentControlsLayout->addWidget(m_btnConfirmCamera1Polaris);
    m_fullFrameCanvas1 = new FullFrameCanvas(cam1Panel);
    cam1PanelLayout->addWidget(m_lblFullFrameCam1);
    cam1PanelLayout->addWidget(m_lblAlignmentSolveCam1);
    cam1PanelLayout->addWidget(cam1AlignmentControls);
    cam1PanelLayout->addWidget(m_fullFrameCanvas1, 1);

    auto* cam2Panel = new QFrame(ui->previewCanvas);
    cam2Panel->setObjectName(QStringLiteral("fullFrameCam2Panel"));
    auto* cam2PanelLayout = new QVBoxLayout(cam2Panel);
    cam2PanelLayout->setContentsMargins(0, 0, 0, 0);
    cam2PanelLayout->setSpacing(6);
    m_lblFullFrameCam2 = new QLabel(QStringLiteral("全画幅预览 - 相机2"), cam2Panel);
    m_lblFullFrameCam2->setAlignment(Qt::AlignCenter);
    m_lblAlignmentSolveCam2 = new QLabel(QStringLiteral("自动识别: 未启动"), cam2Panel);
    m_lblAlignmentSolveCam2->setAlignment(Qt::AlignCenter);
    m_lblAlignmentSolveCam2->setStyleSheet(QStringLiteral("color: %1").arg(uiStatusColor(UiStatusLevel::Muted)));
    m_lblAlignmentSolveCam2->setVisible(false);
    auto* cam2AlignmentControls = new QWidget(cam2Panel);
    auto* cam2AlignmentControlsLayout = new QHBoxLayout(cam2AlignmentControls);
    cam2AlignmentControlsLayout->setContentsMargins(0, 0, 0, 0);
    cam2AlignmentControlsLayout->setSpacing(6);
    m_btnRetryCamera2PolarisSolve = new QPushButton(QStringLiteral("重新识别"), cam2AlignmentControls);
    m_btnConfirmCamera2Polaris = new QPushButton(QStringLiteral("人工确认"), cam2AlignmentControls);
    m_btnRetryCamera2PolarisSolve->setVisible(false);
    m_btnConfirmCamera2Polaris->setVisible(false);
    cam2AlignmentControlsLayout->addWidget(m_btnRetryCamera2PolarisSolve);
    cam2AlignmentControlsLayout->addWidget(m_btnConfirmCamera2Polaris);
    m_fullFrameCanvas2 = new FullFrameCanvas(cam2Panel);
    cam2PanelLayout->addWidget(m_lblFullFrameCam2);
    cam2PanelLayout->addWidget(m_lblAlignmentSolveCam2);
    cam2PanelLayout->addWidget(cam2AlignmentControls);
    cam2PanelLayout->addWidget(m_fullFrameCanvas2, 1);

    dualPreviewLayout->addWidget(cam1Panel, 1);
    dualPreviewLayout->addWidget(cam2Panel, 1);
    previewCanvasLayout->addLayout(dualPreviewLayout, 1);
    m_btnRetryBothPolarisSolve = new QPushButton(QStringLiteral("双相机重新识别"), ui->previewCanvas);
    m_btnRetryBothPolarisSolve->setVisible(false);
    previewCanvasLayout->addWidget(m_btnRetryBothPolarisSolve);

    connect(m_btnConfirmCamera1Polaris,
            &QPushButton::clicked,
            this,
            &DIMM::onConfirmCamera1PolarisCandidate);
    connect(m_btnConfirmCamera2Polaris,
            &QPushButton::clicked,
            this,
            &DIMM::onConfirmCamera2PolarisCandidate);
    connect(m_btnRetryCamera1PolarisSolve, &QPushButton::clicked, this, [this]() {
        m_actionRetryCamera1PolarisSolve->trigger();
    });
    connect(m_btnRetryCamera2PolarisSolve, &QPushButton::clicked, this, [this]() {
        m_actionRetryCamera2PolarisSolve->trigger();
    });
    connect(m_btnRetryBothPolarisSolve, &QPushButton::clicked, this, [this]() {
        m_actionRetryBothPolarisSolve->trigger();
    });

    m_cam1RoiCanvas = new RoiStarCanvas(ui->cam1ROICanvas);
    if (auto* cam1Layout = ui->cam1ROICanvas->layout()) {
        while (cam1Layout->count() > 0) {
            auto* item = cam1Layout->takeAt(0);
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }
        delete cam1Layout;
    }
    auto* newCam1Layout = new QVBoxLayout(ui->cam1ROICanvas);
    newCam1Layout->setContentsMargins(0, 0, 0, 0);
    newCam1Layout->setSpacing(4);
    ui->lblCam1ROICoord = new QLabel(QStringLiteral("(0.0, 0.0)"), ui->cam1ROICanvas);
    ui->lblCam1ROICoord->setAlignment(Qt::AlignCenter);
    newCam1Layout->addWidget(ui->lblCam1ROICoord);
    newCam1Layout->addWidget(m_cam1RoiCanvas);

    m_cam2RoiCanvas = new RoiStarCanvas(ui->cam2ROICanvas);
    if (auto* cam2Layout = ui->cam2ROICanvas->layout()) {
        while (cam2Layout->count() > 0) {
            auto* item = cam2Layout->takeAt(0);
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }
        delete cam2Layout;
    }
    auto* newCam2Layout = new QVBoxLayout(ui->cam2ROICanvas);
    newCam2Layout->setContentsMargins(0, 0, 0, 0);
    newCam2Layout->setSpacing(4);
    ui->lblCam2ROICoord = new QLabel(QStringLiteral("(0.0, 0.0)"), ui->cam2ROICanvas);
    ui->lblCam2ROICoord->setAlignment(Qt::AlignCenter);
    newCam2Layout->addWidget(ui->lblCam2ROICoord);
    newCam2Layout->addWidget(m_cam2RoiCanvas);

    m_r0Chart = new ChartWidget(ChartWidget::SeriesKind::R0, ui->r0ChartCanvas);
    if (auto* r0Layout = ui->r0ChartCanvas->layout()) {
        while (r0Layout->count() > 0) {
            auto* item = r0Layout->takeAt(0);
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }
        delete r0Layout;
    }
    auto* r0ChartLayout = new QVBoxLayout(ui->r0ChartCanvas);
    r0ChartLayout->setContentsMargins(0, 0, 0, 0);
    r0ChartLayout->addWidget(m_r0Chart);

    m_seeingChart = new ChartWidget(ChartWidget::SeriesKind::Seeing, ui->seeingChartCanvas);
    if (auto* seeingLayout = ui->seeingChartCanvas->layout()) {
        while (seeingLayout->count() > 0) {
            auto* item = seeingLayout->takeAt(0);
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }
        delete seeingLayout;
    }
    auto* seeingChartLayout = new QVBoxLayout(ui->seeingChartCanvas);
    seeingChartLayout->setContentsMargins(0, 0, 0, 0);
    seeingChartLayout->addWidget(m_seeingChart);

    m_settingsDialog->onApplyCamera = [this](double exposure, double gain, double continuousFrameRateHz) {
        m_configExposureUs = exposure;
        m_configGainDb = gain;
        m_configContinuousFrameRateHz = continuousFrameRateHz;
        for (int i = 0; i < 2; ++i) {
            if (m_cameraManager->isOpen(i)) {
                m_cameraManager->setExposure(i, exposure);
                m_cameraManager->setGain(i, gain);
            }
        }
        QString reason;
        if (!applyContinuousCameraFrameRate(&reason)) {
            setStatusMessage(reason.isEmpty()
                                 ? QStringLiteral("连续采集帧率应用失败")
                                 : reason,
                             UiStatusLevel::Warning);
            return;
        }
        setStatusMessage(QStringLiteral("相机参数已应用"), UiStatusLevel::Success);
    };
    m_settingsDialog->onApplyAutoExposure =
        [this](const AutoExposureConfig& config) {
            m_autoExposureConfig = config;
            resetAutoExposureState();
            if (m_imageProcessor) {
                m_imageProcessor->setAutoExposureMetricConfig(config.enabled, config.hardSaturationDn);
            }
            setStatusMessage(config.enabled ? QStringLiteral("自动曝光已启用: 状态机保护模式")
                                            : QStringLiteral("自动曝光已关闭"),
                             config.enabled ? UiStatusLevel::Success : UiStatusLevel::Warning);
        };
    m_settingsDialog->onApplyTriggerMode = [this](int mode) {
        m_configTriggerMode = mode;
        if (isLiveCaptureActive()) {
            setStatusMessage(QStringLiteral("实时采集中，触发模式变更已保存，将在停止采集后生效"),
                             UiStatusLevel::Warning);
            return;
        }
        for (int i = 0; i < 2; ++i) {
            if (m_cameraManager->isOpen(i)) {
                if (mode == 0) {
                    m_cameraManager->setTriggerMode(i, TriggerMode::Continuous);
                } else {
                    m_cameraManager->configureExternalTrigger(i);
                }
            }
        }
        setStatusMessage(mode == 0 ? QStringLiteral("触发模式已切换为连续采集")
                                   : QStringLiteral("触发模式已切换为硬件触发"),
                         UiStatusLevel::Success);
    };
    m_settingsDialog->onApplyPulseGenerator =
        [this](bool enabled,
               QString portName,
               int baudRate,
               int terminalId,
               double frequencyHz,
               quint32 pulseCount,
               double dutyPercent,
               bool remoteControl,
               QString* errorMessage) -> bool {
        m_pulseGeneratorEnabled = enabled;
        m_pulseGeneratorPort = portName;
        m_pulseGeneratorBaudRate = baudRate;
        m_pulseGeneratorTerminalId = terminalId;
        m_pulseGeneratorFrequencyHz = frequencyHz;
        m_pulseGeneratorPulseCount = pulseCount;
        m_pulseGeneratorDutyPercent = dutyPercent;
        m_pulseGeneratorRemoteControl = remoteControl;
        if (m_imageProcessor) {
            m_imageProcessor->setTargetFrameRateHz(m_pulseGeneratorFrequencyHz);
        }
        if (!m_pulseGenerator) {
            return true;
        }

        if (m_configTriggerMode == 0) {
            const QString savedMessage = enabled
                                             ? QStringLiteral("当前为连续采集模式，触发参数已保存，切换到硬件触发并开始采集时再下发。")
                                             : QStringLiteral("当前为连续采集模式，触发输出已关闭。");
            setStatusMessage(savedMessage, enabled ? UiStatusLevel::Info : UiStatusLevel::Warning);
            if (errorMessage) {
                *errorMessage = savedMessage;
            }
            return true;
        }

        if (isLiveCaptureActive()) {
            const QString pendingMessage = QStringLiteral("实时采集中，触发设置已保存，将在停止采集后再下发。");
            setStatusMessage(pendingMessage, UiStatusLevel::Warning);
            if (errorMessage) {
                *errorMessage = pendingMessage;
            }
            return true;
        }

        PulseGeneratorManager::Config pulseConfig;
        pulseConfig.enabled = enabled;
        pulseConfig.portName = portName;
        pulseConfig.baudRate = baudRate;
        pulseConfig.terminalId = terminalId;
        pulseConfig.frequencyHz = frequencyHz;
        pulseConfig.pulseCount = pulseCount;
        pulseConfig.dutyPercent = dutyPercent;
        pulseConfig.remoteControl = remoteControl;
        if (!m_pulseGenerator->applyConfig(pulseConfig, errorMessage)) {
            setStatusMessage(errorMessage && !errorMessage->isEmpty()
                                 ? *errorMessage
                                 : QStringLiteral("触发设置下发失败"),
                             UiStatusLevel::Error);
            return false;
        }

        setStatusMessage(enabled
                             ? QStringLiteral("触发设置已下发到脉冲板: %1 @ %2 Hz")
                                   .arg(portName)
                                   .arg(frequencyHz, 0, 'f', 1)
                             : QStringLiteral("脉冲板输出已关闭并同步"),
                         enabled ? UiStatusLevel::Success : UiStatusLevel::Warning);
        return true;
        };
    m_settingsDialog->onStartPulseOutput =
        [this](QString portName,
               int baudRate,
               int terminalId,
               double frequencyHz,
               quint32 pulseCount,
               double dutyPercent,
               bool remoteControl,
               QString* errorMessage) -> bool {
        m_pulseGeneratorEnabled = true;
        m_pulseGeneratorPort = portName;
        m_pulseGeneratorBaudRate = baudRate;
        m_pulseGeneratorTerminalId = terminalId;
        m_pulseGeneratorFrequencyHz = frequencyHz;
        m_pulseGeneratorPulseCount = pulseCount;
        m_pulseGeneratorDutyPercent = dutyPercent;
        m_pulseGeneratorRemoteControl = remoteControl;
        if (m_imageProcessor) {
            m_imageProcessor->setTargetFrameRateHz(m_pulseGeneratorFrequencyHz);
        }
        if (!m_pulseGenerator) {
            return true;
        }

        PulseGeneratorManager::Config pulseConfig;
        pulseConfig.enabled = true;
        pulseConfig.portName = portName;
        pulseConfig.baudRate = baudRate;
        pulseConfig.terminalId = terminalId;
        pulseConfig.frequencyHz = frequencyHz;
        pulseConfig.pulseCount = pulseCount;
        pulseConfig.dutyPercent = dutyPercent;
        pulseConfig.remoteControl = remoteControl;
        if (!m_pulseGenerator->configureAndStart(pulseConfig, errorMessage)) {
            setStatusMessage(errorMessage && !errorMessage->isEmpty()
                                 ? *errorMessage
                                 : QStringLiteral("脉冲输出启动失败"),
                             UiStatusLevel::Error);
            return false;
        }

        if (m_captureState == CaptureState::Live && m_configTriggerMode != 0) {
            setStatusMessage(QStringLiteral("状态: 脉冲板已开始输出: %1 @ %2 Hz，等待相机接收触发帧")
                                 .arg(portName)
                                 .arg(frequencyHz, 0, 'f', 1),
                             UiStatusLevel::Success);
            scheduleHardwareTriggerStartupCheck();
        } else {
            setStatusMessage(QStringLiteral("脉冲板已开始输出: %1 @ %2 Hz")
                                 .arg(portName)
                                 .arg(frequencyHz, 0, 'f', 1),
                             UiStatusLevel::Success);
        }
        return true;
        };
    m_settingsDialog->onStopPulseOutput = [this](QString* errorMessage) -> bool {
        if (!m_pulseGenerator) {
            return true;
        }
        if (!m_pulseGenerator->stop(errorMessage)) {
            setStatusMessage(errorMessage && !errorMessage->isEmpty()
                                 ? *errorMessage
                                 : QStringLiteral("关闭脉冲失败"),
                             UiStatusLevel::Error);
            return false;
        }

        setStatusMessage(QStringLiteral("脉冲板输出已关闭"), UiStatusLevel::Warning);
        return true;
    };
    m_settingsDialog->onApplyProcessing = [this](int kernelSize, double sigma, int method) {
        m_imageProcessor->setGaussianKernelSize(kernelSize);
        m_imageProcessor->setGaussianSigma(sigma);
        m_imageProcessor->setCentroidMethod(method);
        setStatusMessage(QStringLiteral("图像处理参数已更新"), UiStatusLevel::Success);
    };
    m_settingsDialog->onApplyRoiRecentering =
        [this](double thresholdPx, int requiredFrames, qint64 cooldownMs, double minimumShiftPx) {
            m_roiRecenteringThresholdPx = thresholdPx;
            m_roiRecenteringRequiredFrames = requiredFrames;
            m_roiRecenteringCooldownMs = cooldownMs;
            m_roiRecenteringMinimumShiftPx = minimumShiftPx;
            activeRuntime().roiRecenteringCandidateFrameCount = 0;
            setStatusMessage(QStringLiteral("ROI 重居中参数已更新"), UiStatusLevel::Success);
        };
    m_settingsDialog->onApplyFullFrameStarDetection =
        [this](double thresholdAbsolute,
               double sigmaThreshold,
               double peakFraction,
               double minimumIntensity,
               int minArea,
               int maxArea) {
            InitialStarDetectionConfig config;
            config.thresholdAbsolute = thresholdAbsolute;
            config.sigmaThreshold = sigmaThreshold;
            config.peakFraction = peakFraction;
            config.minimumIntensity = minimumIntensity;
            config.minArea = minArea;
            config.maxArea = maxArea;
            setCurrentInitialStarDetectionConfig(config);
            setStatusMessage(QStringLiteral("全画幅找星参数已更新"), UiStatusLevel::Success);
        };
    m_settingsDialog->onApplyHotPixelTemplates =
        [this](bool enabled,
               QString camera0MaskPath,
               QString camera0ExcessPath,
               QString camera1MaskPath,
               QString camera1ExcessPath,
               int templateWidth,
               int templateHeight) {
            m_hotPixelTemplatesEnabled = enabled;
            m_hotPixelCamera0MaskPath = enabled ? PathUtils::relativizePathToAppDir(camera0MaskPath) : QString();
            m_hotPixelCamera0ExcessPath = enabled ? PathUtils::relativizePathToAppDir(camera0ExcessPath) : QString();
            m_hotPixelCamera1MaskPath = enabled ? PathUtils::relativizePathToAppDir(camera1MaskPath) : QString();
            m_hotPixelCamera1ExcessPath = enabled ? PathUtils::relativizePathToAppDir(camera1ExcessPath) : QString();
            m_hotPixelTemplateWidth = enabled ? templateWidth : 0;
            m_hotPixelTemplateHeight = enabled ? templateHeight : 0;
            m_hotPixelTemplateExposureUs =
                enabled ? PathUtils::exposureUsFromTemplatePath(m_hotPixelCamera0MaskPath) : 0;
            m_cachedHotPixelTemplateExposures.clear();
            m_cachedHotPixelTemplateScanMs = -1;

            bool matchedRequestedExposureTemplate = false;
            bool missingRequestedExposureTemplate = false;
            const int requestedExposureUs = static_cast<int>(std::lround(m_configExposureUs));
            const int selectedTemplateExposureUs =
                enabled ? selectHotPixelTemplateExposureForCurrentExposure(m_configExposureUs) : 0;
            if (enabled) {
                QString resolvedCamera0Mask;
                QString resolvedCamera0Excess;
                QString resolvedCamera1Mask;
                QString resolvedCamera1Excess;
                if (selectedTemplateExposureUs > 0 &&
                    selectedTemplateExposureUs != m_hotPixelTemplateExposureUs &&
                    resolveHotPixelTemplatePathsForExposure(selectedTemplateExposureUs,
                                                            &resolvedCamera0Mask,
                                                            &resolvedCamera0Excess,
                                                            &resolvedCamera1Mask,
                                                            &resolvedCamera1Excess)) {
                    m_hotPixelCamera0MaskPath = resolvedCamera0Mask;
                    m_hotPixelCamera0ExcessPath = resolvedCamera0Excess;
                    m_hotPixelCamera1MaskPath = resolvedCamera1Mask;
                    m_hotPixelCamera1ExcessPath = resolvedCamera1Excess;
                    m_hotPixelTemplateExposureUs = selectedTemplateExposureUs;
                    if (m_settingsDialog) {
                        m_settingsDialog->hotPixelCam0MaskEdit->setText(m_hotPixelCamera0MaskPath);
                        m_settingsDialog->hotPixelCam0ExcessEdit->setText(m_hotPixelCamera0ExcessPath);
                        m_settingsDialog->hotPixelCam1MaskEdit->setText(m_hotPixelCamera1MaskPath);
                        m_settingsDialog->hotPixelCam1ExcessEdit->setText(m_hotPixelCamera1ExcessPath);
                    }
                    matchedRequestedExposureTemplate = true;
                } else if (selectedTemplateExposureUs > 0 &&
                           selectedTemplateExposureUs != m_hotPixelTemplateExposureUs) {
                    missingRequestedExposureTemplate = true;
                }
            }

            m_imageProcessor->configureHotPixelTemplates(PathUtils::resolvePathFromAppDir(m_hotPixelCamera0MaskPath),
                                                         PathUtils::resolvePathFromAppDir(m_hotPixelCamera0ExcessPath),
                                                         PathUtils::resolvePathFromAppDir(m_hotPixelCamera1MaskPath),
                                                         PathUtils::resolvePathFromAppDir(m_hotPixelCamera1ExcessPath),
                                                         m_hotPixelTemplateWidth,
                                                         m_hotPixelTemplateHeight);
            if (!enabled) {
                setStatusMessage(QStringLiteral("热像素模板修正已关闭"), UiStatusLevel::Warning);
            } else if (matchedRequestedExposureTemplate) {
                setStatusMessage(QStringLiteral("热像素模板已自动切换到 %1 μs 档并启用")
                                     .arg(m_hotPixelTemplateExposureUs),
                                 UiStatusLevel::Success);
            } else if (missingRequestedExposureTemplate) {
                setStatusMessage(QStringLiteral("当前曝光 %1 μs 未找到对应热像素模板，保持 %2 μs 模板")
                                     .arg(requestedExposureUs)
                                     .arg(m_hotPixelTemplateExposureUs),
                                 UiStatusLevel::Warning);
            } else {
                setStatusMessage(QStringLiteral("热像素模板修正已启用"), UiStatusLevel::Success);
            }
        };
    m_settingsDialog->onApplyOptics =
        [this](double apertureDiameterMm,
               double baselineSeparationMm,
               double baselineAngleDeg,
               double focalLengthCm,
               double zenithAngleDeg,
               double lambdaNm,
               double pixelSizeUm) {
            m_imageProcessor->setOpticalParams(apertureDiameterMm,
                                               baselineSeparationMm,
                                               baselineAngleDeg,
                                               focalLengthCm,
                                               zenithAngleDeg,
                                               lambdaNm,
                                               pixelSizeUm);
        setStatusMessage(QStringLiteral("光学参数已更新"), UiStatusLevel::Success);
        };
    m_settingsDialog->onApplyAlignment =
        [this](bool autoRadius,
               double focalLengthMm,
               double pixelSizeUm,
               double polarDistanceArcmin,
               double radiusAdjustPx,
               double previewRateHz) {
            m_alignmentAutoRadius = autoRadius;
            m_alignmentFocalLengthMm = focalLengthMm;
            m_alignmentPixelSizeUm = pixelSizeUm;
            m_alignmentPolarisPolarDistanceArcmin = polarDistanceArcmin;
            m_alignmentRadiusAdjustPx = radiusAdjustPx;
            m_alignmentPreviewRateHz = previewRateHz;
            if (m_captureState == CaptureState::Alignment) {
                if (m_fullFrameCanvas1) {
                    m_fullFrameCanvas1->update();
                }
                if (m_fullFrameCanvas2) {
                    m_fullFrameCanvas2->update();
                }
                setStatusMessage(QStringLiteral("对准参数已更新，轨道半径 %1 px")
                                     .arg(alignmentOrbitRadiusPx(), 0, 'f', 1),
                                 UiStatusLevel::Info);
            }
        };
    m_settingsDialog->onApplyPolarisSolver =
        [this](bool enabled,
               bool showMatchedCatalogStars,
               int maxDetectedStars,
               int minMatchedStars,
               double maxRmsPx,
               int retryIntervalMs,
               double minMatchedSpatialSpreadPx,
               double minPolarisSnr,
               bool allowSaturatedPolarisConfirmation) {
            m_alignmentAutoSolveEnabled = enabled;
            m_alignmentShowMatchedCatalogStars = showMatchedCatalogStars;
            m_alignmentMaxDetectedStars = maxDetectedStars;
            m_alignmentMinMatchedStars = minMatchedStars;
            m_alignmentMaxRmsPx = maxRmsPx;
            m_alignmentRetryIntervalMs = retryIntervalMs;
            m_alignmentMinMatchedSpatialSpreadPx = minMatchedSpatialSpreadPx;
            m_alignmentMinPolarisSnr = minPolarisSnr;
            m_alignmentAllowSaturatedPolarisConfirmation = allowSaturatedPolarisConfirmation;
            if (m_captureState == CaptureState::Alignment) {
                setStatusMessage(enabled
                                     ? QStringLiteral("北极星自动识别参数已更新")
                                     : QStringLiteral("北极星自动识别已关闭"),
                                 enabled ? UiStatusLevel::Info : UiStatusLevel::Warning);
            }
        };
    m_settingsDialog->onApplyStorage = [this](QString path, int interval) {
        m_dataPath = path;
        m_saveInterval = qMax(1, interval);
        setStatusMessage(QStringLiteral("存储参数已更新"), UiStatusLevel::Success);
    };
    m_settingsDialog->onApplyNetwork = [this](QString ip, quint16 port) {
        if (!isSettingsApplyAllowed()) {
            if (m_settingsDialog && m_settingsDialog->applyStatusLabel) {
                m_settingsDialog->applyStatusLabel->setText(QStringLiteral("相机连接中，暂不允许修改网络设置"));
                m_settingsDialog->applyStatusLabel->setStyleSheet(statusLabelStyle(UiStatusLevel::Warning));
            }
            return;
        }
        m_commManager->setRemoteAddress(ip, port);
        setStatusMessage(QStringLiteral("网络参数已保存: %1:%2").arg(ip).arg(port), UiStatusLevel::Success);
        refreshStatusUi();
    };
    m_settingsDialog->onConnectNetwork = [this](QString ip, quint16 port) {
        if (!isSettingsApplyAllowed()) {
            if (m_settingsDialog && m_settingsDialog->applyStatusLabel) {
                m_settingsDialog->applyStatusLabel->setText(QStringLiteral("相机连接中，暂不允许连接上位机"));
                m_settingsDialog->applyStatusLabel->setStyleSheet(statusLabelStyle(UiStatusLevel::Warning));
            }
            return;
        }
        m_commManager->setRemoteAddress(ip, port);
        m_reporting = false;
        m_commConnecting = true;
        if (m_reportTimer) {
            m_reportTimer->stop();
        }
        m_commManager->disconnectFromHost();
        m_commManager->connectToHost(ip, port);
        setStatusMessage(QStringLiteral("正在连接上位机 %1:%2").arg(ip).arg(port), UiStatusLevel::Warning);
        refreshStatusUi();
    };
    m_settingsDialog->onAfterApply = [this]() {
        if (!m_settingsDialog || !m_settingsDialog->applyStatusLabel) {
            return;
        }

        if (!isSettingsApplyAllowed()) {
            m_settingsDialog->applyStatusLabel->setText(QStringLiteral("部分设置待连接流程结束后再处理"));
            m_settingsDialog->applyStatusLabel->setStyleSheet(statusLabelStyle(UiStatusLevel::Warning));
            return;
        }

        const int connectedCameras = openCameraCount();
        QString message;
        UiStatusLevel level = UiStatusLevel::Success;
        if (connectedCameras <= 0) {
            message = QStringLiteral("配置已保存，待相机连接后生效");
            level = UiStatusLevel::Warning;
        } else {
            message = QStringLiteral("配置已下发到 %1 台在线相机").arg(connectedCameras);
            level = UiStatusLevel::Success;
        }

        m_settingsDialog->applyStatusLabel->setText(message);
        m_settingsDialog->applyStatusLabel->setStyleSheet(statusLabelStyle(level));
    };

    connect(m_cameraManager, &CameraManager::frameCaptured, this, &DIMM::onCapturedFramePacket, Qt::QueuedConnection);
    connect(m_cameraManager, &CameraManager::cameraConnected, this, &DIMM::onCameraConnected, Qt::QueuedConnection);
    connect(m_cameraManager, &CameraManager::cameraDisconnected, this, &DIMM::onCameraDisconnected, Qt::QueuedConnection);
    connect(m_cameraManager, &CameraManager::cameraError, this, &DIMM::onCameraError, Qt::QueuedConnection);

    connect(m_imageProcessor,
            &ImageProcessor::centroidReady,
            this,
            [this](int camIdx,
                   double x,
                   double y,
                   double peakValue,
                   double totalFlux,
                   double background,
                   double threshold,
                   quint64 signalPixelCount) {
        if (!hasActiveCapture()) {
            return;
        }
        if (camIdx < 0 || camIdx >= 2) {
            return;
        }
        if (m_captureState == CaptureState::Live &&
            m_liveStartupPhase != LiveStartupPhase::Tracking) {
            return;
        }
        auto& runtime = activeRuntime();
        const bool hadBothCentroids = hasValidCentroidsForRoiUpdate();
        const bool usable = isUsableCentroidSample(camIdx,
                                                   x,
                                                   y,
                                                   peakValue,
                                                   totalFlux,
                                                   background,
                                                   threshold,
                                                   signalPixelCount,
                                                   false);
        const bool liveTrackingEdgeCentroid =
            m_captureState == CaptureState::Live &&
            m_liveStartupPhase == LiveStartupPhase::Tracking &&
            isCentroidNearCurrentRoiEdge(camIdx, x, y);
        auto* label = camIdx == 0 ? ui->lblCam1ROICoord : ui->lblCam2ROICoord;
        label->setText(QStringLiteral("(%1, %2)").arg(x, 0, 'f', 1).arg(y, 0, 'f', 1));
        if (!usable || liveTrackingEdgeCentroid) {
            runtime.hasValidCentroid[camIdx] = false;
            if (liveTrackingEdgeCentroid) {
                handleLiveRoiCentroidLoss(camIdx);
            }
            return;
        }

        runtime.centroidX[camIdx] = x;
        runtime.centroidY[camIdx] = y;
        runtime.peakBrightness[camIdx] = peakValue;
        runtime.hasValidCentroid[camIdx] = true;
        if (m_captureState == CaptureState::Live && m_liveStartupPhase == LiveStartupPhase::Tracking) {
            runtime.lastTargetPosition[camIdx] = QPointF(x, y);
            runtime.hasLastTargetPosition[camIdx] = true;
        }
        if (m_captureState == CaptureState::Simulation &&
            !runtime.simulationRoiSeeded &&
            !hadBothCentroids &&
            hasValidCentroidsForRoiUpdate()) {
            updateMinuteRoi(true);
            runtime.simulationRoiSeeded = true;
        }

        if (m_captureState == CaptureState::Live &&
            m_liveStartupPhase == LiveStartupPhase::Tracking) {
            if (shouldUpdateRoiForRecentering()) {
                updateMinuteRoi(true);
            }
        }
    });

    connect(m_imageProcessor,
            &ImageProcessor::autoExposureSampleReady,
            this,
            [this](int cameraIndex,
                   double peakValue,
                   double fitPeakValue,
                   double background,
                   double noiseSigma,
                   double threshold,
                   quint64 signalPixelCount,
                   quint64 saturatedPixelCount,
                   bool centroidValid,
                   bool measurementUsable,
                   quint64 frameId,
                   qint64 timestampMs) {
        AutoExposureFrameSample sample;
        sample.cameraIndex = cameraIndex;
        sample.peakDn = peakValue;
        sample.fitPeakDn = fitPeakValue;
        sample.backgroundDn = background;
        sample.noiseSigmaDn = noiseSigma;
        sample.thresholdDn = threshold;
        sample.signalPixelCount = signalPixelCount;
        sample.saturatedPixelCount = saturatedPixelCount;
        sample.centroidValid = centroidValid;
        sample.measurementUsable = measurementUsable;
        sample.frameId = frameId;
        sample.timestampMs = timestampMs;
        handleAutoExposureSample(sample);
    });

    connect(m_imageProcessor,
            &ImageProcessor::differentialSampleReady,
            this,
            [this](quint64 pairedSampleCount, quint64 droppedUnpairedCount) {
        if (!hasActiveCapture()) {
            return;
        }
        auto& runtime = activeRuntime();
        runtime.pairedSampleCount = pairedSampleCount;
        runtime.droppedUnpairedSampleCount = droppedUnpairedCount;
        refreshMeasurementUi();
    });

    connect(m_imageProcessor, &ImageProcessor::frameProcessed, this, [this](int camIdx, bool centroidValid, double elapsedMs) {
        if (!hasActiveCapture()) {
            return;
        }
        if (camIdx < 0 || camIdx >= 2) {
            return;
        }
        if (m_captureState == CaptureState::Live &&
            m_liveStartupPhase != LiveStartupPhase::Tracking) {
            return;
        }
        auto& runtime = activeRuntime();
        ++runtime.processedFrameCount;
        ++runtime.processedFrameCountPerCamera[camIdx];
        if (centroidValid) {
            ++runtime.validCentroidCount;
            ++runtime.validCentroidCountPerCamera[camIdx];
            runtime.lostCentroidFrameCount[camIdx] = 0;
            runtime.lostCentroidSinceMs[camIdx] = -1;
        } else {
            runtime.hasValidCentroid[camIdx] = false;
            handleLiveRoiCentroidLoss(camIdx);
        }
        runtime.latestProcessingLatencyMs = elapsedMs;
        if (runtime.processedFrameCount == 1) {
            runtime.averageProcessingLatencyMs = elapsedMs;
        } else {
            runtime.averageProcessingLatencyMs +=
                (elapsedMs - runtime.averageProcessingLatencyMs) /
                static_cast<double>(runtime.processedFrameCount);
        }
    });

    connect(m_imageProcessor, &ImageProcessor::syncSampleReady, this, [this](double syncResidualUs) {
        if (!hasActiveCapture()) {
            return;
        }
        auto& runtime = activeRuntime();
        ++runtime.syncSampleCount;

        runtime.latestSyncResidualUs = syncResidualUs;
        const double syncJitterUs = std::abs(syncResidualUs);
        ++runtime.syncJitterSampleCount;
        runtime.latestSyncJitterUs = syncJitterUs;
        runtime.maxSyncJitterUs = std::max(runtime.maxSyncJitterUs, syncJitterUs);
        if (runtime.syncJitterSampleCount == 1) {
            runtime.averageSyncJitterUs = syncJitterUs;
        } else {
            runtime.averageSyncJitterUs +=
                (syncJitterUs - runtime.averageSyncJitterUs) /
                static_cast<double>(runtime.syncJitterSampleCount);
        }
    });

    connect(m_imageProcessor, &ImageProcessor::roiImageReady, this, [this](int camIdx, cv::Mat roiImage) {
        if (!hasActiveCapture()) {
            return;
        }
        if (camIdx < 0 || camIdx >= 2) {
            return;
        }
        auto& runtime = activeRuntime();
        if (camIdx == 0) {
            m_cam1RoiCanvas->setRoiImage(roiImage);
            const RoiRect roi = m_imageProcessor ? m_imageProcessor->getCurrentRoi(0) : RoiRect();
            m_cam1RoiCanvas->setCentroid(runtime.centroidX[0] - roi.x,
                                         runtime.centroidY[0] - roi.y);
        } else if (camIdx == 1) {
            m_cam2RoiCanvas->setRoiImage(roiImage);
            const RoiRect roi = m_imageProcessor ? m_imageProcessor->getCurrentRoi(1) : RoiRect();
            m_cam2RoiCanvas->setCentroid(runtime.centroidX[1] - roi.x,
                                         runtime.centroidY[1] - roi.y);
        }
    });

    connect(m_imageProcessor, &ImageProcessor::atmosphereReady, this, [this](double r0, double seeing, double theta0, double tau0) {
        if (!hasActiveCapture()) {
            return;
        }
        auto& runtime = activeRuntime();
        runtime.hasValidAtmosphere = true;
        runtime.latestAtmosphere = {r0, seeing, theta0, tau0};
        refreshMeasurementUi();

        saveResultRow(runtime.frameCount);
    });

    const auto bindMouseStatus = [this](FullFrameCanvas* canvas, const QString& cameraLabel) {
        if (!canvas) {
            return;
        }
        connect(canvas, &FullFrameCanvas::mousePositionChanged, this, [this, cameraLabel](int x, int y) {
            if (!hasActiveCapture()) {
                return;
            }
            m_lblStatusROI->setText(QStringLiteral("%1 鼠标: (%2, %3)").arg(cameraLabel).arg(x).arg(y));
        });
    };
    bindMouseStatus(m_fullFrameCanvas1, QStringLiteral("相机1"));
    bindMouseStatus(m_fullFrameCanvas2, QStringLiteral("相机2"));

    m_1hzTimer = new QTimer(this);
    connect(m_1hzTimer, &QTimer::timeout, this, &DIMM::on1hzTick);
    m_1hzTimer->start(1000);

    m_hardwareTriggerStartupTimer = new QTimer(this);
    m_hardwareTriggerStartupTimer->setSingleShot(true);
    connect(m_hardwareTriggerStartupTimer, &QTimer::timeout, this, [this]() {
        checkHardwareTriggerStartup();
    });

    m_fileFlushTimer = new QTimer(this);
    connect(m_fileFlushTimer, &QTimer::timeout, this, &DIMM::flushPendingWrites);
    m_fileFlushTimer->start(2000);

    m_commManager = new CommManager(this);
    connect(m_commManager, &CommManager::connected, this, [this]() {
        m_commConnecting = false;
        updateCommState(true);
        setStatusMessage(QStringLiteral("上位机已连接"), UiStatusLevel::Success);
    });
    connect(m_commManager, &CommManager::disconnected, this, [this]() {
        m_commConnecting = false;
        updateCommState(false);
        m_reporting = false;
        if (m_reportTimer) {
            m_reportTimer->stop();
        }
        setStatusMessage(QStringLiteral("上位机已断开"), UiStatusLevel::Warning);
    });
    connect(m_commManager, &CommManager::connectionError, this, [this](const QString& msg) {
        m_commConnecting = false;
        updateCommState(false);
        m_reporting = false;
        if (m_reportTimer) {
            m_reportTimer->stop();
        }
        setStatusMessage(QStringLiteral("通信错误: %1").arg(msg), UiStatusLevel::Error);
    });
    connect(m_commManager, &CommManager::commandReceived, this, &DIMM::onCommCommand);

    m_reportTimer = new QTimer(this);
    m_reportTimer->setInterval(1000);
    connect(m_reportTimer, &QTimer::timeout, this, [this]() {
        reportMeasurement();
        reportDeviceStatus();
    });

    m_startTimeMs = static_cast<uint32_t>(QDateTime::currentMSecsSinceEpoch());

    hideLegacyRoiScheduleUi();
    updateMinuteRoi(true);
    refreshUi();
    updateCaptureState(m_captureState);

}

DIMM::~DIMM()
{
    if (m_focuserManager) {
        m_focuserManager->shutdown();
    }
    if (m_reportTimer) {
        m_reportTimer->stop();
    }
    if (m_fileFlushTimer) {
        m_fileFlushTimer->stop();
    }
    if (m_1hzTimer) {
        m_1hzTimer->stop();
    }
    if (m_simulationTimer) {
        m_simulationTimer->stop();
    }
    if (m_commManager) {
        m_commManager->disconnectFromHost();
    }
    if (m_polarisSolverController) {
        disconnect(m_polarisSolverController, nullptr, this, nullptr);
    }
    if (m_cameraManager) {
        disconnect(m_cameraManager, nullptr, this, nullptr);
        m_cameraManager->stopAll();
        m_cameraManager->closeAll();
    }
    if (m_pulseGenerator) {
        m_pulseGenerator->stop();
        delete m_pulseGenerator;
        m_pulseGenerator = nullptr;
    }
    closeResultFile();
    delete ui;
}

void DIMM::setupConnections()
{
    connect(ui->btnStart, &QAction::triggered, this, &DIMM::onStartCapture);
    connect(m_actionStartSimulation, &QAction::triggered, this, &DIMM::onStartSimulation);
    connect(ui->btnStop, &QAction::triggered, this, &DIMM::onStopCapture);
    connect(ui->btnFullFrame, &QAction::triggered, this, &DIMM::onShowMainPage);
    connect(ui->btnSettings, &QAction::triggered, this, &DIMM::onShowSettings);
    connect(m_actionAlignmentMode, &QAction::triggered, this, &DIMM::onToggleAlignmentMode);
    connect(m_actionConfirmCamera1Polaris,
            &QAction::triggered,
            this,
            &DIMM::onConfirmCamera1PolarisCandidate);
    connect(m_actionConfirmCamera2Polaris,
            &QAction::triggered,
            this,
            &DIMM::onConfirmCamera2PolarisCandidate);
    connect(m_actionRetryCamera1PolarisSolve, &QAction::triggered, this, [this]() {
        requestAutomaticPolarisSolve(0, true);
    });
    connect(m_actionRetryCamera2PolarisSolve, &QAction::triggered, this, [this]() {
        requestAutomaticPolarisSolve(1, true);
    });
    connect(m_actionRetryBothPolarisSolve,
            &QAction::triggered,
            this,
            &DIMM::requestAutomaticPolarisSolveBoth);
    connect(ui->btnToggleROI, &QPushButton::clicked, this, &DIMM::onToggleRoiImages);
    connect(ui->btnToggleCharts, &QPushButton::clicked, this, &DIMM::onToggleCharts);

    connect(ui->actionSaveConfig, &QAction::triggered, this, &DIMM::onSaveConfig);
    connect(ui->actionLoadConfig, &QAction::triggered, this, &DIMM::onLoadConfig);
    connect(ui->actionExportData, &QAction::triggered, this, &DIMM::onExportData);
    connect(ui->actionExportReport, &QAction::triggered, this, &DIMM::onExportReport);
    connect(ui->actionExit, &QAction::triggered, this, &QMainWindow::close);
    connect(ui->actionConnectAll, &QAction::triggered, this, &DIMM::onConnectAll);
    connect(ui->actionDisconnectAll, &QAction::triggered, this, &DIMM::onDisconnectAll);
    connect(ui->actionCameraSettings, &QAction::triggered, this, &DIMM::onShowSettings);
    connect(ui->actionViewMain, &QAction::triggered, this, &DIMM::onShowMainPage);
    connect(ui->actionViewSettings, &QAction::triggered, this, &DIMM::onShowSettings);
    connect(ui->actionToggleROIImages, &QAction::triggered, this, &DIMM::onToggleRoiImages);
    connect(ui->actionToggleCharts, &QAction::triggered, this, &DIMM::onToggleCharts);
    connect(ui->actionTrajectoryCalc, &QAction::triggered, this, [this]() {
        QMessageBox::information(this, QStringLiteral("轨迹计算"), QStringLiteral("轨迹导入与预览功能将在后续版本中补充。"));
    });
    connect(ui->actionAbout, &QAction::triggered, this, &DIMM::onAbout);

    connect(ui->btnImportTrajectory, &QPushButton::clicked, this, [this]() {
        const QString file = QFileDialog::getOpenFileName(
            this, QStringLiteral("导入轨迹文件"), QString(), QStringLiteral("文本文件 (*.txt *.csv)"));
        if (!file.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("导入轨迹文件"), QStringLiteral("已选择文件: %1").arg(file));
        }
    });

    auto* spaceShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(spaceShortcut, &QShortcut::activated, this, &DIMM::onStartCapture);

    auto* simulationShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+M")), this);
    connect(simulationShortcut, &QShortcut::activated, this, &DIMM::onStartSimulation);

    auto* escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escShortcut, &QShortcut::activated, this, &DIMM::onStopCapture);

}

void DIMM::refreshUi()
{
    refreshStatusUi();
    refreshCameraUi();
    refreshMeasurementUi();
    refreshPanelUi();
    refreshActionStates();
    syncCameraSelectionUi();
}

void DIMM::refreshStatusUi()
{
    if (m_lblStatusState) {
        m_lblStatusState->setText(m_statusText);
        m_lblStatusState->setStyleSheet(QStringLiteral("color: %1").arg(m_statusColor));
    }

    if (m_lblStatusFrames) {
        m_lblStatusFrames->setText(QStringLiteral("帧数: %1 帧").arg(activeRuntime().frameCount));
    }

    if (m_settingsDialog && m_settingsDialog->netStatusLabel) {
        QString netText;
        if (isSimulationCaptureActive()) {
            netText = m_commConnected
                          ? QStringLiteral("状态: 已连接 / 模拟模式不对外上报")
                          : QStringLiteral("状态: 模拟模式本地运行");
        } else if (m_commConnected) {
            netText = QStringLiteral("状态: 已连接");
        } else if (m_commConnecting) {
            netText = QStringLiteral("状态: 正在连接");
        } else {
            netText = QStringLiteral("状态: 未连接");
        }
        if (m_reporting && !isSimulationCaptureActive()) {
            netText += QStringLiteral(" / 正在上报");
        }
        UiStatusLevel netLevel = UiStatusLevel::Muted;
        if (isSimulationCaptureActive()) {
            netLevel = UiStatusLevel::Info;
        } else if (m_commConnected) {
            netLevel = UiStatusLevel::Success;
        } else if (m_commConnecting) {
            netLevel = UiStatusLevel::Warning;
        }
        m_settingsDialog->netStatusLabel->setText(netText);
        m_settingsDialog->netStatusLabel->setStyleSheet(
            QStringLiteral("color: %1").arg(uiStatusColor(netLevel)));
    }
    if (m_settingsDialog && m_settingsDialog->netConnectBtn) {
        m_settingsDialog->netConnectBtn->setText(m_commConnected
                                                     ? QStringLiteral("重新连接上位机")
                                                     : QStringLiteral("连接上位机"));
        m_settingsDialog->netConnectBtn->setEnabled(!m_connectingCameras && !m_commConnecting);
    }
}

void DIMM::refreshCameraUi()
{
    for (int i = 0; i < 2; ++i) {
        auto* statusLabel = i == 0 ? ui->lblCam1Status : ui->lblCam2Status;
        auto* infoLabel = i == 0 ? ui->lblCam1Info : ui->lblCam2Info;
        const bool online = m_cameraManager && m_cameraManager->isOpen(i);

        statusLabel->setText(cameraStatusText(online));
        statusLabel->setStyleSheet(QStringLiteral("color: %1").arg(uiStatusColor(cameraStatusLevel(online))));

        if (!online) {
            infoLabel->setText(QStringLiteral("序列号: 未连接\n帧率: -- fps | 温度: --°C"));
        }
    }
}

void DIMM::refreshMeasurementUi()
{
    const auto& runtime = activeRuntime();
    ui->lblPreviewMode->setText(currentPreviewModeText());
    if (m_lblStatusFrames) {
        m_lblStatusFrames->setText(QStringLiteral("帧数: %1 帧").arg(runtime.frameCount));
    }
    ui->lblStatFrames->setText(
        QStringLiteral("原始/入处理: %1 / %2")
            .arg(QString::number(runtime.frameCount),
                 QString::number(runtime.processedFrameCount)));
    ui->lblStatValid->setText(
        QStringLiteral("质心/配对: %1 / %2")
            .arg(QString::number(runtime.validCentroidCount),
                 QString::number(runtime.pairedSampleCount)));
    ui->lblStatLatency->setText(
        QStringLiteral("未配对丢弃/延迟: %1 / %2 ms")
            .arg(QString::number(runtime.droppedUnpairedSampleCount),
                 QString::number(runtime.averageProcessingLatencyMs, 'f', 2)));
    ui->lblStatWindow->setText(
        QStringLiteral("同步抖动: %1 μs")
            .arg(QString::number(runtime.averageSyncJitterUs, 'f', 1)));
    ui->lblStatExposure->setText(
        QStringLiteral("曝光: %1 μs | AE: %2")
            .arg(QString::number(m_configExposureUs, 'f', 0),
                 autoExposureUiStatusText()));
    ui->lblStatAutoExposure->setText(
        m_autoExposureConfig.enabled
            ? QStringLiteral("ROI峰值: %1/%2 | AE质量: %3/%4%")
                  .arg(QString::number(m_latestAutoExposurePeakDn[0], 'f', 0),
                       QString::number(m_latestAutoExposurePeakDn[1], 'f', 0),
                       QString::number(m_latestAutoExposureUsableRatio[0] * 100.0, 'f', 0),
                       QString::number(m_latestAutoExposureUsableRatio[1] * 100.0, 'f', 0))
            : QStringLiteral("ROI峰值: --/-- | AE质量: --/--%"));

    if (!runtime.hasValidAtmosphere) {
        ui->lblR0Value->setText(QStringLiteral("--"));
        ui->lblSeeingValue->setText(QStringLiteral("--"));
        ui->lblThetaValue->setText(QStringLiteral("--"));
        ui->lblTauValue->setText(QStringLiteral("--"));
        return;
    }

    ui->lblR0Value->setText(QString::number(runtime.latestAtmosphere.r0, 'f', 1));
    ui->lblSeeingValue->setText(QString::number(runtime.latestAtmosphere.seeing, 'f', 2));
    ui->lblThetaValue->setText(QString::number(runtime.latestAtmosphere.theta0, 'f', 2));
    ui->lblTauValue->setText(QString::number(runtime.latestAtmosphere.tau0, 'f', 2));
}

void DIMM::refreshPanelUi()
{
    const bool roiVisible = (static_cast<int>(m_detailViewMode) & static_cast<int>(DetailViewMode::RoiOnly)) != 0;
    const bool chartsVisible = (static_cast<int>(m_detailViewMode) & static_cast<int>(DetailViewMode::ChartsOnly)) != 0;

    ui->topArea->setVisible(true);
    ui->roiImagesArea->setVisible(roiVisible);
    ui->chartsArea->setVisible(chartsVisible);

    if (chartsVisible) {
        ui->mainSplitter->setSizes({380, 560});
    } else if (roiVisible) {
        ui->mainSplitter->setSizes({600, 340});
    } else {
        ui->mainSplitter->setSizes({760, 140});
    }

    ui->btnToggleROI->setStyleSheet(toggleButtonStyle(roiVisible));
    ui->btnToggleCharts->setStyleSheet(toggleButtonStyle(chartsVisible));
}

void DIMM::refreshActionStates()
{
    const bool activeCapture = hasActiveCapture();
    const bool busy = m_connectingCameras;
    ui->btnStop->setEnabled(m_captureState != CaptureState::Idle && !busy);
    ui->actionConnectAll->setEnabled(!activeCapture && !busy);
    ui->actionDisconnectAll->setEnabled(!activeCapture && !busy);
    ui->btnSettings->setEnabled(!busy);
    ui->actionCameraSettings->setEnabled(!busy);
    ui->actionViewSettings->setEnabled(!busy);
    ui->btnStart->setEnabled(!busy);
    if (m_actionStartSimulation) {
        m_actionStartSimulation->setEnabled(!busy);
    }
    if (m_actionAlignmentMode) {
        const bool alignmentActive = m_captureState == CaptureState::Alignment;
        m_actionAlignmentMode->setChecked(alignmentActive);
        m_actionAlignmentMode->setText(alignmentActive ? QStringLiteral("退出对准")
                                                       : QStringLiteral("对准模式"));
        m_actionAlignmentMode->setEnabled(!busy && m_captureState != CaptureState::Live &&
                                          m_captureState != CaptureState::Simulation);
    }
    if (m_actionConfirmCamera1Polaris) {
        m_actionConfirmCamera1Polaris->setEnabled(m_captureState == CaptureState::Alignment && !busy);
        if (m_liveRuntime.hasConfirmedPolarisPosition[0]) {
            const QPointF pos = m_liveRuntime.confirmedPolarisPosition[0];
            m_actionConfirmCamera1Polaris->setText(
                QStringLiteral("相机1北极星: 已确认 (%1, %2)")
                    .arg(pos.x(), 0, 'f', 1)
                    .arg(pos.y(), 0, 'f', 1));
        } else {
            m_actionConfirmCamera1Polaris->setText(QStringLiteral("相机1北极星: 未确认"));
        }
    }
    if (m_actionConfirmCamera2Polaris) {
        m_actionConfirmCamera2Polaris->setEnabled(m_captureState == CaptureState::Alignment && !busy);
        if (m_liveRuntime.hasConfirmedPolarisPosition[1]) {
            const QPointF pos = m_liveRuntime.confirmedPolarisPosition[1];
            m_actionConfirmCamera2Polaris->setText(
                QStringLiteral("相机2北极星: 已确认 (%1, %2)")
                    .arg(pos.x(), 0, 'f', 1)
                    .arg(pos.y(), 0, 'f', 1));
        } else {
            m_actionConfirmCamera2Polaris->setText(QStringLiteral("相机2北极星: 未确认"));
        }
    }
    if (m_actionRetryCamera1PolarisSolve) {
        m_actionRetryCamera1PolarisSolve->setEnabled(m_captureState == CaptureState::Alignment &&
                                                     !busy &&
                                                     !m_alignmentSession.camera(0).lastFrame.empty());
    }
    if (m_actionRetryCamera2PolarisSolve) {
        m_actionRetryCamera2PolarisSolve->setEnabled(m_captureState == CaptureState::Alignment &&
                                                     !busy &&
                                                     !m_alignmentSession.camera(1).lastFrame.empty());
    }
    if (m_actionRetryBothPolarisSolve) {
        m_actionRetryBothPolarisSolve->setEnabled(m_captureState == CaptureState::Alignment &&
                                                  !busy &&
                                                  (!m_alignmentSession.camera(0).lastFrame.empty() ||
                                                   !m_alignmentSession.camera(1).lastFrame.empty()));
    }
    const bool alignmentControlsVisible = m_captureState == CaptureState::Alignment;
    if (m_btnConfirmCamera1Polaris) {
        m_btnConfirmCamera1Polaris->setVisible(alignmentControlsVisible);
        m_btnConfirmCamera1Polaris->setEnabled(m_actionConfirmCamera1Polaris &&
                                               m_actionConfirmCamera1Polaris->isEnabled());
        m_btnConfirmCamera1Polaris->setText(m_liveRuntime.hasConfirmedPolarisPosition[0]
                                                ? QStringLiteral("重新确认")
                                                : QStringLiteral("人工确认"));
    }
    if (m_btnConfirmCamera2Polaris) {
        m_btnConfirmCamera2Polaris->setVisible(alignmentControlsVisible);
        m_btnConfirmCamera2Polaris->setEnabled(m_actionConfirmCamera2Polaris &&
                                               m_actionConfirmCamera2Polaris->isEnabled());
        m_btnConfirmCamera2Polaris->setText(m_liveRuntime.hasConfirmedPolarisPosition[1]
                                                ? QStringLiteral("重新确认")
                                                : QStringLiteral("人工确认"));
    }
    if (m_btnRetryCamera1PolarisSolve) {
        m_btnRetryCamera1PolarisSolve->setVisible(alignmentControlsVisible);
        m_btnRetryCamera1PolarisSolve->setEnabled(m_actionRetryCamera1PolarisSolve &&
                                                  m_actionRetryCamera1PolarisSolve->isEnabled());
    }
    if (m_btnRetryCamera2PolarisSolve) {
        m_btnRetryCamera2PolarisSolve->setVisible(alignmentControlsVisible);
        m_btnRetryCamera2PolarisSolve->setEnabled(m_actionRetryCamera2PolarisSolve &&
                                                  m_actionRetryCamera2PolarisSolve->isEnabled());
    }
    if (m_btnRetryBothPolarisSolve) {
        m_btnRetryBothPolarisSolve->setVisible(alignmentControlsVisible);
        m_btnRetryBothPolarisSolve->setEnabled(m_actionRetryBothPolarisSolve &&
                                               m_actionRetryBothPolarisSolve->isEnabled());
    }

    switch (m_captureState) {
    case CaptureState::Idle:
        ui->btnStart->setText(QStringLiteral("开始采集"));
        if (m_actionStartSimulation) {
            m_actionStartSimulation->setText(QStringLiteral("模拟采集"));
        }
        break;
    case CaptureState::Paused:
        ui->btnStart->setText(QStringLiteral("继续采集"));
        if (m_actionStartSimulation) {
            m_actionStartSimulation->setText(QStringLiteral("模拟采集"));
        }
        break;
    case CaptureState::Live:
        ui->btnStart->setText(QStringLiteral("暂停采集"));
        if (m_actionStartSimulation) {
            m_actionStartSimulation->setText(QStringLiteral("切换到模拟"));
        }
        break;
    case CaptureState::Simulation:
        ui->btnStart->setText(QStringLiteral("开始采集"));
        if (m_actionStartSimulation) {
            m_actionStartSimulation->setText(QStringLiteral("暂停模拟"));
        }
        break;
    case CaptureState::Alignment:
        ui->btnStart->setText(QStringLiteral("开始采集"));
        if (m_actionStartSimulation) {
            m_actionStartSimulation->setText(QStringLiteral("模拟采集"));
        }
        break;
    }
}

void DIMM::syncCameraSelectionUi()
{
    ui->lblFullframeLabel->setText(QStringLiteral("双相机全画幅预览"));
    ui->lblPreviewMode->setText(currentPreviewModeText());

    if (m_lblFullFrameCam1) {
        m_lblFullFrameCam1->setText(QStringLiteral("全画幅预览 - 相机1"));
    }
    if (m_lblFullFrameCam2) {
        m_lblFullFrameCam2->setText(QStringLiteral("全画幅预览 - 相机2"));
    }
}

QString DIMM::currentPreviewModeText() const
{
    if (m_captureState == CaptureState::Alignment) {
        return QStringLiteral("对准模式 (双相机 / 低频全画幅 / 不计算不保存)");
    }

    if (m_captureState == CaptureState::Simulation) {
        return QStringLiteral("模拟模式 (双相机 / 30s 预览 / 1Hz 计算)");
    }

    if (m_captureState != CaptureState::Live) {
        return QStringLiteral("实时模式 (双相机 / 30s 预览 / 实时采集)");
    }

    const auto& runtime = activeRuntime();
    if (runtime.frameCount <= 0) {
        if (m_liveStartupPhase == LiveStartupPhase::LocatePair) {
            return QStringLiteral("实时模式 (双相机全画幅定位 / 等待首帧)");
        }
        return m_configTriggerMode == 0
                   ? QStringLiteral("实时模式 (双相机 / 30s 预览 / 连续采集 / 等待首帧)")
                   : QStringLiteral("实时模式 (双相机 / 30s 预览 / 硬件触发 / 等待外部触发)");
    }

    if (m_configTriggerMode != 0) {
        const bool cam1Ready = runtime.frameCountPerCamera[0] > 0;
        const bool cam2Ready = runtime.frameCountPerCamera[1] > 0;
        if (cam1Ready && !cam2Ready) {
            return QStringLiteral("实时模式 (硬件触发 / 相机1已到帧 / 相机2等待触发)");
        }
        if (!cam1Ready && cam2Ready) {
            return QStringLiteral("实时模式 (硬件触发 / 相机2已到帧 / 相机1等待触发)");
        }
    }

    if (m_liveStartupPhase == LiveStartupPhase::LocatePair) {
        return QStringLiteral("实时模式 (双相机全画幅定位中 / 等待独立 ROI 确认)");
    }

    const bool previewRefreshed =
        runtime.lastLivePreviewUpdateMs[0] >= 0 || runtime.lastLivePreviewUpdateMs[1] >= 0;
    if (previewRefreshed) {
        return QStringLiteral("实时模式 (双相机 / 30s 预览 / 已收到图像 / 预览按30s刷新)");
    }
    return QStringLiteral("实时模式 (双相机 / 30s 预览 / 已收到图像)");
}

void DIMM::setStatusMessage(const QString& text, const QString& color)
{
    m_statusText = text;
    m_statusColor = color;
    refreshStatusUi();
}

void DIMM::setStatusMessage(const QString& text, UiStatusLevel level)
{
    setStatusMessage(text, uiStatusColor(level));
}

void DIMM::setAlignmentSolveLabel(int cameraIndex, const QString& text, UiStatusLevel level)
{
    if (!isValidCameraIndex(cameraIndex)) {
        return;
    }

    QLabel* label = cameraIndex == 0 ? m_lblAlignmentSolveCam1 : m_lblAlignmentSolveCam2;
    if (!label) {
        return;
    }

    label->setText(text);
    label->setStyleSheet(QStringLiteral("color: %1").arg(uiStatusColor(level)));
    label->setVisible(m_captureState == CaptureState::Alignment);
}

void DIMM::setDetailViewMode(DetailViewMode mode)
{
    m_detailViewMode = mode;
    refreshPanelUi();
}

void DIMM::resetMeasurementState()
{
    auto& runtime = activeRuntime();
    const QPointF preservedConfirmedPolarisPosition[2] = {
        runtime.confirmedPolarisPosition[0],
        runtime.confirmedPolarisPosition[1]
    };
    const bool preservedHasConfirmedPolarisPosition[2] = {
        runtime.hasConfirmedPolarisPosition[0],
        runtime.hasConfirmedPolarisPosition[1]
    };
    runtime = CaptureRuntimeContext();
    runtime.confirmedPolarisPosition[0] = preservedConfirmedPolarisPosition[0];
    runtime.confirmedPolarisPosition[1] = preservedConfirmedPolarisPosition[1];
    runtime.hasConfirmedPolarisPosition[0] = preservedHasConfirmedPolarisPosition[0];
    runtime.hasConfirmedPolarisPosition[1] = preservedHasConfirmedPolarisPosition[1];
    runtime.hasLastTargetPosition[0] = false;
    runtime.hasLastTargetPosition[1] = false;
    runtime.lastTargetPosition[0] = QPointF();
    runtime.lastTargetPosition[1] = QPointF();
    runtime.selectedInitialCandidateIndex[0] = -1;
    runtime.selectedInitialCandidateIndex[1] = -1;
    runtime.pendingInitialCandidateSelectionRequired[0] = false;
    runtime.pendingInitialCandidateSelectionRequired[1] = false;
    if (m_fullFrameCanvas1) {
        m_fullFrameCanvas1->clearStarCandidateOverlays();
    }
    if (m_fullFrameCanvas2) {
        m_fullFrameCanvas2->clearStarCandidateOverlays();
    }
    m_resultRowsSeen = 0;
    m_roiUpdateCount = 0;
    m_lastRoiUpdateMs = -1;
    m_lastRoiUpdateReason.clear();
    resetLiveFrameAcceptanceGates();
    resetAutoExposureState();
    if (m_r0Chart) {
        m_r0Chart->clear();
    }
    if (m_seeingChart) {
        m_seeingChart->clear();
    }
    if (m_imageProcessor) {
        m_imageProcessor->resetProcessingState();
        m_liveAcquisitionGeneration = m_imageProcessor->currentAcquisitionGeneration();
    }
    ui->lblCam1ROICoord->setText(QStringLiteral("(0.0, 0.0)"));
    ui->lblCam2ROICoord->setText(QStringLiteral("(0.0, 0.0)"));
    refreshMeasurementUi();
}

void DIMM::updateCaptureState(CaptureState state)
{
    m_captureState = state;
    const bool focuserMotionAllowed =
        m_captureState == CaptureState::Idle || m_captureState == CaptureState::Paused;
    const QString reason = focuserMotionAllowed
                               ? QString()
                               : QStringLiteral("实时采集、模拟或对准模式中禁止移动焦点。请先暂停或停止采集。");
    if (m_focuserManager) {
        m_focuserManager->setMotionAllowed(focuserMotionAllowed, reason);
    }
    if (m_focuserControlWidget) {
        m_focuserControlWidget->setMotionAllowed(focuserMotionAllowed, reason);
    }
    refreshUi();
}

void DIMM::updateCommState(bool connected)
{
    m_commConnected = connected;
    refreshStatusUi();
}

bool DIMM::isSettingsApplyAllowed() const
{
    return !m_connectingCameras;
}

bool DIMM::canStartLiveCapture(QString* reason) const
{
    if (m_connectingCameras) {
        if (reason) {
            *reason = QStringLiteral("相机正在连接中，请等待当前连接流程完成。");
        }
        return false;
    }
    if (m_commConnecting) {
        if (reason) {
            *reason = QStringLiteral("网络通信正在连接中，请稍后再开始采集。");
        }
        return false;
    }
    const int cameraCount = openCameraCount();
    if (cameraCount < 2) {
        if (reason) {
            *reason = cameraCount == 0
                          ? QStringLiteral("当前未连接相机。\n请先连接两台相机后再开始实时采集。")
                          : QStringLiteral("当前只连接了一台相机。\n请先确保两台相机都已连接后再开始实时采集。");
        }
        return false;
    }
    return true;
}

bool DIMM::canConnectOrDisconnectCameras(QString* reason) const
{
    if (hasActiveCapture()) {
        if (reason) {
            *reason = QStringLiteral("请先停止或暂停采集，再执行相机连接操作。");
        }
        return false;
    }
    if (m_connectingCameras) {
        if (reason) {
            *reason = QStringLiteral("相机连接流程仍在进行中，请稍候。");
        }
        return false;
    }
    return true;
}

DIMM::CaptureRuntimeContext& DIMM::activeRuntime()
{
    return isSimulationCaptureActive() ? m_simulationRuntime : m_liveRuntime;
}

const DIMM::CaptureRuntimeContext& DIMM::activeRuntime() const
{
    return isSimulationCaptureActive() ? m_simulationRuntime : m_liveRuntime;
}

DIMM::CaptureRuntimeContext& DIMM::runtimeForState(CaptureState state)
{
    return state == CaptureState::Simulation ? m_simulationRuntime : m_liveRuntime;
}

const DIMM::CaptureRuntimeContext& DIMM::runtimeForState(CaptureState state) const
{
    return state == CaptureState::Simulation ? m_simulationRuntime : m_liveRuntime;
}

bool DIMM::hasAnyOpenCamera() const
{
    return openCameraCount() > 0;
}

int DIMM::openCameraCount() const
{
    int count = 0;
    for (int i = 0; i < 2; ++i) {
        if (m_cameraManager && m_cameraManager->isOpen(i)) {
            ++count;
        }
    }
    return count;
}

bool DIMM::hasActiveCapture() const
{
    return m_captureState == CaptureState::Live ||
           m_captureState == CaptureState::Simulation ||
           m_captureState == CaptureState::Alignment;
}

bool DIMM::isLiveCaptureActive() const
{
    return m_captureState == CaptureState::Live;
}

bool DIMM::isSimulationCaptureActive() const
{
    return m_captureState == CaptureState::Simulation;
}

bool DIMM::canReportMeasurements() const
{
    return m_commConnected && m_reporting && isLiveCaptureActive() && activeRuntime().hasValidAtmosphere;
}

QString DIMM::captureModeName() const
{
    switch (m_captureState) {
    case CaptureState::Live:
        return QStringLiteral("live");
    case CaptureState::Simulation:
        return QStringLiteral("simulation");
    case CaptureState::Paused:
        return QStringLiteral("paused");
    case CaptureState::Alignment:
        return QStringLiteral("alignment");
    case CaptureState::Idle:
    default:
        return QStringLiteral("idle");
    }
}

QString DIMM::captureModeLabel() const
{
    switch (m_captureState) {
    case CaptureState::Live:
        return QStringLiteral("实时采集");
    case CaptureState::Simulation:
        return QStringLiteral("模拟采集");
    case CaptureState::Paused:
        return QStringLiteral("暂停");
    case CaptureState::Alignment:
        return QStringLiteral("对准模式");
    case CaptureState::Idle:
    default:
        return QStringLiteral("空闲");
    }
}

QString DIMM::resultSubdirectoryName() const
{
    switch (m_captureState) {
    case CaptureState::Live:
        return QStringLiteral("live");
    case CaptureState::Simulation:
        return QStringLiteral("simulation");
    case CaptureState::Paused:
        return QStringLiteral("paused");
    case CaptureState::Alignment:
        return QStringLiteral("alignment");
    case CaptureState::Idle:
    default:
        return QStringLiteral("idle");
    }
}

bool DIMM::hasValidCentroidsForRoiUpdate() const
{
    const auto& runtime = activeRuntime();
    return runtime.hasValidCentroid[0] && runtime.hasValidCentroid[1];
}

bool DIMM::isCentroidNearCurrentRoiEdge(int cameraIndex, double x, double y) const
{
    if (!m_imageProcessor || cameraIndex < 0 || cameraIndex >= 2) {
        return false;
    }

    const RoiRect roi = m_imageProcessor->getCurrentRoi(cameraIndex);
    const double localX = x - static_cast<double>(roi.x);
    const double localY = y - static_cast<double>(roi.y);
    return localX <= static_cast<double>(kRoiEdgeUpdateMarginPx) ||
           localY <= static_cast<double>(kRoiEdgeUpdateMarginPx) ||
           localX >= static_cast<double>(roi.w - 1 - kRoiEdgeUpdateMarginPx) ||
           localY >= static_cast<double>(roi.h - 1 - kRoiEdgeUpdateMarginPx);
}

bool DIMM::isCentroidTooFarFromCurrentRoiCenter(int cameraIndex) const
{
    if (!m_imageProcessor || cameraIndex < 0 || cameraIndex >= 2) {
        return false;
    }

    const auto& runtime = activeRuntime();
    if (!runtime.hasValidCentroid[cameraIndex]) {
        return false;
    }

    const RoiRect roi = m_imageProcessor->getCurrentRoi(cameraIndex);
    const double localX = runtime.centroidX[cameraIndex] - static_cast<double>(roi.x);
    const double localY = runtime.centroidY[cameraIndex] - static_cast<double>(roi.y);
    return localX <= m_roiRecenteringThresholdPx ||
           localY <= m_roiRecenteringThresholdPx ||
           localX >= static_cast<double>(roi.w - 1) - m_roiRecenteringThresholdPx ||
           localY >= static_cast<double>(roi.h - 1) - m_roiRecenteringThresholdPx;
}


bool DIMM::shouldUpdateRoiForRecentering()
{
    auto& runtime = activeRuntime();
    if (!hasValidCentroidsForRoiUpdate()) {
        runtime.roiRecenteringCandidateFrameCount = 0;
        return false;
    }

    const bool needsRecentering = isCentroidTooFarFromCurrentRoiCenter(0) ||
                                  isCentroidTooFarFromCurrentRoiCenter(1);
    if (!needsRecentering) {
        runtime.roiRecenteringCandidateFrameCount = 0;
        return false;
    }

    double maximumRoiRecenteringShift = 0.0;
    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        const RoiRect currentRoi = m_imageProcessor->getCurrentRoi(cameraIndex);
        const RoiRect targetRoi = buildCameraCentroidRoi(cameraIndex);
        maximumRoiRecenteringShift =
            std::max(maximumRoiRecenteringShift,
                     std::hypot(static_cast<double>(targetRoi.x - currentRoi.x),
                                static_cast<double>(targetRoi.y - currentRoi.y)));
    }
    if (maximumRoiRecenteringShift < m_roiRecenteringMinimumShiftPx) {
        runtime.roiRecenteringCandidateFrameCount = 0;
        return false;
    }

    ++runtime.roiRecenteringCandidateFrameCount;
    if (runtime.roiRecenteringCandidateFrameCount < m_roiRecenteringRequiredFrames) {
        return false;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_lastRoiUpdateMs >= 0 && (nowMs - m_lastRoiUpdateMs) < m_roiRecenteringCooldownMs) {
        return false;
    }

    return true;
}

void DIMM::requestLiveFullFrameRelocalization(const QString& reason)
{
    if (m_captureState != CaptureState::Live || !m_cameraManager) {
        return;
    }

    auto& runtime = activeRuntime();
    runtime.liveRelocalizationStartedMs = QDateTime::currentMSecsSinceEpoch();
    resetLiveFrameAcceptanceGates();
    QString switchReason;
    const bool fullFrameReady = applyLiveFullFrameForRelocalization(&switchReason);
    // Keep lastTargetPosition across relocalization; it is the identity hint used to
    // choose the nearest full-frame candidate instead of the brightest unrelated star.
    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        runtime.hasValidCentroid[cameraIndex] = false;
        runtime.lostCentroidFrameCount[cameraIndex] = 0;
        runtime.lostCentroidSinceMs[cameraIndex] = -1;
        runtime.initialRoiConfirmed[cameraIndex] = false;
        runtime.pendingInitialRoi[cameraIndex] = RoiRect();
        runtime.pendingInitialRoiReady[cameraIndex] = false;
        runtime.lastLivePreviewUpdateMs[cameraIndex] = -1;
        runtime.liveRelocalizationPreviewFrame[cameraIndex].release();
    }
    if (m_cam1RoiCanvas) {
        m_cam1RoiCanvas->clear();
    }
    if (m_cam2RoiCanvas) {
        m_cam2RoiCanvas->clear();
    }
    if (ui->lblCam1ROICoord) {
        ui->lblCam1ROICoord->setText(QStringLiteral("(0.0, 0.0)"));
    }
    if (ui->lblCam2ROICoord) {
        ui->lblCam2ROICoord->setText(QStringLiteral("(0.0, 0.0)"));
    }
    ui->lblROITimeCurrent->setText(QStringLiteral("全画幅重定位中"));
    ui->lblROITimeNext->setText(QStringLiteral("等待两路重新锁定 ROI"));

    m_liveHardwareRoiActive = false;
    m_liveStartupPhase = LiveStartupPhase::LocatePair;
    if (!fullFrameReady) {
        setStatusMessage(switchReason.isEmpty()
                             ? QStringLiteral("状态: 回全画幅重新定位失败")
                             : switchReason,
                         UiStatusLevel::Error);
    } else {
        setStatusMessage(reason.isEmpty()
                             ? QStringLiteral("状态: 已回到全画幅重新定位")
                             : reason,
                         UiStatusLevel::Warning);
    }
}

void DIMM::handleLiveRoiCentroidLoss(int cameraIndex)
{
    if (m_captureState != CaptureState::Live ||
        m_liveStartupPhase != LiveStartupPhase::Tracking ||
        !m_liveHardwareRoiActive ||
        cameraIndex < 0 ||
        cameraIndex >= 2) {
        return;
    }

    auto& runtime = activeRuntime();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (runtime.lostCentroidFrameCount[cameraIndex] == 0 ||
        runtime.lostCentroidSinceMs[cameraIndex] < 0) {
        runtime.lostCentroidSinceMs[cameraIndex] = nowMs;
    }
    ++runtime.lostCentroidFrameCount[cameraIndex];
    if ((nowMs - runtime.lostCentroidSinceMs[cameraIndex]) < kLostCentroidRelocalizeTimeoutMs) {
        return;
    }

    requestLiveFullFrameRelocalization(
        QStringLiteral("状态: 相机%1星点离开 ROI，已切回全画幅重新定位")
            .arg(cameraIndex + 1));
}

bool DIMM::isUsableCentroidSample(int cameraIndex,
                                  double x,
                                  double y,
                                  double peakValue,
                                  double totalFlux,
                                  double background,
                                  double threshold,
                                  quint64 signalPixelCount,
                                  bool requireCentered) const
{
    if (cameraIndex < 0 || cameraIndex >= 2) {
        return false;
    }

    const auto& runtime = activeRuntime();
    const QSize frameSize = runtime.frameSize[cameraIndex].isValid() ? runtime.frameSize[cameraIndex] : QSize(5120, 5120);
    if (frameSize.width() <= 0 || frameSize.height() <= 0) {
        return false;
    }

    if (!(peakValue > threshold && peakValue > background + 4.0)) {
        return false;
    }
    if (signalPixelCount < 2 || signalPixelCount > 900) {
        return false;
    }
    if (totalFlux <= 80.0) {
        return false;
    }
    if (x < 0.0 || y < 0.0 || x >= frameSize.width() || y >= frameSize.height()) {
        return false;
    }

    if (requireCentered) {
        const int margin = kFixedRoiSize / 2;
        if (x < margin || y < margin ||
            x > static_cast<double>(frameSize.width() - margin) ||
            y > static_cast<double>(frameSize.height() - margin)) {
            return false;
        }
    }

    return true;
}

RoiRect DIMM::sanitizeRoi(const RoiRect& roi, int cameraIndex) const
{
    const int safeIndex = qBound(0, cameraIndex, 1);
    const auto& runtime = activeRuntime();
    const QSize frameSize = runtime.frameSize[safeIndex].isValid() ? runtime.frameSize[safeIndex] : QSize(5120, 5120);

    RoiRect clean = roi;
    clean.w = kFixedRoiSize;
    clean.h = kFixedRoiSize;

    const int frameWidth = qMax(clean.w, frameSize.width());
    const int frameHeight = qMax(clean.h, frameSize.height());
    const int maxX = qMax(0, frameWidth - clean.w);
    const int maxY = qMax(0, frameHeight - clean.h);

    clean.x = qBound(0, clean.x, maxX);
    clean.y = qBound(0, clean.y, maxY);
    return clean;
}

RoiRect DIMM::buildCameraCentroidRoi(int cameraIndex) const
{
    const int safeIndex = qBound(0, cameraIndex, 1);
    const auto& runtime = activeRuntime();
    RoiRect roi;
    roi.x = qRound(runtime.centroidX[safeIndex]) - kFixedRoiSize / 2;
    roi.y = qRound(runtime.centroidY[safeIndex]) - kFixedRoiSize / 2;
    roi.w = kFixedRoiSize;
    roi.h = kFixedRoiSize;
    return sanitizeRoi(roi, safeIndex);
}

void DIMM::applyRoiSummary(const RoiRect& roi, const QString& cameraLabel)
{
    ui->lblROIXValue->setText(QString::number(roi.x));
    ui->lblROIYValue->setText(QString::number(roi.y));
    ui->lblROIWValue->setText(QString::number(roi.w));
    ui->lblROIHValue->setText(QString::number(roi.h));
    m_lblStatusROI->setText(QStringLiteral("当前ROI(%1): (%2, %3) %4x%5")
                                .arg(cameraLabel)
                                .arg(roi.x)
                                .arg(roi.y)
                                .arg(roi.w)
                                .arg(roi.h));
}

void DIMM::recordLiveRoiUpdate(const RoiRect rois[2], const QString& reason)
{
    Q_UNUSED(rois);
    if (m_captureState != CaptureState::Live) {
        return;
    }

    ++m_roiUpdateCount;
    m_lastRoiUpdateMs = QDateTime::currentMSecsSinceEpoch();
    m_lastRoiUpdateReason = reason;
}

QString DIMM::roiRuleDescription() const
{
    return QStringLiteral("ROI 固定为 64x64；启动后两台相机分别全画幅定位，并切换到各自独立 ROI 跟踪。");
}

bool DIMM::validateAndCacheLiveRoiCapabilities(QString* reason)
{
    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        if (!m_cameraManager->prepareFixedRoi(cameraIndex,
                                              kFixedRoiSize,
                                              kFixedRoiSize,
                                              &m_liveRoiCapabilities[cameraIndex])) {
            if (reason) {
                *reason = QStringLiteral("相机%1固定 ROI 能力探测失败。").arg(cameraIndex + 1);
            }
            m_liveRoiCapabilitiesValid = false;
            return false;
        }
    }

    m_liveRoiCapabilitiesValid = true;
    return true;
}

bool DIMM::readLivePairRoiPosition(RoiPosition positions[2], QString* reason)
{
    if (!positions) {
        return false;
    }

    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        if (!m_cameraManager->readRoiPosition(cameraIndex, &positions[cameraIndex])) {
            if (reason) {
                *reason = QStringLiteral("读取相机%1当前 ROI 位置失败。").arg(cameraIndex + 1);
            }
            return false;
        }
    }
    return true;
}

RoiRect DIMM::buildLiveCameraRoi(int cameraIndex, const RoiRect& desiredRoi) const
{
    const int safeIndex = qBound(0, cameraIndex, 1);
    if (!m_liveRoiCapabilitiesValid) {
        return sanitizeRoi(desiredRoi, safeIndex);
    }

    const RoiCapability& capability = m_liveRoiCapabilities[safeIndex];
    const double sensorCenterX = static_cast<double>(desiredRoi.x) + static_cast<double>(desiredRoi.w) / 2.0;
    const double sensorCenterY = static_cast<double>(desiredRoi.y) + static_cast<double>(desiredRoi.h) / 2.0;
    const qint64 requestedX = static_cast<qint64>(
        std::llround(sensorCenterX - static_cast<double>(capability.width) / 2.0));
    const qint64 requestedY = static_cast<qint64>(
        std::llround(sensorCenterY - static_cast<double>(capability.height) / 2.0));

    RoiRect liveRoi;
    liveRoi.x = static_cast<int>(alignRoiValue(requestedX, capability.offsetX));
    liveRoi.y = static_cast<int>(alignRoiValue(requestedY, capability.offsetY));
    liveRoi.w = static_cast<int>(capability.width);
    liveRoi.h = static_cast<int>(capability.height);
    return liveRoi;
}

bool DIMM::configureLiveCameras(QString* reason)
{
    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        if (!m_cameraManager->isOpen(cameraIndex)) {
            if (reason) {
                *reason = QStringLiteral("相机%1未连接，无法开始实时采集。").arg(cameraIndex + 1);
            }
            return false;
        }

        if (!m_cameraManager->prepareFullFrame(cameraIndex)) {
            if (reason) {
                *reason = QStringLiteral("相机%1切换到全画幅失败。").arg(cameraIndex + 1);
            }
            return false;
        }

        if (!m_cameraManager->setExposure(cameraIndex, m_configExposureUs) ||
            !m_cameraManager->setGain(cameraIndex, m_configGainDb)) {
            if (reason) {
                *reason = QStringLiteral("相机%1曝光或增益设置失败。").arg(cameraIndex + 1);
            }
            return false;
        }
    }

    if (!validateAndCacheLiveRoiCapabilities(reason)) {
        return false;
    }

    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        if (!m_cameraManager->prepareFullFrame(cameraIndex)) {
            if (reason) {
                *reason = QStringLiteral("相机%1校验独立 ROI 后恢复全画幅失败。").arg(cameraIndex + 1);
            }
            return false;
        }
    }

    // Configure the trigger path last. In hardware-trigger mode we should avoid
    // touching ROI/full-frame geometry after the camera has been armed, otherwise
    // one camera can end up missing the trigger-wait state while the other keeps it.
    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        const bool triggerConfigured =
            m_configTriggerMode == 0 ? m_cameraManager->setTriggerMode(cameraIndex, TriggerMode::Continuous)
                                     : m_cameraManager->configureExternalTrigger(cameraIndex);
        if (!triggerConfigured) {
            if (reason) {
                *reason = QStringLiteral("相机%1触发模式配置失败。").arg(cameraIndex + 1);
            }
            return false;
        }
    }

    if (!applyContinuousCameraFrameRate(reason)) {
        return false;
    }

    return true;
}

bool DIMM::applyContinuousCameraFrameRate(QString* reason)
{
    if (!m_cameraManager || m_configTriggerMode != 0) {
        return true;
    }

    const bool restartLiveContinuousCapture = m_captureState == CaptureState::Live;
    bool liveCaptureStopped = false;
    if (restartLiveContinuousCapture) {
        if (!m_cameraManager->stopAll()) {
            if (reason) {
                *reason = QStringLiteral("暂停连续采集以设置帧率失败。");
            }
            return false;
        }
        liveCaptureStopped = true;
        resetLiveFrameAcceptanceGates();
    }

    const auto restartLiveCapture = [&]() {
        if (!liveCaptureStopped) {
            return true;
        }
        liveCaptureStopped = false;
        return m_cameraManager->startAll();
    };

    const auto failWithRestart = [&](const QString& message) {
        QString restartReason;
        if (!restartLiveCapture()) {
            restartReason = QStringLiteral("；恢复连续采集失败");
        }
        if (reason) {
            *reason = message + restartReason;
        }
        return false;
    };

    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        if (!m_cameraManager->isOpen(cameraIndex)) {
            continue;
        }
        if (!m_cameraManager->setFrameRate(cameraIndex, m_configContinuousFrameRateHz)) {
            return failWithRestart(QStringLiteral("相机%1连续采集帧率设置失败。").arg(cameraIndex + 1));
        }

        const double actualFrameRate = m_cameraManager->getFrameRate(cameraIndex);
        m_lastContinuousFrameRateReadback[cameraIndex] = actualFrameRate;
        const double tolerance = std::max(0.05, m_configContinuousFrameRateHz * 0.05);
        if (actualFrameRate <= 0.0 ||
            std::abs(actualFrameRate - m_configContinuousFrameRateHz) > tolerance) {
            return failWithRestart(QStringLiteral("相机%1连续采集帧率读回异常: 目标 %2 fps，实际 %3 fps。")
                                       .arg(cameraIndex + 1)
                                       .arg(m_configContinuousFrameRateHz, 0, 'f', 2)
                                       .arg(actualFrameRate, 0, 'f', 2));
        }
    }

    if (!restartLiveCapture()) {
        if (reason) {
            *reason = QStringLiteral("设置连续采集帧率后恢复采集失败。");
        }
        return false;
    }
    if (restartLiveContinuousCapture) {
        advanceLiveAcquisitionGeneration();
    }

    return true;
}

void DIMM::advanceLiveAcquisitionGeneration()
{
    ++m_liveAcquisitionGeneration;
    if (m_imageProcessor) {
        m_imageProcessor->advanceAcquisitionGeneration();
    }
    resetLiveFrameAcceptanceGates();
}

void DIMM::resetLiveFrameAcceptanceGates()
{
    m_liveFrameAcceptAfterMs = QDateTime::currentMSecsSinceEpoch();
    m_lastAcceptedLiveFrameId[0] = 0;
    m_lastAcceptedLiveFrameId[1] = 0;
    m_lastAcceptedContinuousFrameMs[0] = -1;
    m_lastAcceptedContinuousFrameMs[1] = -1;
}

bool DIMM::startDualCameraLocalization(QString* reason)
{
    if (!m_cameraManager) {
        if (reason) {
            *reason = QStringLiteral("相机管理器未初始化。");
        }
        return false;
    }

    if (!m_cameraManager->startAll()) {
        if (reason) {
            *reason = QStringLiteral("双相机全画幅定位启动失败。");
        }
        return false;
    }

    m_liveStartupPhase = LiveStartupPhase::LocatePair;
    return true;
}

bool DIMM::applyLiveHardwareRois(const RoiRect rois[2], QString* reason, RoiRect appliedRois[2])
{
    if (!m_liveRoiCapabilitiesValid) {
        if (reason) {
            *reason = QStringLiteral("独立 ROI 能力尚未准备完成。");
        }
        return false;
    }
    if (!rois) {
        if (reason) {
            *reason = QStringLiteral("独立 ROI 参数无效。");
        }
        return false;
    }

    RoiPosition currentPositions[2];
    if (!readLivePairRoiPosition(currentPositions, reason)) {
        return false;
    }

    RoiRect liveRois[2] = {
        buildLiveCameraRoi(0, rois[0]),
        buildLiveCameraRoi(1, rois[1]),
    };
    RoiPosition targetPositions[2] = {
        RoiPosition{liveRois[0].x, liveRois[0].y},
        RoiPosition{liveRois[1].x, liveRois[1].y},
    };

    if (currentPositions[0].x == targetPositions[0].x &&
        currentPositions[0].y == targetPositions[0].y &&
        currentPositions[1].x == targetPositions[1].x &&
        currentPositions[1].y == targetPositions[1].y) {
        if (appliedRois) {
            appliedRois[0] = liveRois[0];
            appliedRois[1] = liveRois[1];
        }
        const bool rateReady = applyContinuousCameraFrameRate(reason);
        if (rateReady) {
            advanceLiveAcquisitionGeneration();
        }
        return rateReady;
    }

    const bool hardwareTriggerMode = m_configTriggerMode != 0;
    bool triggerGated = false;
    if (hardwareTriggerMode) {
        for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
            if (!m_cameraManager->prepareTriggerInputLine(cameraIndex, QString::fromLatin1(kRoiUpdateGateLine))) {
                if (reason) {
                    *reason = QStringLiteral("准备相机%1 ROI 更新门控触发线失败。").arg(cameraIndex + 1);
                }
                return false;
            }
        }
        if (!m_cameraManager->setPairTriggerSource(QString::fromLatin1(kRoiUpdateGateLine))) {
            if (reason) {
                *reason = QStringLiteral("切换到 ROI 更新门控触发线失败。");
            }
            return false;
        }
        triggerGated = true;
    }

    RoiUpdatePauseState pauseState[2];
    if (!m_cameraManager->pausePairForRoiUpdate(pauseState)) {
        if (reason) {
            *reason = QStringLiteral("暂停采集以更新硬件 ROI 失败。");
        }
        if (triggerGated) {
            m_cameraManager->setPairTriggerSource(QString::fromLatin1(kHardwareTriggerLine));
        }
        return false;
    }

    bool success = true;
    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        success = m_cameraManager->prepareFixedRoi(cameraIndex, liveRois[cameraIndex].w, liveRois[cameraIndex].h) &&
                  m_cameraManager->moveRoi(cameraIndex, targetPositions[cameraIndex]);
        if (!success) {
            if (reason) {
                *reason = QStringLiteral("相机%1硬件 ROI 更新失败。").arg(cameraIndex + 1);
            }
            break;
        }
    }

    const bool resumed = m_cameraManager->resumePairAfterRoiUpdate(pauseState);
    if (!resumed && reason && success) {
        *reason = QStringLiteral("硬件 ROI 更新后恢复采集失败。");
    }

    if (resumed) {
        m_cameraManager->flushPairQueues();
    }

    if (triggerGated &&
        !m_cameraManager->setPairTriggerSource(QString::fromLatin1(kHardwareTriggerLine)) &&
        reason && success && resumed) {
        *reason = QStringLiteral("硬件 ROI 更新后恢复 Line0 触发源失败。");
        success = false;
    }

    if (!success || !resumed) {
        if (triggerGated) {
            m_cameraManager->setPairTriggerSource(QString::fromLatin1(kHardwareTriggerLine));
        }
        return false;
    }

    RoiPosition verifiedPositions[2];
    if (!readLivePairRoiPosition(verifiedPositions, reason)) {
        return false;
    }
    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        if (verifiedPositions[cameraIndex].x != targetPositions[cameraIndex].x ||
            verifiedPositions[cameraIndex].y != targetPositions[cameraIndex].y) {
            if (reason) {
                *reason = QStringLiteral("相机%1硬件 ROI 更新后偏移校验失败。").arg(cameraIndex + 1);
            }
            return false;
        }
    }

    if (appliedRois) {
        appliedRois[0] = liveRois[0];
        appliedRois[1] = liveRois[1];
    }
    const bool rateReady = applyContinuousCameraFrameRate(reason);
    if (rateReady) {
        advanceLiveAcquisitionGeneration();
    }
    return rateReady;
}

bool DIMM::applyLiveFullFrameForRelocalization(QString* reason)
{
    if (!m_cameraManager) {
        if (reason) {
            *reason = QStringLiteral("相机管理器未初始化。");
        }
        return false;
    }

    const bool hardwareTriggerMode = m_configTriggerMode != 0;
    bool triggerGated = false;
    if (hardwareTriggerMode) {
        for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
            if (!m_cameraManager->prepareTriggerInputLine(cameraIndex, QString::fromLatin1(kRoiUpdateGateLine))) {
                if (reason) {
                    *reason = QStringLiteral("准备相机%1全画幅重定位门控触发线失败。").arg(cameraIndex + 1);
                }
                return false;
            }
        }
        if (!m_cameraManager->setPairTriggerSource(QString::fromLatin1(kRoiUpdateGateLine))) {
            if (reason) {
                *reason = QStringLiteral("切换到全画幅重定位门控触发线失败。");
            }
            return false;
        }
        triggerGated = true;
    }

    RoiUpdatePauseState pauseState[2];
    if (!m_cameraManager->pausePairForRoiUpdate(pauseState)) {
        if (reason) {
            *reason = QStringLiteral("暂停采集以切换全画幅失败。");
        }
        if (triggerGated) {
            m_cameraManager->setPairTriggerSource(QString::fromLatin1(kHardwareTriggerLine));
        }
        return false;
    }

    bool success = true;
    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        if (!m_cameraManager->prepareFullFrame(cameraIndex)) {
            success = false;
            if (reason) {
                *reason = QStringLiteral("相机%1切换全画幅失败。").arg(cameraIndex + 1);
            }
            break;
        }
    }

    const bool resumed = m_cameraManager->resumePairAfterRoiUpdate(pauseState);
    if (!resumed && reason && success) {
        *reason = QStringLiteral("切换全画幅后恢复采集失败。");
    }
    if (resumed) {
        m_cameraManager->flushPairQueues();
    }

    if (triggerGated &&
        !m_cameraManager->setPairTriggerSource(QString::fromLatin1(kHardwareTriggerLine)) &&
        reason && success && resumed) {
        *reason = QStringLiteral("全画幅重定位后恢复 Line0 触发源失败。");
        success = false;
    }

    if (!success || !resumed) {
        if (triggerGated) {
            m_cameraManager->setPairTriggerSource(QString::fromLatin1(kHardwareTriggerLine));
        }
        return false;
    }

    advanceLiveAcquisitionGeneration();
    if (m_configTriggerMode != 0) {
        return startFullFrameLocalizationPulse(reason);
    }
    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        if (m_cameraManager->isOpen(cameraIndex) &&
            !m_cameraManager->setFrameRate(cameraIndex, kFullFrameLocalizationPulseHz)) {
            if (reason) {
                *reason = QStringLiteral("相机%1全画幅重定位帧率设置失败。").arg(cameraIndex + 1);
            }
            return false;
        }
    }
    return true;
}
bool DIMM::selectLiveRelocalizationCentroid(int cameraIndex,
                                            const cv::Mat& fullFrame,
                                            QPointF* centroid,
                                            double* peakValue)
{
    if (cameraIndex < 0 || cameraIndex >= 2 || fullFrame.empty() || !centroid) {
        return false;
    }

    QVector<InitialStarCandidate> candidates = detectInitialStarCandidates(fullFrame, peakValue);
    if (candidates.isEmpty()) {
        return false;
    }

    const auto& runtime = activeRuntime();
    if (runtime.hasLastTargetPosition[cameraIndex]) {
        const InitialStarSelection selection =
            PolarisDetectionPipeline::selectInitialStarCandidate(candidates,
                                                                 true,
                                                                 runtime.lastTargetPosition[cameraIndex],
                                                                 0);
        if (!selection.selected) {
            return false;
        }
        *centroid = selection.candidate.center;
        if (peakValue) {
            *peakValue = selection.candidate.peak;
        }
        return true;
    }

    InitialStarCandidate selectedCandidate;
    const InitialStarCandidate strongestCandidate = candidates.first();
    if (!PolarisDetectionPipeline::chooseAutomaticInitialStarCandidate(candidates,
                                                                       strongestCandidate,
                                                                       &selectedCandidate,
                                                                       nullptr)) {
        return false;
    }
    *centroid = selectedCandidate.center;
    if (peakValue) {
        *peakValue = selectedCandidate.peak;
    }
    return true;
}

bool DIMM::maybeSeedRoiFromFrame(int cameraIndex, const cv::Mat& frame)
{
    if (!m_imageProcessor || frame.empty() || cameraIndex < 0 || cameraIndex >= 2) {
        return false;
    }

    const bool liveLocatePhase =
        m_captureState == CaptureState::Live && m_liveStartupPhase != LiveStartupPhase::Tracking;
    if (m_captureState == CaptureState::Live) {
        const bool frameLooksLikeHardwareRoi =
            frame.cols <= kFixedRoiSize && frame.rows <= kFixedRoiSize;
        // Live ROI seeding must use real full-frame images. Stale 64x64 frames can still be
        // delivered while the camera stream is switching back from hardware ROI.
        if (!liveLocatePhase || frameLooksLikeHardwareRoi) {
            return false;
        }
    }

    auto& runtime = activeRuntime();
    if (!liveLocatePhase && runtime.hasValidCentroid[cameraIndex]) {
        return false;
    }
    if (runtime.pendingInitialRoiReady[cameraIndex]) {
        return false;
    }

    cv::Mat grayscale;
    if (frame.channels() == 1) {
        grayscale = frame;
    } else {
        cv::cvtColor(frame, grayscale, cv::COLOR_BGR2GRAY);
    }

    const int camIdx = cameraIndex;
    FullFrameCanvas* targetCanvas = cameraIndex == 0 ? m_fullFrameCanvas1 : m_fullFrameCanvas2;
    QPointF centroid;
    double peakValue = 0.0;
    if (liveLocatePhase) {
        if (!selectLiveRelocalizationCentroid(cameraIndex, grayscale, &centroid, &peakValue)) {
            if (targetCanvas) {
                targetCanvas->clearStarCandidateOverlays();
            }
            setStatusMessage(QStringLiteral("状态: 相机%1 全画幅 SDK 连通域找星未找到有效星点，等待下一帧")
                                 .arg(cameraIndex + 1),
                             UiStatusLevel::Warning);
            return false;
        }
        runtime.liveRelocalizationPreviewFrame[cameraIndex] = frame.clone();
        runtime.pendingInitialCandidateSelectionRequired[cameraIndex] = false;
        setStatusMessage(QStringLiteral("状态: 相机%1全画幅重定位找到星点 (%2, %3)，峰值 %4，第 %5 帧")
                             .arg(cameraIndex + 1)
                             .arg(centroid.x(), 0, 'f', 1)
                             .arg(centroid.y(), 0, 'f', 1)
                             .arg(peakValue, 0, 'f', 1)
                             .arg(runtime.frameCountPerCamera[cameraIndex]),
                         UiStatusLevel::Info);
    } else {
        QVector<InitialStarCandidate> candidates = detectInitialStarCandidates(grayscale, &peakValue);
        const bool hasTrackedTargetPreference = runtime.hasLastTargetPosition[camIdx];
        const bool hasAlignmentPolarisPreference = runtime.hasConfirmedPolarisPosition[camIdx];
        const bool hasPreferredInitialTarget = hasTrackedTargetPreference ||
                                               hasAlignmentPolarisPreference;
        const bool usePreferenceGate = !liveLocatePhase && hasPreferredInitialTarget;
        const QPointF preferredInitialTarget = hasTrackedTargetPreference
                                                   ? runtime.lastTargetPosition[camIdx]
                                                   : runtime.confirmedPolarisPosition[camIdx];
        if (candidates.isEmpty()) {
            if (runtime.pendingInitialCandidateSelectionRequired[cameraIndex]) {
                if (targetCanvas) {
                    targetCanvas->clearStarCandidateOverlays();
                }
                setStatusMessage(QStringLiteral("状态: 相机%1 正在等待有效的全画幅 SDK 连通域候选列表")
                                     .arg(cameraIndex + 1),
                                 UiStatusLevel::Warning);
                return false;
            }
            if (targetCanvas) {
                targetCanvas->clearStarCandidateOverlays();
            }
            runtime.pendingInitialCandidateSelectionRequired[cameraIndex] = false;
            setStatusMessage(QStringLiteral("状态: 相机%1 全画幅 SDK 连通域找星未找到有效星点，未初始化 ROI")
                                 .arg(cameraIndex + 1),
                             UiStatusLevel::Warning);
            return false;
        } else {
            if (targetCanvas) {
                targetCanvas->setStarCandidateOverlays(
                    PolarisDetectionPipeline::buildCandidateOverlays(
                        candidates,
                        runtime.selectedInitialCandidateIndex[cameraIndex]));
            }

            InitialStarSelection selection =
                PolarisDetectionPipeline::selectInitialStarCandidate(
                    candidates,
                    usePreferenceGate,
                    preferredInitialTarget,
                    runtime.selectedInitialCandidateIndex[cameraIndex]);
            runtime.pendingInitialCandidateSelectionRequired[cameraIndex] =
                selection.requiresUserSelection;
            if (!selection.selected) {
                if (usePreferenceGate) {
                    setStatusMessage(QStringLiteral("状态: 相机%1候选星点距离上次位置过远，本帧不更新初始 ROI")
                                         .arg(cameraIndex + 1),
                                     UiStatusLevel::Warning);
                    return false;
                }

                const InitialStarCandidate strongestCandidate = candidates.first();
                QString automaticRejectReason;
                if (!PolarisDetectionPipeline::chooseAutomaticInitialStarCandidate(
                        candidates,
                        strongestCandidate,
                        &selection.candidate,
                        &automaticRejectReason)) {
                    setStatusMessage(QStringLiteral("状态: 相机%1%2")
                                         .arg(cameraIndex + 1)
                                         .arg(automaticRejectReason),
                                     UiStatusLevel::Warning);
                    return false;
                }

                selection.selected = true;
                selection.requiresUserSelection = false;
                centroid = selection.candidate.center;
                setStatusMessage(QStringLiteral("状态: 相机%1未对准确认，自动选择信号最强候选星作为初始 ROI")
                                     .arg(cameraIndex + 1),
                                 UiStatusLevel::Warning);
            }
            centroid = selection.candidate.center;
            if (hasAlignmentPolarisPreference) {
                runtime.confirmedPolarisPosition[cameraIndex] = centroid;
                runtime.hasConfirmedPolarisPosition[cameraIndex] = true;
            }
            if (targetCanvas) {
                targetCanvas->setStarCandidateOverlays(
                    PolarisDetectionPipeline::buildCandidateOverlays(
                        candidates, selection.candidate.index));
            }
            if (runtime.selectedInitialCandidateIndex[cameraIndex] > 0) {
                runtime.selectedInitialCandidateIndex[cameraIndex] = -1;
            }
            runtime.lastInitialCandidatePromptMs[cameraIndex] = -1;
        }
    }

    const RoiRect seeded = sanitizeRoi(
        RoiRect{qRound(centroid.x()) - kFixedRoiSize / 2,
                qRound(centroid.y()) - kFixedRoiSize / 2,
                kFixedRoiSize,
                kFixedRoiSize},
        cameraIndex);
    m_imageProcessor->setCurrentRoi(cameraIndex, seeded);
    runtime.pendingInitialRoi[cameraIndex] = seeded;
    runtime.pendingInitialRoiReady[cameraIndex] = true;
    applyRoiSummary(seeded, QStringLiteral("相机%1").arg(cameraIndex + 1));
    if (m_captureState == CaptureState::Live &&
        (!runtime.pendingInitialRoiReady[0] || !runtime.pendingInitialRoiReady[1])) {
        const int waitingCamera = runtime.pendingInitialRoiReady[0] ? 2 : 1;
        setStatusMessage(QStringLiteral("状态: 相机%1全画幅已找到星点，等待相机%2全画幅定位")
                             .arg(cameraIndex + 1)
                             .arg(waitingCamera),
                         UiStatusLevel::Info);
    }
    return commitPairedInitialRoisIfReady();
}

void DIMM::handleLiveRelocalizationWatchdog(qint64 nowMs)
{
    if (m_captureState != CaptureState::Live) {
        return;
    }

    auto& runtime = activeRuntime();
    const bool relocalizationActive =
        runtime.liveRelocalizationStartedMs >= 0 ||
        m_liveStartupPhase == LiveStartupPhase::LocatePair ||
        !m_liveHardwareRoiActive ||
        !runtime.initialRoiConfirmed[0] ||
        !runtime.initialRoiConfirmed[1];
    if (!relocalizationActive) {
        return;
    }

    if (runtime.liveRelocalizationStartedMs < 0) {
        runtime.liveRelocalizationStartedMs = nowMs;
        return;
    }
    if ((nowMs - runtime.liveRelocalizationStartedMs) < kLiveRelocalizationMaxDurationMs) {
        return;
    }

    clearPendingLiveRelocalizationRois();
    runtime.liveRelocalizationStartedMs = nowMs;
    m_liveStartupPhase = LiveStartupPhase::LocatePair;
    m_liveHardwareRoiActive = false;
    resetLiveFrameAcceptanceGates();
    QString switchReason;
    const bool fullFrameReady = applyLiveFullFrameForRelocalization(&switchReason);
    if (ui->lblROITimeCurrent) {
        ui->lblROITimeCurrent->setText(fullFrameReady
                                           ? QStringLiteral("全画幅重定位重试中")
                                           : QStringLiteral("全画幅重定位重试失败"));
    }
    if (ui->lblROITimeNext) {
        ui->lblROITimeNext->setText(QStringLiteral("已清空本轮候选，等待下一对全画幅"));
    }
    setStatusMessage(fullFrameReady
                         ? QStringLiteral("状态: 全画幅重定位超时，已重新切换全画幅并重新开始检测")
                         : (switchReason.isEmpty()
                                ? QStringLiteral("状态: 全画幅重定位超时，重新切换全画幅失败")
                                : switchReason),
                     fullFrameReady ? UiStatusLevel::Warning : UiStatusLevel::Error);
}

void DIMM::updateFullFrameRoiOverlay(int cameraIndex)
{
    if (!m_imageProcessor || cameraIndex < 0 || cameraIndex >= 2) {
        return;
    }

    auto& runtime = activeRuntime();
    const bool showConfirmedRoiOverlay =
        m_captureState != CaptureState::Live || runtime.initialRoiConfirmed[cameraIndex];

    QVector<RoiRect> rois;
    if (showConfirmedRoiOverlay) {
        rois.append(m_imageProcessor->getCurrentRoi(cameraIndex));
    }

    FullFrameCanvas* targetCanvas = cameraIndex == 0 ? m_fullFrameCanvas1 : m_fullFrameCanvas2;
    if (!targetCanvas) {
        return;
    }

    targetCanvas->setRoiList(rois);
    targetCanvas->setCurrentRoi(rois.isEmpty() ? -1 : 0);
}

void DIMM::showDeferredLiveRelocalizationPreview()
{
    auto& runtime = activeRuntime();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        FullFrameCanvas* targetCanvas = cameraIndex == 0 ? m_fullFrameCanvas1 : m_fullFrameCanvas2;
        if (targetCanvas && !runtime.liveRelocalizationPreviewFrame[cameraIndex].empty()) {
            targetCanvas->setImage(runtime.liveRelocalizationPreviewFrame[cameraIndex]);
        }
        runtime.liveRelocalizationPreviewFrame[cameraIndex].release();
        runtime.lastLivePreviewUpdateMs[cameraIndex] = nowMs;
        updateFullFrameRoiOverlay(cameraIndex);
    }
}

void DIMM::clearPendingLiveRelocalizationRois()
{
    auto& runtime = activeRuntime();
    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        runtime.pendingInitialRoi[cameraIndex] = RoiRect();
        runtime.pendingInitialRoiReady[cameraIndex] = false;
        runtime.liveRelocalizationPreviewFrame[cameraIndex].release();
    }
}

bool DIMM::commitPairedInitialRoisIfReady()
{
    if (!m_imageProcessor) {
        return false;
    }

    auto& runtime = activeRuntime();
    if (!runtime.pendingInitialRoiReady[0] || !runtime.pendingInitialRoiReady[1]) {
        return false;
    }

    const RoiRect pairedRois[2] = {
        runtime.pendingInitialRoi[0],
        runtime.pendingInitialRoi[1],
    };
    RoiRect actualRois[2] = {
        pairedRois[0],
        pairedRois[1],
    };

    QString reason;
    if (m_captureState == CaptureState::Live && !applyLiveHardwareRois(pairedRois, &reason, actualRois)) {
        m_liveHardwareRoiActive = false;
        clearPendingLiveRelocalizationRois();
        setStatusMessage(reason.isEmpty()
                             ? QStringLiteral("状态: 双相机初始 ROI 写入失败")
                             : reason,
                         UiStatusLevel::Warning);
        return false;
    }
    if (m_captureState == CaptureState::Live && !switchToRoiTrackingPulse(&reason)) {
        m_liveHardwareRoiActive = false;
        clearPendingLiveRelocalizationRois();
        setStatusMessage(reason.isEmpty()
                             ? QStringLiteral("状态: ROI 高频触发切换失败")
                             : reason,
                         UiStatusLevel::Warning);
        return false;
    }

    const QString roiUpdateReason = runtime.liveRelocalizationStartedMs >= 0
                                        ? QStringLiteral("full_frame_relocalization")
                                        : QStringLiteral("initial_lock");
    m_imageProcessor->setPairRois(actualRois);
    recordLiveRoiUpdate(actualRois, roiUpdateReason);
    runtime.initialRoiConfirmed[0] = true;
    runtime.initialRoiConfirmed[1] = true;
    runtime.pendingInitialRoiReady[0] = false;
    runtime.pendingInitialRoiReady[1] = false;
    runtime.liveRelocalizationStartedMs = -1;
    if (m_fullFrameCanvas1) {
        m_fullFrameCanvas1->clearStarCandidateOverlays();
    }
    if (m_fullFrameCanvas2) {
        m_fullFrameCanvas2->clearStarCandidateOverlays();
    }
    runtime.pendingInitialCandidateSelectionRequired[0] = false;
    runtime.pendingInitialCandidateSelectionRequired[1] = false;
    m_liveHardwareRoiActive = m_captureState == CaptureState::Live;
    m_liveStartupPhase = LiveStartupPhase::Tracking;
    applyRoiSummary(actualRois[0], QStringLiteral("相机1"));
    showDeferredLiveRelocalizationPreview();
    setStatusMessage(QStringLiteral("状态: 双相机全画幅定位完成，已同步切换到 64x64 ROI 跟踪"),
                     UiStatusLevel::Success);
    return true;
}

bool DIMM::startHardwarePulseStage(double frequencyHz, const QString& stageLabel, QString* reason)
{
    if (m_configTriggerMode == 0 || !m_pulseGeneratorEnabled) {
        return true;
    }
    if (!m_pulseGenerator) {
        if (reason) {
            *reason = QStringLiteral("脉冲板控制器未初始化。");
        }
        return false;
    }

    PulseGeneratorManager::Config pulseConfig;
    pulseConfig.enabled = true;
    pulseConfig.portName = m_pulseGeneratorPort;
    pulseConfig.baudRate = m_pulseGeneratorBaudRate;
    pulseConfig.terminalId = m_pulseGeneratorTerminalId;
    pulseConfig.frequencyHz = frequencyHz;
    pulseConfig.pulseCount = m_pulseGeneratorPulseCount;
    pulseConfig.dutyPercent = m_pulseGeneratorDutyPercent;
    pulseConfig.remoteControl = m_pulseGeneratorRemoteControl;

    if (m_pulseGenerator->isRunning() && pulseConfigsMatch(m_pulseGenerator->config(), pulseConfig)) {
        setStatusMessage(QStringLiteral("状态: 复用当前脉冲输出: %1 @ %2 Hz")
                             .arg(m_pulseGeneratorPort)
                             .arg(frequencyHz, 0, 'f', 1),
                         UiStatusLevel::Success);
        return true;
    }

    QString errorMessage;
    if (!m_pulseGenerator->configureAndStart(pulseConfig, &errorMessage)) {
        if (reason) {
            *reason = errorMessage.isEmpty()
                          ? QStringLiteral("%1触发启动失败。").arg(stageLabel)
                          : errorMessage;
        }
        return false;
    }

    setStatusMessage(QStringLiteral("状态: %1触发已启动: %2 @ %3 Hz")
                         .arg(stageLabel, m_pulseGeneratorPort)
                         .arg(frequencyHz, 0, 'f', 1),
                     UiStatusLevel::Success);
    return true;
}

bool DIMM::startFullFrameLocalizationPulse(QString* reason)
{
    return startHardwarePulseStage(kFullFrameLocalizationPulseHz,
                                   QStringLiteral("全画幅低频定位"),
                                   reason);
}

bool DIMM::switchToRoiTrackingPulse(QString* reason)
{
    return startHardwarePulseStage(m_pulseGeneratorFrequencyHz,
                                   QStringLiteral("ROI 高频跟踪"),
                                   reason);
}

void DIMM::updateMinuteRoi(bool force)
{
    if (!m_imageProcessor) {
        return;
    }

    Q_UNUSED(force);
    auto& runtime = activeRuntime();
    if (!hasValidCentroidsForRoiUpdate()) {
        return;
    }

    RoiRect roi0 = buildCameraCentroidRoi(0);
    RoiRect roi1 = buildCameraCentroidRoi(1);

    RoiRect actualRoi0 = roi0;
    RoiRect actualRoi1 = roi1;

    if (m_captureState == CaptureState::Live) {
        QString reason;
        RoiRect actualRois[2] = {actualRoi0, actualRoi1};
        const RoiRect liveRois[2] = {roi0, roi1};
        if (applyLiveHardwareRois(liveRois, &reason, actualRois)) {
            actualRoi0 = actualRois[0];
            actualRoi1 = actualRois[1];
            m_liveHardwareRoiActive = true;
            m_imageProcessor->setPairRois(actualRois);
            recordLiveRoiUpdate(actualRois, QStringLiteral("centroid_recenter"));
            runtime.roiRecenteringCandidateFrameCount = 0;
            applyRoiSummary(actualRoi0, QStringLiteral("相机1"));
        } else {
            m_liveHardwareRoiActive = false;
            setStatusMessage(reason, UiStatusLevel::Warning);
            return;
        }
    } else {
        m_imageProcessor->setCurrentRoi(0, actualRoi0);
        m_imageProcessor->setCurrentRoi(1, actualRoi1);
        applyRoiSummary(actualRoi0, QStringLiteral("相机1"));
    }

    ui->lblROITimeCurrent->setText(hasValidCentroidsForRoiUpdate()
                                       ? QStringLiteral("已锁定双相机独立 ROI")
                                       : QStringLiteral("等待两路有效质心"));
    ui->lblROITimeNext->setText(QStringLiteral("ROI 固定尺寸: 64 x 64"));
}

void DIMM::hideLegacyRoiScheduleUi()
{
    ui->roiTablePanel->hide();
    ui->btnAddROI->hide();
    ui->btnDeleteROI->hide();
    ui->actionROISchedule->setVisible(false);
    ui->actionViewROI->setVisible(false);
    ui->btnROI->setVisible(false);
    ui->lblROIMapLabel->setText(roiRuleDescription());
    ui->lblROITimeLabel->setText(QStringLiteral("ROI 规则"));
    ui->lblROITimeCurrent->setText(QStringLiteral("等待两路有效质心"));
    ui->lblROITimeNext->setText(QStringLiteral("ROI 固定尺寸: 64 x 64"));
}

void DIMM::onToggleAlignmentMode()
{
    if (m_captureState == CaptureState::Alignment) {
        stopAlignmentMode();
        return;
    }

    QString reason;
    if (!startAlignmentMode(&reason)) {
        const QString message = reason.isEmpty()
                                    ? QStringLiteral("无法进入对准模式。")
                                    : reason;
        QMessageBox::warning(this, QStringLiteral("对准模式"), message);
        setStatusMessage(message, UiStatusLevel::Warning);
    }
}

void DIMM::onConfirmCamera1PolarisCandidate()
{
    requestAlignmentPolarisSelection(0);
}

void DIMM::onConfirmCamera2PolarisCandidate()
{
    requestAlignmentPolarisSelection(1);
}

void DIMM::requestAlignmentPolarisSelection(int cameraIndex)
{
    if (m_captureState != CaptureState::Alignment) {
        setStatusMessage(QStringLiteral("状态: 请先进入对准模式，再确认北极星"), UiStatusLevel::Warning);
        return;
    }
    if (!isValidCameraIndex(cameraIndex)) {
        return;
    }

    auto& runtime = m_liveRuntime;
    auto& cameraState = m_alignmentSession.camera(cameraIndex);
    cameraState.selectionRequested = true;
    cameraState.lastPreviewMs = -1;
    runtime.selectedInitialCandidateIndex[cameraIndex] = -1;
    runtime.lastInitialCandidatePromptMs[cameraIndex] = -1;
    setStatusMessage(QStringLiteral("状态: 已请求确认相机%1的北极星，下一张全画幅候选列表将弹出编号确认")
                         .arg(cameraIndex + 1),
                     UiStatusLevel::Info);
    if (!m_alignmentCachedCandidates[cameraIndex].isEmpty()) {
        int chosenCandidateIndex = -1;
        cameraState.selectionRequested = false;
        if (!promptAlignmentCandidateSelection(cameraIndex,
                                               m_alignmentCachedCandidates[cameraIndex],
                                               &chosenCandidateIndex)) {
            AlignmentSession::recordCandidatePromptCancelled(
                &runtime.lastInitialCandidatePromptMs[cameraIndex],
                QDateTime::currentMSecsSinceEpoch());
            setStatusMessage(QStringLiteral("状态: 相机%1对准候选星点选择已取消，保留候选框等待确认")
                                 .arg(cameraIndex + 1),
                             UiStatusLevel::Warning);
            return;
        }

        AlignmentSession::recordCandidatePromptAccepted(
            &runtime.selectedInitialCandidateIndex[cameraIndex],
            &runtime.lastInitialCandidatePromptMs[cameraIndex],
            chosenCandidateIndex);
        InitialStarSelection selection =
            PolarisDetectionPipeline::selectInitialStarCandidate(
                m_alignmentCachedCandidates[cameraIndex],
                false,
                QPointF(),
                chosenCandidateIndex);
        FullFrameCanvas* targetCanvas =
            cameraIndex == 0 ? m_fullFrameCanvas1 : m_fullFrameCanvas2;
        if (selection.selected) {
            applyAlignmentSelectedCandidate(cameraIndex,
                                            targetCanvas,
                                            m_alignmentCachedCandidates[cameraIndex],
                                            selection,
                                            true,
                                            nullptr);
        }
    }
}

bool DIMM::startAlignmentMode(QString* reason)
{
    const AlignmentStartReadiness readiness =
        validateAlignmentStartReadiness(
            m_cameraManager != nullptr,
            m_captureState == CaptureState::Alignment,
            m_captureState == CaptureState::Idle || m_captureState == CaptureState::Paused,
            m_captureState == CaptureState::Paused,
            openCameraCount());
    if (readiness.alreadyActive) {
        return true;
    }
    if (!readiness.canStart) {
        if (reason) {
            *reason = readiness.reason;
        }
        return false;
    }

    if (readiness.shouldStopPausedCapture) {
        stopLiveCapture();
        updateCaptureState(CaptureState::Idle);
    }

    if (!prepareAlignmentCamerasForPreview(reason)) {
        return false;
    }

    resetAlignmentRuntimeForStart();
    clearAlignmentCanvasesForStart();

    if (!m_cameraManager->startAll()) {
        if (reason) {
            *reason = QStringLiteral("对准模式启动相机连续取图失败。");
        }
        return false;
    }

    showAlignmentModeStarted();
    return true;
}

void DIMM::stopAlignmentMode()
{
    if (m_captureState != CaptureState::Alignment) {
        return;
    }

    restoreCamerasAfterAlignment();

    resetAlignmentRuntimeForStop();
    clearAlignmentCanvasesForStop();

    showAlignmentModeStopped();
}

bool DIMM::prepareAlignmentCamerasForPreview(QString* reason)
{
    return AlignmentCameraCoordinator::preparePreview(m_cameraManager,
                                                      m_alignmentPreviewRateHz,
                                                      reason);
}

void DIMM::restoreCamerasAfterAlignment()
{
    AlignmentCameraCoordinator::restoreAfterAlignment(m_cameraManager,
                                                      m_configTriggerMode);
}

void DIMM::showAlignmentModeStarted()
{
    setDetailViewMode(DetailViewMode::None);
    updateCaptureState(CaptureState::Alignment);
    const QString solveLabel = m_alignmentAutoSolveEnabled
                                   ? AlignmentUiPresenter::waitingAlignmentLabelText()
                                   : AlignmentUiPresenter::solveStateText(AlignmentSolveState::Disabled);
    const UiStatusLevel solveLevel = m_alignmentAutoSolveEnabled ? UiStatusLevel::Info
                                                                 : UiStatusLevel::Warning;
    setAlignmentSolveLabel(0, solveLabel, solveLevel);
    setAlignmentSolveLabel(1, solveLabel, solveLevel);
    setStatusMessage(AlignmentUiPresenter::startedAlignmentStatusText(m_alignmentPreviewRateHz),
                     UiStatusLevel::Info);
}

void DIMM::showAlignmentModeStopped()
{
    updateCaptureState(CaptureState::Idle);
    if (m_lblAlignmentSolveCam1) {
        m_lblAlignmentSolveCam1->setVisible(false);
    }
    if (m_lblAlignmentSolveCam2) {
        m_lblAlignmentSolveCam2->setVisible(false);
    }
    setDetailViewMode(DetailViewMode::RoiOnly);
    setStatusMessage(AlignmentUiPresenter::stoppedAlignmentStatusText(), UiStatusLevel::Warning);
}

void DIMM::resetAlignmentRuntimeForStart()
{
    m_alignmentSession.camera(0).lastPreviewMs = -1;
    m_alignmentSession.camera(1).lastPreviewMs = -1;
    m_alignmentSession.camera(0).selectionRequested = false;
    m_alignmentSession.camera(1).selectionRequested = false;
    m_alignmentCachedCandidates[0].clear();
    m_alignmentCachedCandidates[1].clear();
    m_alignmentLastCandidateDetectionMs[0] = -1;
    m_alignmentLastCandidateDetectionMs[1] = -1;
    const quint64 solveGeneration = m_alignmentSession.advanceSolveGeneration();
    if (m_polarisSolverController) {
        m_polarisSolverController->cancelAll(solveGeneration);
    }
    auto& runtime = m_liveRuntime;
    for (int i = 0; i < kCameraCount; ++i) {
        AlignmentLiveRuntimeAccess access;
        access.confirmedPolarisPosition = &runtime.confirmedPolarisPosition[i];
        access.hasConfirmedPolarisPosition = &runtime.hasConfirmedPolarisPosition[i];
        access.lastTargetPosition = &runtime.lastTargetPosition[i];
        access.hasLastTargetPosition = &runtime.hasLastTargetPosition[i];
        access.selectedInitialCandidateIndex = &runtime.selectedInitialCandidateIndex[i];
        access.pendingInitialCandidateSelectionRequired =
            &runtime.pendingInitialCandidateSelectionRequired[i];
        access.lastInitialCandidatePromptMs = &runtime.lastInitialCandidatePromptMs[i];
        m_alignmentSession.resetCameraForStart(i, access, m_alignmentAutoSolveEnabled);
        const QString solveLabel = m_alignmentAutoSolveEnabled
                                       ? AlignmentUiPresenter::waitingAlignmentLabelText()
                                       : AlignmentUiPresenter::solveStateText(AlignmentSolveState::Disabled);
        const UiStatusLevel solveLevel = m_alignmentAutoSolveEnabled ? UiStatusLevel::Info
                                                                     : UiStatusLevel::Warning;
        setAlignmentSolveLabel(i, solveLabel, solveLevel);
    }
}

void DIMM::resetAlignmentRuntimeForStop()
{
    m_alignmentSession.camera(0).selectionRequested = false;
    m_alignmentSession.camera(1).selectionRequested = false;
    m_alignmentCachedCandidates[0].clear();
    m_alignmentCachedCandidates[1].clear();
    m_alignmentLastCandidateDetectionMs[0] = -1;
    m_alignmentLastCandidateDetectionMs[1] = -1;
    const quint64 solveGeneration = m_alignmentSession.advanceSolveGeneration();
    if (m_polarisSolverController) {
        m_polarisSolverController->cancelAll(solveGeneration);
    }
    for (int i = 0; i < kCameraCount; ++i) {
        m_alignmentSession.resetCameraForStop(i);
        setAlignmentSolveLabel(i, AlignmentUiPresenter::stoppedAlignmentLabelText(), UiStatusLevel::Muted);
    }
}

void DIMM::clearAlignmentCanvasesForStart()
{
    if (m_fullFrameCanvas1) {
        m_fullFrameCanvas1->clearAlignmentOverlay();
        m_fullFrameCanvas1->clearStarCandidateOverlays();
        m_fullFrameCanvas1->setRoiList({});
        m_fullFrameCanvas1->setCurrentRoi(-1);
    }
    if (m_fullFrameCanvas2) {
        m_fullFrameCanvas2->clearAlignmentOverlay();
        m_fullFrameCanvas2->clearStarCandidateOverlays();
        m_fullFrameCanvas2->setRoiList({});
        m_fullFrameCanvas2->setCurrentRoi(-1);
    }
}

void DIMM::clearAlignmentCanvasesForStop()
{
    if (m_fullFrameCanvas1) {
        m_fullFrameCanvas1->clearAlignmentOverlay();
    }
    if (m_fullFrameCanvas2) {
        m_fullFrameCanvas2->clearAlignmentOverlay();
    }
}

double DIMM::fallbackAlignmentOrbitRadiusPx() const
{
    const double focalLengthMm = std::max(1.0, m_alignmentFocalLengthMm);
    const double pixelSizeMm = std::max(0.001, m_alignmentPixelSizeUm / 1000.0);
    const double plateScaleArcsecPerPx = 206265.0 * pixelSizeMm / focalLengthMm;
    const double polarDistanceArcsec = std::max(0.0, m_alignmentPolarisPolarDistanceArcmin) * 60.0;
    const double autoRadius = plateScaleArcsecPerPx > 0.0
                                  ? polarDistanceArcsec / plateScaleArcsecPerPx
                                  : 0.0;
    return std::max(1.0, autoRadius + m_alignmentRadiusAdjustPx);
}

double DIMM::alignmentOrbitRadiusPx() const
{
    return fallbackAlignmentOrbitRadiusPx();
}

void DIMM::handleAlignmentFramePacket(int cameraIndex, const CameraFrame& packet)
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    AlignmentFrameCoordinator::FrameGateInput frameGate;
    frameGate.alignmentActive = m_captureState == CaptureState::Alignment;
    frameGate.cameraIndex = cameraIndex;
    frameGate.nowMs = nowMs;
    frameGate.lastPreviewMs = isValidCameraIndex(cameraIndex)
                                  ? m_alignmentSession.camera(cameraIndex).lastPreviewMs
                                  : -1;
    frameGate.previewIntervalMs =
        AlignmentFrameCoordinator::previewIntervalMs(m_alignmentPreviewRateHz);
    frameGate.frame = &packet.image;
    if (!AlignmentFrameCoordinator::shouldAcceptAlignmentFrame(frameGate)) {
        return;
    }

    if (!prepareAlignmentFramePreview(cameraIndex, packet)) {
        return;
    }

    auto& solveRuntime = m_alignmentSession.camera(cameraIndex).solveRuntime;
    switch (AlignmentFrameCoordinator::nextFrameAction(m_alignmentAutoSolveEnabled,
                                                       solveRuntime,
                                                       nowMs)) {
    case AlignmentFrameCoordinator::FrameAction::Disabled:
        solveRuntime.state = AlignmentSolveState::Disabled;
        finishAlignmentFramePreview(cameraIndex, packet, nowMs);
        return;
    case AlignmentFrameCoordinator::FrameAction::ManualTrack:
        handleManualAlignmentFrameTracking(cameraIndex, packet.image);
        finishAlignmentFramePreview(cameraIndex, packet, nowMs);
        return;
    case AlignmentFrameCoordinator::FrameAction::AutomaticTrack:
        if (handleAutomaticAlignmentFrameTracking(cameraIndex, packet.image, nowMs)) {
            finishAlignmentFramePreview(cameraIndex, packet, nowMs);
            return;
        }
        [[fallthrough]];
    case AlignmentFrameCoordinator::FrameAction::WaitRetry:
        setAlignmentSolveLabel(cameraIndex,
                               AlignmentUiPresenter::formatRetryWaitingSolveLabel(
                                   solveRuntime.nextRetryMs - nowMs),
                               UiStatusLevel::Warning);
        finishAlignmentFramePreview(cameraIndex, packet, nowMs);
        return;
    case AlignmentFrameCoordinator::FrameAction::RequestSolve:
        break;
    }
    requestAutomaticPolarisSolve(cameraIndex, false);
    finishAlignmentFramePreview(cameraIndex, packet, nowMs);
}

bool DIMM::handleManualAlignmentFrameTracking(int cameraIndex, const cv::Mat& frame)
{
    auto& solveRuntime = m_alignmentSession.camera(cameraIndex).solveRuntime;
    auto& runtime = m_liveRuntime;
    QPointF trackedPosition;
    double trackedPeak = 0.0;
    if (trackAlignmentPolarisLocally(cameraIndex, frame, &trackedPosition, &trackedPeak)) {
        AlignmentController::applyManualTrackingSuccess(&solveRuntime, trackedPosition);
        AlignmentController::syncTrackedPolarisPosition(cameraIndex,
                                                        trackedPosition,
                                                        runtime.confirmedPolarisPosition,
                                                        runtime.hasConfirmedPolarisPosition,
                                                        runtime.lastTargetPosition,
                                                        runtime.hasLastTargetPosition);
        setAlignmentSolveLabel(cameraIndex,
                               AlignmentUiPresenter::formatManualTrackingSolveLabel(trackedPosition),
                               UiStatusLevel::Success);
    } else {
        AlignmentController::applyManualTrackingFailure(&solveRuntime);
        setAlignmentSolveLabel(cameraIndex,
                               AlignmentUiPresenter::formatManualTrackingLostSolveLabel(),
                               UiStatusLevel::Warning);
    }
    return true;
}

bool DIMM::handleAutomaticAlignmentFrameTracking(int cameraIndex, const cv::Mat& frame, qint64 nowMs)
{
    auto& solveRuntime = m_alignmentSession.camera(cameraIndex).solveRuntime;
    auto& runtime = m_liveRuntime;
    QPointF trackedPosition;
    double trackedPeak = 0.0;
    if (trackAlignmentPolarisLocally(cameraIndex, frame, &trackedPosition, &trackedPeak)) {
        PolarisTracker::recordTrackSuccess(&solveRuntime, trackedPosition);
        AlignmentController::syncTrackedPolarisPosition(cameraIndex,
                                                        trackedPosition,
                                                        runtime.confirmedPolarisPosition,
                                                        runtime.hasConfirmedPolarisPosition,
                                                        runtime.lastTargetPosition,
                                                        runtime.hasLastTargetPosition);
        setAlignmentSolveLabel(cameraIndex,
                               AlignmentUiPresenter::formatTrackingSolveLabel(trackedPosition),
                               UiStatusLevel::Success);
        return true;
    }

    const int lostTrackRetryCount = buildPolarisSolverConfig().lostTrackRetryCount;
    if (PolarisTracker::recordTrackFailure(&solveRuntime,
                                           lostTrackRetryCount,
                                           nowMs,
                                           m_alignmentRetryIntervalMs)) {
        return false;
    }

    setAlignmentSolveLabel(cameraIndex,
                           AlignmentUiPresenter::formatTrackingLostSolveLabel(
                               solveRuntime.consecutiveTrackFailures,
                               lostTrackRetryCount),
                           UiStatusLevel::Warning);
    return true;
}

bool DIMM::prepareAlignmentFramePreview(int cameraIndex, const CameraFrame& packet)
{
    FullFrameCanvas* targetCanvas = cameraIndex == 0 ? m_fullFrameCanvas1 : m_fullFrameCanvas2;
    if (!targetCanvas) {
        return false;
    }

    targetCanvas->setImage(packet.image);
    targetCanvas->setRoiList({});
    targetCanvas->setCurrentRoi(-1);
    auto& cameraState = m_alignmentSession.camera(cameraIndex);
    cameraState.lastFrame = packet.image.clone();
    cameraState.lastFrameId = packet.frameId;
    return true;
}

void DIMM::finishAlignmentFramePreview(int cameraIndex, const CameraFrame& packet, qint64 nowMs)
{
    updateAlignmentOverlay(cameraIndex, packet);
    m_alignmentSession.camera(cameraIndex).lastPreviewMs = nowMs;
}

void DIMM::requestAutomaticPolarisSolve(int cameraIndex, bool force)
{
    if (m_captureState != CaptureState::Alignment ||
        !isValidCameraIndex(cameraIndex) ||
        !m_polarisSolverController) {
        return;
    }
    auto& cameraState = m_alignmentSession.camera(cameraIndex);
    if (cameraState.lastFrame.empty()) {
        showMissingAlignmentFrameForSolve(cameraIndex);
        return;
    }

    auto& solveRuntime = cameraState.solveRuntime;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (AlignmentTaskManager::prepareFullSolveRequest(&solveRuntime, force, nowMs) !=
        AlignmentFullSolveRequestAction::Submit) {
        return;
    }

    PolarisSolverConfig solverConfig = buildPolarisSolverConfig();
    solverConfig.cameraIndex = cameraIndex;
    AlignmentTaskManager::submitFullSolve(m_polarisSolverController,
                                          cameraIndex,
                                          cameraState.lastFrame,
                                          solverConfig,
                                          m_alignmentSession.solveGeneration(),
                                          cameraState.lastFrameId,
                                          force);
    showSubmittedAlignmentSolve(cameraIndex, force);
}

void DIMM::showMissingAlignmentFrameForSolve(int cameraIndex)
{
    setAlignmentSolveLabel(cameraIndex,
                           AlignmentUiPresenter::formatMissingFrameSolveLabel(),
                           UiStatusLevel::Warning);
    setStatusMessage(AlignmentUiPresenter::formatMissingFrameStatusMessage(cameraIndex),
                     UiStatusLevel::Warning);
}

void DIMM::showSubmittedAlignmentSolve(int cameraIndex, bool force)
{
    setAlignmentSolveLabel(cameraIndex,
                           AlignmentUiPresenter::formatSubmittedSolveLabel(force),
                           UiStatusLevel::Info);
}

bool DIMM::trackAlignmentPolarisLocally(int cameraIndex,
                                        const cv::Mat& frame,
                                        QPointF* trackedPosition,
                                        double* peakValue)
{
    if (!isValidCameraIndex(cameraIndex) ||
        frame.empty() ||
        !trackedPosition ||
        !m_liveRuntime.hasConfirmedPolarisPosition[cameraIndex]) {
        return false;
    }

    const AlignmentLocalTracker::CentroidDetector centroidDetector =
        [](const cv::Mat& roi, QPointF* centroid, double* peak) {
            return detectInitialStarCentroid(roi, centroid, peak) ||
                   detectInitialStarCentroidFast(roi, centroid, peak);
        };
    return AlignmentLocalTracker::trackFromConfirmedPosition(
        frame,
        m_liveRuntime.confirmedPolarisPosition[cameraIndex],
        &m_alignmentSession.camera(cameraIndex).solveRuntime,
        centroidDetector,
        trackedPosition,
        peakValue);
}

void DIMM::requestAutomaticPolarisSolveBoth()
{
    requestAutomaticPolarisSolve(0, true);
    requestAutomaticPolarisSolve(1, true);
}

PolarisSolverConfig DIMM::buildPolarisSolverConfig() const
{
    PolarisSolverConfig config;
    config.enabled = m_alignmentAutoSolveEnabled;
    config.maxDetectedStars = m_alignmentMaxDetectedStars;
    config.minMatchedStars = m_alignmentMinMatchedStars;
    config.maxRmsPx = m_alignmentMaxRmsPx;
    config.nominalPlateScaleArcsecPx =
        206265.0 * std::max(0.001, m_alignmentPixelSizeUm / 1000.0) /
        std::max(1.0, m_alignmentFocalLengthMm);
    config.minPlateScaleArcsecPx = config.nominalPlateScaleArcsecPx * 0.88;
    config.maxPlateScaleArcsecPx = config.nominalPlateScaleArcsecPx * 1.12;
    config.initialMatchTolerancePx = 8.0;
    config.refinedMatchTolerancePx = 4.0;
    config.minScoreMargin = 5.0;
    config.minMatchedSpatialSpreadPx = m_alignmentMinMatchedSpatialSpreadPx;
    config.minPolarisSnr = m_alignmentMinPolarisSnr;
    config.allowSaturatedPolarisConfirmation = m_alignmentAllowSaturatedPolarisConfirmation;
    config.observationEpochYear = decimalYearFromUtc(QDateTime::currentDateTimeUtc());
    config.retryIntervalMs = m_alignmentRetryIntervalMs;
    config.lostTrackRetryCount = 3;
    config.showMatchedCatalogStars = m_alignmentShowMatchedCatalogStars;
    config.hotPixelTemplates[0].enabled = m_hotPixelTemplatesEnabled;
    config.hotPixelTemplates[0].maskPath = PathUtils::resolvePathFromAppDir(m_hotPixelCamera0MaskPath);
    config.hotPixelTemplates[0].excessPath = PathUtils::resolvePathFromAppDir(m_hotPixelCamera0ExcessPath);
    config.hotPixelTemplates[0].templateWidth = m_hotPixelTemplateWidth;
    config.hotPixelTemplates[0].templateHeight = m_hotPixelTemplateHeight;
    config.hotPixelTemplates[1].enabled = m_hotPixelTemplatesEnabled;
    config.hotPixelTemplates[1].maskPath = PathUtils::resolvePathFromAppDir(m_hotPixelCamera1MaskPath);
    config.hotPixelTemplates[1].excessPath = PathUtils::resolvePathFromAppDir(m_hotPixelCamera1ExcessPath);
    config.hotPixelTemplates[1].templateWidth = m_hotPixelTemplateWidth;
    config.hotPixelTemplates[1].templateHeight = m_hotPixelTemplateHeight;
    return config;
}

void DIMM::logPolarisSolveResult(const PolarisSolveResult& result) const
{
    qInfo().noquote() << AlignmentUiPresenter::formatPolarisSolveLogLine(result);
}

void DIMM::onPolarisSolveFinished(PolarisSolveResult result)
{
    if (result.generation != m_alignmentSession.solveGeneration() ||
        !isValidCameraIndex(result.cameraIndex) ||
        m_captureState != CaptureState::Alignment) {
        return;
    }

    auto& cameraState = m_alignmentSession.camera(result.cameraIndex);
    auto& solveRuntime = cameraState.solveRuntime;
    if (AlignmentController::shouldIgnoreSolverResult(solveRuntime)) {
        logPolarisSolveResult(result);
        return;
    }

    logPolarisSolveResult(result);
    cameraState.solveResult = result;
    solveRuntime.lastFullSolve = result;
    auto& runtime = m_liveRuntime;
    if (result.valid && result.hasDetectedPolarisPixel) {
        AlignmentController::applyDetectedPolarisSolve(&solveRuntime, result);
        runtime.confirmedPolarisPosition[result.cameraIndex] = result.detectedPolarisPixel;
        runtime.hasConfirmedPolarisPosition[result.cameraIndex] = true;
        runtime.lastTargetPosition[result.cameraIndex] = result.detectedPolarisPixel;
        runtime.hasLastTargetPosition[result.cameraIndex] = true;
        runtime.pendingInitialCandidateSelectionRequired[result.cameraIndex] = false;
        runtime.selectedInitialCandidateIndex[result.cameraIndex] = -1;
        refreshActionStates();
    }
    if (result.valid && !result.hasDetectedPolarisPixel) {
        AlignmentController::applyPredictedOnlyRetry(&solveRuntime,
                                                     QDateTime::currentMSecsSinceEpoch(),
                                                     m_alignmentRetryIntervalMs);
        setAlignmentSolveLabel(result.cameraIndex,
                               AlignmentUiPresenter::formatPredictedOnlySolveLabel(result),
                               UiStatusLevel::Warning);
        setStatusMessage(AlignmentUiPresenter::formatPredictedOnlyStatusMessage(result),
                         UiStatusLevel::Warning);
    } else if (result.valid) {
        setAlignmentSolveLabel(result.cameraIndex,
                               AlignmentUiPresenter::formatSolvedSolveLabel(result),
                               UiStatusLevel::Success);
        setStatusMessage(AlignmentUiPresenter::formatSolvedStatusMessage(result),
                         UiStatusLevel::Success);
    } else if (result.status == PolarisSolveStatus::InsufficientStars ||
               result.status == PolarisSolveStatus::NoCatalogMatch ||
               result.status == PolarisSolveStatus::LowConfidence) {
        const AlignmentRetryDecision retryDecision =
            AlignmentTaskManager::applySolveFailureRetry(&solveRuntime,
                                                         result.status,
                                                         QDateTime::currentMSecsSinceEpoch(),
                                                         m_alignmentRetryIntervalMs);
        setAlignmentSolveLabel(result.cameraIndex,
                               AlignmentUiPresenter::formatRetrySolveLabel(result),
                               UiStatusLevel::Warning);
        setStatusMessage(AlignmentUiPresenter::formatRetryStatusMessage(
                             result,
                             retryDecision.diagnosticHint),
                         UiStatusLevel::Warning);
    } else if (result.status == PolarisSolveStatus::Error) {
        AlignmentController::applySolveError(&solveRuntime);
        setAlignmentSolveLabel(result.cameraIndex,
                               AlignmentUiPresenter::formatErrorSolveLabel(result),
                               UiStatusLevel::Error);
        setStatusMessage(AlignmentUiPresenter::formatErrorStatusMessage(result),
                         UiStatusLevel::Error);
    }
}

void DIMM::onPolarisSolveStatusChanged(int cameraIndex,
                                       PolarisSolveStatus status,
                                       QString message,
                                       quint64 generation)
{
    if (generation != m_alignmentSession.solveGeneration() ||
        m_captureState != CaptureState::Alignment ||
        !isValidCameraIndex(cameraIndex)) {
        return;
    }
    if (status == PolarisSolveStatus::ManualConfirmed) {
        setAlignmentSolveLabel(cameraIndex,
                               AlignmentUiPresenter::formatManualConfirmedSolveLabel(message),
                               UiStatusLevel::Success);
        setStatusMessage(AlignmentUiPresenter::formatManualConfirmedStatusMessage(cameraIndex, message),
                         UiStatusLevel::Success);
        return;
    }
    if (m_alignmentSession.camera(cameraIndex).solveRuntime.state == AlignmentSolveState::ManualOnly) {
        return;
    }
    if (status == PolarisSolveStatus::DetectingStars ||
        status == PolarisSolveStatus::MatchingCatalog) {
        setAlignmentSolveLabel(cameraIndex,
                               AlignmentUiPresenter::formatMatchingSolveLabel(message),
                               UiStatusLevel::Info);
        setStatusMessage(AlignmentUiPresenter::formatMatchingStatusMessage(cameraIndex, message),
                         UiStatusLevel::Info);
    }
}

QVector<InitialStarCandidate> DIMM::collectAlignmentStarCandidates(
    int cameraIndex,
    const cv::Mat& frame,
    const PolarisSolveResult& solved,
    bool hasCurrentSolverResult,
    bool allowGuiCandidateDetection,
    cv::Mat* mono8,
    double* peakValue) const
{
    AlignmentCandidateCollectionInput input;
    input.cameraIndex = cameraIndex;
    input.frame = &frame;
    input.solved = &solved;
    input.autoSolveEnabled = m_alignmentAutoSolveEnabled;
    input.hasCurrentSolverResult = hasCurrentSolverResult;
    input.lastFrameId = m_alignmentSession.camera(cameraIndex).lastFrameId;
    input.allowGuiCandidateDetection = allowGuiCandidateDetection;
    input.candidateDetector = [mono8](const cv::Mat& grayscale, double* peak) {
        if (mono8) {
            *mono8 = ImageUtils::normalizeMono8Frame(grayscale);
        }
        if (grayscale.empty()) {
            return QVector<InitialStarCandidate>();
        }
        QVector<InitialStarCandidate> candidates = detectInitialStarCandidates(grayscale, peak);
        if (!candidates.isEmpty()) {
            return candidates;
        }

        InitialStarCandidate peakCandidate;
        double peakValue = 0.0;
        if (detectRawInitialStarPeakCandidate(grayscale, &peakCandidate, &peakValue)) {
            if (peak) {
                *peak = peakValue;
            }
            candidates.append(peakCandidate);
        }
        return candidates;
    };
    return AlignmentSession::collectCandidates(input, mono8, peakValue);
}

bool DIMM::handleAlignmentCandidateSelection(
    int cameraIndex,
    FullFrameCanvas* targetCanvas,
    FullFrameCanvas::AlignmentOverlay* overlay,
    const QVector<InitialStarCandidate>& candidates,
    QPointF* selectedStar)
{
    if (!targetCanvas || !overlay || candidates.isEmpty()) {
        return true;
    }

    auto& runtime = m_liveRuntime;
    targetCanvas->setStarCandidateOverlays(
        PolarisDetectionPipeline::buildCandidateOverlays(
            candidates, runtime.selectedInitialCandidateIndex[cameraIndex]));

    const bool manualSelectionRequested = m_alignmentSession.camera(cameraIndex).selectionRequested;
    const bool hadConfirmedPolarisBeforeSelection =
        runtime.hasConfirmedPolarisPosition[cameraIndex];
    QPointF preferredTarget;
    InitialStarSelection selection = selectAlignmentInitialCandidate(cameraIndex,
                                                                    candidates,
                                                                    manualSelectionRequested,
                                                                    &preferredTarget);
    runtime.pendingInitialCandidateSelectionRequired[cameraIndex] =
        selection.requiresUserSelection;
    if (!selection.selected && selection.requiresUserSelection && !manualSelectionRequested) {
        setStatusMessage(QStringLiteral("状态: 相机%1显示到多个候选星点，请点击“确认相机%1的北极星”后选择编号")
                             .arg(cameraIndex + 1),
                         UiStatusLevel::Info);
    }
    if (manualSelectionRequested &&
        !handleManualAlignmentCandidatePrompt(cameraIndex,
                                              targetCanvas,
                                              overlay,
                                              candidates,
                                              preferredTarget,
                                              &selection)) {
        return false;
    }

    const bool canApplyAlignmentSelection =
        AlignmentSession::canApplyCandidateSelection(
            manualSelectionRequested,
            hadConfirmedPolarisBeforeSelection);
    if (selection.selected && canApplyAlignmentSelection) {
        applyAlignmentSelectedCandidate(cameraIndex,
                                        targetCanvas,
                                        candidates,
                                        selection,
                                        manualSelectionRequested,
                                        selectedStar);
    }
    return true;
}

InitialStarSelection DIMM::selectAlignmentInitialCandidate(
    int cameraIndex,
    const QVector<InitialStarCandidate>& candidates,
    bool manualSelectionRequested,
    QPointF* preferredTarget)
{
    auto& runtime = m_liveRuntime;
    AlignmentCandidateRuntimeAccess access;
    access.confirmedPolarisPosition = &runtime.confirmedPolarisPosition[cameraIndex];
    access.hasConfirmedPolarisPosition = &runtime.hasConfirmedPolarisPosition[cameraIndex];
    access.lastTargetPosition = &runtime.lastTargetPosition[cameraIndex];
    access.hasLastTargetPosition = &runtime.hasLastTargetPosition[cameraIndex];
    access.selectedInitialCandidateIndex = &runtime.selectedInitialCandidateIndex[cameraIndex];
    return AlignmentSession::selectInitialCandidate(access,
                                                    candidates,
                                                    manualSelectionRequested,
                                                    preferredTarget);
}

bool DIMM::handleManualAlignmentCandidatePrompt(
    int cameraIndex,
    FullFrameCanvas* targetCanvas,
    FullFrameCanvas::AlignmentOverlay* overlay,
    const QVector<InitialStarCandidate>& candidates,
    const QPointF& preferredTarget,
    InitialStarSelection* selection)
{
    auto& runtime = m_liveRuntime;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 lastPromptMs = runtime.lastInitialCandidatePromptMs[cameraIndex];
    if (!AlignmentSession::shouldShowCandidatePrompt(lastPromptMs, nowMs)) {
        return true;
    }

    int chosenCandidateIndex = -1;
    m_alignmentSession.camera(cameraIndex).selectionRequested = false;
    if (!promptAlignmentCandidateSelection(cameraIndex, candidates, &chosenCandidateIndex)) {
        AlignmentSession::recordCandidatePromptCancelled(
            &runtime.lastInitialCandidatePromptMs[cameraIndex],
            nowMs);
        setStatusMessage(QStringLiteral("状态: 相机%1对准候选星点选择已取消，保留候选框等待确认")
                             .arg(cameraIndex + 1),
                         UiStatusLevel::Warning);
        if (targetCanvas && overlay) {
            targetCanvas->setAlignmentOverlay(*overlay);
        }
        return false;
    }

    AlignmentSession::recordCandidatePromptAccepted(
        &runtime.selectedInitialCandidateIndex[cameraIndex],
        &runtime.lastInitialCandidatePromptMs[cameraIndex],
        chosenCandidateIndex);
    if (targetCanvas) {
        targetCanvas->setStarCandidateOverlays(
            PolarisDetectionPipeline::buildCandidateOverlays(candidates, chosenCandidateIndex));
    }
    if (selection) {
        *selection = PolarisDetectionPipeline::selectInitialStarCandidate(
            candidates,
            false,
            preferredTarget,
            runtime.selectedInitialCandidateIndex[cameraIndex]);
        runtime.pendingInitialCandidateSelectionRequired[cameraIndex] =
            selection->requiresUserSelection;
    }
    return true;
}

void DIMM::applyAlignmentSelectedCandidate(
    int cameraIndex,
    FullFrameCanvas* targetCanvas,
    const QVector<InitialStarCandidate>& candidates,
    const InitialStarSelection& selection,
    bool manualSelectionRequested,
    QPointF* selectedStar)
{
    auto& runtime = m_liveRuntime;
    const QPointF star = selection.candidate.center;
    if (selectedStar) {
        *selectedStar = star;
    }
    AlignmentCandidateRuntimeAccess access;
    access.confirmedPolarisPosition = &runtime.confirmedPolarisPosition[cameraIndex];
    access.hasConfirmedPolarisPosition = &runtime.hasConfirmedPolarisPosition[cameraIndex];
    access.lastTargetPosition = &runtime.lastTargetPosition[cameraIndex];
    access.hasLastTargetPosition = &runtime.hasLastTargetPosition[cameraIndex];
    access.pendingInitialCandidateSelectionRequired =
        &runtime.pendingInitialCandidateSelectionRequired[cameraIndex];
    access.selectedInitialCandidateIndex = &runtime.selectedInitialCandidateIndex[cameraIndex];
    access.selectionRequested = &m_alignmentSession.camera(cameraIndex).selectionRequested;
    AlignmentSession::recordSelectedCandidate(access,
                                              star,
                                              selection.candidate.index);
    if (manualSelectionRequested) {
        applyManualAlignmentConfirmation(cameraIndex, star);
    }
    if (targetCanvas) {
        targetCanvas->setStarCandidateOverlays(
            PolarisDetectionPipeline::buildCandidateOverlays(
                candidates, selection.candidate.index));
    }
    refreshActionStates();
}

bool DIMM::promptAlignmentCandidateSelection(
    int cameraIndex,
    const QVector<InitialStarCandidate>& candidates,
    int* chosenCandidateIndex)
{
    if (!chosenCandidateIndex || candidates.isEmpty()) {
        return false;
    }

    const QStringList candidateLines =
        AlignmentSession::candidatePromptLines(candidates);
    bool ok = false;
    const int candidateIndex =
        QInputDialog::getInt(this,
                             QStringLiteral("相机%1北极星候选选择")
                                 .arg(cameraIndex + 1),
                             QStringLiteral("相机%1候选列表:\n%2\n\n请选择北极星候选编号:")
                                 .arg(cameraIndex + 1)
                                 .arg(candidateLines.join(QLatin1Char('\n'))),
                             1,
                             1,
                             candidates.size(),
                             1,
                             &ok);
    if (ok) {
        *chosenCandidateIndex = candidateIndex;
    }
    return ok;
}

void DIMM::applyManualAlignmentConfirmation(int cameraIndex, const QPointF& star)
{
    m_alignmentSession.applyManualConfirmation(cameraIndex, star);
    if (m_polarisSolverController) {
        m_polarisSolverController->cancelCamera(cameraIndex, m_alignmentSession.solveGeneration());
    }
    const QString manualConfirmedMessage =
        AlignmentSession::manualConfirmedMessage(star);
    onPolarisSolveStatusChanged(cameraIndex,
                                PolarisSolveStatus::ManualConfirmed,
                                manualConfirmedMessage,
                                m_alignmentSession.solveGeneration());
    setAlignmentSolveLabel(cameraIndex,
                           AlignmentUiPresenter::formatManualConfirmedSolveLabel(manualConfirmedMessage),
                           UiStatusLevel::Success);
}

void DIMM::updateConfirmedPolarisFromFallbackCentroid(int cameraIndex,
                                                      const cv::Mat& frame,
                                                      bool allowGuiCandidateDetection,
                                                      cv::Mat* mono8,
                                                      QPointF* star,
                                                      double* peakValue)
{
    auto& runtime = m_liveRuntime;
    if (mono8 && mono8->empty() && allowGuiCandidateDetection) {
        cv::Mat grayscale;
        if (frame.channels() == 1) {
            grayscale = frame;
        } else {
            cv::cvtColor(frame, grayscale, cv::COLOR_BGR2GRAY);
        }
        *mono8 = ImageUtils::normalizeMono8Frame(grayscale);
    }
    AlignmentCandidateRuntimeAccess access;
    access.confirmedPolarisPosition = &runtime.confirmedPolarisPosition[cameraIndex];
    access.hasConfirmedPolarisPosition = &runtime.hasConfirmedPolarisPosition[cameraIndex];
    access.lastTargetPosition = &runtime.lastTargetPosition[cameraIndex];
    access.hasLastTargetPosition = &runtime.hasLastTargetPosition[cameraIndex];
    access.selectedInitialCandidateIndex = &runtime.selectedInitialCandidateIndex[cameraIndex];
    const AlignmentSession::CentroidDetector centroidDetector =
        [](const cv::Mat& image, QPointF* centroid, double* peak) {
            return detectInitialStarCentroid(image, centroid, peak) ||
                   detectInitialStarCentroidFast(image, centroid, peak);
        };
    AlignmentSession::updateFromFallbackCentroid(access,
                                                 frame,
                                                 allowGuiCandidateDetection,
                                                 mono8,
                                                 star,
                                                 peakValue,
                                                 centroidDetector);
}

void DIMM::updateAlignmentOverlay(int cameraIndex, const CameraFrame& packet)
{
    const cv::Mat& frame = packet.image;
    if (!isValidCameraIndex(cameraIndex) || frame.empty()) {
        return;
    }

    FullFrameCanvas* targetCanvas = cameraIndex == 0 ? m_fullFrameCanvas1 : m_fullFrameCanvas2;
    if (!targetCanvas) {
        return;
    }

    auto& cameraState = m_alignmentSession.camera(cameraIndex);
    const PolarisSolveResult& solved = cameraState.solveResult;
    auto& solveRuntime = cameraState.solveRuntime;
    const bool hasCurrentSolverResult = solved.generation == m_alignmentSession.solveGeneration();
    AlignmentUiPresenter::OverlayBuildInput overlayInput;
    overlayInput.frameSize = QSize(frame.cols, frame.rows);
    overlayInput.fallbackOrbitRadiusPx = alignmentOrbitRadiusPx();
    overlayInput.fallbackPlateScaleArcsecPx =
        206265.0 * std::max(0.001, m_alignmentPixelSizeUm / 1000.0) /
        std::max(1.0, m_alignmentFocalLengthMm);
    overlayInput.useSolvedOrbit = m_alignmentAutoRadius;
    overlayInput.radiusAdjustPx = m_alignmentRadiusAdjustPx;
    overlayInput.solveState = solveRuntime.state;
    overlayInput.solved = &solved;
    overlayInput.hasCurrentSolverResult = hasCurrentSolverResult;
    FullFrameCanvas::AlignmentOverlay overlay =
        AlignmentUiPresenter::buildAlignmentOverlay(overlayInput);

    auto& runtime = m_liveRuntime;
    QPointF star;
    double peakValue = 0.0;
    cv::Mat mono8;
    const bool manualSelectionRequested =
        m_alignmentSession.camera(cameraIndex).selectionRequested;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool shouldRefreshCandidateDetection =
        manualSelectionRequested ||
        m_alignmentCachedCandidates[cameraIndex].isEmpty() ||
        m_alignmentLastCandidateDetectionMs[cameraIndex] < 0 ||
        nowMs - m_alignmentLastCandidateDetectionMs[cameraIndex] >=
            kAlignmentCandidateDetectionRefreshMs;
    const bool allowGuiCandidateDetection = shouldRefreshCandidateDetection;
    QVector<InitialStarCandidate> candidates;
    if (allowGuiCandidateDetection) {
        candidates = collectAlignmentStarCandidates(cameraIndex,
                                                    frame,
                                                    solved,
                                                    hasCurrentSolverResult,
                                                    allowGuiCandidateDetection,
                                                    &mono8,
                                                    &peakValue);
        m_alignmentLastCandidateDetectionMs[cameraIndex] = nowMs;
    } else {
        candidates = m_alignmentCachedCandidates[cameraIndex];
    }
    if (!candidates.isEmpty()) {
        m_alignmentCachedCandidates[cameraIndex] = candidates;
        if (!handleAlignmentCandidateSelection(cameraIndex,
                                               targetCanvas,
                                               &overlay,
                                               candidates,
                                               &star)) {
            return;
        }
    } else {
        m_alignmentCachedCandidates[cameraIndex].clear();
        targetCanvas->clearStarCandidateOverlays();
        runtime.pendingInitialCandidateSelectionRequired[cameraIndex] = false;
        if (manualSelectionRequested &&
            AlignmentSession::shouldShowCandidatePrompt(
                runtime.lastInitialCandidatePromptMs[cameraIndex],
                nowMs)) {
            runtime.lastInitialCandidatePromptMs[cameraIndex] = nowMs;
            setStatusMessage(QStringLiteral("状态: 相机%1未检测到候选星点，请降低全画幅找星阈值或确认当前帧为 Mono12 原始帧")
                                 .arg(cameraIndex + 1),
                             UiStatusLevel::Warning);
        }
        updateConfirmedPolarisFromFallbackCentroid(cameraIndex,
                                                   frame,
                                                   allowGuiCandidateDetection,
                                                   &mono8,
                                                   &star,
                                                   &peakValue);
    }

    AlignmentUiPresenter::applyConfirmedPolarisToOverlay(
        runtime.hasConfirmedPolarisPosition[cameraIndex],
        runtime.confirmedPolarisPosition[cameraIndex],
        &overlay);

    targetCanvas->setAlignmentOverlay(overlay);
}

void DIMM::onStartCapture()
{
    if (m_captureState == CaptureState::Alignment) {
        const QString message = QStringLiteral("请先退出对准模式，再开始正式采集。");
        QMessageBox::warning(this, QStringLiteral("开始采集"), message);
        setStatusMessage(QStringLiteral("状态: 请先退出对准模式"), UiStatusLevel::Warning);
        return;
    }

    if (m_captureState == CaptureState::Live) {
        stopLiveCapture();
        updateCaptureState(CaptureState::Paused);
        setStatusMessage(QStringLiteral("状态: 已暂停"), UiStatusLevel::Warning);
        return;
    }

    if (m_captureState == CaptureState::Simulation) {
        stopSimulationCapture();
        m_reporting = false;
        if (m_reportTimer) {
            m_reportTimer->stop();
        }
        updateCaptureState(CaptureState::Idle);
    }

    QString reason;
    if (!canStartLiveCapture(&reason)) {
        QMessageBox::warning(this, QStringLiteral("开始采集"), reason);
        setStatusMessage(QStringLiteral("状态: 等待双相机连接"), UiStatusLevel::Warning);
        return;
    }

    closeResultFile();
    resetMeasurementState();
    m_liveHardwareRoiActive = false;
    m_liveStartupPhase = LiveStartupPhase::None;
    updateMinuteRoi(true);

    if (!configureLiveCameras(&reason)) {
        updateCaptureState(CaptureState::Idle);
        setStatusMessage(reason, UiStatusLevel::Error);
        QMessageBox::warning(this, QStringLiteral("开始采集"), reason);
        return;
    }

    const bool liveStarted =
        m_configTriggerMode == 0 ? startDualCameraLocalization(&reason) : m_cameraManager->startAll();

    if (liveStarted) {
        updateCaptureState(CaptureState::Live);
        if (m_configTriggerMode == 0) {
            setStatusMessage(QStringLiteral("状态: 连续采集已启动，正在双相机全画幅定位"),
                             UiStatusLevel::Warning);
        } else {
            m_liveStartupPhase = LiveStartupPhase::LocatePair;
            const bool reuseRunningPulse =
                m_pulseGeneratorEnabled && m_pulseGenerator && m_pulseGenerator->isRunning();
            if (reuseRunningPulse) {
                setStatusMessage(QStringLiteral("状态: 硬件触发已就绪，复用当前脉冲输出进行双相机全画幅定位"),
                                 UiStatusLevel::Success);
            } else {
                if (!startFullFrameLocalizationPulse(&reason)) {
                    const bool pulseResponseTimeout =
                        reason.contains(QStringLiteral("Timed out waiting for pulse-board response."),
                                        Qt::CaseInsensitive);
                    if (pulseResponseTimeout) {
                        setStatusMessage(QStringLiteral("状态: 脉冲板应答超时，但已继续等待首帧确认硬件触发是否生效"),
                                         UiStatusLevel::Warning);
                        scheduleHardwareTriggerStartupCheck();
                        return;
                    }
                    m_cameraManager->stopAll();
                    updateCaptureState(CaptureState::Idle);
                    setStatusMessage(reason.isEmpty()
                                         ? QStringLiteral("状态: 全画幅低频触发启动失败")
                                         : reason,
                                     UiStatusLevel::Error);
                    QMessageBox::warning(this,
                                         QStringLiteral("开始采集"),
                                         reason.isEmpty()
                                             ? QStringLiteral("全画幅低频触发启动失败。")
                                             : reason);
                    return;
                }
                setStatusMessage(m_pulseGeneratorEnabled
                                     ? QStringLiteral("状态: 硬件触发已就绪，正在以 2Hz 低频脉冲进行双相机全画幅定位")
                                     : QStringLiteral("状态: 硬件触发已就绪，请输出低频脉冲进行双相机全画幅定位"),
                                 m_pulseGeneratorEnabled ? UiStatusLevel::Success : UiStatusLevel::Warning);
            }
            scheduleHardwareTriggerStartupCheck();
        }
        return;
    }

    updateCaptureState(CaptureState::Idle);
    setStatusMessage(reason.isEmpty() ? QStringLiteral("状态: 启动采集失败") : reason, UiStatusLevel::Error);
}

void DIMM::onStartSimulation()
{
    if (m_captureState == CaptureState::Simulation) {
        stopSimulationCapture();
        m_reporting = false;
        if (m_reportTimer) {
            m_reportTimer->stop();
        }
        updateCaptureState(CaptureState::Idle);
        setStatusMessage(QStringLiteral("状态: 模拟采集已停止"), UiStatusLevel::Warning);
        return;
    }

    if (m_captureState == CaptureState::Live) {
        stopLiveCapture();
    }

    m_reporting = false;
    if (m_reportTimer) {
        m_reportTimer->stop();
    }

    if (startSimulationCapture()) {
        updateCaptureState(CaptureState::Simulation);
        setDetailViewMode(DetailViewMode::RoiOnly);
        setStatusMessage(QStringLiteral("状态: 模拟采集中"), UiStatusLevel::Info);
        return;
    }

    updateCaptureState(CaptureState::Idle);
    setStatusMessage(QStringLiteral("状态: 启动模拟采集失败"), UiStatusLevel::Error);
}

void DIMM::onStopCapture()
{
    if (m_captureState == CaptureState::Alignment) {
        stopAlignmentMode();
        return;
    }

    stopLiveCapture();
    stopSimulationCapture();
    m_reporting = false;
    if (m_reportTimer) {
        m_reportTimer->stop();
    }
    closeResultFile();
    updateCaptureState(CaptureState::Idle);
    setStatusMessage(QStringLiteral("状态: 已停止"), UiStatusLevel::Error);
    resetMeasurementState();
    if (m_fullFrameCanvas1) {
        m_fullFrameCanvas1->clear();
    }
    if (m_fullFrameCanvas2) {
        m_fullFrameCanvas2->clear();
    }
    m_cam1RoiCanvas->clear();
    m_cam2RoiCanvas->clear();
}

void DIMM::onShowMainPage()
{
    ui->stackedWidget->setCurrentIndex(0);
    ui->btnFullFrame->setChecked(true);
    if (ui->btnROI) {
        ui->btnROI->setChecked(false);
    }
}

void DIMM::onShowRoiPage()
{
    onShowMainPage();
}

void DIMM::onShowSettings()
{
    if (!isSettingsApplyAllowed()) {
        QMessageBox::information(this,
                                 QStringLiteral("设置"),
                                 QStringLiteral("相机连接流程进行中，请等待完成后再修改设置。"));
        return;
    }

    m_settingsDialog->exposureEdit->setText(QString::number(m_configExposureUs, 'f', 0));
    m_settingsDialog->gainEdit->setText(QString::number(m_configGainDb, 'f', 1));
    m_settingsDialog->continuousFrameRateEdit->setText(
        QString::number(m_configContinuousFrameRateHz, 'f', 1));
    m_settingsDialog->triggerContinuous->setChecked(m_configTriggerMode == 0);
    m_settingsDialog->triggerHardware->setChecked(m_configTriggerMode != 0);
    m_settingsDialog->autoExposureCheck->setChecked(m_autoExposureConfig.enabled);
    m_settingsDialog->autoExpUseFittedPeakCheck->setChecked(m_autoExposureConfig.useFittedPeak);
    m_settingsDialog->autoExpTargetPeakLowEdit->setText(QString::number(m_autoExposureConfig.targetPeakLowDn, 'f', 1));
    m_settingsDialog->autoExpTargetPeakHighEdit->setText(QString::number(m_autoExposureConfig.targetPeakHighDn, 'f', 1));
    m_settingsDialog->autoExpNearSaturationEdit->setText(QString::number(m_autoExposureConfig.nearSaturationDn, 'f', 1));
    m_settingsDialog->autoExpHardSaturationEdit->setText(QString::number(m_autoExposureConfig.hardSaturationDn, 'f', 1));
    m_settingsDialog->autoExpSaturatedPixelCountEdit->setText(QString::number(m_autoExposureConfig.saturatedPixelCount));
    m_settingsDialog->autoExpDarkSnrWarningEdit->setText(QString::number(m_autoExposureConfig.darkSnrWarning, 'f', 2));
    m_settingsDialog->autoExpDarkSnrCriticalEdit->setText(QString::number(m_autoExposureConfig.darkSnrCritical, 'f', 2));
    m_settingsDialog->autoExpMinValidCentroidRatioEdit->setText(QString::number(m_autoExposureConfig.minValidCentroidRatio, 'f', 2));
    m_settingsDialog->autoExpStarLostValidRatioEdit->setText(QString::number(m_autoExposureConfig.starLostValidRatio, 'f', 2));
    m_settingsDialog->autoExpBrightFrameRatioEdit->setText(QString::number(m_autoExposureConfig.brightFrameRatioThreshold, 'f', 2));
    m_settingsDialog->autoExpDarkFrameRatioEdit->setText(QString::number(m_autoExposureConfig.darkFrameRatioThreshold, 'f', 2));
    m_settingsDialog->autoExpSampleWindowSecEdit->setText(QString::number(m_autoExposureConfig.sampleWindowSec));
    m_settingsDialog->autoExpBrightPersistenceSecEdit->setText(QString::number(m_autoExposureConfig.brightPersistenceSec));
    m_settingsDialog->autoExpDarkPersistenceSecEdit->setText(QString::number(m_autoExposureConfig.darkPersistenceSec));
    m_settingsDialog->autoExpStarLostPersistenceSecEdit->setText(QString::number(m_autoExposureConfig.starLostPersistenceSec));
    m_settingsDialog->autoExpTrendConflictPersistenceSecEdit->setText(QString::number(m_autoExposureConfig.trendConflictPersistenceSec));
    m_settingsDialog->autoExpSafePersistenceSecEdit->setText(QString::number(m_autoExposureConfig.safePersistenceSec));
    m_settingsDialog->autoExpCooldownSecEdit->setText(QString::number(m_autoExposureConfig.cooldownSec));
    m_settingsDialog->autoExpMinEdit->setText(QString::number(m_autoExposureConfig.minExposureUs, 'f', 0));
    m_settingsDialog->autoExpMaxEdit->setText(QString::number(m_autoExposureConfig.maxExposureUs, 'f', 0));
    m_settingsDialog->autoExpMaxTemplateStepEdit->setText(QString::number(m_autoExposureConfig.maxTemplateStepPerAdjust));
    m_settingsDialog->autoExpMaxChangeUpEdit->setText(QString::number(m_autoExposureConfig.maxExposureChangeRatioUp, 'f', 2));
    m_settingsDialog->autoExpMaxChangeDownEdit->setText(QString::number(m_autoExposureConfig.maxExposureChangeRatioDown, 'f', 2));
    m_settingsDialog->autoExpCameraAgreementRatioEdit->setText(QString::number(m_autoExposureConfig.cameraAgreementRatio, 'f', 2));
    m_settingsDialog->procKernelSize->setText(QString::number(m_imageProcessor->gaussianKernelSize()));
    m_settingsDialog->procSigma->setText(QString::number(m_imageProcessor->gaussianSigma(), 'f', 2));
    m_settingsDialog->procGravity->setChecked(m_imageProcessor->centroidMethod() == 0);
    m_settingsDialog->procGaussian->setChecked(m_imageProcessor->centroidMethod() != 0);
    m_settingsDialog->roiRecenterThresholdEdit->setText(
        QString::number(m_roiRecenteringThresholdPx, 'f', 1));
    m_settingsDialog->roiRecenterRequiredFramesEdit->setText(
        QString::number(m_roiRecenteringRequiredFrames));
    m_settingsDialog->roiRecenterCooldownMsEdit->setText(
        QString::number(m_roiRecenteringCooldownMs));
    m_settingsDialog->roiRecenterMinimumShiftEdit->setText(
        QString::number(m_roiRecenteringMinimumShiftPx, 'f', 1));
    const InitialStarDetectionConfig starConfig = currentInitialStarDetectionConfig();
    m_settingsDialog->starThresholdAbsoluteEdit->setText(
        QString::number(starConfig.thresholdAbsolute, 'f', 1));
    m_settingsDialog->starSigmaThresholdEdit->setText(
        QString::number(starConfig.sigmaThreshold, 'f', 2));
    m_settingsDialog->starPeakFractionEdit->setText(
        QString::number(starConfig.peakFraction, 'f', 2));
    m_settingsDialog->starMinimumIntensityEdit->setText(
        QString::number(starConfig.minimumIntensity, 'f', 1));
    m_settingsDialog->starMinAreaEdit->setText(QString::number(starConfig.minArea));
    m_settingsDialog->starMaxAreaEdit->setText(QString::number(starConfig.maxArea));
    m_settingsDialog->hotPixelEnableCheck->setChecked(m_hotPixelTemplatesEnabled);
    m_settingsDialog->hotPixelCam0MaskEdit->setText(PathUtils::relativizePathToAppDir(m_hotPixelCamera0MaskPath));
    m_settingsDialog->hotPixelCam0ExcessEdit->setText(PathUtils::relativizePathToAppDir(m_hotPixelCamera0ExcessPath));
    m_settingsDialog->hotPixelCam1MaskEdit->setText(PathUtils::relativizePathToAppDir(m_hotPixelCamera1MaskPath));
    m_settingsDialog->hotPixelCam1ExcessEdit->setText(PathUtils::relativizePathToAppDir(m_hotPixelCamera1ExcessPath));
    m_settingsDialog->hotPixelTemplateWidthEdit->setText(QString::number(m_hotPixelTemplateWidth));
    m_settingsDialog->hotPixelTemplateHeightEdit->setText(QString::number(m_hotPixelTemplateHeight));
    m_settingsDialog->opticsD->setText(QString::number(m_imageProcessor->apertureDiameterMm(), 'f', 1));
    m_settingsDialog->opticsBaseline->setText(QString::number(m_imageProcessor->baselineSeparationMm(), 'f', 1));
    m_settingsDialog->opticsBaselineAngle->setText(QString::number(m_imageProcessor->baselineAngleDeg(), 'f', 1));
    m_settingsDialog->opticsF->setText(QString::number(m_imageProcessor->focalLengthCm(), 'f', 1));
    m_settingsDialog->opticsZenith->setText(QString::number(m_imageProcessor->zenithAngleDeg(), 'f', 1));
    m_settingsDialog->detectorWavelength->setText(QString::number(m_imageProcessor->wavelengthNm(), 'f', 1));
    m_settingsDialog->detectorPixelSize->setText(QString::number(m_imageProcessor->pixelSizeUm(), 'f', 2));
    m_settingsDialog->alignmentAutoRadiusCheck->setChecked(m_alignmentAutoRadius);
    m_settingsDialog->alignmentFocalLengthEdit->setText(QString::number(m_alignmentFocalLengthMm, 'f', 1));
    m_settingsDialog->alignmentPixelSizeEdit->setText(QString::number(m_alignmentPixelSizeUm, 'f', 2));
    m_settingsDialog->alignmentPolarDistanceEdit->setText(
        QString::number(m_alignmentPolarisPolarDistanceArcmin, 'f', 1));
    m_settingsDialog->alignmentRadiusAdjustEdit->setText(
        QString::number(m_alignmentRadiusAdjustPx, 'f', 1));
    m_settingsDialog->alignmentPreviewRateEdit->setText(
        QString::number(m_alignmentPreviewRateHz, 'f', 1));
    m_settingsDialog->alignmentAutoSolveCheck->setChecked(m_alignmentAutoSolveEnabled);
    m_settingsDialog->alignmentShowMatchedCatalogStarsCheck->setChecked(
        m_alignmentShowMatchedCatalogStars);
    m_settingsDialog->alignmentMaxDetectedStarsEdit->setText(
        QString::number(m_alignmentMaxDetectedStars));
    m_settingsDialog->alignmentMinMatchedStarsEdit->setText(
        QString::number(m_alignmentMinMatchedStars));
    m_settingsDialog->alignmentMaxRmsEdit->setText(QString::number(m_alignmentMaxRmsPx, 'f', 1));
    m_settingsDialog->alignmentRetryIntervalEdit->setText(
        QString::number(m_alignmentRetryIntervalMs / 1000.0, 'f', 1));
    m_settingsDialog->storagePathEdit->setText(m_dataPath);
    m_settingsDialog->saveIntervalEdit->setText(QString::number(m_saveInterval));
    m_settingsDialog->setPulseGeneratorState(m_pulseGeneratorEnabled,
                                             m_pulseGeneratorPort,
                                             m_pulseGeneratorBaudRate,
                                             m_pulseGeneratorTerminalId,
                                             m_pulseGeneratorFrequencyHz,
                                             m_pulseGeneratorPulseCount,
                                             m_pulseGeneratorDutyPercent,
                                             m_pulseGeneratorRemoteControl);
    m_settingsDialog->netIpEdit->setText(m_commManager->remoteAddress());
    m_settingsDialog->netPortEdit->setText(QString::number(m_commManager->remotePort()));
    if (m_settingsDialog->applyStatusLabel) {
        m_settingsDialog->applyStatusLabel->setText(QStringLiteral("待应用"));
        m_settingsDialog->applyStatusLabel->setStyleSheet(statusLabelStyle(UiStatusLevel::Muted));
    }
    m_settingsDialog->exec();
}

void DIMM::onToggleRoiImages()
{
    setDetailViewMode(DetailViewMode::RoiOnly);
}

void DIMM::onToggleCharts()
{
    setDetailViewMode(DetailViewMode::ChartsOnly);
}

void DIMM::onSaveConfig()
{
    const QString file = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存配置"), QStringLiteral("config.json"), QStringLiteral("JSON 文件 (*.json)"));
    if (!file.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("保存配置"), QStringLiteral("配置导出功能将在后续版本中补充。\n目标路径: %1").arg(file));
    }
}

void DIMM::onLoadConfig()
{
    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("加载配置"), QString(), QStringLiteral("JSON 文件 (*.json)"));
    if (!file.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("加载配置"), QStringLiteral("配置导入功能将在后续版本中补充。\n目标路径: %1").arg(file));
    }
}

void DIMM::onExportData()
{
    const QString file = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出数据"), QStringLiteral("data.txt"), QStringLiteral("文本文件 (*.txt)"));
    if (file.isEmpty()) {
        return;
    }

    flushPendingWrites();

    if (m_resultFilePath.isEmpty() || !QFile::exists(m_resultFilePath)) {
        QMessageBox::warning(this,
                             QStringLiteral("导出数据"),
                             QStringLiteral("当前还没有可导出的采集结果文件，请先运行一次模拟采集。"));
        return;
    }

    QFile::remove(file);
    if (QFile::copy(m_resultFilePath, file)) {
        QMessageBox::information(this,
                                 QStringLiteral("导出数据"),
                                 QStringLiteral("结果数据已导出到:\n%1").arg(file));
    } else {
        QMessageBox::warning(this,
                             QStringLiteral("导出数据"),
                             QStringLiteral("导出失败，请检查目标路径是否可写。"));
    }
}

void DIMM::onExportReport()
{
    const QString file = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出报告"), QStringLiteral("report.pdf"), QStringLiteral("PDF 文件 (*.pdf)"));
    if (!file.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("导出报告"), QStringLiteral("报告导出功能将在后续版本中补充。\n目标路径: %1").arg(file));
    }
}

void DIMM::onConnectAll()
{
    QString reason;
    if (!canConnectOrDisconnectCameras(&reason)) {
        QMessageBox::warning(this, QStringLiteral("连接相机"), reason);
        return;
    }

    m_connectingCameras = true;
    refreshActionStates();
    setStatusMessage(QStringLiteral("正在扫描相机设备..."), UiStatusLevel::Warning);
    const auto devices = m_cameraManager->enumerateDevices();
    if (devices.isEmpty()) {
        m_connectingCameras = false;
        setStatusMessage(QStringLiteral("未发现相机"), UiStatusLevel::Error);
        refreshCameraUi();
        refreshActionStates();
        return;
    }

    const bool success = m_cameraManager->openAll();
    m_connectingCameras = false;
    refreshUi();

    if (success) {
        QString message = QStringLiteral("已连接设备:\n");
        for (int i = 0; i < devices.size(); ++i) {
            message += QStringLiteral("\n相机%1: %2 (%3) [%4]")
                           .arg(i + 1)
                           .arg(devices[i].serialNumber)
                           .arg(devices[i].modelName)
                           .arg(devices[i].ipAddress);
        }
        setStatusMessage(QStringLiteral("已连接 %1 台相机").arg(devices.size()), UiStatusLevel::Success);
        qInfo().noquote() << message;
    } else {
        setStatusMessage(QStringLiteral("部分相机连接失败"), UiStatusLevel::Error);
    }
}

void DIMM::onDisconnectAll()
{
    QString reason;
    if (!canConnectOrDisconnectCameras(&reason)) {
        QMessageBox::warning(this, QStringLiteral("断开相机"), reason);
        return;
    }

    m_connectingCameras = true;
    refreshActionStates();
    m_cameraManager->closeAll();
    m_connectingCameras = false;
    refreshUi();
    setStatusMessage(QStringLiteral("相机已断开"), UiStatusLevel::Warning);
}

void DIMM::onAbout()
{
    QMessageBox::about(this, QStringLiteral("关于 C-DIMM"),
                       QStringLiteral("<h3>C-DIMM 大气相干长度测量系统</h3>"
                                      "<p>版本: v1.0</p>"
                                      "<ul>"
                                      "<li>双相机同步采集</li>"
                                      "<li>实时质心计算</li>"
                                      "<li>大气参数反演 (r0 / seeing / theta0 / tau0)</li>"
                                      "<li>结果记录与通信上报</li>"
                                      "</ul>"));
}

void DIMM::onUpdateSimulation()
{
    if (m_captureState != CaptureState::Simulation) {
        return;
    }

    auto& runtime = activeRuntime();
    ++runtime.simulationFrameIndex;
    ++runtime.frameCount;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const int previewIntervalFrames = std::max(1, kSimulationPreviewIntervalMs / kSimulationFrameIntervalMs);
    const bool shouldRefreshPreview =
        runtime.lastSimulationPreviewFrame < 0 ||
        (runtime.simulationFrameIndex - runtime.lastSimulationPreviewFrame) >= previewIntervalFrames;
    cv::Mat previewFrame0;
    cv::Mat previewFrame1;
    if (shouldRefreshPreview) {
        previewFrame0 = buildSimulationFrame(0);
        previewFrame1 = buildSimulationFrame(1);
        runtime.frameSize[0] = QSize(previewFrame0.cols, previewFrame0.rows);
        runtime.frameSize[1] = QSize(previewFrame1.cols, previewFrame1.rows);

        if (m_fullFrameCanvas1) {
            QVector<RoiRect> rois0;
            if (m_imageProcessor) {
                rois0.append(m_imageProcessor->getCurrentRoi(0));
            }
            m_fullFrameCanvas1->setImage(previewFrame0);
            m_fullFrameCanvas1->setRoiList(rois0);
            m_fullFrameCanvas1->setCurrentRoi(rois0.isEmpty() ? -1 : 0);
        }
        if (m_fullFrameCanvas2) {
            QVector<RoiRect> rois1;
            if (m_imageProcessor) {
                rois1.append(m_imageProcessor->getCurrentRoi(1));
            }
            m_fullFrameCanvas2->setImage(previewFrame1);
            m_fullFrameCanvas2->setRoiList(rois1);
            m_fullFrameCanvas2->setCurrentRoi(rois1.isEmpty() ? -1 : 0);
        }
        runtime.lastSimulationPreviewFrame = runtime.simulationFrameIndex;
    }

    if (m_imageProcessor) {
        for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
            cv::Mat simulationFrame =
                (cameraIndex == 0 && !previewFrame0.empty()) ? previewFrame0
                : (cameraIndex == 1 && !previewFrame1.empty()) ? previewFrame1
                : buildSimulationFrame(cameraIndex);
            runtime.frameSize[cameraIndex] =
                QSize(simulationFrame.cols, simulationFrame.rows);
            m_imageProcessor->processFrame(cameraIndex, simulationFrame);
        }
    }

    const bool shouldRefreshMeasurementUi =
        runtime.lastMeasurementUiUpdateMs < 0 ||
        (nowMs - runtime.lastMeasurementUiUpdateMs) >= kMeasurementUiIntervalMs;
    if (shouldRefreshMeasurementUi) {
        runtime.lastMeasurementUiUpdateMs = nowMs;
        refreshMeasurementUi();
    }
}

void DIMM::updateParams()
{
    auto& runtime = activeRuntime();
    runtime.latestAtmosphere.r0 = 11.0 + QRandomGenerator::global()->generateDouble() * 3.0;
    runtime.latestAtmosphere.seeing = 0.98 * 0.55 / (runtime.latestAtmosphere.r0 / 100.0) * 206265.0 / 1000.0;
    runtime.latestAtmosphere.theta0 = 4.0 + QRandomGenerator::global()->generateDouble() * 3.0;
    runtime.latestAtmosphere.tau0 = 6.0 + QRandomGenerator::global()->generateDouble() * 4.0;
    runtime.hasValidAtmosphere = true;
    refreshMeasurementUi();
}

void DIMM::onFrameReady(int cameraIndex)
{
    const CameraFrame packet = m_cameraManager ? m_cameraManager->takeLatestFramePacket(cameraIndex) : CameraFrame();
    if (m_captureState == CaptureState::Alignment) {
        handleAlignmentFramePacket(cameraIndex, packet);
        return;
    }
    handleLiveFramePacket(cameraIndex, packet);
}

void DIMM::onCapturedFramePacket(int cameraIndex, CameraFrame packet)
{
    if (m_captureState == CaptureState::Alignment) {
        handleAlignmentFramePacket(cameraIndex, packet);
        return;
    }
    handleLiveFramePacket(cameraIndex, packet);
}

void DIMM::handleLiveFramePacket(int cameraIndex, const CameraFrame& packet)
{
    const cv::Mat frame = packet.image;
    if (frame.empty() || m_captureState != CaptureState::Live) {
        return;
    }
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    handleLiveRelocalizationWatchdog(nowMs);
    const qint64 frameReceivedMs =
        packet.receivedMs > 0 ? packet.receivedMs : nowMs;
    if (packet.receivedMs > 0 && packet.receivedMs < m_liveFrameAcceptAfterMs) {
        return;
    }
    if (cameraIndex >= 0 && cameraIndex < 2 &&
        packet.frameId > 0 && packet.frameId <= m_lastAcceptedLiveFrameId[cameraIndex]) {
        return;
    }
    if (cameraIndex >= 0 && cameraIndex < 2 && m_configTriggerMode == 0) {
        const qint64 continuousFrameIntervalMs =
            qMax<qint64>(1, static_cast<qint64>(std::llround(1000.0 / std::max(0.1, m_configContinuousFrameRateHz))));
        const qint64 lastAcceptedMs = m_lastAcceptedContinuousFrameMs[cameraIndex];
        if (lastAcceptedMs >= 0 && (frameReceivedMs - lastAcceptedMs) < continuousFrameIntervalMs) {
            return;
        }
        m_lastAcceptedContinuousFrameMs[cameraIndex] = frameReceivedMs;
    }

    auto& runtime = activeRuntime();
    const bool frameLooksLikeHardwareRoi =
        frame.cols <= kFixedRoiSize && frame.rows <= kFixedRoiSize;
    if (cameraIndex >= 0 && cameraIndex < 2) {
        // Keep the full-frame geometry once live hardware ROI tracking starts. The centroid
        // pipeline reports absolute coordinates, so shrinking the runtime frame size to 64x64
        // would incorrectly reject otherwise valid centroids as "out of bounds".
        if (!(m_liveHardwareRoiActive && frameLooksLikeHardwareRoi)) {
            runtime.frameSize[cameraIndex] = QSize(frame.cols, frame.rows);
        }
        if (packet.frameId > 0) {
            m_lastAcceptedLiveFrameId[cameraIndex] = packet.frameId;
        }
        ++runtime.frameCountPerCamera[cameraIndex];
    }

    ++runtime.frameCount;
    if (m_configTriggerMode != 0 &&
        (m_statusText.contains(QStringLiteral("Timed out waiting for pulse-board response."),
                               Qt::CaseInsensitive) ||
         m_statusText.contains(QStringLiteral("脉冲板应答超时")))) {
        setStatusMessage(QStringLiteral("状态: 已收到硬件触发图像帧，脉冲板未返回串口应答但采集继续"),
                         UiStatusLevel::Warning);
    }
    if (runtime.frameCount == 1 && m_liveStartupPhase == LiveStartupPhase::Tracking) {
        setStatusMessage(QStringLiteral("状态: 实时采集中，已收到图像帧，预览按30秒刷新"),
                         UiStatusLevel::Success);
    }
    maybeSeedRoiFromFrame(cameraIndex, frame);

    if (cameraIndex >= 0 && cameraIndex < 2) {
        QVector<RoiRect> rois;
        const bool showConfirmedRoiOverlay =
            m_captureState != CaptureState::Live || runtime.initialRoiConfirmed[cameraIndex];
        if (showConfirmedRoiOverlay) {
            rois.append(m_imageProcessor->getCurrentRoi(cameraIndex));
        } else {
            rois.clear();
        }
        const bool shouldRefreshPreview =
            runtime.lastLivePreviewUpdateMs[cameraIndex] < 0 ||
            (nowMs - runtime.lastLivePreviewUpdateMs[cameraIndex]) >= kSimulationPreviewIntervalMs;
        FullFrameCanvas* targetCanvas = cameraIndex == 0 ? m_fullFrameCanvas1 : m_fullFrameCanvas2;
        const bool canUpdateFullFramePreview =
            m_captureState == CaptureState::Live
                ? (!frameLooksLikeHardwareRoi &&
                   m_liveStartupPhase == LiveStartupPhase::Tracking)
                : true;
        if (targetCanvas && canUpdateFullFramePreview && shouldRefreshPreview) {
            targetCanvas->setImage(frame);
            runtime.lastLivePreviewUpdateMs[cameraIndex] = nowMs;
            updateFullFrameRoiOverlay(cameraIndex);
        }
    }

    const bool roiConfirmed =
        cameraIndex >= 0 && cameraIndex < 2 && runtime.initialRoiConfirmed[cameraIndex];
    const bool roiAvailableForThisCamera =
        cameraIndex >= 0 && cameraIndex < 2 &&
        (roiConfirmed || (m_liveHardwareRoiActive && frameLooksLikeHardwareRoi));
    if (roiAvailableForThisCamera) {
        const RoiRect processingRoi = m_imageProcessor->getCurrentRoi(cameraIndex);
        const cv::Mat processingFrame = cropFrameForRoiProcessing(frame, processingRoi);
        if (!processingFrame.empty()) {
            m_imageProcessor->processFrame(cameraIndex,
                                           processingFrame,
                                           packet.frameId,
                                           packet.cameraTimestamp,
                                           m_liveAcquisitionGeneration);
        }
    }
    const bool shouldRefreshMeasurementUi =
        runtime.lastMeasurementUiUpdateMs < 0 ||
        (nowMs - runtime.lastMeasurementUiUpdateMs) >= kMeasurementUiIntervalMs;
    if (shouldRefreshMeasurementUi) {
        runtime.lastMeasurementUiUpdateMs = nowMs;
        refreshMeasurementUi();
    }
}

void DIMM::scheduleHardwareTriggerStartupCheck()
{
    if (!m_hardwareTriggerStartupTimer) {
        return;
    }
    m_hardwareTriggerStartupTimer->start(2500);
}

void DIMM::checkHardwareTriggerStartup()
{
    if (m_captureState != CaptureState::Live || m_configTriggerMode == 0) {
        return;
    }

    const auto& runtime = activeRuntime();
    const bool cam1Ready = runtime.frameCountPerCamera[0] > 0;
    const bool cam2Ready = runtime.frameCountPerCamera[1] > 0;
    if (cam1Ready && cam2Ready) {
        return;
    }

    QString detail;
    if (!cam1Ready && !cam2Ready) {
        detail = QStringLiteral("两台相机在启动后的 2.5 秒内都没有收到首帧。请优先检查触发线、TriggerSource(Line0)、脉冲是否已实际输出，以及脉冲是否发生在相机进入等待态之后。");
    } else if (!cam1Ready) {
        detail = QStringLiteral("只有相机2收到首帧，相机1仍未触发。请检查相机1对应的触发接线、网口带宽和硬件触发输入。");
    } else {
        detail = QStringLiteral("只有相机1收到首帧，相机2仍未触发。请检查相机2对应的触发接线、网口带宽和硬件触发输入。");
    }

    setStatusMessage(QStringLiteral("硬件触发首帧超时: %1").arg(detail), UiStatusLevel::Warning);
}

void DIMM::onCameraConnected(int index, QString serial, QString model)
{
    Q_UNUSED(index);
    Q_UNUSED(model);
    m_connectingCameras = false;
    refreshCameraUi();
    refreshActionStates();
    setStatusMessage(QStringLiteral("相机已连接: %1").arg(serial), UiStatusLevel::Success);
}

void DIMM::onCameraDisconnected(int index)
{
    Q_UNUSED(index);
    m_connectingCameras = false;
    refreshCameraUi();
    refreshActionStates();
    if (!hasAnyOpenCamera() && m_captureState == CaptureState::Live) {
        updateCaptureState(CaptureState::Paused);
        setStatusMessage(QStringLiteral("相机断开，采集已暂停"), UiStatusLevel::Warning);
    } else {
        setStatusMessage(QStringLiteral("相机已断开"), UiStatusLevel::Warning);
    }
}

void DIMM::onCameraError(int index, int errorCode, QString message)
{
    Q_UNUSED(errorCode);
    m_connectingCameras = false;
    refreshActionStates();
    setStatusMessage(QStringLiteral("相机%1错误: %2").arg(index + 1).arg(message), UiStatusLevel::Error);
    QTimer::singleShot(5000, this, [this]() {
        if (!hasActiveCapture()) {
            setStatusMessage(QStringLiteral("状态: 就绪"), UiStatusLevel::Muted);
        }
    });
}

void DIMM::updateCameraInfo()
{
    for (int i = 0; i < 2; ++i) {
        auto* infoLabel = i == 0 ? ui->lblCam1Info : ui->lblCam2Info;
        if (!m_cameraManager->isOpen(i)) {
            infoLabel->setText(QStringLiteral("序列号: 未连接\n帧率: -- fps | 温度: --°C"));
            continue;
        }

        const double fps = m_cameraManager->getFrameRate(i);
        const double temp = m_cameraManager->getTemperature(i);
        infoLabel->setText(QStringLiteral("序列号: %1\n帧率: %2 fps | 温度: %3°C")
                               .arg(m_cameraManager->getSerialNumber(i))
                               .arg(fps, 0, 'f', 0)
                               .arg(temp, 0, 'f', 1));
    }
}

void DIMM::updateCurrentRoi()
{
    updateMinuteRoi(true);
}

void DIMM::initResultFile()
{
    if (m_resultWriter.isOpen()) {
        return;
    }

    QDir rootDir(m_dataPath);
    if (!rootDir.exists()) {
        rootDir.mkpath(QStringLiteral("."));
    }

    const QString modeDirPath = rootDir.filePath(resultSubdirectoryName());
    QDir modeDir(modeDirPath);
    if (!modeDir.exists()) {
        rootDir.mkpath(resultSubdirectoryName());
    }

    const QString filename = QStringLiteral("%1/DIMM_%2_measurements_%3.txt")
                                 .arg(modeDirPath,
                                      captureModeName(),
                                      QDateTime::currentDateTime().toString(
                                          QStringLiteral("yyyy-MM-dd_HHmmss")));
    m_resultFileState = m_captureState;
    ResultFileConfig config;
    config.filePath = filename;
    config.headerLine =
        QStringLiteral("# capture_mode=%1, capture_label=%2\n"
                       "timestamp,mode,frame,paired_samples,dropped_unpaired_samples,"
                       "roi_acquisition_generation,roi_update_count,roi_update_reason,"
                       "roi1_x,roi1_y,roi1_w,roi1_h,roi2_x,roi2_y,roi2_w,roi2_h,ms_since_last_roi_update,"
                       "continuous_frame_rate_target_hz,camera1_frame_rate_readback_hz,camera2_frame_rate_readback_hz,"
                       "camera1_peak_dn,camera2_peak_dn,camera1_snr,camera2_snr,"
                       "camera1_valid_ratio,camera2_valid_ratio,exposure_us,hot_pixel_template_exposure_us,"
                       "ae_enabled,ae_state,ae_reason,ae_sequence_id,ae_target_exposure_us,ae_frames_since_adjust,"
                       "r0_cm,seeing_arcsec,theta0_arcsec,tau0_ms,"
                       "sync_residual_us,sync_jitter_us,sync_jitter_avg_us,sync_jitter_max_us,"
                       "comm_connected,reporting_enabled")
            .arg(captureModeName(), captureModeLabel());
    QString error;
    if (m_resultWriter.open(config, &error)) {
        m_resultFilePath = filename;
    } else {
        setStatusMessage(QStringLiteral("结果文件创建失败"), UiStatusLevel::Error);
    }
}

void DIMM::closeResultFile()
{
    m_resultWriter.close();
    m_resultFileState = CaptureState::Idle;
}

void DIMM::saveResultRow(int frame)
{
    ++m_resultRowsSeen;
    const int interval = qMax(1, m_saveInterval);
    if ((m_resultRowsSeen - 1) % interval != 0) {
        return;
    }

    if (!m_resultWriter.isOpen()) {
        initResultFile();
    }
    if (!m_resultWriter.isOpen()) {
        return;
    }
    if (m_resultFileState != m_captureState) {
        closeResultFile();
        initResultFile();
        if (!m_resultWriter.isOpen()) {
            return;
        }
    }

    const auto& runtime = activeRuntime();
    RoiRect currentRois[2];
    if (m_imageProcessor) {
        currentRois[0] = m_imageProcessor->getCurrentRoi(0);
        currentRois[1] = m_imageProcessor->getCurrentRoi(1);
    }
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 msSinceLastRoiUpdate = m_lastRoiUpdateMs >= 0 ? nowMs - m_lastRoiUpdateMs : -1;
    const QString roiUpdateReason = csvSafeField(m_lastRoiUpdateReason);

    const QStringList fields = {
        QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
        captureModeName(),
        QString::number(frame),
        QString::number(runtime.pairedSampleCount),
        QString::number(runtime.droppedUnpairedSampleCount),
        QString::number(m_liveAcquisitionGeneration),
        QString::number(m_roiUpdateCount),
        roiUpdateReason,
        QString::number(currentRois[0].x),
        QString::number(currentRois[0].y),
        QString::number(currentRois[0].w),
        QString::number(currentRois[0].h),
        QString::number(currentRois[1].x),
        QString::number(currentRois[1].y),
        QString::number(currentRois[1].w),
        QString::number(currentRois[1].h),
        QString::number(msSinceLastRoiUpdate),
        QString::number(m_configContinuousFrameRateHz, 'f', 3),
        QString::number(m_lastContinuousFrameRateReadback[0], 'f', 3),
        QString::number(m_lastContinuousFrameRateReadback[1], 'f', 3),
        QString::number(m_latestAutoExposurePeakDn[0], 'f', 1),
        QString::number(m_latestAutoExposurePeakDn[1], 'f', 1),
        QString::number(m_latestAutoExposureSnr[0], 'f', 2),
        QString::number(m_latestAutoExposureSnr[1], 'f', 2),
        QString::number(m_latestAutoExposureValidRatio[0], 'f', 3),
        QString::number(m_latestAutoExposureValidRatio[1], 'f', 3),
        QString::number(m_configExposureUs, 'f', 0),
        QString::number(m_hotPixelTemplateExposureUs),
        m_autoExposureConfig.enabled ? QStringLiteral("1") : QStringLiteral("0"),
        autoExposureStateName(m_autoExposureState),
        csvSafeField(m_autoExposureReason),
        QString::number(m_autoExposureSequenceId),
        QString::number(m_autoExposureTargetExposureUs),
        QString::number(m_autoExposureFramesSinceAdjust),
        QString::number(runtime.latestAtmosphere.r0, 'f', 3),
        QString::number(runtime.latestAtmosphere.seeing, 'f', 3),
        QString::number(runtime.latestAtmosphere.theta0, 'f', 3),
        QString::number(runtime.latestAtmosphere.tau0, 'f', 3),
        QString::number(runtime.latestSyncResidualUs, 'f', 3),
        QString::number(runtime.latestSyncJitterUs, 'f', 3),
        QString::number(runtime.averageSyncJitterUs, 'f', 3),
        QString::number(runtime.maxSyncJitterUs, 'f', 3),
        m_commConnected ? QStringLiteral("1") : QStringLiteral("0"),
        m_reporting ? QStringLiteral("1") : QStringLiteral("0")
    };

    m_resultWriter.enqueue(MeasurementRecord{fields});
}

void DIMM::flushPendingWrites()
{
    m_resultWriter.flush();
}

bool DIMM::stopLiveCapture()
{
    if (m_captureState != CaptureState::Live) {
        return true;
    }

    if (m_hardwareTriggerStartupTimer) {
        m_hardwareTriggerStartupTimer->stop();
    }
    m_liveHardwareRoiActive = false;
    m_liveStartupPhase = LiveStartupPhase::None;
    if (m_pulseGenerator && m_pulseGenerator->isRunning()) {
        m_pulseGenerator->stop();
    }
    m_cameraManager->stopAll();
    return true;
}

void DIMM::stopSimulationCapture()
{
    if (m_simulationTimer) {
        m_simulationTimer->stop();
    }
}

bool DIMM::startSimulationCapture()
{
    stopSimulationCapture();
    closeResultFile();
    resetMeasurementState();
    auto& runtime = runtimeForState(CaptureState::Simulation);
    runtime.simulationFrameIndex = 0;
    runtime.lastSimulationPreviewFrame = -1;
    runtime.frameSize[0] = QSize(kSimulationFrameSize, kSimulationFrameSize);
    runtime.frameSize[1] = QSize(kSimulationFrameSize, kSimulationFrameSize);
    onUpdateSimulation();
    if (m_simulationTimer) {
        m_simulationTimer->start();
    }
    return true;
}

cv::Mat DIMM::buildSimulationFrame(int cameraIndex) const
{
    cv::Mat frame(kSimulationFrameSize, kSimulationFrameSize, CV_8UC1, cv::Scalar(6));

    const double timeSeconds =
        (static_cast<double>(runtimeForState(CaptureState::Simulation).simulationFrameIndex) *
         kSimulationFrameIntervalMs) /
        1000.0;
    const double baseX = kSimulationFrameSize * 0.5;
    const double baseY = kSimulationFrameSize * 0.5;

    constexpr double kBeijingLatitudeDeg = 39.9042;
    constexpr double kPolarisDeclinationDeg = 89.366;
    constexpr double kSiderealDaySeconds = 86164.0905;
    constexpr double kSimulationPixelSizeM = 2.5e-6;
    constexpr double kSimulationFocalLengthM = 2.69;
    const double pixelPerRadian = kSimulationFocalLengthM / kSimulationPixelSizeM;
    const double latitudeRad = kBeijingLatitudeDeg * kPi / 180.0;
    const double declinationRad = kPolarisDeclinationDeg * kPi / 180.0;
    const double startHourAngle = -0.35;
    const double hourAngle = startHourAngle + (2.0 * kPi * timeSeconds / kSiderealDaySeconds);

    const auto projectedPolaris = [&](double h) {
        const double east = std::cos(declinationRad) * std::sin(h);
        const double north =
            std::cos(latitudeRad) * std::sin(declinationRad) -
            std::sin(latitudeRad) * std::cos(declinationRad) * std::cos(h);
        return QPointF(east, north);
    };

    const QPointF startProjection = projectedPolaris(startHourAngle);
    const QPointF currentProjection = projectedPolaris(hourAngle);
    const double skyMotionX = (currentProjection.x() - startProjection.x()) * pixelPerRadian;
    const double skyMotionY = -(currentProjection.y() - startProjection.y()) * pixelPerRadian;

    const double commonJitterX = 0.35 * std::sin((2.0 * kPi / 6.0) * timeSeconds + 0.2);
    const double commonJitterY = 0.30 * std::cos((2.0 * kPi / 7.5) * timeSeconds + 0.5);

    const int frameIndex = runtimeForState(CaptureState::Simulation).simulationFrameIndex;
    const double slowDifferentialX = 0.35 * std::sin((2.0 * kPi / 5.5) * timeSeconds + 0.8);
    const double slowDifferentialY = 0.35 * std::cos((2.0 * kPi / 6.3) * timeSeconds + 1.1);
    const double seeingNoiseX =
        0.95 * deterministicUnitNoise(frameIndex, 11) +
        0.45 * deterministicUnitNoise(frameIndex / 3, 17) +
        slowDifferentialX;
    const double seeingNoiseY =
        1.10 * deterministicUnitNoise(frameIndex, 23) +
        0.50 * deterministicUnitNoise(frameIndex / 3, 29) +
        slowDifferentialY;
    const double differentialSign = cameraIndex == 0 ? -0.5 : 0.5;
    const double differentialX = differentialSign * seeingNoiseX;
    const double differentialY = differentialSign * seeingNoiseY;

    const double centerX = baseX + skyMotionX + commonJitterX + differentialX;
    const double centerY = baseY + skyMotionY + commonJitterY + differentialY;
    const double amplitude = 220.0 + 10.0 * std::sin((2.0 * kPi / 20.0) * timeSeconds + cameraIndex * 0.4);

    auto stampSpot = [&frame](double cx, double cy, double peak, double sigma) {
        const int minX = qMax(0, static_cast<int>(std::floor(cx - 4.0 * sigma)));
        const int maxX = qMin(frame.cols - 1, static_cast<int>(std::ceil(cx + 4.0 * sigma)));
        const int minY = qMax(0, static_cast<int>(std::floor(cy - 4.0 * sigma)));
        const int maxY = qMin(frame.rows - 1, static_cast<int>(std::ceil(cy + 4.0 * sigma)));

        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                const double dx = x - cx;
                const double dy = y - cy;
                const double value = peak * std::exp(-(dx * dx + dy * dy) / (2.0 * sigma * sigma));
                const int blended = qBound(0, static_cast<int>(frame.at<uchar>(y, x) + value), 255);
                frame.at<uchar>(y, x) = static_cast<uchar>(blended);
            }
        }
    };

    // Match the real sensor view better: a compact star core with a soft halo.
    stampSpot(centerX, centerY, amplitude, 2.4);
    stampSpot(centerX, centerY, amplitude * 0.18, 5.4);

    return frame;
}

void DIMM::on1hzTick()
{
    updateCameraInfo();
    auto& runtime = activeRuntime();
    const QTime now = QTime::currentTime();
    const int minuteKey = now.hour() * 60 + now.minute();
    const int second = now.second();

    if (minuteKey != runtime.chartMinuteKey) {
        runtime.chartMinuteKey = minuteKey;
        runtime.chartSecond = -1;
        if (m_r0Chart) {
            m_r0Chart->clear();
        }
        if (m_seeingChart) {
            m_seeingChart->clear();
        }
    }

    if (runtime.hasValidAtmosphere && second != runtime.chartSecond) {
        runtime.chartSecond = second;
        if (m_r0Chart) {
            m_r0Chart->setSecondValue(second, runtime.latestAtmosphere.r0);
        }
        if (m_seeingChart) {
            m_seeingChart->setSecondValue(second, runtime.latestAtmosphere.seeing);
        }
    }

}

void DIMM::matchRoiTimeSlot()
{
    ui->lblROITimeCurrent->setText(
        hasValidCentroidsForRoiUpdate()
            ? QStringLiteral("已具备独立 ROI 刷新条件")
            : QStringLiteral("等待两路有效质心"));
    ui->lblROITimeNext->setText(QStringLiteral("ROI 固定尺寸: 64 x 64"));
}

void DIMM::onCommCommand(uint8_t cmd)
{
    using namespace CommProtocol;

    switch (cmd) {
    case CMD_START_REPORT:
        if (!isLiveCaptureActive()) {
            m_commManager->sendAck(CMD_START_REPORT, 1);
            m_reporting = false;
            if (m_reportTimer) {
                m_reportTimer->stop();
            }
            setStatusMessage(QStringLiteral("当前为模拟/空闲模式，已拒绝上报请求"), UiStatusLevel::Warning);
            refreshStatusUi();
            return;
        }
        m_commManager->sendAck(CMD_START_REPORT, 0);
        m_reporting = true;
        m_reportTimer->start();
        setStatusMessage(QStringLiteral("上位机请求开始上报"), UiStatusLevel::Success);
        break;
    case CMD_STOP_REPORT:
        m_commManager->sendAck(CMD_STOP_REPORT, 0);
        m_reporting = false;
        m_reportTimer->stop();
        setStatusMessage(QStringLiteral("上位机请求停止上报"), UiStatusLevel::Warning);
        break;
    case CMD_QUERY_STATUS:
        reportDeviceStatus();
        break;
    default:
        qDebug() << "[DIMM] Unknown command:" << QString::number(cmd, 16);
        break;
    }
    refreshStatusUi();
}

void DIMM::reportMeasurement()
{
    if (!canReportMeasurements()) {
        return;
    }

    const auto& runtime = activeRuntime();
    const RoiRect roi0 = m_imageProcessor ? m_imageProcessor->getCurrentRoi(0) : RoiRect();
    const RoiRect roi1 = m_imageProcessor ? m_imageProcessor->getCurrentRoi(1) : RoiRect();
    m_commManager->sendMeasurement(runtime.latestAtmosphere.r0,
                                   runtime.latestAtmosphere.seeing,
                                   runtime.latestAtmosphere.theta0,
                                   runtime.latestAtmosphere.tau0,
                                   runtime.centroidX[0],
                                   runtime.centroidY[0],
                                   runtime.centroidX[1],
                                   runtime.centroidY[1],
                                   runtime.peakBrightness[0],
                                   runtime.peakBrightness[1],
                                   roi0.x,
                                   roi0.y,
                                   roi0.w,
                                   roi0.h,
                                   roi1.x,
                                   roi1.y,
                                   roi1.w,
                                   roi1.h,
                                   static_cast<uint32_t>(runtime.frameCount));
}

void DIMM::reportDeviceStatus()
{
    if (!m_commConnected || !isLiveCaptureActive()) {
        return;
    }

    float temp = 0.0f;
    float fps = 0.0f;
    const bool cam0Connected = m_cameraManager->isOpen(0);
    const bool cam1Connected = m_cameraManager->isOpen(1);
    if (cam0Connected) {
        temp = static_cast<float>(m_cameraManager->getTemperature(0));
        fps = static_cast<float>(m_cameraManager->getFrameRate(0));
    }

    const uint32_t uptimeMs = static_cast<uint32_t>(QDateTime::currentMSecsSinceEpoch()) - m_startTimeMs;
    m_commManager->sendDeviceStatus(temp,
                                    fps,
                                    cam0Connected,
                                    cam1Connected,
                                    hasActiveCapture(),
                                    uptimeMs);
}

void DIMM::handleAutoExposureSample(const AutoExposureFrameSample& sample)
{
    if (!m_autoExposureConfig.enabled ||
        m_captureState != CaptureState::Live ||
        m_liveStartupPhase != LiveStartupPhase::Tracking ||
        sample.cameraIndex < 0 ||
        sample.cameraIndex >= 2) {
        return;
    }

    QVector<int> templates = scanHotPixelExposureTemplates();
    templates.erase(std::remove_if(templates.begin(),
                                   templates.end(),
                                   [this](int exposureUs) {
                                       return exposureUs < m_autoExposureConfig.minExposureUs ||
                                              exposureUs > m_autoExposureConfig.maxExposureUs;
                                   }),
                    templates.end());

    const int currentExposure = static_cast<int>(std::lround(std::max(1.0, m_configExposureUs)));
    const AutoExposureState previousState = m_autoExposureState;
    AutoExposureDecision decision =
        m_autoExposureController.addSampleAndEvaluate(sample, currentExposure, templates, sample.timestampMs);

    m_autoExposureState = decision.state;
    m_autoExposureReason = decision.reason;
    m_latestAutoExposureTrend = decision.snapshot;
    for (int i = 0; i < 2; ++i) {
        m_latestAutoExposurePeakDn[i] = decision.snapshot.camera[i].peakP50Dn;
        m_latestAutoExposureSnr[i] = decision.snapshot.camera[i].medianSnr;
        m_latestAutoExposureValidRatio[i] = decision.snapshot.camera[i].validCentroidRatio;
        m_latestAutoExposureUsableRatio[i] = decision.snapshot.camera[i].measurementUsableRatio;
    }
    if (m_autoExposureFramesSinceAdjust < std::numeric_limits<quint64>::max()) {
        ++m_autoExposureFramesSinceAdjust;
    }

    if (m_autoExposureState != previousState) {
        if (m_autoExposureState == AutoExposureState::StarLost) {
            setStatusMessage(QStringLiteral("自动曝光: WEATHER_TOO_DARK / STAR_LOST，最大曝光下仍无法稳定观测星点"),
                             UiStatusLevel::Error);
        } else if (m_autoExposureState == AutoExposureState::TrendConflict) {
            setStatusMessage(QStringLiteral("自动曝光: 两台相机亮度趋势冲突，保持当前曝光"),
                             UiStatusLevel::Warning);
        } else if (m_autoExposureReason == QStringLiteral("LOWER_EXPOSURE_UNAVAILABLE")) {
            setStatusMessage(QStringLiteral("自动曝光: ROI 已过亮，但%1")
                                 .arg(autoExposureUiStatusText() == QStringLiteral("最小曝光")
                                          ? QStringLiteral("当前已到最小曝光")
                                          : QStringLiteral("没有找到更低热像素模板")),
                             UiStatusLevel::Warning);
        }
    }

    if (!decision.shouldAdjustExposure || decision.targetExposureUs <= 0) {
        return;
    }

    const int oldExposure = currentExposure;
    QString reason;
    if (!applyExposureAndHotPixelTemplate(decision.targetExposureUs, &reason)) {
        m_autoExposureReason = reason;
        setStatusMessage(reason.isEmpty()
                             ? QStringLiteral("自动曝光: 曝光/热像素模板切换失败")
                             : reason,
                         UiStatusLevel::Error);
        return;
    }

    ++m_autoExposureSequenceId;
    m_autoExposureTargetExposureUs = decision.targetExposureUs;
    m_lastAutoExposureAdjustMs = sample.timestampMs;
    m_autoExposureFramesSinceAdjust = 0;
    setStatusMessage(QStringLiteral("自动曝光: %1 -> %2 μs，状态 %3")
                         .arg(oldExposure)
                         .arg(decision.targetExposureUs)
                         .arg(autoExposureStateName(m_autoExposureState)),
                     UiStatusLevel::Warning);
}

void DIMM::resetAutoExposureState()
{
    m_autoExposureController.configure(m_autoExposureConfig);
    m_latestAutoExposureTrend = AutoExposureTrendSnapshot();
    m_autoExposureState = AutoExposureState::Normal;
    m_autoExposureReason.clear();
    m_autoExposureTargetExposureUs = 0;
    m_lastAutoExposureAdjustMs = -1;
    m_autoExposureFramesSinceAdjust = 0;
    m_latestAutoExposurePeakDn[0] = 0.0;
    m_latestAutoExposurePeakDn[1] = 0.0;
    m_latestAutoExposureSnr[0] = 0.0;
    m_latestAutoExposureSnr[1] = 0.0;
    m_latestAutoExposureValidRatio[0] = 0.0;
    m_latestAutoExposureValidRatio[1] = 0.0;
    m_latestAutoExposureUsableRatio[0] = 0.0;
    m_latestAutoExposureUsableRatio[1] = 0.0;
}

QString DIMM::autoExposureStateName(AutoExposureState state) const
{
    switch (state) {
    case AutoExposureState::BrightWarning:
        return QStringLiteral("BRIGHT_WARNING");
    case AutoExposureState::BrightAdjusting:
        return QStringLiteral("BRIGHT_ADJUSTING");
    case AutoExposureState::DarkWarning:
        return QStringLiteral("DARK_WARNING");
    case AutoExposureState::DarkAdjusting:
        return QStringLiteral("DARK_ADJUSTING");
    case AutoExposureState::Cooldown:
        return QStringLiteral("COOLDOWN");
    case AutoExposureState::StarLost:
        return QStringLiteral("STAR_LOST");
    case AutoExposureState::TrendConflict:
        return QStringLiteral("TREND_CONFLICT");
    case AutoExposureState::Normal:
    default:
        return QStringLiteral("NORMAL");
    }
}

QString DIMM::autoExposureStateShortText(AutoExposureState state) const
{
    switch (state) {
    case AutoExposureState::BrightWarning:
        return QStringLiteral("亮警");
    case AutoExposureState::BrightAdjusting:
        return QStringLiteral("降曝");
    case AutoExposureState::DarkWarning:
        return QStringLiteral("暗警");
    case AutoExposureState::DarkAdjusting:
        return QStringLiteral("增曝");
    case AutoExposureState::Cooldown:
        return QStringLiteral("冷却");
    case AutoExposureState::StarLost:
        return QStringLiteral("丢星");
    case AutoExposureState::TrendConflict:
        return QStringLiteral("冲突");
    case AutoExposureState::Normal:
    default:
        return QStringLiteral("正常");
    }
}

QString DIMM::autoExposureUiStatusText() const
{
    if (!m_autoExposureConfig.enabled) {
        return QStringLiteral("关闭");
    }
    if (m_autoExposureReason == QStringLiteral("LOWER_EXPOSURE_UNAVAILABLE")) {
        const int currentExposure = static_cast<int>(std::lround(std::max(1.0, m_configExposureUs)));
        return currentExposure <= int(std::lround(m_autoExposureConfig.minExposureUs))
                   ? QStringLiteral("最小曝光")
                   : QStringLiteral("无低档模板");
    }
    return autoExposureStateShortText(m_autoExposureState);
}

QString DIMM::csvSafeField(QString value) const
{
    value.replace(QLatin1Char(','), QLatin1Char(';'));
    value.replace(QLatin1Char('\n'), QLatin1Char(' '));
    value.replace(QLatin1Char('\r'), QLatin1Char(' '));
    return value;
}

QVector<int> DIMM::scanHotPixelExposureTemplates() const
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    constexpr qint64 kHotPixelTemplateCacheMs = 5000;
    if (m_cachedHotPixelTemplateScanMs >= 0 &&
        nowMs - m_cachedHotPixelTemplateScanMs < kHotPixelTemplateCacheMs) {
        return m_cachedHotPixelTemplateExposures;
    }

    QVector<int> exposures;
    if (!m_hotPixelTemplatesEnabled || m_hotPixelCamera0MaskPath.isEmpty()) {
        m_cachedHotPixelTemplateExposures = exposures;
        m_cachedHotPixelTemplateScanMs = nowMs;
        return exposures;
    }

    QDir exposureDir = QFileInfo(PathUtils::resolvePathFromAppDir(m_hotPixelCamera0MaskPath)).absoluteDir();
    if (!exposureDir.cdUp()) {
        m_cachedHotPixelTemplateExposures = exposures;
        m_cachedHotPixelTemplateScanMs = nowMs;
        return exposures;
    }

    const QFileInfoList entries =
        exposureDir.entryInfoList(QStringList() << QStringLiteral("exposure_*us"),
                                  QDir::Dirs | QDir::NoDotAndDotDot,
                                  QDir::Name);
    for (const QFileInfo& entry : entries) {
        const int exposureUs = PathUtils::exposureUsFromTemplateDirName(entry.fileName());
        if (exposureUs <= 0 || exposures.contains(exposureUs)) {
            continue;
        }

        QString camera0Mask;
        QString camera0Excess;
        QString camera1Mask;
        QString camera1Excess;
        if (resolveHotPixelTemplatePathsForExposure(exposureUs,
                                                    &camera0Mask,
                                                    &camera0Excess,
                                                    &camera1Mask,
                                                    &camera1Excess)) {
            exposures.push_back(exposureUs);
        }
    }

    std::sort(exposures.begin(), exposures.end());
    m_cachedHotPixelTemplateExposures = exposures;
    m_cachedHotPixelTemplateScanMs = nowMs;
    return exposures;
}

int DIMM::selectHotPixelTemplateExposureForCurrentExposure(double currentExposure) const
{
    QVector<int> exposures = scanHotPixelExposureTemplates();
    if (exposures.isEmpty()) {
        return 0;
    }

    const int currentUs = static_cast<int>(std::lround(std::max(1.0, currentExposure)));
    const int requestedTemplateUs = currentUs <= 1000 ? 1000 : currentUs;
    for (int exposureUs : exposures) {
        if (exposureUs >= requestedTemplateUs) {
            return exposureUs;
        }
    }
    return exposures.back();
}

bool DIMM::resolveHotPixelTemplatePathsForExposure(int exposureUs,
                                                   QString* camera0Mask,
                                                   QString* camera0Excess,
                                                   QString* camera1Mask,
                                                   QString* camera1Excess) const
{
    if (exposureUs <= 0) {
        return false;
    }

    const QString cam0Mask = PathUtils::replaceTemplateExposurePath(m_hotPixelCamera0MaskPath, exposureUs);
    const QString cam0Excess = PathUtils::replaceTemplateExposurePath(m_hotPixelCamera0ExcessPath, exposureUs);
    const QString cam1Mask = PathUtils::replaceTemplateExposurePath(m_hotPixelCamera1MaskPath, exposureUs);
    const QString cam1Excess = PathUtils::replaceTemplateExposurePath(m_hotPixelCamera1ExcessPath, exposureUs);
    if (cam0Mask.isEmpty() || cam0Excess.isEmpty() || cam1Mask.isEmpty() || cam1Excess.isEmpty()) {
        return false;
    }
    if (!QFileInfo::exists(PathUtils::resolvePathFromAppDir(cam0Mask)) ||
        !QFileInfo::exists(PathUtils::resolvePathFromAppDir(cam0Excess)) ||
        !QFileInfo::exists(PathUtils::resolvePathFromAppDir(cam1Mask)) ||
        !QFileInfo::exists(PathUtils::resolvePathFromAppDir(cam1Excess))) {
        return false;
    }

    if (camera0Mask) {
        *camera0Mask = cam0Mask;
    }
    if (camera0Excess) {
        *camera0Excess = cam0Excess;
    }
    if (camera1Mask) {
        *camera1Mask = cam1Mask;
    }
    if (camera1Excess) {
        *camera1Excess = cam1Excess;
    }
    return true;
}

bool DIMM::applyExposureAndHotPixelTemplate(int exposureUs, QString* reason)
{
    QString camera0Mask;
    QString camera0Excess;
    QString camera1Mask;
    QString camera1Excess;
    if (!resolveHotPixelTemplatePathsForExposure(exposureUs,
                                                 &camera0Mask,
                                                 &camera0Excess,
                                                 &camera1Mask,
                                                 &camera1Excess)) {
        if (reason) {
            *reason = QStringLiteral("自动曝光: 缺少 %1 μs 对应的热像素模板，保持当前曝光。")
                          .arg(exposureUs);
        }
        return false;
    }

    for (int i = 0; i < 2; ++i) {
        if (m_cameraManager->isOpen(i) && !m_cameraManager->setExposure(i, exposureUs)) {
            if (reason) {
                *reason = QStringLiteral("自动曝光: 相机%1设置 %2 μs 曝光失败。")
                              .arg(i + 1)
                              .arg(exposureUs);
            }
            return false;
        }
    }

    m_configExposureUs = exposureUs;
    m_hotPixelCamera0MaskPath = PathUtils::relativizePathToAppDir(camera0Mask);
    m_hotPixelCamera0ExcessPath = PathUtils::relativizePathToAppDir(camera0Excess);
    m_hotPixelCamera1MaskPath = PathUtils::relativizePathToAppDir(camera1Mask);
    m_hotPixelCamera1ExcessPath = PathUtils::relativizePathToAppDir(camera1Excess);
    m_hotPixelTemplateExposureUs = exposureUs;
    m_cachedHotPixelTemplateExposures.clear();
    m_cachedHotPixelTemplateScanMs = -1;
    if (m_imageProcessor) {
        m_imageProcessor->configureHotPixelTemplates(PathUtils::resolvePathFromAppDir(m_hotPixelCamera0MaskPath),
                                                     PathUtils::resolvePathFromAppDir(m_hotPixelCamera0ExcessPath),
                                                     PathUtils::resolvePathFromAppDir(m_hotPixelCamera1MaskPath),
                                                     PathUtils::resolvePathFromAppDir(m_hotPixelCamera1ExcessPath),
                                                     m_hotPixelTemplateWidth,
                                                     m_hotPixelTemplateHeight);
    }
    if (m_settingsDialog) {
        m_settingsDialog->exposureEdit->setText(QString::number(m_configExposureUs, 'f', 0));
        m_settingsDialog->hotPixelCam0MaskEdit->setText(m_hotPixelCamera0MaskPath);
        m_settingsDialog->hotPixelCam0ExcessEdit->setText(m_hotPixelCamera0ExcessPath);
        m_settingsDialog->hotPixelCam1MaskEdit->setText(m_hotPixelCamera1MaskPath);
        m_settingsDialog->hotPixelCam1ExcessEdit->setText(m_hotPixelCamera1ExcessPath);
    }
    return true;
}
