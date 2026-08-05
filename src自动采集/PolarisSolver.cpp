#include "PolarisSolver.h"

#include "AstronomyTransform.h"
#include "ImageUtils.h"
#include "StarPatternMatcher.h"

#include <QtMath>

#include <array>
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

#include <QDateTime>
#include <QMetaObject>
#include <QElapsedTimer>
#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>

class PolarisSolverWorker : public QObject {
public:
    using QObject::QObject;
};

namespace {
double rawSaturationLevelForDepth(int depth)
{
    switch (depth) {
    case CV_8U:
        return 255.0;
    case CV_16U:
        return 4095.0;
    default:
        return std::numeric_limits<double>::infinity();
    }
}

double rawIntensityAt(const cv::Mat& grayscale, int y, int x)
{
    switch (grayscale.depth()) {
    case CV_8U:
        return static_cast<double>(grayscale.at<uchar>(y, x));
    case CV_16U:
        return static_cast<double>(grayscale.at<quint16>(y, x));
    case CV_32F:
        return static_cast<double>(grayscale.at<float>(y, x));
    case CV_64F:
        return grayscale.at<double>(y, x);
    default:
        return 0.0;
    }
}

double normalizedExcessForDepth(quint16 excess, int depth)
{
    if (depth == CV_8U) {
        return std::clamp(static_cast<double>(excess) / 257.0, 0.0, 255.0);
    }
    return static_cast<double>(excess);
}

int correctedPixelValue(int originalValue, double excess)
{
    return std::max(0, originalValue - static_cast<int>(std::lround(excess)));
}

struct PolarisHotPixelTemplateCache {
    bool valid = false;
    QString maskPath;
    QString excessPath;
    int templateWidth = 0;
    int templateHeight = 0;
    QDateTime maskLastModified;
    QDateTime excessLastModified;
    QByteArray maskBytes;
    QByteArray excessBytes;

