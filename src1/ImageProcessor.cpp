#include "ImageProcessor.h"

#include "ConfigTextUtils.h"
#include "CentroidLogic.h"
#include "ImageUtils.h"

#include <QElapsedTimer>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <vector>
#include <utility>

namespace {
constexpr quint64 kMaxMeasurementSignalPixels = 1600;

cv::Mat makeCentroidIntensityImage(const cv::Mat& image)
{
    if (image.empty() || image.channels() != 1) {
        return cv::Mat();
    }
    if (image.type() == CV_64FC1) {
        return image;
    }

    cv::Mat intensity;
    image.convertTo(intensity, CV_64F);
    return intensity;
}

double pixelValueAt(const cv::Mat& image, int y, int x)
{
    switch (image.depth()) {
    case CV_8U:
        return static_cast<double>(image.at<uchar>(y, x));
    case CV_16U:
        return static_cast<double>(image.at<quint16>(y, x));
    case CV_32F:
        return static_cast<double>(image.at<float>(y, x));
    case CV_64F:
        return image.at<double>(y, x);
    default:
        return 0.0;
    }
}
}

ImageProcessorWorker::ImageProcessorWorker(std::shared_ptr<std::atomic<quint64>> acquisitionGeneration,
                                           QObject* parent)
    : QObject(parent),
      m_acquisitionGeneration(std::move(acquisitionGeneration))
{
}

void ImageProcessorWorker::setCentroidMethod(int method)
{
    setCentroidMode(method);
}

void ImageProcessorWorker::setCentroidMode(int mode)
{
    QMutexLocker locker(&m_mutex);
    m_centroidMode = mode == 1 ? 1 : 0;
}

void ImageProcessorWorker::setPeakKernelCentroidConfig(int method,
                                                       int radiusPx,
                                                       double strongHotPixelExcessDn)
{
    QMutexLocker locker(&m_mutex);
    m_peakKernelMethod = method == 0 ? 0 : 1;
    m_peakKernelRadiusPx = std::clamp(radiusPx, 1, 20);
    m_strongHotPixelExcessDn = std::clamp(strongHotPixelExcessDn, 1.0, 4095.0);
}

void ImageProcessorWorker::setGaussianKernelSize(int size)
{
    setBackgroundDenoiseKernelSize(size);
}

void ImageProcessorWorker::setGaussianSigma(double sigma)
{
    setBackgroundDenoiseSigmaMultiplier(sigma);
}

void ImageProcessorWorker::setBackgroundDenoiseKernelSize(int size)
{
    QMutexLocker locker(&m_mutex);
    int sanitized = std::max(1, size);
    sanitized = (sanitized % 2 == 0) ? sanitized + 1 : sanitized;
    m_backgroundDenoiseKernelSize = std::min(sanitized, 31);
}

void ImageProcessorWorker::setBackgroundDenoiseSigmaMultiplier(double multiplier)
{
    QMutexLocker locker(&m_mutex);
    m_backgroundDenoiseSigmaMultiplier = std::max(0.0, multiplier);
}

void ImageProcessorWorker::setThreshold(double threshold)
{
    QMutexLocker locker(&m_mutex);
    m_threshold = std::max(0.0, threshold);
}

void ImageProcessorWorker::setRoiCentroidConfig(double thresholdAbsolute,
                                                double sigmaThreshold,
                                                double minimumIntensity,
                                                int minimumSignalPixels,
                                                double noiseTrimFraction)
{
    QMutexLocker locker(&m_mutex);
    m_roiThresholdAbsolute = thresholdAbsolute >= 0.0 ? thresholdAbsolute : -1.0;
    m_centroidSigmaThreshold = std::max(0.0, sigmaThreshold);
    m_backgroundDenoiseSigmaMultiplier = m_centroidSigmaThreshold;
    m_centroidMinimumIntensity =
        static_cast<int>(std::lround(std::max(0.0, minimumIntensity)));
    m_centroidMinimumSignalPixels = std::max(1, minimumSignalPixels);
    m_roiNoiseTrimFraction = std::clamp(noiseTrimFraction, 0.0, 0.80);
}

void ImageProcessorWorker::configureHotPixelTemplates(const QString& camera0MaskPath,
                                                      const QString& camera0ExcessPath,
                                                      const QString& camera1MaskPath,
                                                      const QString& camera1ExcessPath,
                                                      int templateWidth,
                                                      int templateHeight)
{
    QMutexLocker locker(&m_mutex);
    const QString maskPaths[2] = {camera0MaskPath, camera1MaskPath};
    const QString excessPaths[2] = {camera0ExcessPath, camera1ExcessPath};
    for (int i = 0; i < 2; ++i) {
        m_hotPixelTemplates[i].maskPath = maskPaths[i];
        m_hotPixelTemplates[i].excessPath = excessPaths[i];
        m_hotPixelTemplates[i].width = templateWidth;
        m_hotPixelTemplates[i].height = templateHeight;
        m_hotPixelTemplates[i].enabled =
            templateWidth > 0 && templateHeight > 0 &&
            !maskPaths[i].isEmpty() && !excessPaths[i].isEmpty() &&
            QFileInfo::exists(maskPaths[i]) && QFileInfo::exists(excessPaths[i]);
        m_hotPixelCaches[i] = HotPixelRoiCache();
    }
}

void ImageProcessorWorker::setOpticalParams(double apertureDiameterMm,
                                            double baselineSeparationMm,
                                            double baselineAngleDeg,
                                            double focalLengthCm,
                                            double zenithAngleDeg,
                                            double lambdaNm,
                                            double pixelSizeUm)
{
    QMutexLocker locker(&m_mutex);
    m_apertureDiameter = std::max(1e-6, apertureDiameterMm * 1e-3);
    m_baselineSeparation = std::max(1e-6, baselineSeparationMm * 1e-3);
    baselineAngleDeg = std::isfinite(baselineAngleDeg) ? baselineAngleDeg : 0.0;
    m_baselineAngleDeg = baselineAngleDeg;
    m_f = std::max(0.01, focalLengthCm / 100.0);
    m_zenithAngleDeg = std::clamp(zenithAngleDeg, 0.0, 80.0);
    m_lambda = std::max(1e-9, lambdaNm * 1e-9);
    m_pixelSize = std::max(1e-9, pixelSizeUm * 1e-6);
}

void ImageProcessorWorker::setTargetFrameRateHz(double frameRateHz)
{
    QMutexLocker locker(&m_mutex);
    m_targetFrameRateHz = std::clamp(frameRateHz, 1.0, 1000.0);
}

void ImageProcessorWorker::setAutoExposureMetricConfig(bool enabled,
                                                       double hardSaturationDn,
                                                       int sampleIntervalMs,
                                                       int peakSupportRadiusPx,
                                                       double peakSupportFraction,
                                                       int minPeakSupportPixelCount,
                                                       double minNeighborPeakRatio,
                                                       int maxPeakCandidateCount,
                                                       double supportedPeakPercentile,
                                                       int saturatedPixelCount)
{
    QMutexLocker locker(&m_mutex);
    m_autoExposureMetricsEnabled = enabled;
    m_autoExposureHardSaturationDn = std::clamp(hardSaturationDn, 0.0, 4095.0);
    m_autoExposureMetricIntervalMs = std::clamp(sampleIntervalMs, 200, 10000);
    m_autoExposureSpotConfig.hardSaturationDn = m_autoExposureHardSaturationDn;
    m_autoExposureSpotConfig.supportRadiusPx = std::clamp(peakSupportRadiusPx, 1, 8);
    m_autoExposureSpotConfig.supportFraction = std::clamp(peakSupportFraction, 0.05, 1.0);
    m_autoExposureSpotConfig.minSupportPixelCount = std::max(1, minPeakSupportPixelCount);
    m_autoExposureSpotConfig.minNeighborPeakRatio = std::clamp(minNeighborPeakRatio, 0.0, 1.0);
    m_autoExposureSpotConfig.maxCandidateCount = std::max(1, maxPeakCandidateCount);
    m_autoExposureSpotConfig.supportedPeakPercentile =
        std::clamp(supportedPeakPercentile, 0.0, 1.0);
    m_autoExposureSpotConfig.saturatedPixelCount = std::max(1, saturatedPixelCount);
    if (!enabled) {
        m_lastAutoExposureSampleMs[0] = -1;
        m_lastAutoExposureSampleMs[1] = -1;
    }
}

void ImageProcessorWorker::setCurrentRoi(int cameraIndex, const RoiRect& roi)
{
    if (cameraIndex < 0 || cameraIndex >= 2) {
        return;
    }

    QMutexLocker locker(&m_mutex);
    RoiRect sanitized = roi;
    sanitized.x = std::max(0, sanitized.x);
    sanitized.y = std::max(0, sanitized.y);
    sanitized.w = std::max(MIN_ROI_SIZE, sanitized.w);
    sanitized.h = std::max(MIN_ROI_SIZE, sanitized.h);
    m_currentRoi[cameraIndex] = sanitized;
    resetRoiProcessingHistory();
}

