#include "DIMM.h"

#include "CameraManager.h"
#include "CanvasWidgets.h"
#include "CommManager.h"
#include "ImageProcessor.h"
#include "PulseGeneratorManager.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <QAction>
#include <QApplication>
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
constexpr int kMeasurementUiIntervalMs = 100;
constexpr int kRoiEdgeUpdateMarginPx = 8;
constexpr qint64 kLostCentroidRelocalizeTimeoutMs = 1500;
constexpr qint64 kLiveRelocalizationMaxDurationMs = 15000;
constexpr int kAutoExposureDefaultIntervalMs = 4 * 60 * 60 * 1000;
constexpr int kAutoExposureMaxPeakSamples = 4096;
constexpr quint64 kSyncOffsetCalibrationSamples = 200;
constexpr double kFullFrameLocalizationPulseHz = 2.0;
constexpr double kAlignmentDefaultPolarisPolarDistanceArcmin = 37.6;
constexpr const char* kHardwareTriggerLine = "Line0";
constexpr const char* kRoiUpdateGateLine = "Line2";
constexpr double kPi = 3.14159265358979323846;

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

cv::Mat normalizeInitialStarDetectionFrame(const cv::Mat& grayscale)
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

QString stripConfigComment(QString line)
{
    const int hashPos = line.indexOf(QLatin1Char('#'));
    const int semicolonPos = line.indexOf(QLatin1Char(';'));
    int cutPos = -1;
    if (hashPos >= 0) {
        cutPos = hashPos;
    }
    if (semicolonPos >= 0) {
        cutPos = cutPos < 0 ? semicolonPos : std::min(cutPos, semicolonPos);
    }
    return cutPos >= 0 ? line.left(cutPos).trimmed() : line.trimmed();
}

QString appDeploymentDirPath()
{
    return QDir::cleanPath(QApplication::applicationDirPath());
}

QString resolvePathFromAppDir(const QString& rawPath)
{
    const QString trimmed = rawPath.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    const QFileInfo candidate(trimmed);
    if (candidate.isAbsolute()) {
        return QDir::cleanPath(candidate.absoluteFilePath());
    }

    return QDir::cleanPath(QDir(appDeploymentDirPath()).filePath(trimmed));
}

QString relativizePathToAppDir(const QString& rawPath)
{
    const QString trimmed = rawPath.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    const QFileInfo candidate(trimmed);
    if (!candidate.isAbsolute()) {
        return QDir::cleanPath(trimmed);
    }

    const QDir appDir(appDeploymentDirPath());
    const QString absolutePath = QDir::cleanPath(candidate.absoluteFilePath());
    const QString relativePath = QDir::cleanPath(appDir.relativeFilePath(absolutePath));
    if (!relativePath.startsWith(QStringLiteral(".."))) {
        return relativePath;
    }
    return absolutePath;
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

struct InitialStarDetectionConfig {
    double thresholdAbsolute = -1.0;
    double sigmaThreshold = 4.0;
    double peakFraction = 0.20;
    double minimumIntensity = 16.0;
    int minArea = 1;
    int maxArea = 1000;
};

struct InitialStarCandidate {
    int index = 0;
    QPointF center;
    int area = 0;
    double peak = 0.0;
    double signal = 0.0;
    QRect bbox;
    double distanceToPreference = std::numeric_limits<double>::infinity();
};

struct InitialStarSelection {
    bool selected = false;
    InitialStarCandidate candidate;
    bool requiresUserSelection = false;
    QString reason;
};

struct HotPixelTemplateSettings {
    bool enabled = false;
    QString camera0Mask;
    QString camera0Excess;
    QString camera1Mask;
    QString camera1Excess;
    int width = 0;
    int height = 0;
};

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
    const auto resolvePath = [&configInfo](const QString& rawPath) {
        const QString trimmed = rawPath.trimmed();
        QFileInfo candidate(trimmed);
        if (candidate.isAbsolute()) {
            return candidate.absoluteFilePath();
        }
        return QFileInfo(configInfo.absoluteDir(), trimmed).absoluteFilePath();
    };

    QTextStream input(&file);
    while (!input.atEnd()) {
        const QString line = stripConfigComment(input.readLine());
        if (line.isEmpty()) {
            continue;
        }

        const int equalPos = line.indexOf(QLatin1Char('='));
        if (equalPos <= 0) {
            continue;
        }

        const QString key = line.left(equalPos).trimmed().toLower();
        const QString valueText = line.mid(equalPos + 1).trimmed();
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
            parsed.camera0Mask = resolvePath(valueText);
        } else if (key == QStringLiteral("camera_a_hot_pixel_excess") ||
                   key == QStringLiteral("camera0_hot_pixel_excess")) {
            parsed.camera0Excess = resolvePath(valueText);
        } else if (key == QStringLiteral("camera_b_hot_pixel_mask") ||
                   key == QStringLiteral("camera1_hot_pixel_mask")) {
            parsed.camera1Mask = resolvePath(valueText);
        } else if (key == QStringLiteral("camera_b_hot_pixel_excess") ||
                   key == QStringLiteral("camera1_hot_pixel_excess")) {
            parsed.camera1Excess = resolvePath(valueText);
        }
    }

    parsed.enabled =
        parsed.width > 0 && parsed.height > 0 &&
        QFileInfo::exists(parsed.camera0Mask) &&
        QFileInfo::exists(parsed.camera0Excess) &&
        QFileInfo::exists(parsed.camera1Mask) &&
        QFileInfo::exists(parsed.camera1Excess);
    *settings = parsed;
    return parsed.enabled;
}

int exposureUsFromTemplatePath(const QString& path)
{
    const QString marker = QStringLiteral("exposure_");
    const int start = path.indexOf(marker, 0, Qt::CaseInsensitive);
    if (start < 0) {
        return 0;
    }
    const int valueStart = start + marker.size();
    const int valueEnd = path.indexOf(QStringLiteral("us"), valueStart, Qt::CaseInsensitive);
    if (valueEnd <= valueStart) {
        return 0;
    }

    bool ok = false;
    const int exposureUs = path.mid(valueStart, valueEnd - valueStart).toInt(&ok);
    return ok ? exposureUs : 0;
}

int exposureUsFromTemplateDirName(const QString& name)
{
    const QString marker = QStringLiteral("exposure_");
    if (!name.startsWith(marker, Qt::CaseInsensitive) || !name.endsWith(QStringLiteral("us"), Qt::CaseInsensitive)) {
        return 0;
    }
    bool ok = false;
    const int exposureUs = name.mid(marker.size(), name.size() - marker.size() - 2).toInt(&ok);
    return ok ? exposureUs : 0;
}

QString replaceTemplateExposurePath(const QString& path, int exposureUs)
{
    const QString marker = QStringLiteral("exposure_");
    const int start = path.indexOf(marker, 0, Qt::CaseInsensitive);
    if (start < 0) {
        return QString();
    }
    const int valueStart = start + marker.size();
    const int valueEnd = path.indexOf(QStringLiteral("us"), valueStart, Qt::CaseInsensitive);
    if (valueEnd <= valueStart) {
        return QString();
    }

    const QString exposureDir = QStringLiteral("exposure_%1us")
                                    .arg(exposureUs, 7, 10, QLatin1Char('0'));
    QString replaced = path;
    replaced.replace(start, valueEnd + 2 - start, exposureDir);
    return replaced;
}

double medianOfSamples(QVector<double> samples)
{
    samples.erase(std::remove_if(samples.begin(),
                                 samples.end(),
                                 [](double value) { return !std::isfinite(value); }),
                  samples.end());
    if (samples.isEmpty()) {
        return 0.0;
    }
    std::sort(samples.begin(), samples.end());
    const int mid = samples.size() / 2;
    if ((samples.size() % 2) != 0) {
        return samples[mid];
    }
    return (samples[mid - 1] + samples[mid]) * 0.5;
}

InitialStarDetectionConfig loadInitialStarDetectionConfig()
{
    InitialStarDetectionConfig config;
    const QString appThresholdPath =
        QDir(QApplication::applicationDirPath()).filePath(QStringLiteral("threshold.txt"));
    const QString cwdThresholdPath = QDir::current().filePath(QStringLiteral("threshold.txt"));
    const QString path = QFileInfo::exists(appThresholdPath)
                             ? appThresholdPath
                             : (QFileInfo::exists(cwdThresholdPath) ? cwdThresholdPath : QString());
    if (path.isEmpty()) {
        return config;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return config;
    }

    QTextStream input(&file);
    while (!input.atEnd()) {
        const QString line = stripConfigComment(input.readLine());
        if (line.isEmpty()) {
            continue;
        }
        const int equalPos = line.indexOf(QLatin1Char('='));
        if (equalPos <= 0) {
            continue;
        }

        const QString key = line.left(equalPos).trimmed().toLower();
        const QString valueText = line.mid(equalPos + 1).trimmed();
        bool ok = false;
        const double number = valueText.toDouble(&ok);
        if (!ok) {
            continue;
        }

        if (key == QStringLiteral("threshold_absolute") || key == QStringLiteral("absolute")) {
            config.thresholdAbsolute = normalizeThresholdToMono8(number);
        } else if (key == QStringLiteral("threshold_sigma") || key == QStringLiteral("sigma")) {
            config.sigmaThreshold = std::max(0.0, number);
        } else if (key == QStringLiteral("threshold_peak_fraction") ||
                   key == QStringLiteral("peak_fraction")) {
            config.peakFraction = std::clamp(number, 0.01, 0.95);
        } else if (key == QStringLiteral("threshold_min_intensity") ||
                   key == QStringLiteral("min_intensity")) {
            config.minimumIntensity = normalizeThresholdToMono8(number);
        } else if (key == QStringLiteral("star_min_area")) {
            config.minArea = std::max(1, static_cast<int>(std::lround(number)));
        } else if (key == QStringLiteral("star_max_area")) {
            config.maxArea = std::max(config.minArea, static_cast<int>(std::lround(number)));
        }
    }
    return config;
}

InitialStarDetectionConfig sanitizeInitialStarDetectionConfig(InitialStarDetectionConfig config)
{
    config.thresholdAbsolute =
        config.thresholdAbsolute >= 0.0 ? normalizeThresholdToMono8(config.thresholdAbsolute) : -1.0;
    config.sigmaThreshold = std::clamp(config.sigmaThreshold, 0.0, 20.0);
    config.peakFraction = std::clamp(config.peakFraction, 0.01, 0.95);
    config.minimumIntensity = normalizeThresholdToMono8(std::max(0.0, config.minimumIntensity));
    config.minArea = std::max(1, config.minArea);
    config.maxArea = std::max(config.minArea, config.maxArea);
    return config;
}

InitialStarDetectionConfig& mutableInitialStarDetectionConfig()
{
    static InitialStarDetectionConfig config =
        sanitizeInitialStarDetectionConfig(loadInitialStarDetectionConfig());
    return config;
}

InitialStarDetectionConfig currentInitialStarDetectionConfig()
{
    return mutableInitialStarDetectionConfig();
}

void setCurrentInitialStarDetectionConfig(const InitialStarDetectionConfig& config)
{
    mutableInitialStarDetectionConfig() = sanitizeInitialStarDetectionConfig(config);
}