    bool matches(const PolarisHotPixelTemplateConfig& hot,
                 const QFileInfo& maskInfo,
                 const QFileInfo& excessInfo) const
    {
        return valid &&
               maskPath == hot.maskPath &&
               excessPath == hot.excessPath &&
               templateWidth == hot.templateWidth &&
               templateHeight == hot.templateHeight &&
               maskLastModified == maskInfo.lastModified() &&
               excessLastModified == excessInfo.lastModified();
    }
};

bool loadPolarisHotPixelTemplateBytes(int cameraIndex,
                                      const PolarisHotPixelTemplateConfig& hot,
                                      qsizetype pixelCount,
                                      QByteArray* maskBytes,
                                      QByteArray* excessBytes)
{
    if (!maskBytes || !excessBytes) {
        return false;
    }

    const QFileInfo maskInfo(hot.maskPath);
    const QFileInfo excessInfo(hot.excessPath);
    if (!maskInfo.exists() || !excessInfo.exists()) {
        return false;
    }

    static QMutex cacheMutex;
    static std::array<PolarisHotPixelTemplateCache, 2> caches;
    QMutexLocker locker(&cacheMutex);

    PolarisHotPixelTemplateCache& cache = caches[std::clamp(cameraIndex, 0, 1)];
    if (!cache.matches(hot, maskInfo, excessInfo)) {
        QFile maskFile(hot.maskPath);
        QFile excessFile(hot.excessPath);
        if (!maskFile.open(QIODevice::ReadOnly) || !excessFile.open(QIODevice::ReadOnly)) {
            cache = PolarisHotPixelTemplateCache();
            return false;
        }

        const QByteArray loadedMaskBytes = maskFile.readAll();
        const QByteArray loadedExcessBytes = excessFile.readAll();
        if (loadedMaskBytes.size() < pixelCount ||
            loadedExcessBytes.size() < pixelCount * static_cast<qsizetype>(sizeof(quint16))) {
            cache = PolarisHotPixelTemplateCache();
            return false;
        }

        cache.valid = true;
        cache.maskPath = hot.maskPath;
        cache.excessPath = hot.excessPath;
        cache.templateWidth = hot.templateWidth;
        cache.templateHeight = hot.templateHeight;
        cache.maskLastModified = maskInfo.lastModified();
        cache.excessLastModified = excessInfo.lastModified();
        cache.maskBytes = loadedMaskBytes;
        cache.excessBytes = loadedExcessBytes;
    }

    *maskBytes = cache.maskBytes;
    *excessBytes = cache.excessBytes;
    return true;
}

bool isSolveCancelled(const std::shared_ptr<std::atomic_bool>& cancelled);

cv::Mat applyPolarisHotPixelCorrection(const cv::Mat& frame,
                                       const PolarisSolverConfig& config,
                                       const std::shared_ptr<std::atomic_bool>& cancelled)
{
    if (frame.empty()) {
        return frame;
    }
    if (isSolveCancelled(cancelled)) {
        return cv::Mat();
    }
    const int cameraIndex = std::clamp(config.cameraIndex, 0, 1);
    const PolarisHotPixelTemplateConfig& hot = config.hotPixelTemplates[cameraIndex];
    if (!hot.enabled ||
        hot.templateWidth <= 0 ||
        hot.templateHeight <= 0 ||
        hot.maskPath.isEmpty() ||
        hot.excessPath.isEmpty()) {
        return frame;
    }

    const cv::Mat grayscale = ImageUtils::grayscaleDetectionFrame(frame);
    if (grayscale.empty() ||
        grayscale.cols != hot.templateWidth ||
        grayscale.rows != hot.templateHeight ||
        (grayscale.depth() != CV_8U && grayscale.depth() != CV_16U)) {
        return frame;
    }

    const qsizetype pixelCount =
        static_cast<qsizetype>(hot.templateWidth) * static_cast<qsizetype>(hot.templateHeight);
    QByteArray maskBytes;
    QByteArray excessBytes;
    if (!loadPolarisHotPixelTemplateBytes(cameraIndex, hot, pixelCount, &maskBytes, &excessBytes)) {
        return frame;
    }

    cv::Mat corrected = grayscale.clone();
    const uchar* mask = reinterpret_cast<const uchar*>(maskBytes.constData());
    const quint16* excess = reinterpret_cast<const quint16*>(excessBytes.constData());
    for (int y = 0; y < corrected.rows; ++y) {
        if ((y & 0x0F) == 0 && isSolveCancelled(cancelled)) {
            return cv::Mat();
        }
        for (int x = 0; x < corrected.cols; ++x) {
            const int index = y * corrected.cols + x;
            if (mask[index] == 0) {
                continue;
            }
            const double excessValue = normalizedExcessForDepth(excess[index], corrected.depth());
            if (corrected.depth() == CV_8U) {
                uchar& value = corrected.at<uchar>(y, x);
                value = static_cast<uchar>(std::min(255, correctedPixelValue(value, excessValue)));
            } else if (corrected.depth() == CV_16U) {
                quint16& value = corrected.at<quint16>(y, x);
                value = static_cast<quint16>(std::min(65535, correctedPixelValue(value, excessValue)));
            }
        }
    }
    return corrected;
}

QVector<PatternCatalogPoint> buildPatternCatalogPoints(const PolarisSolverConfig& config)
{
    QVector<PatternCatalogPoint> points;
    const QVector<CatalogStar>& catalogStars = PolarisCatalog::stars();
    points.reserve(catalogStars.size());
    for (const CatalogStar& catalogStar : catalogStars) {
        PatternCatalogPoint point;
        point.sourceId = catalogStar.sourceId;
        point.name = catalogStar.name;
        point.planeRad = northPolePlaneAtEpoch(catalogStar, config.observationEpochYear);
        point.magnitude = catalogStar.magnitude;
        point.isPolaris = catalogStar.isPolaris;
        points.push_back(point);
    }
    return points;
}

PatternMatcherConfig matcherConfigFromSolverConfig(const PolarisSolverConfig& config)
{
    PatternMatcherConfig matcherConfig;
    matcherConfig.minMatchedStars = config.minMatchedStars;
    matcherConfig.initialMaxResidualPx = config.initialMatchTolerancePx;
    matcherConfig.maxResidualPx = config.refinedMatchTolerancePx;
    matcherConfig.minPlateScaleArcsecPx = config.minPlateScaleArcsecPx;
    matcherConfig.maxPlateScaleArcsecPx = config.maxPlateScaleArcsecPx;
    matcherConfig.nominalPlateScaleArcsecPx = config.nominalPlateScaleArcsecPx;
    matcherConfig.minScoreMargin = config.minScoreMargin;
    matcherConfig.minMatchedSpatialSpreadPx = config.minMatchedSpatialSpreadPx;
    return matcherConfig;
}

bool isSolveCancelled(const std::shared_ptr<std::atomic_bool>& cancelled)
{
    return cancelled && cancelled->load();
}

using SolveProgressCallback = std::function<void(PolarisSolveStatus, const QString&)>;

PolarisSolveResult solveFrameWithProgress(const cv::Mat& frame,
                                          const PolarisSolverConfig& config,
                                          const std::shared_ptr<std::atomic_bool>& cancelled,
                                          const SolveProgressCallback& progress)
{
    QElapsedTimer totalTimer;
    totalTimer.start();
    QElapsedTimer detectionTimer;
    detectionTimer.start();
    QVector<DetectedStar> detections = detectStarsFromFrame(frame, config, cancelled);
    const double detectionMs = static_cast<double>(detectionTimer.nsecsElapsed()) / 1000000.0;
    if (isSolveCancelled(cancelled)) {
        PolarisSolveResult result;
        result.status = PolarisSolveStatus::Cancelled;
        result.message = QStringLiteral("Polaris solve cancelled");
        result.detectedStarCount = detections.size();
        result.detections = detections;
        result.timing.detectionMs = detectionMs;
        result.timing.totalMs = static_cast<double>(totalTimer.nsecsElapsed()) / 1000000.0;
        return result;
    }
    if (progress) {
        progress(PolarisSolveStatus::MatchingCatalog, QStringLiteral("Matching catalog"));
    }
    PolarisSolveResult result = solveDetectedStars(detections, config, cancelled);
    result.timing.detectionMs = detectionMs;
    result.timing.totalMs = static_cast<double>(totalTimer.nsecsElapsed()) / 1000000.0;
    result.detectedStarCount = detections.size();
    result.detections = detections;
    if (detections.size() < config.minMatchedStars && result.status == PolarisSolveStatus::NoCatalogMatch) {
        result.status = PolarisSolveStatus::InsufficientStars;
    }
    return result;
}
}