void ImageProcessorWorker::setPairRois(RoiRect roi0, RoiRect roi1)
{
    RoiRect sanitized[2] = {roi0, roi1};
    for (RoiRect& roi : sanitized) {
        roi.x = std::max(0, roi.x);
        roi.y = std::max(0, roi.y);
        roi.w = std::max(MIN_ROI_SIZE, roi.w);
        roi.h = std::max(MIN_ROI_SIZE, roi.h);
    }

    QMutexLocker locker(&m_mutex);
    m_currentRoi[0] = sanitized[0];
    m_currentRoi[1] = sanitized[1];
    resetRoiProcessingHistory();
}

cv::Mat ImageProcessorWorker::preprocess(const cv::Mat& image)
{
    cv::Mat processed;
    if (image.channels() == 3) {
        cv::cvtColor(image, processed, cv::COLOR_BGR2GRAY);
    } else {
        processed = image.clone();
    }

    return processed;
}

void ImageProcessorWorker::advanceAcquisitionGeneration()
{
    QMutexLocker locker(&m_mutex);
    resetRoiProcessingHistory();
}

cv::Mat ImageProcessorWorker::applyHotPixelCorrection(int cameraIndex,
                                                      const RoiRect& roi,
                                                      const cv::Mat& roiImage)
{
    if (roiImage.empty() || cameraIndex < 0 || cameraIndex >= 2) {
        return roiImage;
    }

    HotPixelTemplate hot;
    {
        QMutexLocker locker(&m_mutex);
        hot = m_hotPixelTemplates[cameraIndex];
        if (!hot.enabled) {
            return roiImage;
        }
    }

    if (roi.w <= 0 || roi.h <= 0 || roi.x < 0 || roi.y < 0 ||
        roi.x + roi.w > hot.width || roi.y + roi.h > hot.height ||
        roiImage.cols != roi.w || roiImage.rows != roi.h) {
        return roiImage;
    }

    HotPixelRoiCache cache;
    bool cacheHit = false;
    {
        QMutexLocker locker(&m_mutex);
        const HotPixelRoiCache& current = m_hotPixelCaches[cameraIndex];
        cacheHit = current.valid &&
                   current.roi.x == roi.x &&
                   current.roi.y == roi.y &&
                   current.roi.w == roi.w &&
                   current.roi.h == roi.h;
        if (cacheHit) {
            cache = current;
        }
    }

    if (!cacheHit) {
        const size_t pixelCount = static_cast<size_t>(roi.w) * static_cast<size_t>(roi.h);
        cache.valid = false;
        cache.roi = roi;
        cache.mask.resize(static_cast<int>(pixelCount));
        cache.excess.resize(static_cast<int>(pixelCount));

        std::ifstream maskIn(hot.maskPath.toLocal8Bit().constData(), std::ios::in | std::ios::binary);
        std::ifstream excessIn(hot.excessPath.toLocal8Bit().constData(), std::ios::in | std::ios::binary);
        if (!maskIn || !excessIn) {
            return roiImage;
        }

        for (int y = 0; y < roi.h; ++y) {
            const qint64 sourceIndex =
                static_cast<qint64>(roi.y + y) * static_cast<qint64>(hot.width) + roi.x;
            const qint64 maskOffset = sourceIndex;
            const qint64 excessOffset = sourceIndex * static_cast<qint64>(sizeof(quint16));
            const int rowStart = y * roi.w;
            maskIn.seekg(static_cast<std::streamoff>(maskOffset), std::ios::beg);
            maskIn.read(reinterpret_cast<char*>(cache.mask.data() + rowStart),
                        static_cast<std::streamsize>(roi.w));
            excessIn.seekg(static_cast<std::streamoff>(excessOffset), std::ios::beg);
            excessIn.read(reinterpret_cast<char*>(cache.excess.data() + rowStart),
                          static_cast<std::streamsize>(roi.w * static_cast<int>(sizeof(quint16))));
            if (!maskIn || !excessIn) {
                return roiImage;
            }
        }
        cache.valid = true;
        {
            QMutexLocker locker(&m_mutex);
            m_hotPixelCaches[cameraIndex] = cache;
        }
    }

    if (roiImage.type() == CV_16UC1) {
        cv::Mat corrected = roiImage.clone();
        for (int y = 0; y < corrected.rows; ++y) {
            quint16* row = corrected.ptr<quint16>(y);
            for (int x = 0; x < corrected.cols; ++x) {
                const int index = y * corrected.cols + x;
                if (index >= cache.mask.size() || cache.mask[index] == 0) {
                    continue;
                }
                const int value = std::max(0, static_cast<int>(row[x]) - static_cast<int>(cache.excess[index]));
                row[x] = static_cast<quint16>(
                    std::min(value, static_cast<int>(std::numeric_limits<quint16>::max())));
            }
        }
        return corrected;
    }

    cv::Mat mono8;
    if (roiImage.type() == CV_8UC1) {
        mono8 = roiImage.clone();
    } else if (roiImage.channels() == 1) {
        roiImage.convertTo(mono8, CV_8UC1);
    } else {
        cv::cvtColor(roiImage, mono8, cv::COLOR_BGR2GRAY);
    }

    cv::Mat corrected = mono8.clone();
    for (int y = 0; y < corrected.rows; ++y) {
        uchar* row = corrected.ptr<uchar>(y);
        for (int x = 0; x < corrected.cols; ++x) {
            const int index = y * corrected.cols + x;
            if (index >= cache.mask.size() || cache.mask[index] == 0) {
                continue;
            }
            const double excess8 = ImageUtils::normalizeThresholdToMono8(static_cast<double>(cache.excess[index]));
            const int value = std::max(0, static_cast<int>(row[x]) - static_cast<int>(std::lround(excess8)));
            row[x] = static_cast<uchar>(std::min(255, value));
        }
    }
    return corrected;
}

ImageProcessorWorker::HotPixelRoiCache ImageProcessorWorker::hotPixelCacheSnapshot(
    int cameraIndex,
    const RoiRect& roi) const
{
    QMutexLocker locker(&m_mutex);
    if (cameraIndex < 0 || cameraIndex >= 2) {
        return HotPixelRoiCache();
    }

    const HotPixelRoiCache& cache = m_hotPixelCaches[cameraIndex];
    if (!cache.valid ||
        cache.roi.x != roi.x ||
        cache.roi.y != roi.y ||
        cache.roi.w != roi.w ||
        cache.roi.h != roi.h) {
        return HotPixelRoiCache();
    }
    return cache;
}

CentroidResult ImageProcessorWorker::calculateCentroid(int cameraIndex,
                                                       const RoiRect& roi,
                                                       const cv::Mat& roiImage)
{
    if (roiImage.empty()) {
        return CentroidResult();
    }

    const cv::Mat processed = preprocess(roiImage);

    int centroidMode = 0;
    {
        QMutexLocker locker(&m_mutex);
        centroidMode = m_centroidMode;
    }

    return centroidMode == 1 ? peakKernelCentroid(cameraIndex, roi, processed)
                             : centerOfGravity(processed);
}

bool ImageProcessorWorker::hasThresholdSignalNearRoiEdge(const cv::Mat& roiImage, double threshold) const
{
    if (roiImage.empty() || roiImage.channels() != 1) {
        return false;
    }

    constexpr int edgeBand = 6;
    const int bandX = std::min(edgeBand, roiImage.cols);
    const int bandY = std::min(edgeBand, roiImage.rows);
    const double edgeThreshold = std::max(0.0, threshold);
    for (int y = 0; y < roiImage.rows; ++y) {
        const bool inYEdge = y < bandY || y >= roiImage.rows - bandY;
        for (int x = 0; x < roiImage.cols; ++x) {
            if (!inYEdge && x >= bandX && x < roiImage.cols - bandX) {
                continue;
            }
            if (pixelValueAt(roiImage, y, x) > edgeThreshold) {
                return true;
            }
        }
    }

    return false;
}

CentroidQuality ImageProcessorWorker::measurementCentroidQuality(const CentroidResult& centroid,
                                                                 const cv::Mat& roiImage) const
{
    if (!centroid.valid || roiImage.empty()) {
        return CentroidQuality::EmptyImage;
    }
    if (!(centroid.peakValue > centroid.threshold && centroid.peakValue > centroid.background + 4.0)) {
        return CentroidQuality::BelowThreshold;
    }
    if (centroid.signalPixelCount < 2) {
        return CentroidQuality::TooFewPixels;
    }
    if (centroid.signalPixelCount > kMaxMeasurementSignalPixels) {
        return CentroidQuality::TooManyPixels;
    }
    if (centroid.totalFlux <= 80.0) {
        return CentroidQuality::LowFlux;
    }

    constexpr double roiEdgeMargin = 6.0;
    if (centroid.x < roiEdgeMargin ||
        centroid.y < roiEdgeMargin ||
        centroid.x > static_cast<double>(roiImage.cols) - roiEdgeMargin ||
        centroid.y > static_cast<double>(roiImage.rows) - roiEdgeMargin) {
        return CentroidQuality::NearRoiEdge;
    }
    return CentroidQuality::Valid;
}