QVector<InitialStarCandidate> detectInitialStarCandidates(const cv::Mat& grayscale,
                                                          double* peakValue = nullptr,
                                                          double* thresholdValue = nullptr)
{
    QVector<InitialStarCandidate> candidates;
    if (grayscale.empty() || grayscale.channels() != 1) {
        return candidates;
    }

    cv::Mat mono8 = normalizeInitialStarDetectionFrame(grayscale);
    if (mono8.empty()) {
        return candidates;
    }

    cv::Scalar mean;
    cv::Scalar stddev;
    cv::meanStdDev(mono8, mean, stddev);

    double minValue = 0.0;
    double maxValue = 0.0;
    cv::minMaxLoc(mono8, &minValue, &maxValue);
    if (peakValue) {
        *peakValue = maxValue;
    }

    InitialStarDetectionConfig config = currentInitialStarDetectionConfig();
    const double dynamicThreshold = std::max({config.minimumIntensity,
                                              mean[0] + config.sigmaThreshold * stddev[0],
                                              mean[0] + (maxValue - mean[0]) * config.peakFraction});
    const double threshold = config.thresholdAbsolute >= 0.0
                                 ? config.thresholdAbsolute
                                 : dynamicThreshold;
    if (thresholdValue) {
        *thresholdValue = threshold;
    }
    if (maxValue <= threshold) {
        return candidates;
    }

    cv::Mat binary;
    cv::threshold(mono8, binary, threshold, 255.0, cv::THRESH_BINARY);

    cv::Mat labels;
    cv::Mat stats;
    cv::Mat centroids;
    const int componentCount =
        cv::connectedComponentsWithStats(binary, labels, stats, centroids, 8, CV_32S);

    std::vector<double> componentSignal(static_cast<size_t>(componentCount), 0.0);
    std::vector<double> componentPeak(static_cast<size_t>(componentCount), 0.0);
    for (int y = 0; y < labels.rows; ++y) {
        const int* labelRow = labels.ptr<int>(y);
        const uchar* imageRow = mono8.ptr<uchar>(y);
        for (int x = 0; x < labels.cols; ++x) {
            const int label = labelRow[x];
            if (label > 0 && label < componentCount) {
                const double value = static_cast<double>(imageRow[x]);
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
        if (area < config.minArea || area > config.maxArea || width > 96 || height > 96) {
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

InitialStarSelection selectInitialStarCandidate(QVector<InitialStarCandidate> candidates,
                                                bool hasPreference,
                                                const QPointF& preference,
                                                int selectedCandidateIndex)
{
    InitialStarSelection selection;
    if (candidates.isEmpty()) {
        selection.reason = QStringLiteral("No initial star candidates detected");
        return selection;
    }

    if (selectedCandidateIndex > 0) {
        for (const InitialStarCandidate& candidate : candidates) {
            if (candidate.index == selectedCandidateIndex) {
                selection.selected = true;
                selection.candidate = candidate;
                return selection;
            }
        }
        selection.requiresUserSelection = true;
        selection.reason = QStringLiteral("Selected candidate index is not in the current candidate list");
        return selection;
    }

    if (hasPreference) {
        for (InitialStarCandidate& candidate : candidates) {
            const QPointF delta = candidate.center - preference;
            candidate.distanceToPreference = std::hypot(delta.x(), delta.y());
        }
        const auto best = std::min_element(candidates.cbegin(), candidates.cend(),
                                           [](const InitialStarCandidate& a,
                                              const InitialStarCandidate& b) {
            return a.distanceToPreference < b.distanceToPreference;
        });
        if (best != candidates.cend() && best->distanceToPreference <= 128.0) {
            selection.selected = true;
            selection.candidate = *best;
            return selection;
        }
        if (candidates.size() == 1) {
            selection.selected = true;
            selection.candidate = candidates.first();
            selection.reason = QStringLiteral("Single candidate accepted after target motion");
            return selection;
        }
        const InitialStarCandidate& strongest = candidates.first();
        const double nextSignal = candidates.size() > 1
                                      ? std::max(1.0, candidates.at(1).signal)
                                      : 1.0;
        if (strongest.area >= 6 && strongest.peak >= 24.0 &&
            strongest.signal >= nextSignal * 2.0) {
            selection.selected = true;
            selection.candidate = strongest;
            selection.reason = QStringLiteral("Dominant candidate accepted after target motion");
            return selection;
        }
        selection.requiresUserSelection = true;
        selection.reason = QStringLiteral("Nearest candidate is too far from the last target position");
        return selection;
    }

    if (candidates.size() == 1) {
        selection.selected = true;
        selection.candidate = candidates.first();
        return selection;
    }

    selection.requiresUserSelection = true;
    selection.reason = QStringLiteral("Multiple star candidates detected; confirm the Polaris candidate index");
    return selection;
}

bool chooseAutomaticInitialStarCandidate(const QVector<InitialStarCandidate>& candidates,
                                         const InitialStarCandidate& strongestCandidate,
                                         InitialStarCandidate* selected,
                                         QString* reason)
{
    if (candidates.isEmpty() || !selected) {
        if (reason) {
            *reason = QStringLiteral("未检测到可用于自动定位的候选星点");
        }
        return false;
    }

    if (strongestCandidate.area < 6 || strongestCandidate.peak < 24.0) {
        if (reason) {
            *reason = QStringLiteral("最强候选星点过弱或面积过小，继续全画幅定位");
        }
        return false;
    }

    if (candidates.size() > 1) {
        const double nextSignal = std::max(1.0, candidates.at(1).signal);
        if (strongestCandidate.signal < nextSignal * 2.0) {
            if (reason) {
                *reason = QStringLiteral("检测到多个亮星且最强候选不够突出，请进入对准模式确认北极星");
            }
            return false;
        }
    }

    *selected = strongestCandidate;
    return true;
}

QVector<FullFrameCanvas::StarCandidateOverlay> buildCandidateOverlays(
    const QVector<InitialStarCandidate>& candidates,
    int selectedIndex)
{
    QVector<FullFrameCanvas::StarCandidateOverlay> overlays;
    overlays.reserve(candidates.size());
    for (const InitialStarCandidate& candidate : candidates) {
        FullFrameCanvas::StarCandidateOverlay overlay;
        overlay.index = candidate.index;
        overlay.center = candidate.center;
        overlay.bbox = QRectF(candidate.bbox);
        overlay.selected = candidate.index == selectedIndex;
        overlays.append(overlay);
    }
    return overlays;
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

    InitialStarDetectionConfig config = currentInitialStarDetectionConfig();
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

    cv::Mat mono8 = normalizeInitialStarDetectionFrame(grayscale);
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

    InitialStarDetectionConfig config = currentInitialStarDetectionConfig();
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

bool detectInitialStarPeakCandidate(const cv::Mat& grayscale,
                                    InitialStarCandidate* candidate,
                                    double* peakValue)
{
    if (grayscale.empty() || grayscale.channels() != 1 || !candidate) {
        return false;
    }

    cv::Mat mono8 = normalizeInitialStarDetectionFrame(grayscale);
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

    constexpr int kSearchRadius = 10;
    if (maxLoc.x < kSearchRadius || maxLoc.y < kSearchRadius ||
        maxLoc.x >= mono8.cols - kSearchRadius ||
        maxLoc.y >= mono8.rows - kSearchRadius) {
        return false;
    }

    const int x0 = std::max(0, maxLoc.x - kSearchRadius);
    const int y0 = std::max(0, maxLoc.y - kSearchRadius);
    const int x1 = std::min(mono8.cols - 1, maxLoc.x + kSearchRadius);
    const int y1 = std::min(mono8.rows - 1, maxLoc.y + kSearchRadius);

    double backgroundSum = 0.0;
    double backgroundSquareSum = 0.0;
    int backgroundCount = 0;
    for (int y = y0; y <= y1; ++y) {
        const uchar* row = mono8.ptr<uchar>(y);
        for (int x = x0; x <= x1; ++x) {
            const int dx = std::abs(x - maxLoc.x);
            const int dy = std::abs(y - maxLoc.y);
            if (dx <= 3 && dy <= 3) {
                continue;
            }
            const double value = static_cast<double>(row[x]);
            backgroundSum += value;
            backgroundSquareSum += value * value;
            ++backgroundCount;
        }
    }

    const double background =
        backgroundCount > 0 ? backgroundSum / static_cast<double>(backgroundCount) : minValue;
    const double peakContrast = maxValue - background;
    if (peakContrast < 0.5) {
        return false;
    }

    const double supportThreshold = background;
    double weightedX = 0.0;
    double weightedY = 0.0;
    double weightSum = 0.0;
    int supportPixelCount = 0;
    for (int y = maxLoc.y - 1; y <= maxLoc.y + 1; ++y) {
        const uchar* row = mono8.ptr<uchar>(y);
        for (int x = maxLoc.x - 1; x <= maxLoc.x + 1; ++x) {
            const double value = static_cast<double>(row[x]);
            if (value < supportThreshold && !(x == maxLoc.x && y == maxLoc.y)) {
                continue;
            }
            const double weight = std::max(0.0, value - background);
            if (weight <= 0.0) {
                continue;
            }
            weightedX += static_cast<double>(x) * weight;
            weightedY += static_cast<double>(y) * weight;
            weightSum += weight;
            ++supportPixelCount;
        }
    }

    if (weightSum <= 0.0 || supportPixelCount < 1) {
        return false;
    }

    candidate->index = 1;
    candidate->center = QPointF(weightedX / weightSum, weightedY / weightSum);
    candidate->area = std::max(1, supportPixelCount);
    candidate->peak = maxValue;
    candidate->signal = std::max(maxValue, weightSum);
    candidate->bbox = QRect(maxLoc.x - 2, maxLoc.y - 2, 5, 5);
    candidate->distanceToPreference = std::numeric_limits<double>::infinity();
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

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("设置"));
    resize(920, 760);
    setMinimumSize(860, 680);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    auto* tabWidget = new QTabWidget(this);
    tabWidget->setDocumentMode(true);

    const auto addScrollableTab = [tabWidget](QWidget* page, const QString& title) {
        auto* scrollArea = new QScrollArea(tabWidget);
        scrollArea->setWidgetResizable(true);
        scrollArea->setFrameShape(QFrame::NoFrame);
        scrollArea->setWidget(page);
        tabWidget->addTab(scrollArea, title);
    };

    auto* camTab = new QWidget();
    auto* camLayout = new QVBoxLayout(camTab);
    camLayout->setContentsMargins(12, 12, 12, 12);
    camLayout->setSpacing(14);
    auto* infoGroup = new QGroupBox(QStringLiteral("连接说明"));
    auto* infoLayout = new QVBoxLayout(infoGroup);
    auto* infoLabel = new QLabel(QStringLiteral(
        "1. 确保两台相机均已连接后再开始实时采集。\n"
        "2. 网络参数用于连接上位机或远端控制端。\n"
        "3. 点击应用后将立即写入当前运行配置。\n"
        "4. ROI 固定为 64x64，启动后两台相机分别全画幅定位，再切换各自独立 ROI。"));
    infoLabel->setWordWrap(true);
    infoLayout->addWidget(infoLabel);
    camLayout->addWidget(infoGroup);

    auto* acqGroup = new QGroupBox(QStringLiteral("采集参数"));
    auto* acqLayout = new QFormLayout(acqGroup);
    acqLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    acqLayout->setFormAlignment(Qt::AlignTop);
    acqLayout->setHorizontalSpacing(16);
    acqLayout->setVerticalSpacing(12);
    exposureEdit = new QLineEdit(QStringLiteral("2000"));
    acqLayout->addRow(QStringLiteral("曝光时间 (μs):"), exposureEdit);
    gainEdit = new QLineEdit(QStringLiteral("10.0"));
    acqLayout->addRow(QStringLiteral("增益 (dB):"), gainEdit);
    continuousFrameRateEdit = new QLineEdit(QStringLiteral("200"));
    acqLayout->addRow(QStringLiteral("连续采集帧率 (fps):"), continuousFrameRateEdit);
    camLayout->addWidget(acqGroup);

    auto* autoExposureGroup = new QGroupBox(QStringLiteral("自动曝光"));
    auto* autoExposureLayout = new QFormLayout(autoExposureGroup);
    autoExposureLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    autoExposureLayout->setFormAlignment(Qt::AlignTop);
    autoExposureLayout->setHorizontalSpacing(16);
    autoExposureLayout->setVerticalSpacing(10);
    autoExposureCheck = new QCheckBox(QStringLiteral("启用自动曝光"));
    autoExposureLayout->addRow(autoExposureCheck);
    autoExpLowEdit = new QLineEdit(QStringLiteral("80"));
    autoExposureLayout->addRow(QStringLiteral("峰值下阈值:"), autoExpLowEdit);
    autoExpHighEdit = new QLineEdit(QStringLiteral("220"));
    autoExposureLayout->addRow(QStringLiteral("峰值上阈值:"), autoExpHighEdit);
    autoExpDarkRatioEdit = new QLineEdit(QStringLiteral("1.2"));
    autoExposureLayout->addRow(QStringLiteral("过暗调整比例:"), autoExpDarkRatioEdit);
    autoExpBrightRatioEdit = new QLineEdit(QStringLiteral("0.8"));
    autoExposureLayout->addRow(QStringLiteral("过亮调整比例:"), autoExpBrightRatioEdit);
    autoExpMinEdit = new QLineEdit(QStringLiteral("500"));
    autoExposureLayout->addRow(QStringLiteral("最小曝光 (μs):"), autoExpMinEdit);
    autoExpMaxEdit = new QLineEdit(QStringLiteral("20000"));
    autoExposureLayout->addRow(QStringLiteral("最大曝光 (μs):"), autoExpMaxEdit);
    camLayout->addWidget(autoExposureGroup);
    camLayout->addStretch();
    addScrollableTab(camTab, QStringLiteral("相机设置"));

    auto* triggerTab = new QWidget();
    auto* triggerTabLayout = new QVBoxLayout(triggerTab);
    triggerTabLayout->setContentsMargins(12, 12, 12, 12);
    triggerTabLayout->setSpacing(14);

    auto* triggerModeGroup = new QGroupBox(QStringLiteral("触发模式"));
    auto* triggerModeLayout = new QVBoxLayout(triggerModeGroup);
    triggerModeLayout->setContentsMargins(16, 14, 16, 14);
    triggerModeLayout->setSpacing(10);
    triggerContinuous = new QRadioButton(QStringLiteral("连续采集"));
    triggerContinuous->setChecked(true);
    triggerHardware = new QRadioButton(QStringLiteral("硬件触发"));
    triggerModeLayout->addWidget(triggerContinuous);
    triggerModeLayout->addWidget(triggerHardware);
    triggerTabLayout->addWidget(triggerModeGroup);

    auto* pulseGroup = new QGroupBox(QStringLiteral("脉冲发生器"));
    auto* pulseLayout = new QFormLayout(pulseGroup);
    pulseLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    pulseLayout->setFormAlignment(Qt::AlignTop);
    pulseLayout->setHorizontalSpacing(16);
    pulseLayout->setVerticalSpacing(10);
    pulseEnableCheck = new QCheckBox(QStringLiteral("启用触发输出"));
    pulseLayout->addRow(pulseEnableCheck);
    pulsePortEdit = new QLineEdit(QStringLiteral("COM6"));
    pulseLayout->addRow(QStringLiteral("端口号:"), pulsePortEdit);
    pulseBaudCombo = new QComboBox();
    pulseBaudCombo->addItems({QStringLiteral("9600"),
                              QStringLiteral("19200"),
                              QStringLiteral("38400"),
                              QStringLiteral("57600"),
                              QStringLiteral("115200")});
    pulseBaudCombo->setCurrentText(QStringLiteral("19200"));
    pulseLayout->addRow(QStringLiteral("波特率:"), pulseBaudCombo);
    pulseTerminalEdit = new QLineEdit(QStringLiteral("1"));
    pulseLayout->addRow(QStringLiteral("终端号:"), pulseTerminalEdit);
    pulseFreqEdit = new QLineEdit(QStringLiteral("200.0"));
    pulseApplyFreqBtn = new QPushButton(QStringLiteral("修改频率"));
    auto* freqRow = new QWidget();
    auto* freqLayout = new QHBoxLayout(freqRow);
    freqLayout->setContentsMargins(0, 0, 0, 0);
    freqLayout->setSpacing(10);
    freqLayout->addWidget(pulseFreqEdit, 1);
    freqLayout->addWidget(pulseApplyFreqBtn);
    pulseLayout->addRow(QStringLiteral("输出频率 (Hz):"), freqRow);

    pulseCountEdit = new QLineEdit(QStringLiteral("2000000"));
    pulseApplyCountBtn = new QPushButton(QStringLiteral("修改脉冲个数"));
    auto* countRow = new QWidget();
    auto* countLayout = new QHBoxLayout(countRow);
    countLayout->setContentsMargins(0, 0, 0, 0);
    countLayout->setSpacing(10);
    countLayout->addWidget(pulseCountEdit, 1);
    countLayout->addWidget(pulseApplyCountBtn);
    pulseLayout->addRow(QStringLiteral("脉冲个数:"), countRow);

    pulseDutyEdit = new QLineEdit(QStringLiteral("50"));
    pulseApplyDutyBtn = new QPushButton(QStringLiteral("修改占空比"));
    auto* dutyRow = new QWidget();
    auto* dutyLayout = new QHBoxLayout(dutyRow);
    dutyLayout->setContentsMargins(0, 0, 0, 0);
    dutyLayout->setSpacing(10);
    dutyLayout->addWidget(pulseDutyEdit, 1);
    dutyLayout->addWidget(pulseApplyDutyBtn);
    pulseLayout->addRow(QStringLiteral("占空比 (%):"), dutyRow);

    auto* sourceWidget = new QWidget();
    auto* sourceLayout = new QHBoxLayout(sourceWidget);
    sourceLayout->setContentsMargins(0, 0, 0, 0);
    sourceLayout->setSpacing(16);
    pulseSourceLocal = new QRadioButton(QStringLiteral("本地"));
    pulseSourceRemote = new QRadioButton(QStringLiteral("远程"));
    pulseSourceRemote->setChecked(true);
    sourceLayout->addWidget(pulseSourceLocal);
    sourceLayout->addWidget(pulseSourceRemote);
    pulseApplySourceBtn = new QPushButton(QStringLiteral("修改控制类型"));
    sourceLayout->addWidget(pulseApplySourceBtn);
    sourceLayout->addStretch();
    pulseLayout->addRow(QStringLiteral("来源控制:"), sourceWidget);

    auto* pulseHint = new QLabel(QStringLiteral("默认建议: 波特率 19200，终端号 1，占空比 50，来源控制选择远程。"));
    pulseHint->setWordWrap(true);
    pulseLayout->addRow(QString(), pulseHint);
    auto* pulseCommitHint = new QLabel(QStringLiteral("说明: 输出频率、脉冲个数、占空比、来源控制修改后，需点击对应按钮才算生效。"));
    pulseCommitHint->setWordWrap(true);
    pulseLayout->addRow(QString(), pulseCommitHint);
    auto* pulseActionRow = new QWidget();
    auto* pulseActionLayout = new QHBoxLayout(pulseActionRow);
    pulseActionLayout->setContentsMargins(0, 4, 0, 0);
    pulseActionLayout->setSpacing(10);
    pulseStartBtn = new QPushButton(QStringLiteral("输出脉冲"));
    pulseStartBtn->setProperty("role", "primary");
    pulseStopBtn = new QPushButton(QStringLiteral("关闭脉冲"));
    pulseStopBtn->setProperty("role", "secondary");
    pulseActionLayout->addWidget(pulseStartBtn);
    pulseActionLayout->addWidget(pulseStopBtn);
    pulseActionLayout->addStretch();
    pulseLayout->addRow(QStringLiteral("即时控制:"), pulseActionRow);
    triggerTabLayout->addWidget(pulseGroup);
    triggerTabLayout->addStretch();
    addScrollableTab(triggerTab, QStringLiteral("触发设置"));

    auto* procTab = new QWidget();
    auto* procLayout = new QVBoxLayout(procTab);
    procLayout->setContentsMargins(12, 12, 12, 12);
    procLayout->setSpacing(14);
    auto* centroidGroup = new QGroupBox(QStringLiteral("质心算法"));
    auto* centroidLayout = new QVBoxLayout(centroidGroup);
    procGravity = new QRadioButton(QStringLiteral("重心法"));
    procGaussian = new QRadioButton(QStringLiteral("高斯加权精细化"));
    procGaussian->setChecked(true);
    centroidLayout->addWidget(procGravity);
    centroidLayout->addWidget(procGaussian);
    procLayout->addWidget(centroidGroup);

    auto* preprocessGroup = new QGroupBox(QStringLiteral("ROI质心预处理参数"));
    auto* preprocessLayout = new QGridLayout(preprocessGroup);
    preprocessLayout->addWidget(new QLabel(QStringLiteral("高斯滤波核大小:")), 0, 0);
    procKernelSize = new QLineEdit(QStringLiteral("3"));
    preprocessLayout->addWidget(procKernelSize, 0, 1);
    preprocessLayout->addWidget(new QLabel(QStringLiteral("高斯标准差 σ:")), 1, 0);
    procSigma = new QLineEdit(QStringLiteral("1.0"));
    preprocessLayout->addWidget(procSigma, 1, 1);
    auto* centroidPipelineHint =
        new QLabel(QStringLiteral("ROI质心流程: 热像素修正 -> 高斯滤波 -> 噪声阈值 -> 背景扣除重心。"));
    centroidPipelineHint->setWordWrap(true);
    preprocessLayout->addWidget(centroidPipelineHint, 2, 0, 1, 2);
    procLayout->addWidget(preprocessGroup);

    auto* roiRecenterGroup = new QGroupBox(QStringLiteral("ROI 重居中参数"));
    auto* roiRecenterLayout = new QFormLayout(roiRecenterGroup);
    roiRecenterLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    roiRecenterLayout->setFormAlignment(Qt::AlignTop);
    roiRecenterLayout->setHorizontalSpacing(16);
    roiRecenterLayout->setVerticalSpacing(10);
    roiRecenterThresholdEdit = new QLineEdit(QStringLiteral("16.0"));
    roiRecenterLayout->addRow(QStringLiteral("距边缘阈值(px):"), roiRecenterThresholdEdit);
    roiRecenterRequiredFramesEdit = new QLineEdit(QStringLiteral("5"));
    roiRecenterLayout->addRow(QStringLiteral("连续帧数:"), roiRecenterRequiredFramesEdit);
    roiRecenterCooldownMsEdit = new QLineEdit(QStringLiteral("3000"));
    roiRecenterLayout->addRow(QStringLiteral("冷却时间(ms):"), roiRecenterCooldownMsEdit);
    roiRecenterMinimumShiftEdit = new QLineEdit(QStringLiteral("8.0"));
    roiRecenterLayout->addRow(QStringLiteral("最小位移(px):"), roiRecenterMinimumShiftEdit);
    auto* roiRecenterHint = new QLabel(QStringLiteral(
        "这些参数只控制运行中 ROI 重新居中；星点靠边或丢失时仍会进入全画幅重定位。"));
    roiRecenterHint->setWordWrap(true);
    roiRecenterLayout->addRow(QString(), roiRecenterHint);
    procLayout->addWidget(roiRecenterGroup);

    const InitialStarDetectionConfig starConfig = currentInitialStarDetectionConfig();
    auto* starDetectionGroup = new QGroupBox(QStringLiteral("全画幅找星参数"));
    auto* starDetectionLayout = new QFormLayout(starDetectionGroup);
    starDetectionLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    starDetectionLayout->setFormAlignment(Qt::AlignTop);
    starDetectionLayout->setHorizontalSpacing(16);
    starDetectionLayout->setVerticalSpacing(10);
    starThresholdAbsoluteEdit =
        new QLineEdit(QString::number(starConfig.thresholdAbsolute, 'f', 1));
    starDetectionLayout->addRow(QStringLiteral("绝对阈值 (-1 自动):"), starThresholdAbsoluteEdit);
    starSigmaThresholdEdit = new QLineEdit(QString::number(starConfig.sigmaThreshold, 'f', 2));
    starDetectionLayout->addRow(QStringLiteral("背景倍数 σ:"), starSigmaThresholdEdit);
    starPeakFractionEdit = new QLineEdit(QString::number(starConfig.peakFraction, 'f', 2));
    starDetectionLayout->addRow(QStringLiteral("峰值比例:"), starPeakFractionEdit);
    starMinimumIntensityEdit = new QLineEdit(QString::number(starConfig.minimumIntensity, 'f', 1));
    starDetectionLayout->addRow(QStringLiteral("最低亮度:"), starMinimumIntensityEdit);
    starMinAreaEdit = new QLineEdit(QString::number(starConfig.minArea));
    starDetectionLayout->addRow(QStringLiteral("最小面积:"), starMinAreaEdit);
    starMaxAreaEdit = new QLineEdit(QString::number(starConfig.maxArea));
    starDetectionLayout->addRow(QStringLiteral("最大面积:"), starMaxAreaEdit);
    auto* starDetectionHint = new QLabel(QStringLiteral(
        "这些参数只影响全画幅候选框/首次定位/重定位，不改变 ROI 内高频质心算法。"));
    starDetectionHint->setWordWrap(true);
    starDetectionLayout->addRow(QString(), starDetectionHint);
    procLayout->addWidget(starDetectionGroup);

    auto* hotPixelGroup = new QGroupBox(QStringLiteral("热像素模板"));
    auto* hotPixelLayout = new QFormLayout(hotPixelGroup);
    hotPixelLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    hotPixelLayout->setFormAlignment(Qt::AlignTop);
    hotPixelLayout->setHorizontalSpacing(16);
    hotPixelLayout->setVerticalSpacing(10);
    hotPixelEnableCheck = new QCheckBox(QStringLiteral("启用热像素修正"));
    hotPixelLayout->addRow(hotPixelEnableCheck);

    const auto makeFileRow = [this](QLineEdit** edit, const QString& title) {
        auto* row = new QWidget();
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        *edit = new QLineEdit();
        auto* browse = new QPushButton(QStringLiteral("浏览..."));
        connect(browse, &QPushButton::clicked, this, [this, edit, title]() {
            const QString file = QFileDialog::getOpenFileName(
                this,
                title,
                (*edit)->text().trimmed(),
                QStringLiteral("模板文件 (*.bin *.raw *.dat *.txt);;所有文件 (*.*)"));
            if (!file.isEmpty()) {
                (*edit)->setText(relativizePathToAppDir(file));
            }
        });
        layout->addWidget(*edit, 1);
        layout->addWidget(browse);
        return row;
    };

    hotPixelLayout->addRow(QStringLiteral("相机1 Mask:"), makeFileRow(&hotPixelCam0MaskEdit, QStringLiteral("选择相机1热像素 Mask")));
    hotPixelLayout->addRow(QStringLiteral("相机1 Excess:"), makeFileRow(&hotPixelCam0ExcessEdit, QStringLiteral("选择相机1热像素 Excess")));
    hotPixelLayout->addRow(QStringLiteral("相机2 Mask:"), makeFileRow(&hotPixelCam1MaskEdit, QStringLiteral("选择相机2热像素 Mask")));
    hotPixelLayout->addRow(QStringLiteral("相机2 Excess:"), makeFileRow(&hotPixelCam1ExcessEdit, QStringLiteral("选择相机2热像素 Excess")));
    hotPixelTemplateWidthEdit = new QLineEdit(QStringLiteral("0"));
    hotPixelLayout->addRow(QStringLiteral("模板宽度:"), hotPixelTemplateWidthEdit);
    hotPixelTemplateHeightEdit = new QLineEdit(QStringLiteral("0"));
    hotPixelLayout->addRow(QStringLiteral("模板高度:"), hotPixelTemplateHeightEdit);
    auto* hotPixelHint = new QLabel(QStringLiteral("启用后需提供两台相机的 mask/excess 模板和完整模板尺寸；未启用时会清空当前热像素修正。"));
    hotPixelHint->setWordWrap(true);
    hotPixelLayout->addRow(QString(), hotPixelHint);
    procLayout->addWidget(hotPixelGroup);
    procLayout->addStretch();
    addScrollableTab(procTab, QStringLiteral("图像处理"));

    auto* sysTab = new QWidget();
    auto* sysLayout = new QVBoxLayout(sysTab);
    sysLayout->setContentsMargins(12, 12, 12, 12);
    sysLayout->setSpacing(14);
    auto* opticsGroup = new QGroupBox(QStringLiteral("光学系统"));
    auto* opticsLayout = new QGridLayout(opticsGroup);
    opticsLayout->addWidget(new QLabel(QStringLiteral("子孔径直径 D (mm):")), 0, 0);
    opticsD = new QLineEdit(QStringLiteral("56"));
    opticsLayout->addWidget(opticsD, 0, 1);
    opticsLayout->addWidget(new QLabel(QStringLiteral("中心间距 d (mm):")), 1, 0);
    opticsBaseline = new QLineEdit(QStringLiteral("269"));
    opticsLayout->addWidget(opticsBaseline, 1, 1);
    opticsLayout->addWidget(new QLabel(QStringLiteral("焦距 f (mm):")), 2, 0);
    opticsF = new QLineEdit(QStringLiteral("269"));
    opticsLayout->addWidget(opticsF, 2, 1);
    opticsLayout->addWidget(new QLabel(QStringLiteral("天顶角 Z (deg):")), 3, 0);
    opticsZenith = new QLineEdit(QStringLiteral("50"));
    opticsLayout->addWidget(opticsZenith, 3, 1);
    sysLayout->addWidget(opticsGroup);

    auto* detectorGroup = new QGroupBox(QStringLiteral("探测器"));
    auto* detectorLayout = new QGridLayout(detectorGroup);
    detectorLayout->addWidget(new QLabel(QStringLiteral("像素尺寸 (μm):")), 0, 0);
    detectorPixelSize = new QLineEdit(QStringLiteral("2.5"));
    detectorLayout->addWidget(detectorPixelSize, 0, 1);
    detectorLayout->addWidget(new QLabel(QStringLiteral("对比波长 (nm):")), 1, 0);
    detectorWavelength = new QLineEdit(QStringLiteral("500"));
    detectorLayout->addWidget(detectorWavelength, 1, 1);
    sysLayout->addWidget(detectorGroup);

    auto* alignmentGroup = new QGroupBox(QStringLiteral("对准设置"));
    auto* alignmentLayout = new QFormLayout(alignmentGroup);
    alignmentLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    alignmentLayout->setFormAlignment(Qt::AlignTop);
    alignmentLayout->setHorizontalSpacing(16);
    alignmentLayout->setVerticalSpacing(10);
    alignmentAutoRadiusCheck = new QCheckBox(QStringLiteral("启用自动半径计算"));
    alignmentAutoRadiusCheck->setChecked(true);
    alignmentLayout->addRow(alignmentAutoRadiusCheck);
    alignmentFocalLengthEdit = new QLineEdit(QStringLiteral("269"));
    alignmentLayout->addRow(QStringLiteral("焦距 (mm):"), alignmentFocalLengthEdit);
    alignmentPixelSizeEdit = new QLineEdit(QStringLiteral("2.5"));
    alignmentLayout->addRow(QStringLiteral("像元尺寸 (μm):"), alignmentPixelSizeEdit);
    alignmentPolarDistanceEdit =
        new QLineEdit(QString::number(kAlignmentDefaultPolarisPolarDistanceArcmin, 'f', 1));
    alignmentLayout->addRow(QStringLiteral("北极星极距 (arcmin):"), alignmentPolarDistanceEdit);
    alignmentRadiusAdjustEdit = new QLineEdit(QStringLiteral("0"));
    alignmentLayout->addRow(QStringLiteral("轨道半径微调 (px):"), alignmentRadiusAdjustEdit);
    alignmentPreviewRateEdit = new QLineEdit(QStringLiteral("1.0"));
    alignmentLayout->addRow(QStringLiteral("对准预览频率 (Hz):"), alignmentPreviewRateEdit);
    auto* alignmentHint = new QLabel(QStringLiteral(
        "对准模式只用于低频全画幅寻星，不启用 ROI、不计算大气参数、不保存测量数据。"));
    alignmentHint->setWordWrap(true);
    alignmentLayout->addRow(QString(), alignmentHint);
    sysLayout->addWidget(alignmentGroup);
    sysLayout->addStretch();
    addScrollableTab(sysTab, QStringLiteral("系统参数"));

    auto* storeTab = new QWidget();
    auto* storeLayout = new QVBoxLayout(storeTab);
    storeLayout->setContentsMargins(12, 12, 12, 12);
    storeLayout->setSpacing(14);
    auto* pathGroup = new QGroupBox(QStringLiteral("存储路径"));
    auto* pathLayout = new QHBoxLayout(pathGroup);
    storagePathEdit = new QLineEdit(QStringLiteral("D:/C-DIMM/data"));
    auto* browseBtn = new QPushButton(QStringLiteral("浏览..."));
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, QStringLiteral("选择存储路径"), storagePathEdit->text());
        if (!dir.isEmpty()) {
            storagePathEdit->setText(dir);
        }
    });
    pathLayout->addWidget(storagePathEdit);
    pathLayout->addWidget(browseBtn);
    storeLayout->addWidget(pathGroup);

    auto* paramGroup = new QGroupBox(QStringLiteral("参数存储"));
    auto* paramLayout = new QVBoxLayout(paramGroup);
    auto* intervalLayout = new QHBoxLayout();
    intervalLayout->addWidget(new QLabel(QStringLiteral("参数记录间隔 (次):")));
    saveIntervalEdit = new QLineEdit(QStringLiteral("1"));
    intervalLayout->addWidget(saveIntervalEdit);
    auto* paramInfo = new QLabel(QStringLiteral("仅保存计算后的质心、ROI 和大气参数，不保存全画幅或 ROI 图像。"));
    paramInfo->setWordWrap(true);
    paramLayout->addLayout(intervalLayout);
    paramLayout->addWidget(paramInfo);
    storeLayout->addWidget(paramGroup);

    auto* resultGroup = new QGroupBox(QStringLiteral("结果存储 (CSV)"));
    auto* resultLayout = new QVBoxLayout(resultGroup);
    auto* resultInfo = new QLabel(QStringLiteral("自动保存: 时间戳、帧号、双相机质心、ROI、峰值亮度和大气参数"));
    resultInfo->setWordWrap(true);
    resultLayout->addWidget(resultInfo);
    storeLayout->addWidget(resultGroup);
    storeLayout->addStretch();
    addScrollableTab(storeTab, QStringLiteral("数据存储"));

    auto* netTab = new QWidget();
    auto* netLayout = new QVBoxLayout(netTab);
    netLayout->setContentsMargins(12, 12, 12, 12);
    netLayout->setSpacing(14);
    auto* connGroup = new QGroupBox(QStringLiteral("上位机连接"));
    auto* connLayout = new QGridLayout(connGroup);
    connLayout->addWidget(new QLabel(QStringLiteral("IP地址:")), 0, 0);
    netIpEdit = new QLineEdit(QStringLiteral("192.168.10.1"));
    connLayout->addWidget(netIpEdit, 0, 1);
    connLayout->addWidget(new QLabel(QStringLiteral("端口:")), 1, 0);
    netPortEdit = new QLineEdit(QStringLiteral("5000"));
    connLayout->addWidget(netPortEdit, 1, 1);
    netConnectBtn = new QPushButton(QStringLiteral("连接上位机"));
    connLayout->addWidget(netConnectBtn, 2, 0, 1, 2);
    netStatusLabel = new QLabel(QStringLiteral("状态: 未连接"));
    netStatusLabel->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
                                      .arg(uiStatusColor(UiStatusLevel::Muted)));
    connLayout->addWidget(netStatusLabel, 3, 0, 1, 2);
    netLayout->addWidget(connGroup);

    auto* protoGroup = new QGroupBox(QStringLiteral("通信协议"));
    auto* protoLayout = new QVBoxLayout(protoGroup);
    auto* protoInfo = new QLabel(QStringLiteral(
        "协议: TCP 二进制\n"
        "帧头: 0xAA55\n"
        "校验: XOR\n\n"
        "指令:\n"
        "  上位机→设备: 0x01 开始上报 / 0x02 停止 / 0x03 查询状态\n"
        "  设备→上位机: 0x81 测量结果 / 0x82 设备状态 / 0x83 应答"));
    protoInfo->setWordWrap(true);
    protoLayout->addWidget(protoInfo);
    netLayout->addWidget(protoGroup);
    netLayout->addStretch();
    addScrollableTab(netTab, QStringLiteral("网络通信"));

    mainLayout->addWidget(tabWidget);

    applyStatusLabel = new QLabel(QStringLiteral("待应用"));
    applyStatusLabel->setStyleSheet(statusLabelStyle(UiStatusLevel::Muted));
    mainLayout->addWidget(applyStatusLabel);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply);
    if (auto* okBtn = buttonBox->button(QDialogButtonBox::Ok)) {
        okBtn->setProperty("role", "primary");
    }
    if (auto* cancelBtn = buttonBox->button(QDialogButtonBox::Cancel)) {
        cancelBtn->setProperty("role", "secondary");
    }
    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        if (applySettings()) {
            accept();
        }
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    if (auto* applyBtn = buttonBox->button(QDialogButtonBox::Apply)) {
        applyBtn->setProperty("role", "primary");
        connect(applyBtn, &QPushButton::clicked, this, [this]() {
            if (applySettings()) {
                updateApplyStatus(QStringLiteral("设置已应用"), UiStatusLevel::Success);
            }
        });
    }
    mainLayout->addWidget(buttonBox);

    connect(pulseApplyFreqBtn, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const double value = pulseFreqEdit->text().toDouble(&ok);
        if (!ok || value <= 0.0) {
            QMessageBox::warning(this, QStringLiteral("触发设置"), QStringLiteral("输出频率必须大于 0。"));
            updateApplyStatus(QStringLiteral("输出频率未提交"), UiStatusLevel::Error);
            return;
        }
        m_committedPulseFrequencyHz = value;
        if (applyCommittedPulseSettings(true)) {
            updateApplyStatus(QStringLiteral("输出频率已设为当前值"), UiStatusLevel::Success);
        }
    });

    connect(pulseApplyCountBtn, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const quint32 value = pulseCountEdit->text().toUInt(&ok);
        if (!ok || value == 0U) {
            QMessageBox::warning(this, QStringLiteral("触发设置"), QStringLiteral("脉冲个数必须大于 0。"));
            updateApplyStatus(QStringLiteral("脉冲个数未提交"), UiStatusLevel::Error);
            return;
        }
        m_committedPulseCount = value;
        if (applyCommittedPulseSettings(true)) {
            updateApplyStatus(QStringLiteral("脉冲个数已设为当前值"), UiStatusLevel::Success);
        }
    });

    connect(pulseApplyDutyBtn, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const double value = pulseDutyEdit->text().toDouble(&ok);
        if (!ok || value < 0.0 || value > 100.0) {
            QMessageBox::warning(this, QStringLiteral("触发设置"), QStringLiteral("占空比必须在 0 到 100 之间。"));
            updateApplyStatus(QStringLiteral("占空比未提交"), UiStatusLevel::Error);
            return;
        }
        m_committedPulseDutyPercent = value;
        if (applyCommittedPulseSettings(true)) {
            updateApplyStatus(QStringLiteral("占空比已设为当前值"), UiStatusLevel::Success);
        }
    });

    connect(pulseApplySourceBtn, &QPushButton::clicked, this, [this]() {
        m_committedPulseRemoteControl = pulseSourceRemote->isChecked();
        if (applyCommittedPulseSettings(true)) {
            updateApplyStatus(QStringLiteral("来源控制已设为当前值"), UiStatusLevel::Success);
        }
    });

    connect(pulseStartBtn, &QPushButton::clicked, this, [this]() {
        if (!pulseEnableCheck || !pulseEnableCheck->isChecked()) {
            QMessageBox::warning(this,
                                 QStringLiteral("触发设置"),
                                 QStringLiteral("请先勾选“启用触发输出”，再启动脉冲。"));
            updateApplyStatus(QStringLiteral("输出脉冲失败：未启用触发输出"), UiStatusLevel::Error);
            return;
        }

        if (!pulsePortEdit || pulsePortEdit->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("触发设置"), QStringLiteral("启动脉冲前请填写端口号。"));
            updateApplyStatus(QStringLiteral("输出脉冲失败：串口为空"), UiStatusLevel::Error);
            return;
        }

        bool ok = false;
        const int pulseBaudRate = pulseBaudCombo->currentText().toInt(&ok);
        if (!ok || pulseBaudRate <= 0) {
            QMessageBox::warning(this, QStringLiteral("触发设置"), QStringLiteral("波特率无效。"));
            updateApplyStatus(QStringLiteral("输出脉冲失败：波特率无效"), UiStatusLevel::Error);
            return;
        }

        const int pulseTerminalId = pulseTerminalEdit->text().toInt(&ok);
        if (!ok || pulseTerminalId < 1 || pulseTerminalId > 255) {
            QMessageBox::warning(this, QStringLiteral("触发设置"), QStringLiteral("终端号必须在 1 到 255 之间。"));
            updateApplyStatus(QStringLiteral("输出脉冲失败：终端号无效"), UiStatusLevel::Error);
            return;
        }

        m_committedPulseFrequencyHz = pulseFreqEdit->text().toDouble(&ok);
        if (!ok || m_committedPulseFrequencyHz <= 0.0) {
            QMessageBox::warning(this, QStringLiteral("触发设置"), QStringLiteral("输出频率必须大于 0。"));
            updateApplyStatus(QStringLiteral("输出脉冲失败：频率无效"), UiStatusLevel::Error);
            return;
        }

        m_committedPulseCount = pulseCountEdit->text().toUInt(&ok);
        if (!ok || m_committedPulseCount == 0U) {
            QMessageBox::warning(this, QStringLiteral("触发设置"), QStringLiteral("脉冲个数必须大于 0。"));
            updateApplyStatus(QStringLiteral("输出脉冲失败：脉冲个数无效"), UiStatusLevel::Error);
            return;
        }

        m_committedPulseDutyPercent = pulseDutyEdit->text().toDouble(&ok);
        if (!ok || m_committedPulseDutyPercent < 0.0 || m_committedPulseDutyPercent > 100.0) {
            QMessageBox::warning(this, QStringLiteral("触发设置"), QStringLiteral("占空比必须在 0 到 100 之间。"));
            updateApplyStatus(QStringLiteral("输出脉冲失败：占空比无效"), UiStatusLevel::Error);
            return;
        }
        m_committedPulseRemoteControl = pulseSourceRemote->isChecked();

        if (!onStartPulseOutput) {
            updateApplyStatus(QStringLiteral("当前版本未接入脉冲板启动控制"), UiStatusLevel::Error);
            return;
        }

        QString errorMessage;
        if (!onStartPulseOutput(pulsePortEdit->text().trimmed(),
                                pulseBaudRate,
                                pulseTerminalId,
                                m_committedPulseFrequencyHz,
                                m_committedPulseCount,
                                m_committedPulseDutyPercent,
                                m_committedPulseRemoteControl,
                                &errorMessage)) {
            updateApplyStatus(errorMessage.isEmpty() ? QStringLiteral("输出脉冲失败") : errorMessage,
                              UiStatusLevel::Error);
            return;
        }

        updateApplyStatus(QStringLiteral("脉冲输出已启动"), UiStatusLevel::Success);
    });

    connect(pulseStopBtn, &QPushButton::clicked, this, [this]() {
        if (!onStopPulseOutput) {
            updateApplyStatus(QStringLiteral("当前版本未接入脉冲板停止控制"), UiStatusLevel::Error);
            return;
        }

        QString errorMessage;
        if (!onStopPulseOutput(&errorMessage)) {
            updateApplyStatus(errorMessage.isEmpty() ? QStringLiteral("关闭脉冲失败") : errorMessage,
                              UiStatusLevel::Error);
            return;
        }

        updateApplyStatus(QStringLiteral("脉冲输出已关闭"), UiStatusLevel::Warning);
    });

    connect(netConnectBtn, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const quint16 port = netPortEdit->text().toUShort(&ok);
        if (!ok || port == 0) {
            QMessageBox::warning(this, QStringLiteral("网络设置"), QStringLiteral("端口必须在 1 到 65535 之间。"));
            updateApplyStatus(QStringLiteral("网络连接失败：端口无效"), UiStatusLevel::Error);
            return;
        }
        const QString ip = netIpEdit->text().trimmed();
        if (ip.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("网络设置"), QStringLiteral("IP地址不能为空。"));
            updateApplyStatus(QStringLiteral("网络连接失败：IP地址为空"), UiStatusLevel::Error);
            return;
        }
        if (onConnectNetwork) {
            onConnectNetwork(ip, port);
            updateApplyStatus(QStringLiteral("正在按当前网络参数连接上位机"), UiStatusLevel::Warning);
        }
    });
}