QVector<DetectedStar> detectStarsFromFrame(const cv::Mat& frame,
                                           const PolarisSolverConfig& config,
                                           const std::shared_ptr<std::atomic_bool>& cancelled)
{
    QVector<DetectedStar> detections;
    if (isSolveCancelled(cancelled)) {
        return detections;
    }
    const cv::Mat correctedFrame = applyPolarisHotPixelCorrection(frame, config, cancelled);
    if (isSolveCancelled(cancelled)) {
        return detections;
    }
    const cv::Mat grayscale = ImageUtils::grayscaleDetectionFrame(correctedFrame);
    if (grayscale.empty()) {
        return detections;
    }

    cv::Scalar mean;
    cv::Scalar stddev;
    cv::meanStdDev(grayscale, mean, stddev);
    double minValue = 0.0;
    double maxValue = 0.0;
    cv::minMaxLoc(grayscale, &minValue, &maxValue);

    const double background = mean[0];
    const double noise = std::max(1.0, stddev[0]);
    const double dynamicThreshold =
        std::max({config.starMinimumIntensity,
                  background + config.starThresholdSigma * noise,
                  background + (maxValue - background) * config.starPeakFraction});
    const double threshold = config.starThresholdAbsolute >= 0.0
                                 ? std::min(config.starThresholdAbsolute, dynamicThreshold)
                                 : dynamicThreshold;
    if (maxValue <= threshold) {
        return detections;
    }

    cv::Mat binary;
    if (isSolveCancelled(cancelled)) {
        return detections;
    }
    cv::compare(grayscale, cv::Scalar(threshold), binary, cv::CMP_GT);

    cv::Mat labels;
    cv::Mat stats;
    cv::Mat centroids;
    if (isSolveCancelled(cancelled)) {
        return detections;
    }
    const int componentCount =
        cv::connectedComponentsWithStats(binary, labels, stats, centroids, 8, CV_32S);
    if (isSolveCancelled(cancelled)) {
        return detections;
    }

    std::vector<double> componentFlux(static_cast<size_t>(componentCount), 0.0);
    std::vector<double> componentPeak(static_cast<size_t>(componentCount), 0.0);
    std::vector<double> componentWeightedX(static_cast<size_t>(componentCount), 0.0);
    std::vector<double> componentWeightedY(static_cast<size_t>(componentCount), 0.0);
    std::vector<bool> componentSaturated(static_cast<size_t>(componentCount), false);
    const double rawSaturationValue = rawSaturationLevelForDepth(grayscale.depth());
    for (int y = 0; y < labels.rows; ++y) {
        if ((y & 0x0F) == 0 && isSolveCancelled(cancelled)) {
            return detections;
        }
        const int* labelRow = labels.ptr<int>(y);
        for (int x = 0; x < labels.cols; ++x) {
            const int label = labelRow[x];
            if (label <= 0 || label >= componentCount) {
                continue;
            }
            const double value = rawIntensityAt(grayscale, y, x);
            const double signal = std::max(0.0, value - background);
            componentFlux[static_cast<size_t>(label)] += signal;
            componentWeightedX[static_cast<size_t>(label)] += static_cast<double>(x) * signal;
            componentWeightedY[static_cast<size_t>(label)] += static_cast<double>(y) * signal;
            componentPeak[static_cast<size_t>(label)] =
                std::max(componentPeak[static_cast<size_t>(label)], value);
            componentSaturated[static_cast<size_t>(label)] =
                componentSaturated[static_cast<size_t>(label)] ||
                rawIntensityAt(grayscale, y, x) >= rawSaturationValue;
        }
    }

    for (int label = 1; label < componentCount; ++label) {
        if ((label & 0x3F) == 1 && isSolveCancelled(cancelled)) {
            return detections;
        }
        const int area = stats.at<int>(label, cv::CC_STAT_AREA);
        const int left = stats.at<int>(label, cv::CC_STAT_LEFT);
        const int top = stats.at<int>(label, cv::CC_STAT_TOP);
        const int width = stats.at<int>(label, cv::CC_STAT_WIDTH);
        const int height = stats.at<int>(label, cv::CC_STAT_HEIGHT);
        const bool touchesEdge = left <= 0 || top <= 0 ||
                                 left + width >= grayscale.cols ||
                                 top + height >= grayscale.rows;
        if (touchesEdge ||
            area < config.minStarAreaPx ||
            area > config.maxStarAreaPx ||
            width > 96 ||
            height > 96) {
            continue;
        }

        DetectedStar detection;
        detection.centroidPx = componentFlux[static_cast<size_t>(label)] > 0.0
                                   ? QPointF(componentWeightedX[static_cast<size_t>(label)] /
                                                 componentFlux[static_cast<size_t>(label)],
                                             componentWeightedY[static_cast<size_t>(label)] /
                                                 componentFlux[static_cast<size_t>(label)])
                                   : QPointF(centroids.at<double>(label, 0),
                                             centroids.at<double>(label, 1));
        detection.boundingBoxPx = QRectF(left, top, width, height);
        detection.areaPx = area;
        detection.peak = componentPeak[static_cast<size_t>(label)];
        detection.flux = componentFlux[static_cast<size_t>(label)];
        detection.background = background;
        detection.snr = detection.flux / (noise * std::sqrt(static_cast<double>(area)));
        detection.saturated = componentSaturated[static_cast<size_t>(label)];
        detections.push_back(detection);
    }

    std::sort(detections.begin(), detections.end(), [](const DetectedStar& a, const DetectedStar& b) {
        if (a.saturated != b.saturated) {
            return !a.saturated;
        }
        return a.snr == b.snr ? a.flux > b.flux : a.snr > b.snr;
    });
    if (detections.size() > config.maxDetectedStars) {
        detections.resize(config.maxDetectedStars);
    }
    for (int i = 0; i < detections.size(); ++i) {
        detections[i].detectionIndex = i + 1;
    }
    return detections;
}