bool ImageProcessorWorker::isMeasurementUsableCentroid(const CentroidResult& centroid,
                                                       const cv::Mat& roiImage) const
{
    return measurementCentroidQuality(centroid, roiImage) == CentroidQuality::Valid;
}

CentroidResult ImageProcessorWorker::centerOfGravity(const cv::Mat& image)
{
    CentroidResult result;
    if (image.empty() || image.channels() != 1) {
        return result;
    }

    int centroidMinimumIntensity = 16;
    int centroidMinimumSignalPixels = 3;
    double manualThreshold = 0.0;
    double roiThresholdAbsolute = -1.0;
    int backgroundKernelSize = 5;
    double noiseSigmaMultiplier = 4.0;
    {
        QMutexLocker locker(&m_mutex);
        centroidMinimumIntensity = m_centroidMinimumIntensity;
        centroidMinimumSignalPixels = m_centroidMinimumSignalPixels;
        manualThreshold = m_threshold;
        roiThresholdAbsolute = m_roiThresholdAbsolute;
        backgroundKernelSize = m_backgroundDenoiseKernelSize;
        noiseSigmaMultiplier = m_backgroundDenoiseSigmaMultiplier;
    }

    const cv::Mat intensity = makeCentroidIntensityImage(image);
    if (intensity.empty()) {
        return result;
    }

    int maxKernelSize = std::min(intensity.rows, intensity.cols);
    maxKernelSize = std::min(maxKernelSize, 31);
    if (maxKernelSize % 2 == 0) {
        --maxKernelSize;
    }
    if (maxKernelSize < 1) {
        return result;
    }

    backgroundKernelSize = std::clamp(backgroundKernelSize, 1, maxKernelSize);
    if (backgroundKernelSize % 2 == 0) {
        --backgroundKernelSize;
    }
    if (backgroundKernelSize < 1) {
        return result;
    }

    long double backgroundSum = 0.0;
    long double backgroundSquareSum = 0.0;
    quint64 backgroundSampleCount = 0;
    for (int y = 0; y < backgroundKernelSize; ++y) {
        const double* row = intensity.ptr<double>(y);
        for (int x = 0; x < backgroundKernelSize; ++x) {
            const double value = row[x];
            backgroundSum += value;
            backgroundSquareSum += static_cast<long double>(value) * value;
            ++backgroundSampleCount;
        }
    }
    if (backgroundSampleCount == 0) {
        return result;
    }

    result.background =
        static_cast<double>(backgroundSum / static_cast<long double>(backgroundSampleCount));
    const double meanSquare =
        static_cast<double>(backgroundSquareSum / static_cast<long double>(backgroundSampleCount));
    const double variance = std::max(0.0, meanSquare - result.background * result.background);
    const double sigma = std::sqrt(variance);
    result.noiseSigma = sigma;
    result.threshold = roiThresholdAbsolute >= 0.0
                           ? roiThresholdAbsolute
                           : (manualThreshold > 0.0
                                  ? manualThreshold
                                  : std::max(static_cast<double>(centroidMinimumIntensity),
                                             result.background + noiseSigmaMultiplier * sigma));

    long double weightedX = 0.0;
    long double weightedY = 0.0;
    long double totalWeight = 0.0;
    double peakValue = 0.0;
    for (int y = 0; y < intensity.rows; ++y) {
        const double* row = intensity.ptr<double>(y);
        for (int x = 0; x < intensity.cols; ++x) {
            const double value = row[x];
            if (value <= result.threshold) {
                continue;
            }
            const long double weight =
                std::max<long double>(0.0, static_cast<long double>(value - result.background));
            if (weight <= 0.0) {
                continue;
            }
            weightedX += weight * static_cast<long double>(x);
            weightedY += weight * static_cast<long double>(y);
            totalWeight += weight;
            ++result.signalPixelCount;
            peakValue = std::max(peakValue, value);
        }
    }

    if (result.signalPixelCount < static_cast<quint64>(centroidMinimumSignalPixels) ||
        totalWeight <= 0.0) {
        return result;
    }

    result.peakValue = peakValue;
    result.x = static_cast<double>(weightedX / totalWeight);
    result.y = static_cast<double>(weightedY / totalWeight);
    result.totalFlux = static_cast<double>(totalWeight);
    result.valid = true;
    return result;
}

CentroidResult ImageProcessorWorker::peakKernelCentroid(int cameraIndex,
                                                        const RoiRect& roi,
                                                        const cv::Mat& image)
{
    CentroidResult result;
    if (image.empty()) {
        return result;
    }

    cv::Mat intensity = makeCentroidIntensityImage(image);
    if (intensity.empty()) {
        return result;
    }
    if (!intensity.isContinuous()) {
        intensity = intensity.clone();
    }
    if (!intensity.isContinuous()) {
        return result;
    }

    int peakKernelMethod = 1;
    int peakKernelRadiusPx = 3;
    double strongHotPixelExcessDn = 100.0;
    AutoExposureSpotConfig spotConfig;
    {
        QMutexLocker locker(&m_mutex);
        peakKernelMethod = m_peakKernelMethod;
        peakKernelRadiusPx = m_peakKernelRadiusPx;
        strongHotPixelExcessDn = m_strongHotPixelExcessDn;
        spotConfig = m_autoExposureSpotConfig;
        spotConfig.thresholdDn = m_roiThresholdAbsolute >= 0.0
                                     ? m_roiThresholdAbsolute
                                     : static_cast<double>(m_centroidMinimumIntensity);
    }

    const double* pixels = intensity.ptr<double>(0);
    const AutoExposureSpotResult spot =
        analyzeAutoExposureSpot(pixels, intensity.cols, intensity.rows, spotConfig);
    if (!spot.validSpotPeak || spot.peakX < 0 || spot.peakY < 0) {
        return result;
    }

    const HotPixelRoiCache cache = hotPixelCacheSnapshot(cameraIndex, roi);
    const bool cacheSizeMatches =
        cache.valid &&
        cache.mask.size() >= intensity.cols * intensity.rows &&
        cache.excess.size() >= intensity.cols * intensity.rows;

    CentroidLogic::PeakKernelConfig kernelConfig;
    kernelConfig.radiusPx = peakKernelRadiusPx;
    kernelConfig.method = peakKernelMethod == 0
                              ? CentroidLogic::PeakKernelMethod::IntensityCog
                              : CentroidLogic::PeakKernelMethod::GaussianFit;
    kernelConfig.strongHotPixelExcessDn = strongHotPixelExcessDn;

    const unsigned char* mask = cacheSizeMatches ? cache.mask.constData() : nullptr;
    const std::uint16_t* excess =
        cacheSizeMatches ? reinterpret_cast<const std::uint16_t*>(cache.excess.constData()) : nullptr;
    const CentroidLogic::PeakKernelResult kernel =
        CentroidLogic::computePeakKernelCentroid(pixels,
                                                 intensity.cols,
                                                 intensity.rows,
                                                 spot.peakX,
                                                 spot.peakY,
                                                 mask,
                                                 excess,
                                                 kernelConfig);
    if (!kernel.valid) {
        return result;
    }

    result.valid = true;
    result.x = kernel.x;
    result.y = kernel.y;
    result.peakValue = spot.peakDn;
    result.totalFlux = kernel.totalFlux;
    result.background = 0.0;
    result.noiseSigma = 0.0;
    result.threshold = 0.0;
    result.signalPixelCount = static_cast<quint64>(std::max(0, kernel.usedPixelCount));
    return result;
}