void SettingsDialog::setPulseGeneratorState(bool enabled,
                                            const QString& portName,
                                            int baudRate,
                                            int terminalId,
                                            double frequencyHz,
                                            quint32 pulseCount,
                                            double dutyPercent,
                                            bool remoteControl)
{
    if (pulseEnableCheck) {
        pulseEnableCheck->setChecked(enabled);
    }
    if (pulsePortEdit) {
        pulsePortEdit->setText(portName);
    }
    if (pulseBaudCombo) {
        pulseBaudCombo->setCurrentText(QString::number(baudRate));
    }
    if (pulseTerminalEdit) {
        pulseTerminalEdit->setText(QString::number(terminalId));
    }
    if (pulseFreqEdit) {
        pulseFreqEdit->setText(QString::number(frequencyHz, 'f', 1));
    }
    if (pulseCountEdit) {
        pulseCountEdit->setText(QString::number(pulseCount));
    }
    if (pulseDutyEdit) {
        pulseDutyEdit->setText(QString::number(dutyPercent, 'f', 0));
    }
    if (pulseSourceRemote && pulseSourceLocal) {
        pulseSourceRemote->setChecked(remoteControl);
        pulseSourceLocal->setChecked(!remoteControl);
    }

    m_committedPulseFrequencyHz = frequencyHz;
    m_committedPulseCount = pulseCount;
    m_committedPulseDutyPercent = dutyPercent;
    m_committedPulseRemoteControl = remoteControl;
}