PolarisSolveResult solveFrame(const cv::Mat& frame,
                              const PolarisSolverConfig& config)
{
    return solveFrame(frame, config, {});
}

PolarisSolveResult solveFrame(const cv::Mat& frame,
                              const PolarisSolverConfig& config,
                              const std::shared_ptr<std::atomic_bool>& cancelled)
{
    return solveFrameWithProgress(frame, config, cancelled, {});
}

PolarisSolveResult solveDetectedStars(const QVector<DetectedStar>& detections,
                                       const PolarisSolverConfig& config)
{
    return solveDetectedStars(detections, config, {});
}

PolarisSolveResult solveDetectedStars(const QVector<DetectedStar>& detections,
                                       const PolarisSolverConfig& config,
                                       const std::shared_ptr<std::atomic_bool>& cancelled)
{
    PolarisSolveResult result;
    result.observationEpochYear = config.observationEpochYear;
    result.coordinateModel = QStringLiteral("目录近似北天极；不含岁差章动极移折射");
    result.showMatchedCatalogStars = config.showMatchedCatalogStars;
    result.detections = detections;
    result.detectedStarCount = detections.size();
    if (!config.enabled) {
        result.status = PolarisSolveStatus::Idle;
        result.message = QStringLiteral("北极星自动识别未启用");
        return result;
    }
    if (!PolarisCatalog::isValid()) {
        result.status = PolarisSolveStatus::Error;
        result.message = QStringLiteral("内置北极星星表无效");
        return result;
    }
    if (detections.size() < config.minMatchedStars) {
        result.status = PolarisSolveStatus::InsufficientStars;
        result.message = QStringLiteral("可靠星点数量不足");
        return result;
    }
    if (cancelled && cancelled->load()) {
        result.status = PolarisSolveStatus::Cancelled;
        result.message = QStringLiteral("Polaris solve cancelled");
        return result;
    }

    QVector<DetectedStar> limitedDetections = detections;
    std::sort(limitedDetections.begin(),
              limitedDetections.end(),
              [](const DetectedStar& a, const DetectedStar& b) {
                  return a.snr == b.snr ? a.flux > b.flux : a.snr > b.snr;
              });
    if (limitedDetections.size() > config.maxDetectedStars) {
        limitedDetections.resize(config.maxDetectedStars);
    }
    result.detections = limitedDetections;
    result.detectedStarCount = limitedDetections.size();

    QVector<QPointF> detectedPoints;
    detectedPoints.reserve(limitedDetections.size());
    for (const DetectedStar& detected : limitedDetections) {
        detectedPoints.push_back(detected.centroidPx);
    }

    const QVector<PatternCatalogPoint> catalogPoints = buildPatternCatalogPoints(config);
    result.stats.catalogStarCount = catalogPoints.size();
    PatternMatcherConfig matcherConfig = matcherConfigFromSolverConfig(config);
    matcherConfig.cancelled = cancelled;
    StarPatternMatcher matcher;
    QElapsedTimer matchingTimer;
    matchingTimer.start();
    const PatternMatchResult matchResult =
        matcher.matchDetectedToCatalog(detectedPoints, catalogPoints, matcherConfig);
    result.timing.matchingMs = static_cast<double>(matchingTimer.nsecsElapsed()) / 1000000.0;
    result.timing.totalMs = result.timing.matchingMs;
    result.bestScore = matchResult.bestScore;
    result.secondBestScore = matchResult.secondBestScore;
    result.scoreMargin = matchResult.scoreMargin;
    result.stats.catalogTriangleCount = matchResult.catalogTriangleCount;
    result.stats.imageTriangleCount = matchResult.imageTriangleCount;
    result.stats.candidateTriangleCount = matchResult.candidateTriangleCount;
    result.stats.testedTransformCount = matchResult.testedTransformCount;
    result.stats.solutionClusterCount = matchResult.solutionClusterCount;
    result.stats.bestSolutionSupportCount = matchResult.bestSolutionSupportCount;
    result.stats.secondBestSolutionSupportCount = matchResult.secondBestSolutionSupportCount;
    result.maxResidualPx = matchResult.maxResidualPx;
    result.matchedSpatialSpreadPx = matchResult.matchedSpatialSpreadPx;
    if (!matchResult.valid || matchResult.rmsPx > config.maxRmsPx ||
        !matchResult.hasPolarisPixel || !matchResult.hasNorthCelestialPolePixel) {
        result.status = PolarisSolveStatus::NoCatalogMatch;
        result.message = QStringLiteral("未找到可靠星图匹配");
        result.matchedStarCount = matchResult.matchedCount;
        result.rmsPx = matchResult.rmsPx;
        return result;
    }
    if (matchResult.matchedSpatialSpreadPx < config.minMatchedSpatialSpreadPx) {
        result.status = PolarisSolveStatus::LowConfidence;
        result.message = QStringLiteral("匹配星空间分布不足");
        result.matchedStarCount = matchResult.matchedCount;
        result.rmsPx = matchResult.rmsPx;
        return result;
    }
    if (matchResult.scoreMargin < config.minScoreMargin) {
        result.status = PolarisSolveStatus::LowConfidence;
        result.message = QStringLiteral("星图匹配置信度不足");
        result.matchedStarCount = matchResult.matchedCount;
        result.rmsPx = matchResult.rmsPx;
        return result;
    }

    result.valid = true;
    result.status = PolarisSolveStatus::Solved;
    result.message = QStringLiteral("星图匹配成功");
    result.matchedStarCount = matchResult.matchedCount;
    result.rmsPx = matchResult.rmsPx;
    result.hasPolarisPixel = matchResult.hasPolarisPixel;
    result.polarisPixel = matchResult.polarisPixel;
    result.hasPredictedPolarisPixel = matchResult.hasPolarisPixel;
    result.predictedPolarisPixel = matchResult.polarisPixel;
    result.hasNorthCelestialPolePixel = matchResult.hasNorthCelestialPolePixel;
    result.northCelestialPolePixel = matchResult.northCelestialPolePixel;
    result.polarisPolarRadiusPx = matchResult.polarisPolarRadiusPx;
    result.plateScaleArcsecPx =
        matchResult.transform.scale > 0.0
            ? qRadiansToDegrees(1.0) * 3600.0 / matchResult.transform.scale
            : 0.0;
    result.rotationDeg = qRadiansToDegrees(matchResult.transform.rotationRad);
    result.mirrored = matchResult.transform.mirrored;

    for (const PatternMatchPair& pair : matchResult.pairs) {
        if (pair.catalogIndex < 0 || pair.catalogIndex >= catalogPoints.size() ||
            pair.detectedIndex < 0 || pair.detectedIndex >= limitedDetections.size()) {
            continue;
        }
        const PatternCatalogPoint& matchedCatalog = catalogPoints[pair.catalogIndex];
        const DetectedStar& matchedDetected = limitedDetections[pair.detectedIndex];

        CatalogImageMatch outputMatch;
        outputMatch.sourceId = matchedCatalog.sourceId;
        outputMatch.name = matchedCatalog.name;
        outputMatch.catalogPlaneRad = matchedCatalog.planeRad;
        outputMatch.predictedPixel = pair.predictedPixel;
        outputMatch.detectedPixel = matchedDetected.centroidPx;
        outputMatch.residualPx = pair.residualPx;
        outputMatch.isPolaris = matchedCatalog.isPolaris;
        result.matches.push_back(outputMatch);
        if (outputMatch.isPolaris) {
            const bool polarisDetectionQualityOk =
                matchedDetected.snr >= config.minPolarisSnr &&
                (config.allowSaturatedPolarisConfirmation || !matchedDetected.saturated);
            if (polarisDetectionQualityOk) {
                result.hasDetectedPolarisPixel = true;
                result.detectedPolarisPixel = outputMatch.detectedPixel;
                result.hasPolarisPixel = true;
                result.polarisPixel = outputMatch.detectedPixel;
            } else {
                result.polarisDetectionQualityRejected = true;
            }
        }
    }
    if (result.polarisDetectionQualityRejected && !result.hasDetectedPolarisPixel) {
        result.message = QStringLiteral("星图匹配成功，北极星检测质量不足");
    }
    return result;
}