AtmosphericParams ImageProcessorWorker::calculateAtmosphere(const QList<DifferentialSample>& samples)
{
    AtmosphericParams params;
    constexpr double kPi = 3.14159265358979323846;

    double apertureDiameter = 0.0;
    double baselineSeparation = 0.0;
    double f = 0.0;
    double zenithAngleDeg = 0.0;
    double lambda = 0.0;
    double pixelSize = 0.0;
    {
        QMutexLocker locker(&m_mutex);
        apertureDiameter = m_apertureDiameter;
        baselineSeparation = m_baselineSeparation;
        f = m_f;
        zenithAngleDeg = m_zenithAngleDeg;
        lambda = m_lambda;
        pixelSize = m_pixelSize;
    }

    if (samples.size() < 2 || apertureDiameter <= 0.0 || baselineSeparation <= 0.0 ||
        f <= 0.0 || lambda <= 0.0 || pixelSize <= 0.0) {
        return params;
    }

    double meanLongitudinal = 0.0;
    double meanTransverse = 0.0;
    for (const auto& sample : samples) {
        meanLongitudinal += sample.longitudinal;
        meanTransverse += sample.transverse;
    }
    meanLongitudinal /= samples.size();
    meanTransverse /= samples.size();

    double varLongitudinalPx = 0.0;
    double varTransversePx = 0.0;
    for (const auto& sample : samples) {
        varLongitudinalPx +=
            (sample.longitudinal - meanLongitudinal) * (sample.longitudinal - meanLongitudinal);
        varTransversePx +=
            (sample.transverse - meanTransverse) * (sample.transverse - meanTransverse);
    }

    const double denominator = static_cast<double>(samples.size());
    varLongitudinalPx /= denominator;
    varTransversePx /= denominator;
    params.sampleCount = static_cast<quint64>(samples.size());
    params.longitudinalVariancePx2 = varLongitudinalPx;
    params.transverseVariancePx2 = varTransversePx;

    const double pixelScaleRad = pixelSize / f;
    const double sigmaLongitudinal2 = varLongitudinalPx * pixelScaleRad * pixelScaleRad;
    const double sigmaTransverse2 = varTransversePx * pixelScaleRad * pixelScaleRad;
    params.longitudinalVarianceRad2 = sigmaLongitudinal2;
    params.transverseVarianceRad2 = sigmaTransverse2;

    if (sigmaLongitudinal2 <= 0.0 || sigmaTransverse2 <= 0.0) {
        return params;
    }
    constexpr double kRadToArcsec = 206265.0;
    const double sigmaLongitudinalArcsec2 = sigmaLongitudinal2 * kRadToArcsec * kRadToArcsec;

    const double invAperture = std::pow(apertureDiameter, -1.0 / 3.0);
    const double invBaseline = std::pow(baselineSeparation, -1.0 / 3.0);
    const double longitudinalCoeff =
        2.0 * lambda * lambda * (0.179 * invAperture - 0.0968 * invBaseline);
    const double transverseCoeff =
        2.0 * lambda * lambda * (0.179 * invAperture - 0.145 * invBaseline);

    if (longitudinalCoeff <= 0.0 || transverseCoeff <= 0.0) {
        return params;
    }

    const double r0Longitudinal = std::pow(longitudinalCoeff / sigmaLongitudinal2, 3.0 / 5.0);
    const double r0Transverse = std::pow(transverseCoeff / sigmaTransverse2, 3.0 / 5.0);
    params.r0LongitudinalCm = r0Longitudinal * 100.0;
    params.r0TransverseCm = r0Transverse * 100.0;
    const double r0LineOfSight = 0.5 * (r0Longitudinal + r0Transverse);
    const double cosZenith = std::cos(zenithAngleDeg * kPi / 180.0);
    const double zenithCorrection =
        cosZenith > 0.0 ? std::pow(cosZenith, -3.0 / 5.0) : 1.0;
    const double r0Zenith = r0LineOfSight * zenithCorrection;

    if (r0Zenith > 0.0) {
        params.r0 = r0Zenith * 100.0;
        params.seeing = 0.98 * lambda / r0Zenith * kRadToArcsec;
        params.theta0 = 0.64 * (4.0 / std::pow(sigmaLongitudinalArcsec2, 0.65)) * std::pow(cosZenith, 8.0 / 5.0);
        const Tau0Estimate tau0Estimate =
            estimateDifferentialAutocorrelationTimeMs(samples);

        params.tau0 = tau0Estimate.valueMs;
        params.tau0Valid = tau0Estimate.valid;
        params.tau0UnderResolved = tau0Estimate.underResolved;
        params.tau0ResolutionMs = tau0Estimate.resolutionMs;
    }

    return params;
}

QList<DifferentialSample> ImageProcessorWorker::tau0WindowSamples(
    const QList<DifferentialSample>& samples) const
{
    if (samples.isEmpty()) {
        return {};
    }

    int startIndex = 0;
    const qint64 newestTimestampMs = samples.last().timestampMs;

    if (newestTimestampMs > 0) {
        const qint64 cutoffTimestampMs =
            newestTimestampMs -
            static_cast<qint64>(TAU0_HISTORY_WINDOW_SECONDS * 1000.0);

        while (startIndex < samples.size() - 1 &&
               samples[startIndex].timestampMs > 0 &&
               samples[startIndex].timestampMs < cutoffTimestampMs) {
            ++startIndex;
        }
    } else {
        // 只有在主机时间戳不可用时，才使用目标帧率估算最近 3 秒样本数。
        double targetFrameRateHz = 0.0;
        {
            QMutexLocker locker(&m_mutex);
            targetFrameRateHz = m_targetFrameRateHz;
        }

        const int desiredSampleCount = std::max(
            TAU0_MIN_SAMPLES,
            static_cast<int>(std::lround(
                targetFrameRateHz * TAU0_HISTORY_WINDOW_SECONDS)));

        startIndex = std::max(0, static_cast<int>(samples.size()) - desiredSampleCount);
    }

    // tau0 对时间连续性敏感。
    // 只保留最近一次 Frame ID 跳变之后的连续区间。
    for (int i = samples.size() - 1; i > startIndex; --i) {
        const DifferentialSample& previous = samples[i - 1];
        const DifferentialSample& current = samples[i];

        const bool camera1Gap =
            previous.frameId1 > 0 &&
            current.frameId1 > 0 &&
            current.frameId1 != previous.frameId1 + 1;

        const bool camera2Gap =
            previous.frameId2 > 0 &&
            current.frameId2 > 0 &&
            current.frameId2 != previous.frameId2 + 1;

        if (camera1Gap || camera2Gap) {
            startIndex = i;
            break;
        }
    }

    return samples.mid(startIndex);
}

Tau0Estimate ImageProcessorWorker::estimateDifferentialAutocorrelationTimeMs(
    const QList<DifferentialSample>& allSamples) const
{
    const QList<DifferentialSample> samples = tau0WindowSamples(allSamples);

    if (samples.size() < TAU0_MIN_SAMPLES) {
        return {};
    }

    const Tau0Estimate longitudinalEstimate =
        estimateScalarAutocorrelationCrossingMs(samples, true);
    const Tau0Estimate transverseEstimate =
        estimateScalarAutocorrelationCrossingMs(samples, false);

    if (longitudinalEstimate.valid && transverseEstimate.valid) {
        // 任意一个方向欠分辨时，不再把两个方向平均成一个伪精确值。
        // 采用保守状态：整体结果标记为欠分辨。
        if (longitudinalEstimate.underResolved ||
            transverseEstimate.underResolved) {
            Tau0Estimate result;
            result.valid = true;
            result.underResolved = true;
            result.resolutionMs = std::max(
                longitudinalEstimate.resolutionMs,
                transverseEstimate.resolutionMs);
            result.valueMs = result.resolutionMs;
            return result;
        }

        Tau0Estimate result;
        result.valid = true;
        result.underResolved = false;
        result.valueMs = 0.5 *
            (longitudinalEstimate.valueMs + transverseEstimate.valueMs);
        result.resolutionMs = 0.5 *
            (longitudinalEstimate.resolutionMs +
             transverseEstimate.resolutionMs);
        return result;
    }

    if (longitudinalEstimate.valid) {
        return longitudinalEstimate;
    }

    if (transverseEstimate.valid) {
        return transverseEstimate;
    }

    return {};
}