void SettingsDialog::updateApplyStatus(const QString& text, const QString& color)
{
    if (!applyStatusLabel) {
        return;
    }
    applyStatusLabel->setText(text);
    applyStatusLabel->setStyleSheet(statusLabelStyle(color));
}

void SettingsDialog::updateApplyStatus(const QString& text, UiStatusLevel level)
{
    updateApplyStatus(text, uiStatusColor(level));
}

bool SettingsDialog::applyCommittedPulseSettings(bool requireEnabledPort)
{
    if (!onApplyPulseGenerator || !pulseEnableCheck || !pulsePortEdit || !pulseBaudCombo || !pulseTerminalEdit) {
        return true;
    }

    bool ok = false;
    const int pulseBaudRate = pulseBaudCombo->currentText().toInt(&ok);
    if (!ok || pulseBaudRate <= 0) {
        updateApplyStatus(QStringLiteral("触发设置中的波特率无效"), UiStatusLevel::Error);
        return false;
    }
    const int pulseTerminalId = pulseTerminalEdit->text().toInt(&ok);
    if (!ok || pulseTerminalId < 1 || pulseTerminalId > 255) {
        updateApplyStatus(QStringLiteral("终端号必须在 1 到 255 之间"), UiStatusLevel::Error);
        return false;
    }
    if (requireEnabledPort && pulseEnableCheck->isChecked() && pulsePortEdit->text().trimmed().isEmpty()) {
        updateApplyStatus(QStringLiteral("启用脉冲板时，串口不能为空"), UiStatusLevel::Error);
        return false;
    }

    QString errorMessage;
    if (!onApplyPulseGenerator(pulseEnableCheck->isChecked(),
                               pulsePortEdit->text().trimmed(),
                               pulseBaudRate,
                               pulseTerminalId,
                               m_committedPulseFrequencyHz,
                               m_committedPulseCount,
                               m_committedPulseDutyPercent,
                               m_committedPulseRemoteControl,
                               &errorMessage)) {
        updateApplyStatus(errorMessage.isEmpty() ? QStringLiteral("触发设置下发失败") : errorMessage,
                          UiStatusLevel::Error);
        return false;
    }
    if (!errorMessage.isEmpty()) {
        updateApplyStatus(errorMessage, UiStatusLevel::Warning);
    }
    return true;
}