PolarisSolverController::PolarisSolverController(QObject* parent)
    : QObject(parent)
    , m_workerThread(new QThread(this))
    , m_worker(new PolarisSolverWorker())
{
    static_assert(PolarisSolverController::kSolverWorkerThreadCount == 1,
                  "P1 keeps Polaris solves on one serial worker thread.");
    m_workerThread->setObjectName(QStringLiteral("polarisSolverSingleWorker"));
    m_worker->moveToThread(m_workerThread);
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_workerThread->start();
}

PolarisSolverController::~PolarisSolverController()
{
    ++m_generation;
    cancelCurrentSolveTasks();
    m_taskRunning[0] = false;
    m_taskRunning[1] = false;
    m_activeTaskId[0] = 0;
    m_activeTaskId[1] = 0;
    m_pendingLatestTask[0] = PendingSolveTask();
    m_pendingLatestTask[1] = PendingSolveTask();
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
    }
}

void PolarisSolverController::submitFrame(int cameraIndex,
                                          const cv::Mat& frame,
                                          const PolarisSolverConfig& config,
                                          quint64 generation,
                                          quint64 frameId,
                                          bool forceSolve)
{
    PolarisSolveResult result;
    result.generation = generation;
    result.frameId = frameId;
    result.cameraIndex = cameraIndex;
    if (!isValidCameraIndex(cameraIndex)) {
        result.status = PolarisSolveStatus::Error;
        result.message = QStringLiteral("Invalid camera index");
        emit solveFinished(result);
        return;
    }
    if (!config.enabled) {
        result.status = PolarisSolveStatus::Idle;
        result.message = QStringLiteral("Polaris auto solve disabled");
        emit solveStatusChanged(cameraIndex, result.status, result.message, generation);
        emit solveFinished(result);
        return;
    } else if (!PolarisCatalog::isValid()) {
        result.status = PolarisSolveStatus::Error;
        result.message = QStringLiteral("Embedded Polaris catalog is invalid");
        emit solveStatusChanged(cameraIndex, result.status, result.message, generation);
        emit solveFinished(result);
        return;
    }

    m_generation = generation;
    const quint64 taskId = ++m_nextTaskId;
    if (m_taskRunning[cameraIndex]) {
        m_pendingLatestTask[cameraIndex] =
            PendingSolveTask{true,
                             taskId,
                             cameraIndex,
                             generation,
                             frameId,
                             frame,
                             config,
                             std::make_shared<std::atomic_bool>(false)};
        emit solveStatusChanged(cameraIndex,
                                PolarisSolveStatus::DetectingStars,
                                forceSolve
                                    ? QStringLiteral("Queued latest forced Polaris solve request")
                                    : QStringLiteral("Queued latest Polaris solve request"),
                                generation);
        return;
    }

    startSolveTask(cameraIndex, taskId, frame, config, generation, frameId);
}