Tau0Estimate ImageProcessorWorker::estimateScalarAutocorrelationCrossingMs(
    const QList<DifferentialSample>& samples,
    bool useLongitudinal) const
{
    if (samples.size() < TAU0_MIN_SAMPLES) {
        return {};
    }

    std::vector<double> intervalsMs;
    intervalsMs.reserve(static_cast<std::size_t>(samples.size() - 1));

    for (int i = 1; i < samples.size(); ++i) {
        double dtMs = 0.0;

        if (samples[i].cameraTimestamp1 >
                samples[i - 1].cameraTimestamp1 &&
            samples[i - 1].cameraTimestamp1 > 0) {
            const quint64 dtTicks =
                samples[i].cameraTimestamp1 -
                samples[i - 1].cameraTimestamp1;

            dtMs = static_cast<double>(dtTicks) *
                   MARS_GIGE_TIMESTAMP_TICK_US /
                   1000.0;
        } else if (samples[i].cameraTimestamp2 >
                       samples[i - 1].cameraTimestamp2 &&
                   samples[i - 1].cameraTimestamp2 > 0) {
            const quint64 dtTicks =
                samples[i].cameraTimestamp2 -
                samples[i - 1].cameraTimestamp2;

            dtMs = static_cast<double>(dtTicks) *
                   MARS_GIGE_TIMESTAMP_TICK_US /
                   1000.0;
        } else if (samples[i].timestampMs >
                   samples[i - 1].timestampMs) {
            dtMs = static_cast<double>(
                samples[i].timestampMs -
                samples[i - 1].timestampMs);
        }

        if (std::isfinite(dtMs) && dtMs > 0.0) {
            intervalsMs.push_back(dtMs);
        }
    }

    if (intervalsMs.empty()) {
        return {};
    }

    const std::size_t middleIndex = intervalsMs.size() / 2;
    std::nth_element(intervalsMs.begin(),
                     intervalsMs.begin() +
                         static_cast<std::ptrdiff_t>(middleIndex),
                     intervalsMs.end());

    const double sampleIntervalMs = intervalsMs[middleIndex];
    if (!std::isfinite(sampleIntervalMs) || sampleIntervalMs <= 0.0) {
        return {};
    }

    const int sampleCount = static_cast<int>(samples.size());

    double mean = 0.0;
    for (const DifferentialSample& sample : samples) {
        mean += useLongitudinal
                    ? sample.longitudinal
                    : sample.transverse;
    }
    mean /= static_cast<double>(sampleCount);

    double varianceSum = 0.0;
    for (const DifferentialSample& sample : samples) {
        const double value = useLongitudinal
                                 ? sample.longitudinal
                                 : sample.transverse;
        const double centered = value - mean;
        varianceSum += centered * centered;
    }

    if (!std::isfinite(varianceSum) || varianceSum <= 0.0) {
        return {};
    }

    const int maxLagByTime = std::max(
        1,
        static_cast<int>(std::ceil(
            TAU0_MAX_LAG_MS / sampleIntervalMs)));

    const int maxLag = std::min(
        sampleCount / 2,
        maxLagByTime);

    if (maxLag < 1) {
        return {};
    }

    const double oneOverE = 1.0 / std::exp(1.0);
    double previousCorrelation = 1.0;

    for (int lag = 1; lag <= maxLag; ++lag) {
        double numerator = 0.0;

        for (int i = 0; i + lag < sampleCount; ++i) {
            const DifferentialSample& a = samples[i];
            const DifferentialSample& b = samples[i + lag];

            const double valueA = useLongitudinal
                                      ? a.longitudinal
                                      : a.transverse;
            const double valueB = useLongitudinal
                                      ? b.longitudinal
                                      : b.transverse;

            numerator += (valueA - mean) * (valueB - mean);
        }

        const double correlation =
            (numerator / static_cast<double>(sampleCount - lag)) /
            (varianceSum / static_cast<double>(sampleCount));

        if (!std::isfinite(correlation)) {
            continue;
        }

        if (correlation <= oneOverE) {
            Tau0Estimate result;
            result.valid = true;
            result.resolutionMs = sampleIntervalMs;

            if (lag == 1) {
                // 只能确定 tau0 小于或接近一个采样周期。
                // 不进行 lag=0 到 lag=1 的伪精确插值。
                result.underResolved = true;
                result.valueMs = sampleIntervalMs;
                return result;
            }

            const double denominator =
                previousCorrelation - correlation;

            if (!std::isfinite(denominator) ||
                std::abs(denominator) <= 1e-12) {
                result.underResolved = false;
                result.valueMs =
                    static_cast<double>(lag) * sampleIntervalMs;
                return result;
            }

            const double crossingLag =
                static_cast<double>(lag - 1) +
                (previousCorrelation - oneOverE) /
                    denominator;

            if (!std::isfinite(crossingLag) || crossingLag <= 0.0) {
                return {};
            }

            result.underResolved = false;
            result.valueMs = crossingLag * sampleIntervalMs;
            return result;
        }

        previousCorrelation = correlation;
    }

    // 200 ms 范围内没有找到 1/e 交点。
    // 当前任务不增加“超出最大搜索范围”状态，按无效结果处理。
    return {};
}

int ImageProcessorWorker::historyWindowSize() const
{
    QMutexLocker locker(&m_mutex);
    return std::clamp(
        static_cast<int>(std::lround(m_targetFrameRateHz * ATMOSPHERE_HISTORY_WINDOW_SECONDS)),
        MIN_HISTORY_WINDOW,
        MAX_HISTORY_WINDOW);
}

int ImageProcessorWorker::minimumAtmosphereSamples() const
{
    return historyWindowSize();
}

int ImageProcessorWorker::pendingCentroidQueueLimit() const
{
    QMutexLocker locker(&m_mutex);
    return std::clamp(static_cast<int>(std::lround(m_targetFrameRateHz)),
                      MIN_HISTORY_WINDOW,
                      MAX_PENDING_PAIR_QUEUE);
}

void ImageProcessorWorker::resetRoiProcessingHistory()
{
    m_lastAtmospherePublishMs = 0;
    m_lastRoiImagePublishMs[0] = 0;
    m_lastRoiImagePublishMs[1] = 0;
    m_differentialHistory.clear();
    m_pendingCentroids[0].clear();
    m_pendingCentroids[1].clear();
    m_syncCalibrated = false;
    m_timestampOffsetCalibrated = false;
    m_firstRawFrameId[0] = 0;
    m_firstRawFrameId[1] = 0;
    m_firstRawTimestamp[0] = 0;
    m_firstRawTimestamp[1] = 0;
    m_frameIdOffset = 0;
    m_timestampOffsetTicks = 0.0L;
    m_diagnosticUnpairedDropLogCount = 0;
}

void ImageProcessorWorker::resetRunProcessingState()
{
    QMutexLocker locker(&m_mutex);
    m_lastPairedSerial = 0;
    m_droppedUnpairedSamples = 0;
    resetRoiProcessingHistory();
}

bool ImageProcessorWorker::appendDifferentialSample()
{
    if (!m_syncCalibrated) {
        return false;
    }

    while (!m_pendingCentroids[0].isEmpty() && !m_pendingCentroids[1].isEmpty()) {
        const PendingCentroidSample& front0 = m_pendingCentroids[0].first();
        const PendingCentroidSample& front1 = m_pendingCentroids[1].first();

        if (front0.frameId == 0 || front1.frameId == 0) {
            return false;
        }

        const qint64 alignedFrameId0 = static_cast<qint64>(front0.frameId);
        const qint64 alignedFrameId1 = static_cast<qint64>(front1.frameId) - m_frameIdOffset;

        // Pair only matching hardware-trigger FrameID positions. If one camera skipped a usable
        // centroid, discard the older unpaired sample so following pairs stay aligned.
        if (alignedFrameId0 < alignedFrameId1) {
            ++m_droppedUnpairedSamples;
            ++m_diagnosticUnpairedDropLogCount;
            emit unpairedSampleDropped(0,
                                       front0.frameId,
                                       front1.frameId,
                                       m_frameIdOffset,
                                       alignedFrameId0,
                                       alignedFrameId1,
                                       front0.cameraTimestamp,
                                       front1.cameraTimestamp,
                                       m_droppedUnpairedSamples);
            m_pendingCentroids[0].removeFirst();
            continue;
        }
        if (alignedFrameId1 < alignedFrameId0) {
            ++m_droppedUnpairedSamples;
            ++m_diagnosticUnpairedDropLogCount;
            emit unpairedSampleDropped(1,
                                       front0.frameId,
                                       front1.frameId,
                                       m_frameIdOffset,
                                       alignedFrameId0,
                                       alignedFrameId1,
                                       front0.cameraTimestamp,
                                       front1.cameraTimestamp,
                                       m_droppedUnpairedSamples);
            m_pendingCentroids[1].removeFirst();
            continue;
        }
        break;
    }

    if (m_pendingCentroids[0].isEmpty() || m_pendingCentroids[1].isEmpty()) {
        return false;
    }

    const PendingCentroidSample cam0 = m_pendingCentroids[0].takeFirst();
    const PendingCentroidSample cam1 = m_pendingCentroids[1].takeFirst();
    double syncResidualUs = 0.0;
    if (m_timestampOffsetCalibrated && cam0.cameraTimestamp > 0 && cam1.cameraTimestamp > 0) {
        const long double signedDeltaTicks =
            static_cast<long double>(cam1.cameraTimestamp) -
            static_cast<long double>(cam0.cameraTimestamp);
        const long double residualTicks = signedDeltaTicks - m_timestampOffsetTicks;
        syncResidualUs = static_cast<double>(residualTicks) * MARS_GIGE_TIMESTAMP_TICK_US;
        emit syncSampleReady(syncResidualUs);
    }

    DifferentialSample sample;
    double baselineAngleDeg = 0.0;
    {
        QMutexLocker locker(&m_mutex);
        baselineAngleDeg = m_baselineAngleDeg;
    }
    constexpr double kPi = 3.14159265358979323846;
    const double baselineAngleRad = baselineAngleDeg * kPi / 180.0;
    const double baselineCos = std::cos(baselineAngleRad);
    const double baselineSin = std::sin(baselineAngleRad);
    const double dx = cam1.centroid.x - cam0.centroid.x;
    const double dy = cam1.centroid.y - cam0.centroid.y;
    sample.longitudinal = dx * baselineCos + dy * baselineSin;
    sample.transverse = -dx * baselineSin + dy * baselineCos;
    sample.centroid1X = cam0.centroid.x;
    sample.centroid1Y = cam0.centroid.y;
    sample.centroid2X = cam1.centroid.x;
    sample.centroid2Y = cam1.centroid.y;
    sample.frameId1 = cam0.frameId;
    sample.frameId2 = cam1.frameId;
    sample.cameraTimestamp1 = cam0.cameraTimestamp;
    sample.cameraTimestamp2 = cam1.cameraTimestamp;
    sample.syncResidualUs = syncResidualUs;
    sample.timestampMs = std::max(cam0.timestampMs, cam1.timestampMs);
    m_differentialHistory.append(sample);
    while (m_differentialHistory.size() > historyWindowSize()) {
        m_differentialHistory.removeFirst();
    }
    ++m_lastPairedSerial;
    emit differentialSampleDetailReady(m_lastPairedSerial,
                                       sample.frameId1,
                                       sample.frameId2,
                                       sample.cameraTimestamp1,
                                       sample.cameraTimestamp2,
                                       sample.centroid1X,
                                       sample.centroid1Y,
                                       sample.centroid2X,
                                       sample.centroid2Y,
                                       sample.longitudinal,
                                       sample.transverse,
                                       sample.syncResidualUs,
                                       sample.timestampMs);
    return true;
}