bool SettingsDialog::applySettings()
{
    auto showInvalid = [this](const QString& message) {
        updateApplyStatus(message, UiStatusLevel::Error);
        QMessageBox::warning(this, QStringLiteral("参数错误"), message);
    };

    bool ok = false;
    const double exposure = exposureEdit->text().toDouble(&ok);
    if (!ok || exposure <= 0.0) {
        showInvalid(QStringLiteral("曝光时间必须大于 0。"));
        return false;
    }

    const double gain = gainEdit->text().toDouble(&ok);
    if (!ok || gain < 0.0) {
        showInvalid(QStringLiteral("增益必须大于或等于 0。"));
        return false;
    }

    const double continuousFrameRate =
        continuousFrameRateEdit ? continuousFrameRateEdit->text().toDouble(&ok) : 200.0;
    if (!ok || continuousFrameRate < 0.1 || continuousFrameRate > 1000.0) {
        showInvalid(QStringLiteral("连续采集帧率必须在 0.1 到 1000 fps 之间。"));
        return false;
    }

    const double autoExpLow = autoExpLowEdit->text().toDouble(&ok);
    if (!ok || autoExpLow < 0.0) {
        showInvalid(QStringLiteral("自动曝光峰值下阈值必须大于或等于 0。"));
        return false;
    }

    const double autoExpHigh = autoExpHighEdit->text().toDouble(&ok);
    if (!ok || autoExpHigh <= autoExpLow) {
        showInvalid(QStringLiteral("自动曝光峰值上阈值必须大于下阈值。"));
        return false;
    }

    const double autoExpDarkRatio = autoExpDarkRatioEdit->text().toDouble(&ok);
    if (!ok || autoExpDarkRatio <= 1.0) {
        showInvalid(QStringLiteral("过暗调整比例必须大于 1。"));
        return false;
    }

    const double autoExpBrightRatio = autoExpBrightRatioEdit->text().toDouble(&ok);
    if (!ok || autoExpBrightRatio <= 0.0 || autoExpBrightRatio >= 1.0) {
        showInvalid(QStringLiteral("过亮调整比例必须在 0 到 1 之间。"));
        return false;
    }

    const double autoExpMin = autoExpMinEdit->text().toDouble(&ok);
    if (!ok || autoExpMin <= 0.0) {
        showInvalid(QStringLiteral("最小曝光必须大于 0。"));
        return false;
    }

    const double autoExpMax = autoExpMaxEdit->text().toDouble(&ok);
    if (!ok || autoExpMax < autoExpMin) {
        showInvalid(QStringLiteral("最大曝光必须大于或等于最小曝光。"));
        return false;
    }

    const int kernelSize = procKernelSize->text().toInt(&ok);
    if (!ok || kernelSize <= 0) {
        showInvalid(QStringLiteral("滤波核大小必须为正整数。"));
        return false;
    }

    const double sigma = procSigma->text().toDouble(&ok);
    if (!ok || sigma <= 0.0) {
        showInvalid(QStringLiteral("高斯标准差必须大于 0。"));
        return false;
    }

    const double roiRecenterThreshold = roiRecenterThresholdEdit->text().toDouble(&ok);
    if (!ok || roiRecenterThreshold < 1.0 || roiRecenterThreshold > 31.0) {
        showInvalid(QStringLiteral("ROI 距边缘重居中阈值必须在 1 到 31 px 之间。"));
        return false;
    }

    const int roiRecenterRequiredFrames = roiRecenterRequiredFramesEdit->text().toInt(&ok);
    if (!ok || roiRecenterRequiredFrames < 1 || roiRecenterRequiredFrames > 100) {
        showInvalid(QStringLiteral("ROI 重居中连续帧数必须在 1 到 100 之间。"));
        return false;
    }

    const int roiRecenterCooldownMs = roiRecenterCooldownMsEdit->text().toInt(&ok);
    if (!ok || roiRecenterCooldownMs < 0 || roiRecenterCooldownMs > 60000) {
        showInvalid(QStringLiteral("ROI 重居中冷却时间必须在 0 到 60000 ms 之间。"));
        return false;
    }

    const double roiRecenterMinimumShift = roiRecenterMinimumShiftEdit->text().toDouble(&ok);
    if (!ok || roiRecenterMinimumShift < 0.0 || roiRecenterMinimumShift > 31.0) {
        showInvalid(QStringLiteral("ROI 重居中最小位移必须在 0 到 31 px 之间。"));
        return false;
    }

    const double starThresholdAbsolute = starThresholdAbsoluteEdit->text().toDouble(&ok);
    if (!ok ||
        !(qFuzzyCompare(starThresholdAbsolute, -1.0) ||
          (starThresholdAbsolute >= 0.0 && starThresholdAbsolute <= 255.0))) {
        showInvalid(QStringLiteral("全画幅找星绝对阈值必须为 -1 或 0 到 255 之间的数值。"));
        return false;
    }

    const double starSigmaThreshold = starSigmaThresholdEdit->text().toDouble(&ok);
    if (!ok || starSigmaThreshold < 0.0 || starSigmaThreshold > 20.0) {
        showInvalid(QStringLiteral("全画幅找星背景倍数必须在 0 到 20 之间。"));
        return false;
    }

    const double starPeakFraction = starPeakFractionEdit->text().toDouble(&ok);
    if (!ok || starPeakFraction < 0.01 || starPeakFraction > 0.95) {
        showInvalid(QStringLiteral("全画幅找星峰值比例必须在 0.01 到 0.95 之间。"));
        return false;
    }

    const double starMinimumIntensity = starMinimumIntensityEdit->text().toDouble(&ok);
    if (!ok || starMinimumIntensity < 0.0 || starMinimumIntensity > 255.0) {
        showInvalid(QStringLiteral("全画幅找星最低亮度必须在 0 到 255 之间。"));
        return false;
    }

    const int starMinArea = starMinAreaEdit->text().toInt(&ok);
    if (!ok || starMinArea < 1 || starMinArea > 100000) {
        showInvalid(QStringLiteral("全画幅找星最小面积必须为正整数。"));
        return false;
    }

    const int starMaxArea = starMaxAreaEdit->text().toInt(&ok);
    if (!ok || starMaxArea < starMinArea || starMaxArea > 100000) {
        showInvalid(QStringLiteral("全画幅找星最大面积必须大于或等于最小面积。"));
        return false;
    }

    const bool hotPixelEnabled = hotPixelEnableCheck && hotPixelEnableCheck->isChecked();
    const QString hotCam0Mask = hotPixelCam0MaskEdit ? hotPixelCam0MaskEdit->text().trimmed() : QString();
    const QString hotCam0Excess = hotPixelCam0ExcessEdit ? hotPixelCam0ExcessEdit->text().trimmed() : QString();
    const QString hotCam1Mask = hotPixelCam1MaskEdit ? hotPixelCam1MaskEdit->text().trimmed() : QString();
    const QString hotCam1Excess = hotPixelCam1ExcessEdit ? hotPixelCam1ExcessEdit->text().trimmed() : QString();
    const int hotTemplateWidth =
        hotPixelTemplateWidthEdit ? hotPixelTemplateWidthEdit->text().toInt(&ok) : 0;
    if (hotPixelEnabled && (!ok || hotTemplateWidth <= 0)) {
        showInvalid(QStringLiteral("启用热像素修正时，模板宽度必须为正整数。"));
        return false;
    }
    const int hotTemplateHeight =
        hotPixelTemplateHeightEdit ? hotPixelTemplateHeightEdit->text().toInt(&ok) : 0;
    if (hotPixelEnabled && (!ok || hotTemplateHeight <= 0)) {
        showInvalid(QStringLiteral("启用热像素修正时，模板高度必须为正整数。"));
        return false;
    }
    if (hotPixelEnabled) {
        const QStringList hotPixelFiles = {hotCam0Mask, hotCam0Excess, hotCam1Mask, hotCam1Excess};
        const QStringList hotPixelNames = {
            QStringLiteral("相机1 Mask"),
            QStringLiteral("相机1 Excess"),
            QStringLiteral("相机2 Mask"),
            QStringLiteral("相机2 Excess")
        };
        for (int i = 0; i < hotPixelFiles.size(); ++i) {
            if (hotPixelFiles[i].isEmpty()) {
                showInvalid(QStringLiteral("%1 文件不能为空。").arg(hotPixelNames[i]));
                return false;
            }
            const QString resolvedHotPixelFile = resolvePathFromAppDir(hotPixelFiles[i]);
            if (!QFileInfo::exists(resolvedHotPixelFile)) {
                showInvalid(QStringLiteral("%1 文件不存在: %2").arg(hotPixelNames[i], resolvedHotPixelFile));
                return false;
            }
        }
    }

    const double diameter = opticsD->text().toDouble(&ok);
    if (!ok || diameter <= 0.0) {
        showInvalid(QStringLiteral("口径 D 必须大于 0。"));
        return false;
    }

    const double baseline = opticsBaseline->text().toDouble(&ok);
    if (!ok || baseline <= diameter) {
        showInvalid(QStringLiteral("中心间距 d 必须大于子孔径直径 D。"));
        return false;
    }

    const double focal = opticsF->text().toDouble(&ok);
    if (!ok || focal <= 0.0) {
        showInvalid(QStringLiteral("焦距 f 必须大于 0。"));
        return false;
    }

    const double zenithAngle = opticsZenith->text().toDouble(&ok);
    if (!ok || zenithAngle < 0.0 || zenithAngle >= 90.0) {
        showInvalid(QStringLiteral("天顶角 Z 必须在 0 到 90 度之间。"));
        return false;
    }

    const double wavelength = detectorWavelength->text().toDouble(&ok);
    if (!ok || wavelength <= 0.0) {
        showInvalid(QStringLiteral("对比波长必须大于 0。"));
        return false;
    }

    const double pixelSize = detectorPixelSize->text().toDouble(&ok);
    if (!ok || pixelSize <= 0.0) {
        showInvalid(QStringLiteral("像素尺寸必须大于 0。"));
        return false;
    }

    const double alignmentFocalLength =
        alignmentFocalLengthEdit ? alignmentFocalLengthEdit->text().toDouble(&ok) : 269.0;
    if (!ok || alignmentFocalLength <= 0.0) {
        showInvalid(QStringLiteral("对准焦距必须大于 0。"));
        return false;
    }

    const double alignmentPixelSize =
        alignmentPixelSizeEdit ? alignmentPixelSizeEdit->text().toDouble(&ok) : 2.5;
    if (!ok || alignmentPixelSize <= 0.0) {
        showInvalid(QStringLiteral("对准像元尺寸必须大于 0。"));
        return false;
    }

    const double alignmentPolarDistance =
        alignmentPolarDistanceEdit ? alignmentPolarDistanceEdit->text().toDouble(&ok)
                                   : kAlignmentDefaultPolarisPolarDistanceArcmin;
    if (!ok || alignmentPolarDistance <= 0.0) {
        showInvalid(QStringLiteral("北极星极距必须大于 0。"));
        return false;
    }

    const double alignmentRadiusAdjust =
        alignmentRadiusAdjustEdit ? alignmentRadiusAdjustEdit->text().toDouble(&ok) : 0.0;
    if (!ok) {
        showInvalid(QStringLiteral("轨道半径微调必须是有效数字。"));
        return false;
    }

    const double alignmentPreviewRate =
        alignmentPreviewRateEdit ? alignmentPreviewRateEdit->text().toDouble(&ok) : 1.0;
    if (!ok || alignmentPreviewRate <= 0.0 || alignmentPreviewRate > 10.0) {
        showInvalid(QStringLiteral("对准预览频率必须在 0 到 10 Hz 之间。"));
        return false;
    }

    const int interval = saveIntervalEdit->text().toInt(&ok);
    if (!ok || interval <= 0) {
        showInvalid(QStringLiteral("保存间隔必须为正整数。"));
        return false;
    }

    const quint16 port = netPortEdit->text().toUShort(&ok);
    if (!ok || port == 0) {
        showInvalid(QStringLiteral("端口必须在 1 到 65535 之间。"));
        return false;
    }

    if (netIpEdit->text().trimmed().isEmpty()) {
        showInvalid(QStringLiteral("IP地址不能为空。"));
        return false;
    }

    if (storagePathEdit->text().trimmed().isEmpty()) {
        showInvalid(QStringLiteral("存储路径不能为空。"));
        return false;
    }

    if (onApplyCamera) {
        onApplyCamera(exposure, gain, continuousFrameRate);
    }
    if (onApplyAutoExposure) {
        onApplyAutoExposure(autoExposureCheck->isChecked(),
                            autoExpLow,
                            autoExpHigh,
                            autoExpDarkRatio,
                            autoExpBrightRatio,
                            autoExpMin,
                            autoExpMax);
    }
    if (onApplyTriggerMode && triggerContinuous && triggerHardware) {
        onApplyTriggerMode(triggerContinuous->isChecked() ? 0 : 1);
    }
    if (pulseEnableCheck->isChecked() && pulsePortEdit->text().trimmed().isEmpty()) {
        showInvalid(QStringLiteral("启用脉冲板时，串口不能为空。"));
        return false;
    }

    const double pulseFrequency = pulseFreqEdit->text().toDouble(&ok);
    if (!ok || pulseFrequency <= 0.0) {
        showInvalid(QStringLiteral("输出频率必须大于 0。"));
        return false;
    }

    const quint32 pulseCount = pulseCountEdit->text().toUInt(&ok);
    if (!ok || pulseCount == 0U) {
        showInvalid(QStringLiteral("脉冲个数必须为正整数。"));
        return false;
    }

    const double pulseDuty = pulseDutyEdit->text().toDouble(&ok);
    if (!ok || pulseDuty <= 0.0 || pulseDuty >= 100.0) {
        showInvalid(QStringLiteral("占空比必须在 0 到 100 之间。"));
        return false;
    }

    m_committedPulseFrequencyHz = pulseFrequency;
    m_committedPulseCount = pulseCount;
    m_committedPulseDutyPercent = pulseDuty;
    m_committedPulseRemoteControl = pulseSourceRemote && pulseSourceRemote->isChecked();

    if (!applyCommittedPulseSettings(true)) {
        const QString pulseMessage =
            (applyStatusLabel && !applyStatusLabel->text().trimmed().isEmpty())
                ? applyStatusLabel->text().trimmed()
                : QStringLiteral("触发设置存在未完成提交或参数无效。");
        QMessageBox::warning(this, QStringLiteral("参数错误"), pulseMessage);
        return false;
    }
    if (onApplyProcessing && procGravity && procGaussian) {
        onApplyProcessing(kernelSize, sigma, procGravity->isChecked() ? 0 : 1);
    }
    if (onApplyRoiRecentering) {
        onApplyRoiRecentering(roiRecenterThreshold,
                              roiRecenterRequiredFrames,
                              roiRecenterCooldownMs,
                              roiRecenterMinimumShift);
    }
    if (onApplyFullFrameStarDetection) {
        onApplyFullFrameStarDetection(starThresholdAbsolute,
                                      starSigmaThreshold,
                                      starPeakFraction,
                                      starMinimumIntensity,
                                      starMinArea,
                                      starMaxArea);
    }
    if (onApplyHotPixelTemplates) {
        onApplyHotPixelTemplates(hotPixelEnabled,
                                 hotCam0Mask,
                                 hotCam0Excess,
                                 hotCam1Mask,
                                 hotCam1Excess,
                                 hotPixelEnabled ? hotTemplateWidth : 0,
                                 hotPixelEnabled ? hotTemplateHeight : 0);
    }
    if (onApplyOptics) {
        onApplyOptics(diameter, baseline, focal, zenithAngle, wavelength, pixelSize);
    }
    if (onApplyAlignment) {
        onApplyAlignment(alignmentAutoRadiusCheck ? alignmentAutoRadiusCheck->isChecked() : true,
                         alignmentFocalLength,
                         alignmentPixelSize,
                         alignmentPolarDistance,
                         alignmentRadiusAdjust,
                         alignmentPreviewRate);
    }
    if (onApplyStorage) {
        onApplyStorage(storagePathEdit->text().trimmed(), interval);
    }
    if (onApplyNetwork) {
        onApplyNetwork(netIpEdit->text().trimmed(), port);
    }
    if (onAfterApply) {
        onAfterApply();
    } else if (applyStatusLabel) {
        applyStatusLabel->setText(QStringLiteral("设置已应用到当前配置"));
        applyStatusLabel->setStyleSheet(statusLabelStyle(UiStatusLevel::Success));
    }
    return true;
}