void PolarisSolverController::startSolveTask(int cameraIndex,
                                             quint64 taskId,
                                             const cv::Mat& frame,
                                             const PolarisSolverConfig& config,
                                             quint64 generation,
                                             quint64 frameId,
                                             const std::shared_ptr<std::atomic_bool>& requestedCancelled)
{
    auto cancelled = requestedCancelled ? requestedCancelled : std::make_shared<std::atomic_bool>(false);
    m_cancelledTokens[cameraIndex] = cancelled;
    m_activeTaskId[cameraIndex] = taskId;
    m_taskRunning[cameraIndex] = true;
    emit solveStatusChanged(cameraIndex,
                            PolarisSolveStatus::DetectingStars,
                            QStringLiteral("Detecting stars"),
                            generation);

    cv::Mat frameForWorker = frame;
    QMetaObject::invokeMethod(
        m_worker,
        [this, cameraIndex, taskId, frameForWorker, config, generation, frameId, cancelled]() mutable {
            const auto progress = [this, cameraIndex, generation](PolarisSolveStatus status,
                                                                  const QString& message) {
                if (status != PolarisSolveStatus::MatchingCatalog) {
                    return;
                }
                QMetaObject::invokeMethod(
                    this,
                    [this, cameraIndex, status, message, generation]() {
                        emit solveStatusChanged(cameraIndex, status, message, generation);
                    },
                    Qt::QueuedConnection);
            };
            PolarisSolveResult result = solveFrameWithProgress(frameForWorker, config, cancelled, progress);
            result.cameraIndex = cameraIndex;
            result.taskId = taskId;
            result.generation = generation;
            result.frameId = frameId;
            QMetaObject::invokeMethod(
                this,
                [this, result]() mutable {
                    if (isValidCameraIndex(result.cameraIndex)) {
                        if (result.taskId != m_activeTaskId[result.cameraIndex]) {
                            return;
                        }
                        m_taskRunning[result.cameraIndex] = false;
                        const PendingSolveTask pendingTask = m_pendingLatestTask[result.cameraIndex];
                        m_pendingLatestTask[result.cameraIndex] = PendingSolveTask();
                        m_cancelledTokens[result.cameraIndex].reset();
                        m_activeTaskId[result.cameraIndex] = 0;
                        if (pendingTask.valid && pendingTask.generation == m_generation) {
                            startSolveTask(result.cameraIndex,
                                           pendingTask.taskId,
                                           pendingTask.frame,
                                           pendingTask.config,
                                           pendingTask.generation,
                                           pendingTask.frameId,
                                           pendingTask.cancelled);
                        }
                    }
                    if (result.generation != m_generation) {
                        return;
                    }
                    emit solveStatusChanged(result.cameraIndex,
                                            result.status,
                                            result.message,
                                            result.generation);
                    emit solveFinished(result);
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

void PolarisSolverController::cancelCurrentSolveTasks()
{
    for (const std::shared_ptr<std::atomic_bool>& token : m_cancelledTokens) {
        if (token) {
            token->store(true);
        }
    }
}

void PolarisSolverController::cancelCamera(int cameraIndex, quint64 generation)
{
    if (!isValidCameraIndex(cameraIndex)) {
        return;
    }

    m_generation = generation;
    if (m_cancelledTokens[cameraIndex]) {
        m_cancelledTokens[cameraIndex]->store(true);
    }
    m_pendingLatestTask[cameraIndex] = PendingSolveTask();
    emit solveStatusChanged(cameraIndex,
                            PolarisSolveStatus::Cancelled,
                            QStringLiteral("Polaris solve cancelled"),
                            generation);
}

void PolarisSolverController::cancelAll(quint64 newGeneration)
{
    m_generation = newGeneration;
    cancelCurrentSolveTasks();
    m_pendingLatestTask[0] = PendingSolveTask();
    m_pendingLatestTask[1] = PendingSolveTask();
    emit solveStatusChanged(-1,
                            PolarisSolveStatus::Cancelled,
                            QStringLiteral("Polaris solve cancelled"),
                            newGeneration);
}