void ImageProcessorWorker::emitRoiImageIfDue(int cameraIndex,
                                             const cv::Mat& roiImage,
                                             qint64 nowMs,
                                             bool force)
{
    if (cameraIndex < 0 || cameraIndex >= 2 || roiImage.empty()) {
        return;
    }

    if (force ||
        m_lastRoiImagePublishMs[cameraIndex] <= 0 ||
        (nowMs - m_lastRoiImagePublishMs[cameraIndex]) >= ROI_IMAGE_PUBLISH_INTERVAL_MS) {
        m_lastRoiImagePublishMs[cameraIndex] = nowMs;
        emit roiImageReady(cameraIndex, roiImage);
    }
}

void ImageProcessorWorker::processFrame(int cameraIndex,
                                        cv::Mat frame,
                                        quint64 frameId,
                                        quint64 cameraTimestamp,
                                        quint64 acquisitionGeneration)
{
    QElapsedTimer elapsedTimer;
    elapsedTimer.start();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const auto finishProcessing = [this, cameraIndex, &elapsedTimer](bool centroidValid) {
        emit frameProcessed(cameraIndex,
                            centroidValid,
                            static_cast<double>(elapsedTimer.nsecsElapsed()) / 1000000.0);
    };

    if (frame.empty() || cameraIndex < 0 || cameraIndex >= 2) {
        finishProcessing(false);
        return;
    }

    RoiRect roi;
    {
        QMutexLocker locker(&m_mutex);
        if (acquisitionGeneration > 0 && acquisitionGeneration != m_acquisitionGeneration->load()) {
            finishProcessing(false);
            return;
        }
        roi = m_currentRoi[cameraIndex];
    }

    if (frameId > 0) {
        QMutexLocker locker(&m_mutex);
        if (m_firstRawFrameId[cameraIndex] == 0) {
            m_firstRawFrameId[cameraIndex] = frameId;
        }
        if (!m_syncCalibrated && m_firstRawFrameId[0] > 0 && m_firstRawFrameId[1] > 0) {
            m_frameIdOffset =
                static_cast<qint64>(m_firstRawFrameId[1]) -
                static_cast<qint64>(m_firstRawFrameId[0]);
            m_syncCalibrated = true;
        }
    }
    if (cameraTimestamp > 0) {
        QMutexLocker locker(&m_mutex);
        if (m_firstRawTimestamp[cameraIndex] == 0) {
            m_firstRawTimestamp[cameraIndex] = cameraTimestamp;
        }
        if (!m_timestampOffsetCalibrated &&
            m_firstRawTimestamp[0] > 0 &&
            m_firstRawTimestamp[1] > 0) {
            m_timestampOffsetTicks =
                static_cast<long double>(m_firstRawTimestamp[1]) -
                static_cast<long double>(m_firstRawTimestamp[0]);
            m_timestampOffsetCalibrated = true;
        }
    }

    // In live hardware-ROI mode the camera already returns the 64x64 window itself.
    // Keep the absolute ROI offset for centroid reporting, but crop from (0, 0)
    // instead of treating the absolute sensor coordinates as local frame coordinates.
    const bool frameAlreadyMatchesRoiWindow =
        frame.cols <= roi.w && frame.rows <= roi.h;
    int cropOriginX = std::max(0, roi.x);
    int cropOriginY = std::max(0, roi.y);
    if (frameAlreadyMatchesRoiWindow) {
        cropOriginX = 0;
        cropOriginY = 0;
    }

    const int w = std::min(roi.w, frame.cols - cropOriginX);
    const int h = std::min(roi.h, frame.rows - cropOriginY);
    if (w < MIN_ROI_SIZE || h < MIN_ROI_SIZE) {
        finishProcessing(false);
        return;
    }

    cv::Mat roiImage = frame(cv::Rect(cropOriginX, cropOriginY, w, h)).clone();
    if (roiImage.empty()) {
        finishProcessing(false);
        return;
    }

    bool shouldEmitAutoExposureSample = false;
    double hardSaturationDn = 4090.0;
    {
        QMutexLocker locker(&m_mutex);
        if (cameraIndex >= 0 && cameraIndex < 2 && m_autoExposureMetricsEnabled) {
            const qint64 lastSampleMs = m_lastAutoExposureSampleMs[cameraIndex];
            shouldEmitAutoExposureSample =
                lastSampleMs < 0 || nowMs - lastSampleMs >= m_autoExposureMetricIntervalMs;
            if (shouldEmitAutoExposureSample) {
                m_lastAutoExposureSampleMs[cameraIndex] = nowMs;
                hardSaturationDn = m_autoExposureHardSaturationDn;
            }
        }
    }
    const double autoExposureDnScale = roiImage.depth() == CV_8U ? 4095.0 / 255.0 : 1.0;
    const double rawHardSaturationDn = hardSaturationDn / autoExposureDnScale;
    quint64 saturatedPixelCount = 0;
    if (shouldEmitAutoExposureSample) {
        for (int y = 0; y < roiImage.rows; ++y) {
            for (int x = 0; x < roiImage.cols; ++x) {
                if (pixelValueAt(roiImage, y, x) >= rawHardSaturationDn) {
                    ++saturatedPixelCount;
                }
            }
        }
    }

    const cv::Mat correctedRoiImage = applyHotPixelCorrection(cameraIndex, roi, roiImage);
    CentroidResult centroid = calculateCentroid(cameraIndex, roi, correctedRoiImage);
    AutoExposureSpotResult autoExposureSpot;
    const bool correctedMeasurementUsable =
        centroid.valid && isMeasurementUsableCentroid(centroid, correctedRoiImage);
    const bool measurementUsable = correctedMeasurementUsable;
    const bool autoExposureMeasurementUsable =
        centroid.valid && centroid.signalPixelCount <= kMaxMeasurementSignalPixels;
    if (shouldEmitAutoExposureSample) {
        AutoExposureSpotConfig spotConfig;
        double fallbackSpotThresholdDn = 0.0;
        {
            QMutexLocker locker(&m_mutex);
            spotConfig = m_autoExposureSpotConfig;
            fallbackSpotThresholdDn =
                m_roiThresholdAbsolute >= 0.0
                    ? m_roiThresholdAbsolute
                    : (m_threshold > 0.0
                           ? m_threshold
                           : static_cast<double>(m_centroidMinimumIntensity));
        }
        spotConfig.thresholdDn = centroid.threshold > 0.0 ? centroid.threshold
                                                          : fallbackSpotThresholdDn;
        spotConfig.hardSaturationDn = rawHardSaturationDn;
        const cv::Mat aeIntensity = makeCentroidIntensityImage(correctedRoiImage);
        std::vector<double> aePixels;
        if (!aeIntensity.empty()) {
            aePixels.reserve(static_cast<std::size_t>(aeIntensity.rows * aeIntensity.cols));
            for (int y = 0; y < aeIntensity.rows; ++y) {
                const double* row = aeIntensity.ptr<double>(y);
                for (int x = 0; x < aeIntensity.cols; ++x) {
                    aePixels.push_back(row[x]);
                }
            }
            autoExposureSpot = analyzeAutoExposureSpot(aePixels.data(),
                                                       aeIntensity.cols,
                                                       aeIntensity.rows,
                                                       spotConfig);
        }
        const double aePeakValue =
            autoExposureSpot.validSpotPeak
                ? autoExposureSpot.supportedPeakDn
                : centroid.peakValue;
        emit autoExposureSampleReady(cameraIndex,
                                     aePeakValue * autoExposureDnScale,
                                     centroid.background * autoExposureDnScale,
                                     centroid.noiseSigma * autoExposureDnScale,
                                     centroid.threshold * autoExposureDnScale,
                                     centroid.signalPixelCount,
                                     saturatedPixelCount,
                                     static_cast<int>(autoExposureSpot.quality),
                                     autoExposureSpot.supportedPeakDn * autoExposureDnScale,
                                     static_cast<quint64>(std::max(0, autoExposureSpot.supportPixelCount)),
                                     autoExposureSpot.rejectedPeakDn * autoExposureDnScale,
                                     autoExposureSpot.rejectedCandidateCount,
                                     autoExposureSpot.spotHardSaturated,
                                     centroid.valid,
                                     autoExposureMeasurementUsable,
                                     frameId,
                                     nowMs);
    }
    if (centroid.valid) {
        CentroidResult absoluteCentroid = centroid;
        absoluteCentroid.x += roi.x;
        absoluteCentroid.y += roi.y;
        emit centroidReady(cameraIndex,
                           absoluteCentroid.x,
                           absoluteCentroid.y,
                           absoluteCentroid.peakValue,
                           absoluteCentroid.totalFlux,
                           absoluteCentroid.background,
                           absoluteCentroid.threshold,
                           absoluteCentroid.signalPixelCount);

        if (!measurementUsable) {
            emitRoiImageIfDue(cameraIndex, roiImage, nowMs);
            finishProcessing(true);
            return;
        }

        PendingCentroidSample pending;
        pending.centroid = absoluteCentroid;
        pending.frameId = frameId;
        pending.cameraTimestamp = cameraTimestamp;
        pending.timestampMs = nowMs;
        m_pendingCentroids[cameraIndex].append(pending);
        while (m_pendingCentroids[cameraIndex].size() > pendingCentroidQueueLimit()) {
            m_pendingCentroids[cameraIndex].removeFirst();
            ++m_droppedUnpairedSamples;
        }

        if (appendDifferentialSample()) {
            emit differentialSampleReady(m_lastPairedSerial, m_droppedUnpairedSamples);
            if (m_differentialHistory.size() < minimumAtmosphereSamples()) {
                emitRoiImageIfDue(cameraIndex, roiImage, nowMs);
                finishProcessing(true);
                return;
            }
            if (m_lastAtmospherePublishMs > 0 &&
                (nowMs - m_lastAtmospherePublishMs) < ATMOSPHERE_PUBLISH_INTERVAL_MS) {
                emitRoiImageIfDue(cameraIndex, roiImage, nowMs);
                finishProcessing(true);
                return;
            }
            const AtmosphericParams params = calculateAtmosphere(m_differentialHistory);
            if (params.r0 > 0.0) {
                m_lastAtmospherePublishMs = nowMs;
                emit atmosphereReady(params.r0,
                                     params.seeing,
                                     params.theta0,
                                     params.tau0,
                                     params.tau0Valid,
                                     params.tau0UnderResolved,
                                     params.tau0ResolutionMs,
                                     params.longitudinalVariancePx2,
                                     params.transverseVariancePx2,
                                     params.longitudinalVarianceRad2,
                                     params.transverseVarianceRad2,
                                     params.r0LongitudinalCm,
                                     params.r0TransverseCm,
                                     params.sampleCount);
            }
        }
    }

    emitRoiImageIfDue(cameraIndex, roiImage, nowMs);
    finishProcessing(centroid.valid);
}