DIMM::DIMM(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui_DIMM)
{
    ui->setupUi(this);

    m_autoExposureIntervalMs = kAutoExposureDefaultIntervalMs;
    m_settingsDialog = new SettingsDialog(this);

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
                          ui->lblStatWindow}) {
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
    if (ui->toolbar) {
        ui->toolbar->insertAction(ui->btnSettings, m_actionConfirmCamera2Polaris);
        ui->toolbar->insertAction(m_actionConfirmCamera2Polaris, m_actionConfirmCamera1Polaris);
    }
    if (ui->menuTools) {
        ui->menuTools->insertAction(ui->actionROISchedule, m_actionConfirmCamera2Polaris);
        ui->menuTools->insertAction(m_actionConfirmCamera2Polaris, m_actionConfirmCamera1Polaris);
    }

    hideLegacyRoiScheduleUi();

    m_cameraManager = &CameraManager::instance();
    m_cameraManager->init();
    m_imageProcessor = new ImageProcessor(this);
    m_imageProcessor->setTargetFrameRateHz(m_pulseGeneratorFrequencyHz);
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
                m_hotPixelCamera0MaskPath = relativizePathToAppDir(hotSettings.camera0Mask);
                m_hotPixelCamera0ExcessPath = relativizePathToAppDir(hotSettings.camera0Excess);
                m_hotPixelCamera1MaskPath = relativizePathToAppDir(hotSettings.camera1Mask);
                m_hotPixelCamera1ExcessPath = relativizePathToAppDir(hotSettings.camera1Excess);
                m_hotPixelTemplateWidth = hotSettings.width;
                m_hotPixelTemplateHeight = hotSettings.height;
                m_hotPixelTemplateExposureUs =
                    exposureUsFromTemplatePath(m_hotPixelCamera0MaskPath);
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
    m_fullFrameCanvas1 = new FullFrameCanvas(cam1Panel);
    cam1PanelLayout->addWidget(m_lblFullFrameCam1);
    cam1PanelLayout->addWidget(m_fullFrameCanvas1, 1);

    auto* cam2Panel = new QFrame(ui->previewCanvas);
    cam2Panel->setObjectName(QStringLiteral("fullFrameCam2Panel"));
    auto* cam2PanelLayout = new QVBoxLayout(cam2Panel);
    cam2PanelLayout->setContentsMargins(0, 0, 0, 0);
    cam2PanelLayout->setSpacing(6);
    m_lblFullFrameCam2 = new QLabel(QStringLiteral("全画幅预览 - 相机2"), cam2Panel);
    m_lblFullFrameCam2->setAlignment(Qt::AlignCenter);
    m_fullFrameCanvas2 = new FullFrameCanvas(cam2Panel);
    cam2PanelLayout->addWidget(m_lblFullFrameCam2);
    cam2PanelLayout->addWidget(m_fullFrameCanvas2, 1);

    dualPreviewLayout->addWidget(cam1Panel, 1);
    dualPreviewLayout->addWidget(cam2Panel, 1);
    previewCanvasLayout->addLayout(dualPreviewLayout, 1);

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
        [this](bool enabled,
               double lowThreshold,
               double highThreshold,
               double darkRatio,
               double brightRatio,
               double minExposure,
               double maxExposure) {
            m_autoExposureEnabled = enabled;
            m_autoExposureLowThreshold = lowThreshold;
            m_autoExposureHighThreshold = highThreshold;
            m_autoExposureDarkRatio = darkRatio;
            m_autoExposureBrightRatio = brightRatio;
            m_autoExposureMinUs = minExposure;
            m_autoExposureMaxUs = maxExposure;
            m_lastAutoExposureCheckMs = -1;
            m_autoExposurePeakSamples[0].clear();
            m_autoExposurePeakSamples[1].clear();
            setStatusMessage(enabled ? QStringLiteral("自动曝光已启用: 每4小时检查ROI峰值并匹配热像素模板")
                                     : QStringLiteral("自动曝光已关闭"),
                             enabled ? UiStatusLevel::Success : UiStatusLevel::Warning);
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
            m_hotPixelCamera0MaskPath = enabled ? relativizePathToAppDir(camera0MaskPath) : QString();
            m_hotPixelCamera0ExcessPath = enabled ? relativizePathToAppDir(camera0ExcessPath) : QString();
            m_hotPixelCamera1MaskPath = enabled ? relativizePathToAppDir(camera1MaskPath) : QString();
            m_hotPixelCamera1ExcessPath = enabled ? relativizePathToAppDir(camera1ExcessPath) : QString();
            m_hotPixelTemplateWidth = enabled ? templateWidth : 0;
            m_hotPixelTemplateHeight = enabled ? templateHeight : 0;
            m_hotPixelTemplateExposureUs =
                enabled ? exposureUsFromTemplatePath(m_hotPixelCamera0MaskPath) : 0;

            bool matchedRequestedExposureTemplate = false;
            bool missingRequestedExposureTemplate = false;
            const int requestedExposureUs = static_cast<int>(std::lround(m_configExposureUs));
            if (enabled) {
                QString resolvedCamera0Mask;
                QString resolvedCamera0Excess;
                QString resolvedCamera1Mask;
                QString resolvedCamera1Excess;
                if (requestedExposureUs > 0 &&
                    requestedExposureUs != m_hotPixelTemplateExposureUs &&
                    resolveHotPixelTemplatePathsForExposure(requestedExposureUs,
                                                            &resolvedCamera0Mask,
                                                            &resolvedCamera0Excess,
                                                            &resolvedCamera1Mask,
                                                            &resolvedCamera1Excess)) {
                    m_hotPixelCamera0MaskPath = resolvedCamera0Mask;
                    m_hotPixelCamera0ExcessPath = resolvedCamera0Excess;
                    m_hotPixelCamera1MaskPath = resolvedCamera1Mask;
                    m_hotPixelCamera1ExcessPath = resolvedCamera1Excess;
                    m_hotPixelTemplateExposureUs = requestedExposureUs;
                    if (m_settingsDialog) {
                        m_settingsDialog->hotPixelCam0MaskEdit->setText(m_hotPixelCamera0MaskPath);
                        m_settingsDialog->hotPixelCam0ExcessEdit->setText(m_hotPixelCamera0ExcessPath);
                        m_settingsDialog->hotPixelCam1MaskEdit->setText(m_hotPixelCamera1MaskPath);
                        m_settingsDialog->hotPixelCam1ExcessEdit->setText(m_hotPixelCamera1ExcessPath);
                    }
                    matchedRequestedExposureTemplate = true;
                } else if (requestedExposureUs > 0 &&
                           requestedExposureUs != m_hotPixelTemplateExposureUs) {
                    missingRequestedExposureTemplate = true;
                }
            }

            m_imageProcessor->configureHotPixelTemplates(resolvePathFromAppDir(m_hotPixelCamera0MaskPath),
                                                         resolvePathFromAppDir(m_hotPixelCamera0ExcessPath),
                                                         resolvePathFromAppDir(m_hotPixelCamera1MaskPath),
                                                         resolvePathFromAppDir(m_hotPixelCamera1ExcessPath),
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
               double focalLengthMm,
               double zenithAngleDeg,
               double lambdaNm,
               double pixelSizeUm) {
            m_imageProcessor->setOpticalParams(apertureDiameterMm,
                                               baselineSeparationMm,
                                               focalLengthMm,
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
        applyAutoExposure(camIdx, peakValue);

        if (m_captureState == CaptureState::Simulation &&
            !runtime.simulationRoiSeeded &&
            !hadBothCentroids &&
            hasValidCentroidsForRoiUpdate()) {
            updateMinuteRoi(true);
            runtime.simulationRoiSeeded = true;
        }

        if (m_captureState == CaptureState::Live &&
            m_liveStartupPhase == LiveStartupPhase::Tracking &&
            shouldUpdateRoiForRecentering()) {
            updateMinuteRoi(true);
        }
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

    connect(m_imageProcessor,
            &ImageProcessor::measurementDiagnosticReady,
            this,
            [this](int camIdx, bool centroidDetected, bool measurementUsable, int rejectReason) {
        if (!hasActiveCapture() || camIdx < 0 || camIdx >= 2) {
            return;
        }

        auto& runtime = activeRuntime();
        if (centroidDetected) {
            ++runtime.detectedCentroidCount;
            ++runtime.detectedCentroidCountPerCamera[camIdx];
        }
        if (measurementUsable) {
            ++runtime.usableCentroidCount;
            ++runtime.usableCentroidCountPerCamera[camIdx];
            return;
        }

        switch (static_cast<MeasurementRejectReason>(rejectReason)) {
        case MeasurementRejectReason::EdgeSignal:
            ++runtime.edgeRejectedCountPerCamera[camIdx];
            break;
        case MeasurementRejectReason::RoiInvalid:
            ++runtime.roiInvalidRejectedCountPerCamera[camIdx];
            break;
        case MeasurementRejectReason::NoCentroid:
            ++runtime.noCentroidRejectedCountPerCamera[camIdx];
            break;
        case MeasurementRejectReason::QualityGate:
            ++runtime.qualityRejectedCountPerCamera[camIdx];
            break;
        case MeasurementRejectReason::None:
            break;
        }
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

        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (runtime.lastMeasurementUiUpdateMs < 0 ||
            (nowMs - runtime.lastMeasurementUiUpdateMs) >= kMeasurementUiIntervalMs) {
            runtime.lastMeasurementUiUpdateMs = nowMs;
            refreshMeasurementUi();
        }
    });

    connect(m_imageProcessor, &ImageProcessor::syncSampleReady, this, [this](double syncDeltaRawUs) {
        if (!hasActiveCapture()) {
            return;
        }
        auto& runtime = activeRuntime();
        ++runtime.syncSampleCount;

        runtime.latestSyncDeltaRawUs = syncDeltaRawUs;
        if (runtime.syncOffsetSampleCount < kSyncOffsetCalibrationSamples) {
            ++runtime.syncOffsetSampleCount;
            runtime.syncOffsetUs +=
                (syncDeltaRawUs - runtime.syncOffsetUs) /
                static_cast<double>(runtime.syncOffsetSampleCount);
        }

        const double syncJitterUs = std::abs(syncDeltaRawUs - runtime.syncOffsetUs);
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

}

DIMM::~DIMM()
{
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
        QStringLiteral("原始/入处理: %1 / %2 | 延迟 %3 ms")
            .arg(QString::number(runtime.frameCount),
                 QString::number(runtime.processedFrameCount),
                 QString::number(runtime.averageProcessingLatencyMs, 'f', 2)));
    ui->lblStatValid->setText(
        QStringLiteral("检测质心/可用: %1 / %2")
            .arg(QString::number(runtime.detectedCentroidCount),
                 QString::number(runtime.usableCentroidCount)));
    ui->lblStatLatency->setText(
        QStringLiteral("可用 相机1/相机2: %1 / %2 | 配对/丢弃: %3 / %4")
            .arg(QString::number(runtime.usableCentroidCountPerCamera[0]),
                 QString::number(runtime.usableCentroidCountPerCamera[1]),
                 QString::number(runtime.pairedSampleCount),
                 QString::number(runtime.droppedUnpairedSampleCount)));
    const quint64 edgeRejected =
        runtime.edgeRejectedCountPerCamera[0] + runtime.edgeRejectedCountPerCamera[1];
    const quint64 qualityRejected =
        runtime.qualityRejectedCountPerCamera[0] + runtime.qualityRejectedCountPerCamera[1] +
        runtime.noCentroidRejectedCountPerCamera[0] + runtime.noCentroidRejectedCountPerCamera[1];
    const quint64 roiInvalidRejected =
        runtime.roiInvalidRejectedCountPerCamera[0] + runtime.roiInvalidRejectedCountPerCamera[1];
    ui->lblStatWindow->setText(
        QStringLiteral("拒绝 边缘/质量/ROI: %1 / %2 / %3 | 同步 %4 μs")
            .arg(QString::number(edgeRejected),
                 QString::number(qualityRejected),
                 QString::number(roiInvalidRejected),
                 QString::number(runtime.averageSyncJitterUs, 'f', 1)));

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
    m_autoExposurePeakSamples[0].clear();
    m_autoExposurePeakSamples[1].clear();
    m_lastAutoExposureCheckMs = -1;
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

    InitialStarCandidate peakCandidate;
    if (!detectInitialStarPeakCandidate(fullFrame, &peakCandidate, peakValue)) {
        return false;
    }

    const auto& runtime = activeRuntime();
    if (runtime.hasLastTargetPosition[cameraIndex]) {
        const QPointF delta = peakCandidate.center - runtime.lastTargetPosition[cameraIndex];
        peakCandidate.distanceToPreference = std::hypot(delta.x(), delta.y());
    }
    *centroid = peakCandidate.center;
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
    QPointF centroid;
    double peakValue = 0.0;
    if (liveLocatePhase) {
        if (!selectLiveRelocalizationCentroid(cameraIndex, grayscale, &centroid, &peakValue)) {
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
        if (candidates.isEmpty()) {
            InitialStarCandidate peakCandidate;
            if (detectInitialStarPeakCandidate(grayscale, &peakCandidate, &peakValue)) {
                candidates.append(peakCandidate);
            }
        }
        FullFrameCanvas* targetCanvas = cameraIndex == 0 ? m_fullFrameCanvas1 : m_fullFrameCanvas2;
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
                setStatusMessage(
                    QStringLiteral("状态: 相机%1 Multiple star candidate confirmation is pending; "
                                   "waiting for a valid full-frame candidate list")
                        .arg(cameraIndex + 1),
                    UiStatusLevel::Warning);
                return false;
            }
            if (!detectInitialStarCentroid(grayscale, &centroid, &peakValue) &&
                !detectInitialStarCentroidFast(grayscale, &centroid, &peakValue)) {
                if (targetCanvas) {
                    targetCanvas->clearStarCandidateOverlays();
                }
                return false;
            }
            if (targetCanvas) {
                targetCanvas->clearStarCandidateOverlays();
            }
            runtime.pendingInitialCandidateSelectionRequired[cameraIndex] = false;
        } else {
            if (targetCanvas) {
                targetCanvas->setStarCandidateOverlays(
                    buildCandidateOverlays(candidates,
                                           runtime.selectedInitialCandidateIndex[cameraIndex]));
            }

            InitialStarSelection selection =
                selectInitialStarCandidate(candidates,
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
                if (!chooseAutomaticInitialStarCandidate(candidates,
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
                    buildCandidateOverlays(candidates, selection.candidate.index));
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
    if (cameraIndex < 0 || cameraIndex >= 2) {
        return;
    }

    auto& runtime = m_liveRuntime;
    m_alignmentSelectionRequested[cameraIndex] = true;
    m_alignmentLastPreviewMs[cameraIndex] = -1;
    runtime.selectedInitialCandidateIndex[cameraIndex] = -1;
    runtime.lastInitialCandidatePromptMs[cameraIndex] = -1;
    setStatusMessage(QStringLiteral("状态: 已请求确认相机%1的北极星，下一张全画幅候选列表将弹出编号确认")
                         .arg(cameraIndex + 1),
                     UiStatusLevel::Info);
}

bool DIMM::startAlignmentMode(QString* reason)
{
    if (!m_cameraManager) {
        if (reason) {
            *reason = QStringLiteral("相机管理器未初始化。");
        }
        return false;
    }

    if (m_captureState == CaptureState::Alignment) {
        return true;
    }

    if (m_captureState != CaptureState::Idle && m_captureState != CaptureState::Paused) {
        if (reason) {
            *reason = QStringLiteral("请先停止当前采集或模拟采集，再进入对准模式。");
        }
        return false;
    }

    if (openCameraCount() < 2) {
        if (reason) {
            *reason = QStringLiteral("对准模式需要两台相机均已连接。");
        }
        return false;
    }

    if (m_captureState == CaptureState::Paused) {
        stopLiveCapture();
        updateCaptureState(CaptureState::Idle);
    }

    m_cameraManager->stopAll();
    for (int i = 0; i < 2; ++i) {
        if (!m_cameraManager->isOpen(i)) {
            continue;
        }
        if (!m_cameraManager->prepareFullFrame(i)) {
            if (reason) {
                *reason = QStringLiteral("相机%1切换全画幅失败。").arg(i + 1);
            }
            return false;
        }
        if (!m_cameraManager->setTriggerMode(i, TriggerMode::Continuous)) {
            if (reason) {
                *reason = QStringLiteral("相机%1切换连续取图失败。").arg(i + 1);
            }
            return false;
        }
        m_cameraManager->setFrameRate(i, std::max(0.1, m_alignmentPreviewRateHz));
    }

    m_alignmentLastPreviewMs[0] = -1;
    m_alignmentLastPreviewMs[1] = -1;
    m_alignmentSelectionRequested[0] = false;
    m_alignmentSelectionRequested[1] = false;
    auto& runtime = m_liveRuntime;
    for (int i = 0; i < 2; ++i) {
        runtime.confirmedPolarisPosition[i] = QPointF();
        runtime.hasConfirmedPolarisPosition[i] = false;
        runtime.lastTargetPosition[i] = QPointF();
        runtime.hasLastTargetPosition[i] = false;
        runtime.selectedInitialCandidateIndex[i] = -1;
        runtime.pendingInitialCandidateSelectionRequired[i] = false;
        runtime.lastInitialCandidatePromptMs[i] = -1;
    }
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

    if (!m_cameraManager->startAll()) {
        if (reason) {
            *reason = QStringLiteral("对准模式启动相机连续取图失败。");
        }
        return false;
    }

    setDetailViewMode(DetailViewMode::None);
    updateCaptureState(CaptureState::Alignment);
    setStatusMessage(QStringLiteral("状态: 对准模式已启动，双相机 %1 Hz 全画幅预览")
                         .arg(m_alignmentPreviewRateHz, 0, 'f', 1),
                     UiStatusLevel::Info);
    return true;
}

void DIMM::stopAlignmentMode()
{
    if (m_captureState != CaptureState::Alignment) {
        return;
    }

    if (m_cameraManager) {
        m_cameraManager->stopAll();
        for (int i = 0; i < 2; ++i) {
            if (!m_cameraManager->isOpen(i)) {
                continue;
            }
            if (m_configTriggerMode == 0) {
                m_cameraManager->setTriggerMode(i, TriggerMode::Continuous);
            } else {
                m_cameraManager->configureExternalTrigger(i);
            }
        }
    }

    m_alignmentSelectionRequested[0] = false;
    m_alignmentSelectionRequested[1] = false;

    if (m_fullFrameCanvas1) {
        m_fullFrameCanvas1->clearAlignmentOverlay();
    }
    if (m_fullFrameCanvas2) {
        m_fullFrameCanvas2->clearAlignmentOverlay();
    }

    updateCaptureState(CaptureState::Idle);
    setDetailViewMode(DetailViewMode::RoiOnly);
    setStatusMessage(QStringLiteral("状态: 已退出对准模式"), UiStatusLevel::Warning);
}

double DIMM::alignmentOrbitRadiusPx() const
{
    const double focalLengthMm = std::max(1.0, m_alignmentFocalLengthMm);
    const double pixelSizeMm = std::max(0.001, m_alignmentPixelSizeUm / 1000.0);
    const double plateScaleArcsecPerPx = 206265.0 * pixelSizeMm / focalLengthMm;
    const double polarDistanceArcsec = std::max(0.0, m_alignmentPolarisPolarDistanceArcmin) * 60.0;
    const double autoRadius = plateScaleArcsecPerPx > 0.0
                                  ? polarDistanceArcsec / plateScaleArcsecPerPx
                                  : 0.0;
    return std::max(1.0, (m_alignmentAutoRadius ? autoRadius : autoRadius) + m_alignmentRadiusAdjustPx);
}

void DIMM::handleAlignmentFramePacket(int cameraIndex, const CameraFrame& packet)
{
    if (m_captureState != CaptureState::Alignment ||
        cameraIndex < 0 ||
        cameraIndex >= 2 ||
        packet.image.empty()) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const int intervalMs = std::max(100, static_cast<int>(1000.0 / std::max(0.1, m_alignmentPreviewRateHz)));
    if (m_alignmentLastPreviewMs[cameraIndex] >= 0 &&
        nowMs - m_alignmentLastPreviewMs[cameraIndex] < intervalMs) {
        return;
    }

    FullFrameCanvas* targetCanvas = cameraIndex == 0 ? m_fullFrameCanvas1 : m_fullFrameCanvas2;
    if (!targetCanvas) {
        return;
    }

    targetCanvas->setImage(packet.image);
    targetCanvas->setRoiList({});
    targetCanvas->setCurrentRoi(-1);
    updateAlignmentOverlay(cameraIndex, packet.image);
    m_alignmentLastPreviewMs[cameraIndex] = nowMs;
}

void DIMM::updateAlignmentOverlay(int cameraIndex, const cv::Mat& frame)
{
    if (cameraIndex < 0 || cameraIndex >= 2 || frame.empty()) {
        return;
    }

    FullFrameCanvas* targetCanvas = cameraIndex == 0 ? m_fullFrameCanvas1 : m_fullFrameCanvas2;
    if (!targetCanvas) {
        return;
    }

    cv::Mat grayscale;
    if (frame.channels() == 1) {
        grayscale = frame;
    } else {
        cv::cvtColor(frame, grayscale, cv::COLOR_BGR2GRAY);
    }

    cv::Mat mono8 = normalizeInitialStarDetectionFrame(grayscale);
    if (mono8.empty()) {
        return;
    }

    FullFrameCanvas::AlignmentOverlay overlay;
    overlay.enabled = true;
    overlay.orbitCenter = QPointF((frame.cols - 1) * 0.5, (frame.rows - 1) * 0.5);
    overlay.orbitRadiusPx = alignmentOrbitRadiusPx();

    auto& runtime = m_liveRuntime;
    QPointF star;
    double peakValue = 0.0;
    const QVector<InitialStarCandidate> candidates = detectInitialStarCandidates(mono8, &peakValue);
    if (!candidates.isEmpty()) {
        targetCanvas->setStarCandidateOverlays(
            buildCandidateOverlays(candidates, runtime.selectedInitialCandidateIndex[cameraIndex]));

        const bool manualSelectionRequested = m_alignmentSelectionRequested[cameraIndex];
        const bool hadConfirmedPolarisBeforeSelection =
            runtime.hasConfirmedPolarisPosition[cameraIndex];
        const bool hasPreferredTarget =
            !manualSelectionRequested &&
            (runtime.hasConfirmedPolarisPosition[cameraIndex] ||
             runtime.hasLastTargetPosition[cameraIndex]);
        const QPointF preferredTarget = runtime.hasConfirmedPolarisPosition[cameraIndex]
                                            ? runtime.confirmedPolarisPosition[cameraIndex]
                                            : runtime.lastTargetPosition[cameraIndex];
        InitialStarSelection selection =
            selectInitialStarCandidate(candidates,
                                       hasPreferredTarget,
                                       preferredTarget,
                                       runtime.selectedInitialCandidateIndex[cameraIndex]);
        runtime.pendingInitialCandidateSelectionRequired[cameraIndex] =
            selection.requiresUserSelection;
        if (!selection.selected && selection.requiresUserSelection && !manualSelectionRequested) {
            setStatusMessage(QStringLiteral("状态: 相机%1显示到多个候选星点，请点击“确认相机%1的北极星”后选择编号")
                                 .arg(cameraIndex + 1),
                             UiStatusLevel::Info);
        }
        if (manualSelectionRequested) {
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            const qint64 lastPromptMs = runtime.lastInitialCandidatePromptMs[cameraIndex];
            if (lastPromptMs < 0 || nowMs - lastPromptMs >= 2000) {
                QStringList candidateLines;
                candidateLines.reserve(candidates.size());
                for (const InitialStarCandidate& candidate : candidates) {
                    const QString distanceText =
                        std::isfinite(candidate.distanceToPreference)
                            ? QString::number(candidate.distanceToPreference, 'f', 1)
                            : QStringLiteral("--");
                    candidateLines << QStringLiteral("候选 %1: 中心=(%2, %3), 面积=%4, 峰值=%5, 距离上次=%6 px")
                                          .arg(candidate.index)
                                          .arg(candidate.center.x(), 0, 'f', 1)
                                          .arg(candidate.center.y(), 0, 'f', 1)
                                          .arg(candidate.area)
                                          .arg(candidate.peak, 0, 'f', 1)
                                          .arg(distanceText);
                }

                bool ok = false;
                m_alignmentSelectionRequested[cameraIndex] = false;
                const int chosenCandidateIndex =
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
                if (!ok) {
                    runtime.lastInitialCandidatePromptMs[cameraIndex] = nowMs;
                    setStatusMessage(QStringLiteral("状态: 相机%1对准候选星点选择已取消，保留候选框等待确认")
                                         .arg(cameraIndex + 1),
                                     UiStatusLevel::Warning);
                    targetCanvas->setAlignmentOverlay(overlay);
                    return;
                }

                runtime.selectedInitialCandidateIndex[cameraIndex] = chosenCandidateIndex;
                runtime.lastInitialCandidatePromptMs[cameraIndex] = -1;
                targetCanvas->setStarCandidateOverlays(
                    buildCandidateOverlays(candidates, chosenCandidateIndex));
                selection = selectInitialStarCandidate(candidates,
                                                       false,
                                                       preferredTarget,
                                                       runtime.selectedInitialCandidateIndex[cameraIndex]);
                runtime.pendingInitialCandidateSelectionRequired[cameraIndex] =
                    selection.requiresUserSelection;
            }
        }

        const bool canApplyAlignmentSelection =
            manualSelectionRequested || hadConfirmedPolarisBeforeSelection;
        if (selection.selected && canApplyAlignmentSelection) {
            star = selection.candidate.center;
            runtime.confirmedPolarisPosition[cameraIndex] = star;
            runtime.hasConfirmedPolarisPosition[cameraIndex] = true;
            runtime.lastTargetPosition[cameraIndex] = star;
            runtime.hasLastTargetPosition[cameraIndex] = true;
            runtime.pendingInitialCandidateSelectionRequired[cameraIndex] = false;
            runtime.selectedInitialCandidateIndex[cameraIndex] = selection.candidate.index;
            m_alignmentSelectionRequested[cameraIndex] = false;
            targetCanvas->setStarCandidateOverlays(
                buildCandidateOverlays(candidates, selection.candidate.index));
            refreshActionStates();
        }
    } else {
        targetCanvas->clearStarCandidateOverlays();
        runtime.pendingInitialCandidateSelectionRequired[cameraIndex] = false;
        if (runtime.hasConfirmedPolarisPosition[cameraIndex] &&
            (detectInitialStarCentroid(mono8, &star, &peakValue) ||
             detectInitialStarCentroidFast(mono8, &star, &peakValue))) {
            runtime.confirmedPolarisPosition[cameraIndex] = star;
            runtime.hasConfirmedPolarisPosition[cameraIndex] = true;
            runtime.lastTargetPosition[cameraIndex] = star;
            runtime.hasLastTargetPosition[cameraIndex] = true;
        }
    }

    if (runtime.hasConfirmedPolarisPosition[cameraIndex]) {
        star = runtime.confirmedPolarisPosition[cameraIndex];
        const QPointF delta = star - overlay.orbitCenter;
        const double distance = std::hypot(delta.x(), delta.y());
        overlay.hasStar = true;
        overlay.starPosition = star;
        overlay.deviationPx = std::abs(distance - overlay.orbitRadiusPx);
        overlay.label = QStringLiteral("偏离轨道: %1 px").arg(overlay.deviationPx, 0, 'f', 1);
    }

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
    m_settingsDialog->autoExposureCheck->setChecked(m_autoExposureEnabled);
    m_settingsDialog->autoExpLowEdit->setText(QString::number(m_autoExposureLowThreshold, 'f', 1));
    m_settingsDialog->autoExpHighEdit->setText(QString::number(m_autoExposureHighThreshold, 'f', 1));
    m_settingsDialog->autoExpDarkRatioEdit->setText(QString::number(m_autoExposureDarkRatio, 'f', 2));
    m_settingsDialog->autoExpBrightRatioEdit->setText(QString::number(m_autoExposureBrightRatio, 'f', 2));
    m_settingsDialog->autoExpMinEdit->setText(QString::number(m_autoExposureMinUs, 'f', 0));
    m_settingsDialog->autoExpMaxEdit->setText(QString::number(m_autoExposureMaxUs, 'f', 0));
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
    m_settingsDialog->hotPixelCam0MaskEdit->setText(relativizePathToAppDir(m_hotPixelCamera0MaskPath));
    m_settingsDialog->hotPixelCam0ExcessEdit->setText(relativizePathToAppDir(m_hotPixelCamera0ExcessPath));
    m_settingsDialog->hotPixelCam1MaskEdit->setText(relativizePathToAppDir(m_hotPixelCamera1MaskPath));
    m_settingsDialog->hotPixelCam1ExcessEdit->setText(relativizePathToAppDir(m_hotPixelCamera1ExcessPath));
    m_settingsDialog->hotPixelTemplateWidthEdit->setText(QString::number(m_hotPixelTemplateWidth));
    m_settingsDialog->hotPixelTemplateHeightEdit->setText(QString::number(m_hotPixelTemplateHeight));
    m_settingsDialog->opticsD->setText(QString::number(m_imageProcessor->apertureDiameterMm(), 'f', 1));
    m_settingsDialog->opticsBaseline->setText(QString::number(m_imageProcessor->baselineSeparationMm(), 'f', 1));
    m_settingsDialog->opticsF->setText(QString::number(m_imageProcessor->focalLengthMm(), 'f', 1));
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
    if (m_resultFile) {
        m_resultFile->flush();
    }

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
    if (m_resultFile) {
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
    m_resultFilePath = filename;
    m_resultFileState = m_captureState;
    m_resultFile = new QFile(filename, this);
    if (m_resultFile->open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_resultStream = new QTextStream(m_resultFile);
        *m_resultStream
            << "# capture_mode=" << captureModeName() << ", capture_label=" << captureModeLabel() << "\n"
            << "timestamp,mode,frame,paired_samples,dropped_unpaired_samples,"
               "roi_acquisition_generation,roi_update_count,roi_update_reason,"
               "roi1_x,roi1_y,roi1_w,roi1_h,roi2_x,roi2_y,roi2_w,roi2_h,ms_since_last_roi_update,"
               "continuous_frame_rate_target_hz,camera1_frame_rate_readback_hz,camera2_frame_rate_readback_hz,"
               "r0_cm,seeing_arcsec,theta0_arcsec,tau0_ms,"
               "sync_delta_raw_us,sync_delta_offset_us,sync_jitter_us,sync_jitter_avg_us,sync_jitter_max_us,"
               "comm_connected,reporting_enabled\n";
        m_resultStream->flush();
    } else {
        setStatusMessage(QStringLiteral("结果文件创建失败"), UiStatusLevel::Error);
    }
}

void DIMM::closeResultFile()
{
    flushPendingWrites();

    delete m_resultStream;
    m_resultStream = nullptr;

    if (m_resultFile) {
        m_resultFile->close();
        delete m_resultFile;
        m_resultFile = nullptr;
    }
    m_resultFileState = CaptureState::Idle;
    m_pendingWrites.clear();
}

void DIMM::saveResultRow(int frame)
{
    ++m_resultRowsSeen;
    const int interval = qMax(1, m_saveInterval);
    if ((m_resultRowsSeen - 1) % interval != 0) {
        return;
    }

    if (!m_resultFile) {
        initResultFile();
    }
    if (!m_resultStream) {
        return;
    }
    if (m_resultFileState != m_captureState) {
        closeResultFile();
        initResultFile();
        if (!m_resultStream) {
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
    QString roiUpdateReason = m_lastRoiUpdateReason;
    roiUpdateReason.replace(',', ';');

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
        QString::number(runtime.latestAtmosphere.r0, 'f', 3),
        QString::number(runtime.latestAtmosphere.seeing, 'f', 3),
        QString::number(runtime.latestAtmosphere.theta0, 'f', 3),
        QString::number(runtime.latestAtmosphere.tau0, 'f', 3),
        QString::number(runtime.latestSyncDeltaRawUs, 'f', 3),
        QString::number(runtime.syncOffsetUs, 'f', 3),
        QString::number(runtime.latestSyncJitterUs, 'f', 3),
        QString::number(runtime.averageSyncJitterUs, 'f', 3),
        QString::number(runtime.maxSyncJitterUs, 'f', 3),
        m_commConnected ? QStringLiteral("1") : QStringLiteral("0"),
        m_reporting ? QStringLiteral("1") : QStringLiteral("0")
    };

    m_pendingWrites.append(fields.join(','));
}

void DIMM::flushPendingWrites()
{
    if (m_pendingWrites.isEmpty() || !m_resultStream) {
        return;
    }
    for (const QString& line : std::as_const(m_pendingWrites)) {
        *m_resultStream << line << "\n";
    }
    m_resultStream->flush();
    m_pendingWrites.clear();
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

void DIMM::applyAutoExposure(int cameraIndex, double peakValue)
{
    if (!m_autoExposureEnabled || m_captureState != CaptureState::Live) {
        return;
    }
    if (cameraIndex < 0 || cameraIndex >= 2 || !m_cameraManager->isOpen(cameraIndex)) {
        return;
    }

    if (std::isfinite(peakValue) && peakValue > 0.0) {
        auto& samples = m_autoExposurePeakSamples[cameraIndex];
        samples.push_back(peakValue);
        if (samples.size() > kAutoExposureMaxPeakSamples) {
            samples.erase(samples.begin(), samples.begin() + (samples.size() - kAutoExposureMaxPeakSamples));
        }
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_lastAutoExposureCheckMs < 0) {
        m_lastAutoExposureCheckMs = nowMs;
        return;
    }
    if (nowMs - m_lastAutoExposureCheckMs < m_autoExposureIntervalMs) {
        return;
    }
    m_lastAutoExposureCheckMs = nowMs;

    const double peak0 = medianOfSamples(m_autoExposurePeakSamples[0]);
    const double peak1 = medianOfSamples(m_autoExposurePeakSamples[1]);
    m_autoExposurePeakSamples[0].clear();
    m_autoExposurePeakSamples[1].clear();
    if (peak0 <= 0.0 || peak1 <= 0.0) {
        setStatusMessage(QStringLiteral("自动曝光: 峰值样本不足，保持当前曝光"), UiStatusLevel::Warning);
        return;
    }

    const bool dark0 = peak0 < m_autoExposureLowThreshold;
    const bool dark1 = peak1 < m_autoExposureLowThreshold;
    const bool bright0 = peak0 > m_autoExposureHighThreshold;
    const bool bright1 = peak1 > m_autoExposureHighThreshold;
    if ((dark0 || dark1) && (bright0 || bright1)) {
        setStatusMessage(QStringLiteral("自动曝光: 两台相机亮度趋势冲突，保持当前曝光"), UiStatusLevel::Warning);
        return;
    }

    double decisionPeak = (peak0 + peak1) * 0.5;
    if (dark0 || dark1) {
        decisionPeak = std::min(peak0, peak1);
    } else if (bright0 || bright1) {
        decisionPeak = std::max(peak0, peak1);
    }

    double currentExposure = m_configExposureUs;
    if (m_cameraManager->isOpen(0)) {
        const double cameraExposure = m_cameraManager->getExposure(0);
        if (cameraExposure > 0.0) {
            currentExposure = cameraExposure;
        }
    }
    if (currentExposure <= 0.0) {
        return;
    }

    const int targetExposure = selectTemplateExposureForPeak(currentExposure, decisionPeak);
    if (targetExposure <= 0 || qAbs(static_cast<double>(targetExposure) - currentExposure) < 1.0) {
        setStatusMessage(QStringLiteral("自动曝光: ROI峰值正常，保持 %1 μs")
                             .arg(currentExposure, 0, 'f', 0),
                         UiStatusLevel::Info);
        return;
    }

    QString reason;
    if (!applyExposureAndHotPixelTemplate(targetExposure, &reason)) {
        setStatusMessage(reason.isEmpty()
                             ? QStringLiteral("自动曝光: 曝光/热像素模板切换失败")
                             : reason,
                         UiStatusLevel::Error);
        return;
    }

    setStatusMessage(QStringLiteral("自动曝光: %1 -> %2 μs，已同步热像素模板 (峰值 %3 / %4)")
                         .arg(currentExposure, 0, 'f', 0)
                         .arg(targetExposure)
                         .arg(peak0, 0, 'f', 1)
                         .arg(peak1, 0, 'f', 1),
                     UiStatusLevel::Success);
}

QVector<int> DIMM::scanHotPixelExposureTemplates() const
{
    QVector<int> exposures;
    if (!m_hotPixelTemplatesEnabled || m_hotPixelCamera0MaskPath.isEmpty()) {
        return exposures;
    }

    QDir exposureDir = QFileInfo(resolvePathFromAppDir(m_hotPixelCamera0MaskPath)).absoluteDir();
    if (!exposureDir.cdUp()) {
        return exposures;
    }

    const QFileInfoList entries =
        exposureDir.entryInfoList(QStringList() << QStringLiteral("exposure_*us"),
                                  QDir::Dirs | QDir::NoDotAndDotDot,
                                  QDir::Name);
    for (const QFileInfo& entry : entries) {
        const int exposureUs = exposureUsFromTemplateDirName(entry.fileName());
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
    return exposures;
}

int DIMM::selectTemplateExposureForPeak(double currentExposure, double peakValue) const
{
    QVector<int> exposures = scanHotPixelExposureTemplates();
    if (exposures.isEmpty()) {
        return 0;
    }

    exposures.erase(std::remove_if(exposures.begin(),
                                   exposures.end(),
                                   [this](int exposureUs) {
                                       return exposureUs < m_autoExposureMinUs ||
                                              exposureUs > m_autoExposureMaxUs;
                                   }),
                    exposures.end());
    if (exposures.isEmpty()) {
        return 0;
    }

    const int currentUs = static_cast<int>(std::lround(currentExposure));
    if (peakValue < m_autoExposureLowThreshold) {
        const int desired = static_cast<int>(std::lround(currentExposure * m_autoExposureDarkRatio));
        for (int exposureUs : exposures) {
            if (exposureUs >= desired && exposureUs > currentUs) {
                return exposureUs;
            }
        }
        return exposures.back() > currentUs ? exposures.back() : currentUs;
    }
    if (peakValue > m_autoExposureHighThreshold) {
        const int desired = static_cast<int>(std::lround(currentExposure * m_autoExposureBrightRatio));
        for (auto it = exposures.crbegin(); it != exposures.crend(); ++it) {
            if (*it <= desired && *it < currentUs) {
                return *it;
            }
        }
        return exposures.front() < currentUs ? exposures.front() : currentUs;
    }
    return currentUs;
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

    const QString cam0Mask = replaceTemplateExposurePath(m_hotPixelCamera0MaskPath, exposureUs);
    const QString cam0Excess = replaceTemplateExposurePath(m_hotPixelCamera0ExcessPath, exposureUs);
    const QString cam1Mask = replaceTemplateExposurePath(m_hotPixelCamera1MaskPath, exposureUs);
    const QString cam1Excess = replaceTemplateExposurePath(m_hotPixelCamera1ExcessPath, exposureUs);
    if (cam0Mask.isEmpty() || cam0Excess.isEmpty() || cam1Mask.isEmpty() || cam1Excess.isEmpty()) {
        return false;
    }
    if (!QFileInfo::exists(resolvePathFromAppDir(cam0Mask)) ||
        !QFileInfo::exists(resolvePathFromAppDir(cam0Excess)) ||
        !QFileInfo::exists(resolvePathFromAppDir(cam1Mask)) ||
        !QFileInfo::exists(resolvePathFromAppDir(cam1Excess))) {
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
    m_hotPixelCamera0MaskPath = relativizePathToAppDir(camera0Mask);
    m_hotPixelCamera0ExcessPath = relativizePathToAppDir(camera0Excess);
    m_hotPixelCamera1MaskPath = relativizePathToAppDir(camera1Mask);
    m_hotPixelCamera1ExcessPath = relativizePathToAppDir(camera1Excess);
    m_hotPixelTemplateExposureUs = exposureUs;
    if (m_imageProcessor) {
        m_imageProcessor->configureHotPixelTemplates(resolvePathFromAppDir(m_hotPixelCamera0MaskPath),
                                                     resolvePathFromAppDir(m_hotPixelCamera0ExcessPath),
                                                     resolvePathFromAppDir(m_hotPixelCamera1MaskPath),
                                                     resolvePathFromAppDir(m_hotPixelCamera1ExcessPath),
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