ImageProcessor::ImageProcessor(QObject* parent)
    : QObject(parent)
{
    m_workerThread = new QThread(this);
    m_worker = new ImageProcessorWorker(m_acquisitionGeneration);
    m_worker->moveToThread(m_workerThread);

    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &ImageProcessorWorker::centroidReady, this, &ImageProcessor::centroidReady);
    connect(m_worker, &ImageProcessorWorker::differentialSampleReady, this, &ImageProcessor::differentialSampleReady);
    connect(m_worker,
            &ImageProcessorWorker::differentialSampleDetailReady,
            this,
            &ImageProcessor::differentialSampleDetailReady);
    connect(m_worker, &ImageProcessorWorker::roiImageReady, this, &ImageProcessor::roiImageReady);
    connect(m_worker, &ImageProcessorWorker::atmosphereReady, this, &ImageProcessor::atmosphereReady);
    connect(m_worker, &ImageProcessorWorker::frameProcessed, this, &ImageProcessor::frameProcessed);
    connect(m_worker, &ImageProcessorWorker::syncSampleReady, this, &ImageProcessor::syncSampleReady);
    connect(m_worker,
            &ImageProcessorWorker::unpairedSampleDropped,
            this,
            &ImageProcessor::unpairedSampleDropped);
    connect(m_worker,
            &ImageProcessorWorker::autoExposureSampleReady,
            this,
            &ImageProcessor::autoExposureSampleReady);

    m_workerThread->start();
}

ImageProcessor::~ImageProcessor()
{
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
    }
}

void ImageProcessor::setCentroidMethod(int method)
{
    setCentroidMode(method);
}

void ImageProcessor::setCentroidMode(int mode)
{
    m_centroidMode = mode == 1 ? 1 : 0;
    QMetaObject::invokeMethod(m_worker,
                              "setCentroidMode",
                              Qt::QueuedConnection,
                              Q_ARG(int, m_centroidMode));
}

void ImageProcessor::setPeakKernelCentroidConfig(int method,
                                                 int radiusPx,
                                                 double strongHotPixelExcessDn)
{
    m_peakKernelMethod = method == 0 ? 0 : 1;
    m_peakKernelRadiusPx = std::clamp(radiusPx, 1, 20);
    m_strongHotPixelExcessDn = std::clamp(strongHotPixelExcessDn, 1.0, 4095.0);
    QMetaObject::invokeMethod(m_worker,
                              "setPeakKernelCentroidConfig",
                              Qt::QueuedConnection,
                              Q_ARG(int, m_peakKernelMethod),
                              Q_ARG(int, m_peakKernelRadiusPx),
                              Q_ARG(double, m_strongHotPixelExcessDn));
}

void ImageProcessor::setGaussianKernelSize(int size)
{
    setBackgroundDenoiseKernelSize(size);
}

void ImageProcessor::setGaussianSigma(double sigma)
{
    setBackgroundDenoiseSigmaMultiplier(sigma);
}

void ImageProcessor::setBackgroundDenoiseKernelSize(int size)
{
    int sanitized = std::max(1, size);
    sanitized = (sanitized % 2 == 0) ? sanitized + 1 : sanitized;
    m_backgroundDenoiseKernelSize = std::min(sanitized, 31);
    QMetaObject::invokeMethod(m_worker,
                              "setBackgroundDenoiseKernelSize",
                              Qt::QueuedConnection,
                              Q_ARG(int, size));
}

void ImageProcessor::setBackgroundDenoiseSigmaMultiplier(double multiplier)
{
    m_backgroundDenoiseSigmaMultiplier = std::max(0.0, multiplier);
    QMetaObject::invokeMethod(m_worker,
                              "setBackgroundDenoiseSigmaMultiplier",
                              Qt::QueuedConnection,
                              Q_ARG(double, multiplier));
}

void ImageProcessor::setThreshold(double threshold)
{
    QMetaObject::invokeMethod(m_worker, "setThreshold", Qt::QueuedConnection, Q_ARG(double, threshold));
}

void ImageProcessor::setRoiCentroidConfig(double thresholdAbsolute,
                                          double sigmaThreshold,
                                          double minimumIntensity,
                                          int minimumSignalPixels,
                                          double noiseTrimFraction)
{
    m_backgroundDenoiseSigmaMultiplier = std::max(0.0, sigmaThreshold);
    QMetaObject::invokeMethod(m_worker,
                              "setRoiCentroidConfig",
                              Qt::QueuedConnection,
                              Q_ARG(double, thresholdAbsolute),
                              Q_ARG(double, sigmaThreshold),
                              Q_ARG(double, minimumIntensity),
                              Q_ARG(int, minimumSignalPixels),
                              Q_ARG(double, noiseTrimFraction));
}

void ImageProcessor::setTargetFrameRateHz(double frameRateHz)
{
    m_targetFrameRateHz = std::clamp(frameRateHz, 1.0, 1000.0);
    QMetaObject::invokeMethod(m_worker,
                              "setTargetFrameRateHz",
                              Qt::QueuedConnection,
                              Q_ARG(double, m_targetFrameRateHz));
}

void ImageProcessor::setAutoExposureMetricConfig(bool enabled,
                                                 double hardSaturationDn,
                                                 int sampleIntervalMs,
                                                 int peakSupportRadiusPx,
                                                 double peakSupportFraction,
                                                 int minPeakSupportPixelCount,
                                                 double minNeighborPeakRatio,
                                                 int maxPeakCandidateCount,
                                                 double supportedPeakPercentile,
                                                 int saturatedPixelCount)
{
    QMetaObject::invokeMethod(m_worker,
                              "setAutoExposureMetricConfig",
                              Qt::QueuedConnection,
                              Q_ARG(bool, enabled),
                              Q_ARG(double, hardSaturationDn),
                              Q_ARG(int, sampleIntervalMs),
                              Q_ARG(int, peakSupportRadiusPx),
                              Q_ARG(double, peakSupportFraction),
                              Q_ARG(int, minPeakSupportPixelCount),
                              Q_ARG(double, minNeighborPeakRatio),
                              Q_ARG(int, maxPeakCandidateCount),
                              Q_ARG(double, supportedPeakPercentile),
                              Q_ARG(int, saturatedPixelCount));
}

void ImageProcessor::configureHotPixelTemplates(const QString& camera0MaskPath,
                                                const QString& camera0ExcessPath,
                                                const QString& camera1MaskPath,
                                                const QString& camera1ExcessPath,
                                                int templateWidth,
                                                int templateHeight)
{
    QMetaObject::invokeMethod(m_worker,
                              "configureHotPixelTemplates",
                              Qt::QueuedConnection,
                              Q_ARG(QString, camera0MaskPath),
                              Q_ARG(QString, camera0ExcessPath),
                              Q_ARG(QString, camera1MaskPath),
                              Q_ARG(QString, camera1ExcessPath),
                              Q_ARG(int, templateWidth),
                              Q_ARG(int, templateHeight));
}

bool ImageProcessor::loadProcessingConfig(const QString& path, QString* message)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (message) {
            *message = QStringLiteral("未找到图像处理配置: %1").arg(path);
        }
        return false;
    }

    double thresholdAbsolute = -1.0;
    double sigmaThreshold = 4.0;
    double minimumIntensity = 16.0;
    int minimumSignalPixels = 3;
    double noiseTrimFraction = 0.10;
    QString camera0Mask;
    QString camera0Excess;
    QString camera1Mask;
    QString camera1Excess;
    int hotTemplateWidth = 0;
    int hotTemplateHeight = 0;

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
        QString valueText = line.mid(equalPos + 1).trimmed();
        const QFileInfo configInfo(path);
        auto resolvePath = [&](const QString& rawPath) {
            QFileInfo candidate(rawPath);
            if (candidate.isAbsolute()) {
                return candidate.absoluteFilePath();
            }
            return QFileInfo(configInfo.absoluteDir(), rawPath).absoluteFilePath();
        };

        bool ok = false;
        const double number = valueText.toDouble(&ok);
        if ((key == QStringLiteral("roi_threshold_absolute") ||
             key == QStringLiteral("roi_absolute")) && ok) {
            thresholdAbsolute = number;
        } else if ((key == QStringLiteral("roi_signal_sigma") ||
                    key == QStringLiteral("threshold_sigma") ||
                    key == QStringLiteral("sigma")) && ok) {
            sigmaThreshold = number;
        } else if ((key == QStringLiteral("roi_min_intensity") ||
                    key == QStringLiteral("roi_minimum_intensity") ||
                    key == QStringLiteral("threshold_min_intensity") ||
                    key == QStringLiteral("min_intensity")) && ok) {
            minimumIntensity = number;
        } else if ((key == QStringLiteral("roi_min_signal_pixels") ||
                    key == QStringLiteral("roi_minimum_signal_pixels") ||
                    key == QStringLiteral("minimum_signal_pixels")) && ok) {
            minimumSignalPixels = std::max(1, static_cast<int>(std::lround(number)));
        } else if ((key == QStringLiteral("roi_noise_trim_fraction") ||
                    key == QStringLiteral("noise_trim_fraction")) && ok) {
            noiseTrimFraction = number;
        } else if ((key == QStringLiteral("hot_pixel_template_width") ||
                    key == QStringLiteral("hot_template_width")) && ok) {
            hotTemplateWidth = std::max(0, static_cast<int>(std::lround(number)));
        } else if ((key == QStringLiteral("hot_pixel_template_height") ||
                    key == QStringLiteral("hot_template_height")) && ok) {
            hotTemplateHeight = std::max(0, static_cast<int>(std::lround(number)));
        } else if (key == QStringLiteral("camera_a_hot_pixel_mask") ||
                   key == QStringLiteral("camera0_hot_pixel_mask")) {
            camera0Mask = resolvePath(valueText);
        } else if (key == QStringLiteral("camera_a_hot_pixel_excess") ||
                   key == QStringLiteral("camera0_hot_pixel_excess")) {
            camera0Excess = resolvePath(valueText);
        } else if (key == QStringLiteral("camera_b_hot_pixel_mask") ||
                   key == QStringLiteral("camera1_hot_pixel_mask")) {
            camera1Mask = resolvePath(valueText);
        } else if (key == QStringLiteral("camera_b_hot_pixel_excess") ||
                   key == QStringLiteral("camera1_hot_pixel_excess")) {
            camera1Excess = resolvePath(valueText);
        }
    }

    setRoiCentroidConfig(thresholdAbsolute,
                         sigmaThreshold,
                         minimumIntensity,
                         minimumSignalPixels,
                         noiseTrimFraction);
    configureHotPixelTemplates(camera0Mask,
                               camera0Excess,
                               camera1Mask,
                               camera1Excess,
                               hotTemplateWidth,
                               hotTemplateHeight);
    if (message) {
        *message = QStringLiteral("图像处理配置已加载: %1").arg(path);
    }
    return true;
}

void ImageProcessor::setOpticalParams(double apertureDiameterMm,
                                      double baselineSeparationMm,
                                      double baselineAngleDeg,
                                      double focalLengthCm,
                                      double zenithAngleDeg,
                                      double lambdaNm,
                                      double pixelSizeUm)
{
    m_apertureDiameterMm = std::max(1e-3, apertureDiameterMm);
    m_baselineSeparationMm = std::max(1e-3, baselineSeparationMm);
    baselineAngleDeg = std::isfinite(baselineAngleDeg) ? baselineAngleDeg : 0.0;
    m_baselineAngleDeg = baselineAngleDeg;
    m_focalLengthCm = std::max(1e-3, focalLengthCm);
    m_zenithAngleDeg = std::clamp(zenithAngleDeg, 0.0, 80.0);
    m_wavelengthNm = std::max(1e-6, lambdaNm);
    m_pixelSizeUm = std::max(1e-6, pixelSizeUm);
    QMetaObject::invokeMethod(m_worker,
                              "setOpticalParams",
                              Qt::QueuedConnection,
                              Q_ARG(double, apertureDiameterMm),
                              Q_ARG(double, baselineSeparationMm),
                              Q_ARG(double, baselineAngleDeg),
                              Q_ARG(double, focalLengthCm),
                              Q_ARG(double, zenithAngleDeg),
                              Q_ARG(double, lambdaNm),
                              Q_ARG(double, pixelSizeUm));
}

void ImageProcessor::setCurrentRoi(int cameraIndex, const RoiRect& roi)
{
    if (cameraIndex < 0 || cameraIndex >= 2) {
        return;
    }

    m_currentRoi[cameraIndex] = roi;
    QMetaObject::invokeMethod(m_worker,
                              "setCurrentRoi",
                              Qt::QueuedConnection,
                              Q_ARG(int, cameraIndex),
                              Q_ARG(RoiRect, roi));
}

void ImageProcessor::setPairRois(const RoiRect rois[2])
{
    if (!rois) {
        return;
    }

    m_currentRoi[0] = rois[0];
    m_currentRoi[1] = rois[1];
    QMetaObject::invokeMethod(m_worker,
                              "setPairRois",
                              Qt::QueuedConnection,
                              Q_ARG(RoiRect, rois[0]),
                              Q_ARG(RoiRect, rois[1]));
}

void ImageProcessor::advanceAcquisitionGeneration()
{
    ++(*m_acquisitionGeneration);
    QMetaObject::invokeMethod(m_worker, "advanceAcquisitionGeneration", Qt::QueuedConnection);
}

quint64 ImageProcessor::currentAcquisitionGeneration() const
{
    return m_acquisitionGeneration->load();
}

void ImageProcessor::resetProcessingState()
{
    ++(*m_acquisitionGeneration);
    QMetaObject::invokeMethod(m_worker, "resetRunProcessingState", Qt::QueuedConnection);
}

RoiRect ImageProcessor::getCurrentRoi(int cameraIndex) const
{
    if (cameraIndex < 0 || cameraIndex >= 2) {
        return RoiRect();
    }
    return m_currentRoi[cameraIndex];
}

void ImageProcessor::processFrame(int cameraIndex,
                                  const cv::Mat& frame,
                                  quint64 frameId,
                                  quint64 cameraTimestamp,
                                  quint64 acquisitionGeneration)
{
    if (frame.empty() || cameraIndex < 0 || cameraIndex >= 2) {
        return;
    }

    cv::Mat frameCopy = frame.clone();
    QMetaObject::invokeMethod(m_worker,
                              "processFrame",
                              Qt::QueuedConnection,
                              Q_ARG(int, cameraIndex),
                              Q_ARG(cv::Mat, frameCopy),
                              Q_ARG(quint64, frameId),
                              Q_ARG(quint64, cameraTimestamp),
                              Q_ARG(quint64, acquisitionGeneration));
}
