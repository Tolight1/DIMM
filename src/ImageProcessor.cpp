#include "ImageProcessor.h"

#include "GdimmTau0Estimator.h"

#include "ConfigTextUtils.h"
#include "BackgroundNoiseThresholdEstimator.h"
#include "CentroidLogic.h"
#include "ConnectedDomain.h"
#include "ImageUtils.h"
#include "InitialStarDetectionConfig.h"
#include "RoiComponentSelection.h"

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

RoiRect sanitizeRoiRect(RoiRect roi, int minimumSize)
{
    roi.x = std::max(0, roi.x);
    roi.y = std::max(0, roi.y);
    roi.w = std::max(minimumSize, roi.w);
    roi.h = std::max(minimumSize, roi.h);
    return roi;
}

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

cv::Mat makeBackgroundSubtractedCalculationImage(const cv::Mat& image, double threshold)
{
    if (image.empty() || image.channels() != 1 || !std::isfinite(threshold)) {
        return cv::Mat();
    }

    const cv::Mat intensity = makeCentroidIntensityImage(image);
    if (intensity.empty()) {
        return cv::Mat();
    }

    cv::Mat calculationImage(intensity.size(), CV_16UC1, cv::Scalar(0));
    for (int y = 0; y < intensity.rows; ++y) {
        const double* intensityRow = intensity.ptr<double>(y);
        std::uint16_t* calculationRow = calculationImage.ptr<std::uint16_t>(y);
        for (int x = 0; x < intensity.cols; ++x) {
            const double weight = intensityRow[x] - threshold;
            if (!std::isfinite(weight) || weight <= 0.0) {
                continue;
            }
            calculationRow[x] = static_cast<std::uint16_t>(
                std::clamp(std::lround(weight), 0L, 65535L));
        }
    }
    return calculationImage;
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
    m_centroidMode = std::clamp(mode, 0, 1);
}

void ImageProcessorWorker::setPeakKernelCentroidConfig(int radiusPx,
                                                       double strongHotPixelExcessDn)
{
    QMutexLocker locker(&m_mutex);
    m_peakKernelRadiusPx = std::clamp(radiusPx, 1, 20);
    m_strongHotPixelExcessDn = std::clamp(strongHotPixelExcessDn, 1.0, 4095.0);
}

void ImageProcessorWorker::setBackgroundNoiseThresholdConfig(int clipIterations,
                                                              double clipSigma,
                                                              double thresholdSigmaMultiplier)
{
    QMutexLocker locker(&m_mutex);
    m_backgroundThresholdClipIterations = std::clamp(clipIterations, 0, 20);
    m_backgroundThresholdClipSigma = std::clamp(clipSigma, 0.01, 20.0);
    m_backgroundThresholdSigmaMultiplier = std::clamp(thresholdSigmaMultiplier, 0.0, 20.0);
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
                                            double pixelSizeUm,
                                            double outerScaleM)
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
    m_outerScaleMeters = std::isfinite(outerScaleM) && outerScaleM > 0.0
                               ? outerScaleM
                               : 20.0;
}

void ImageProcessorWorker::setTargetFrameRateHz(double frameRateHz)
{
    QMutexLocker locker(&m_mutex);
    m_targetFrameRateHz = std::clamp(frameRateHz, 1.0, 1000.0);
}

void ImageProcessorWorker::setAtmosphereHistoryWindowFrames(int frames)
{
    QMutexLocker locker(&m_mutex);
    m_atmosphereHistoryWindowFrames = std::clamp(frames, MIN_HISTORY_WINDOW, MAX_HISTORY_WINDOW);
    while (m_differentialHistory.size() > m_atmosphereHistoryWindowFrames) {
        m_differentialHistory.removeFirst();
    }
}

void ImageProcessorWorker::setPsdAnalysisConfig(const CdimPsdAnalysisConfig& config)
{
    QMutexLocker locker(&m_mutex);
    m_psdAnalysisConfig = config;
    m_psdAnalysisConfig.welchSegmentLength =
        std::clamp(m_psdAnalysisConfig.welchSegmentLength, 2, MAX_HISTORY_WINDOW);
    m_psdAnalysisConfig.welchOverlap =
        std::clamp(m_psdAnalysisConfig.welchOverlap, 0.0, 0.95);
    m_psdAnalysisConfig.nfft = std::max(0, m_psdAnalysisConfig.nfft);
    m_psdAnalysisConfig.noiseCandidateStartNyquist =
        std::clamp(m_psdAnalysisConfig.noiseCandidateStartNyquist, 0.0, 1.0);
    m_psdAnalysisConfig.noiseCandidateEndNyquist =
        std::clamp(m_psdAnalysisConfig.noiseCandidateEndNyquist, 0.0, 1.0);
    if (m_psdAnalysisConfig.noiseCandidateEndNyquist
        <= m_psdAnalysisConfig.noiseCandidateStartNyquist) {
        m_psdAnalysisConfig.noiseCandidateStartNyquist = 0.60;
        m_psdAnalysisConfig.noiseCandidateEndNyquist = 0.90;
    }
    m_psdAnalysisConfig.minimumNoiseBandBins =
        std::clamp(m_psdAnalysisConfig.minimumNoiseBandBins, 4, MAX_HISTORY_WINDOW);
    m_psdAnalysisConfig.minimumNoiseBandNyquistWidth =
        std::clamp(m_psdAnalysisConfig.minimumNoiseBandNyquistWidth, 0.0, 1.0);
    m_psdAnalysisConfig.fitNoiseDominanceKappa =
        std::max(0.01, m_psdAnalysisConfig.fitNoiseDominanceKappa);
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
    Q_UNUSED(sampleIntervalMs);
    m_autoExposureMetricsEnabled = enabled;
    m_autoExposureHardSaturationDn = std::clamp(hardSaturationDn, 0.0, 4095.0);
    m_autoExposureSpotConfig.hardSaturationDn = m_autoExposureHardSaturationDn;
    m_autoExposureSpotConfig.supportRadiusPx = std::clamp(peakSupportRadiusPx, 1, 8);
    m_autoExposureSpotConfig.supportFraction = std::clamp(peakSupportFraction, 0.05, 1.0);
    m_autoExposureSpotConfig.minSupportPixelCount = std::max(1, minPeakSupportPixelCount);
    m_autoExposureSpotConfig.minNeighborPeakRatio = std::clamp(minNeighborPeakRatio, 0.0, 1.0);
    m_autoExposureSpotConfig.maxCandidateCount = std::max(1, maxPeakCandidateCount);
    m_autoExposureSpotConfig.supportedPeakPercentile =
        std::clamp(supportedPeakPercentile, 0.0, 1.0);
    m_autoExposureSpotConfig.saturatedPixelCount = std::max(1, saturatedPixelCount);
}

void ImageProcessorWorker::setCurrentRoi(int cameraIndex, const RoiRect& roi)
{
    if (cameraIndex < 0 || cameraIndex >= 2) {
        return;
    }

    QMutexLocker locker(&m_mutex);
    RoiRect sanitized = sanitizeRoiRect(roi, MIN_ROI_SIZE);
    m_currentRoi[cameraIndex] = sanitized;
    clearLastValidGlobalCentroid(cameraIndex);
    resetPairingState();
}

void ImageProcessorWorker::setPairRois(RoiRect roi0, RoiRect roi1)
{
    RoiRect sanitized[2] = {sanitizeRoiRect(roi0, MIN_ROI_SIZE),
                            sanitizeRoiRect(roi1, MIN_ROI_SIZE)};

    QMutexLocker locker(&m_mutex);
    m_currentRoi[0] = sanitized[0];
    m_currentRoi[1] = sanitized[1];
    clearAllLastValidGlobalCentroids();
    resetRoiProcessingHistory();
}

void ImageProcessorWorker::setPairRoisPreservingAtmosphereWindow(RoiRect roi0, RoiRect roi1)
{
    const RoiRect sanitized[2] = {sanitizeRoiRect(roi0, MIN_ROI_SIZE),
                                  sanitizeRoiRect(roi1, MIN_ROI_SIZE)};

    QMutexLocker locker(&m_mutex);
    m_currentRoi[0] = sanitized[0];
    m_currentRoi[1] = sanitized[1];
    clearAllLastValidGlobalCentroids();
    resetPairingState();
}

void ImageProcessorWorker::discardActiveAtmosphereWindow()
{
    QMutexLocker locker(&m_mutex);
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
    clearAllLastValidGlobalCentroids();
    resetRoiProcessingHistory();
}

void ImageProcessorWorker::resetAcquisitionStatistics()
{
    QMutexLocker locker(&m_mutex);
    m_lastPairedSerial = 0;
    m_droppedUnpairedSamples = 0;
}

void ImageProcessorWorker::recordFrameMetadata(int cameraIndex,
                                               quint64 frameId,
                                               quint64 cameraTimestamp)
{
    if (cameraIndex < 0 || cameraIndex >= 2 || (frameId == 0 && cameraTimestamp == 0)) {
        return;
    }

    QMutexLocker locker(&m_mutex);
    if (frameId > 0) {
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

    return centroidMode == 0
               ? backgroundThresholdKernelCentroid(cameraIndex, roi, processed)
               : backgroundSubtractedFullRoiCentroid(cameraIndex, roi, processed);
}

CentroidResult ImageProcessorWorker::backgroundThresholdKernelCentroid(int cameraIndex,
                                                                        const RoiRect& roi,
                                                                        const cv::Mat& image)
{
    CentroidResult result;
    if (image.empty() || image.channels() != 1 || cameraIndex < 0 || cameraIndex >= 2) {
        return result;
    }

    int clipIterations = 3;
    double clipSigma = 3.0;
    double thresholdSigmaMultiplier = 1.0;
    int peakKernelRadiusPx = 3;
    double strongHotPixelExcessDn = 100.0;
    int centroidMinimumSignalPixels = 3;
    RoiComponentSelection::PreviousGlobalCentroid previous;
    {
        QMutexLocker locker(&m_mutex);
        centroidMinimumSignalPixels = m_centroidMinimumSignalPixels;
        clipIterations = m_backgroundThresholdClipIterations;
        clipSigma = m_backgroundThresholdClipSigma;
        thresholdSigmaMultiplier = m_backgroundThresholdSigmaMultiplier;
        peakKernelRadiusPx = m_peakKernelRadiusPx;
        strongHotPixelExcessDn = m_strongHotPixelExcessDn;
        previous.valid = m_hasLastValidGlobalCentroid[cameraIndex];
        previous.x = m_lastValidGlobalCentroid[cameraIndex].x();
        previous.y = m_lastValidGlobalCentroid[cameraIndex].y();
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

    const BackgroundNoiseThresholdEstimator::Estimate estimate =
        BackgroundNoiseThresholdEstimator::estimate(
            intensity.ptr<double>(0),
            intensity.total(),
            BackgroundNoiseThresholdEstimator::Config{
                clipIterations,
                clipSigma,
                thresholdSigmaMultiplier});
    if (!estimate.valid) {
        return result;
    }
    result.background = estimate.background;
    result.noiseSigma = estimate.noiseSigma;
    result.threshold = estimate.threshold;
    emit roiBackgroundThresholdReady(cameraIndex,
                                     result.background,
                                     result.noiseSigma,
                                     result.threshold);

    cv::Mat foregroundMask(intensity.size(), CV_8UC1, cv::Scalar(0));
    for (int y = 0; y < intensity.rows; ++y) {
        const double* row = intensity.ptr<double>(y);
        unsigned char* maskRow = foregroundMask.ptr<unsigned char>(y);
        for (int x = 0; x < intensity.cols; ++x) {
            const double value = row[x];
            if (std::isfinite(value) && value > result.threshold) {
                maskRow[x] = 255;
            }
        }
    }

    cv::Mat labels;
    cv::Mat stats;
    cv::Mat centroids;
    const int componentCount =
        cv::connectedComponentsWithStats(foregroundMask,
                                         labels,
                                         stats,
                                         centroids,
                                         ConnectedDomain::sanitizeConnectivity(
                                             currentInitialStarDetectionConfig().connectivity),
                                         CV_32S);
    if (componentCount <= 1) {
        return result;
    }

    std::vector<RoiComponentSelection::ComponentCandidate> candidates;
    candidates.reserve(static_cast<std::size_t>(std::max(0, componentCount - 1)));
    for (int label = 1; label < componentCount; ++label) {
        RoiComponentSelection::ComponentCandidate candidate;
        candidate.label = label;
        candidate.area = stats.at<int>(label, cv::CC_STAT_AREA);
        candidate.localX = centroids.at<double>(label, 0);
        candidate.localY = centroids.at<double>(label, 1);
        candidates.push_back(candidate);
    }

    const RoiComponentSelection::SelectionResult selected =
        RoiComponentSelection::selectTargetComponent(candidates,
                                                     std::max(ConnectedDomain::kMinimumComponentArea,
                                                              centroidMinimumSignalPixels),
                                                     roi.x,
                                                     roi.y,
                                                     previous);
    if (!selected.selected) {
        return result;
    }

    const int centerX = std::clamp(static_cast<int>(std::lround(selected.localX)),
                                   0,
                                   intensity.cols - 1);
    const int centerY = std::clamp(static_cast<int>(std::lround(selected.localY)),
                                   0,
                                   intensity.rows - 1);

    const HotPixelRoiCache cache = hotPixelCacheSnapshot(cameraIndex, roi);
    const bool cacheSizeMatches =
        cache.valid &&
        cache.mask.size() >= intensity.cols * intensity.rows &&
        cache.excess.size() >= intensity.cols * intensity.rows;

    CentroidLogic::PeakKernelConfig kernelConfig;
    kernelConfig.radiusPx = peakKernelRadiusPx;
    kernelConfig.strongHotPixelExcessDn = strongHotPixelExcessDn;

    const double* pixels = intensity.ptr<double>(0);
    const unsigned char* mask = cacheSizeMatches ? cache.mask.constData() : nullptr;
    const std::uint16_t* excess =
        cacheSizeMatches ? reinterpret_cast<const std::uint16_t*>(cache.excess.constData()) : nullptr;
    const CentroidLogic::PeakKernelResult kernel =
        CentroidLogic::computePeakKernelCentroid(pixels,
                                                 intensity.cols,
                                                 intensity.rows,
                                                 centerX,
                                                 centerY,
                                                 mask,
                                                 excess,
                                                 kernelConfig);
    if (!kernel.valid) {
        return result;
    }

    result.valid = true;
    result.x = kernel.x;
    result.y = kernel.y;
    result.peakValue = kernel.peakValue;
    result.totalFlux = kernel.totalFlux;
    result.signalPixelCount = static_cast<quint64>(std::max(0, kernel.usedPixelCount));
    return result;
}

CentroidResult ImageProcessorWorker::backgroundSubtractedFullRoiCentroid(int cameraIndex,
                                                                          const RoiRect& roi,
                                                                          const cv::Mat& image)
{
    CentroidResult result;
    if (image.empty() || image.channels() != 1 || cameraIndex < 0 || cameraIndex >= 2) {
        return result;
    }

    int clipIterations = 3;
    double clipSigma = 3.0;
    double thresholdSigmaMultiplier = 1.0;
    int minimumSignalPixels = 3;
    {
        QMutexLocker locker(&m_mutex);
        clipIterations = m_backgroundThresholdClipIterations;
        clipSigma = m_backgroundThresholdClipSigma;
        thresholdSigmaMultiplier = m_backgroundThresholdSigmaMultiplier;
        minimumSignalPixels = m_centroidMinimumSignalPixels;
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

    const BackgroundNoiseThresholdEstimator::Estimate estimate =
        BackgroundNoiseThresholdEstimator::estimate(
            intensity.ptr<double>(0),
            intensity.total(),
            BackgroundNoiseThresholdEstimator::Config{
                clipIterations,
                clipSigma,
                thresholdSigmaMultiplier});
    if (!estimate.valid) {
        return result;
    }
    result.background = estimate.background;
    result.noiseSigma = estimate.noiseSigma;
    result.threshold = estimate.threshold;
    emit roiBackgroundThresholdReady(cameraIndex,
                                     result.background,
                                     result.noiseSigma,
                                     result.threshold);

    double weightedX = 0.0;
    double weightedY = 0.0;
    double totalWeight = 0.0;
    quint64 signalPixelCount = 0;
    double peakValue = 0.0;
    cv::Mat calculationImage(intensity.size(), CV_16UC1, cv::Scalar(0));
    for (int y = 0; y < intensity.rows; ++y) {
        const double* row = intensity.ptr<double>(y);
        std::uint16_t* calculationRow = calculationImage.ptr<std::uint16_t>(y);
        for (int x = 0; x < intensity.cols; ++x) {
            const double weight = row[x] - result.threshold;
            if (!std::isfinite(weight) || weight <= 0.0) {
                continue;
            }
            calculationRow[x] = static_cast<std::uint16_t>(
                std::clamp(std::lround(weight), 0L, 65535L));
            totalWeight += weight;
            weightedX += static_cast<double>(x) * weight;
            weightedY += static_cast<double>(y) * weight;
            peakValue = std::max(peakValue, row[x]);
            ++signalPixelCount;
        }
    }
    if (signalPixelCount < static_cast<quint64>(minimumSignalPixels) || totalWeight <= 0.0) {
        return result;
    }

    result.valid = true;
    result.x = weightedX / totalWeight;
    result.y = weightedY / totalWeight;
    result.peakValue = peakValue;
    result.totalFlux = totalWeight;
    result.signalPixelCount = signalPixelCount;
    result.calculationImage = calculationImage;
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

    int peakKernelRadiusPx = 3;
    double strongHotPixelExcessDn = 100.0;
    AutoExposureSpotConfig spotConfig;
    {
        QMutexLocker locker(&m_mutex);
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
    double baselineAngleDeg = 0.0;
    double f = 0.0;
    double zenithAngleDeg = 0.0;
    double lambda = 0.0;
    double pixelSize = 0.0;
    double outerScaleMeters = 0.0;
    double configuredFrameRateHz = 0.0;
    CdimPsdAnalysisConfig psdConfig;
    {
        QMutexLocker locker(&m_mutex);
        apertureDiameter = m_apertureDiameter;
        baselineSeparation = m_baselineSeparation;
        baselineAngleDeg = m_baselineAngleDeg;
        f = m_f;
        zenithAngleDeg = m_zenithAngleDeg;
        lambda = m_lambda;
        pixelSize = m_pixelSize;
        outerScaleMeters = m_outerScaleMeters;
        configuredFrameRateHz = m_targetFrameRateHz;
        psdConfig = m_psdAnalysisConfig;
    }

    params.psdAnalysis.enabled = psdConfig.enabled;
    params.psdAnalysis.psdMode = psdConfig.psdMode;
    params.psdAnalysis.noiseDetectionMode = psdConfig.noiseDetectionMode;

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

    params.longitudinalMeasuredVariancePx2 = varLongitudinalPx;
    params.transverseMeasuredVariancePx2 = varTransversePx;

    CdimPsdInput psdInput;
    psdInput.configuredFrameRateHz = configuredFrameRateHz;
    psdInput.longitudinal.reserve(samples.size());
    psdInput.transverse.reserve(samples.size());
    psdInput.cameraTimestamps.reserve(samples.size());
    psdInput.frameIds.reserve(samples.size());
    for (const DifferentialSample& sample : samples) {
        psdInput.longitudinal.append(sample.longitudinal);
        psdInput.transverse.append(sample.transverse);
        psdInput.cameraTimestamps.append(sample.cameraTimestamp1 > 0
                                             ? sample.cameraTimestamp1
                                             : sample.cameraTimestamp2);
        psdInput.frameIds.append(sample.frameId1 > 0 ? sample.frameId1 : sample.frameId2);
    }
    // PSD is a post-processing step over the differential sequence produced
    // by either image centroid algorithm. Its enabled flag is
    // deliberately read only from the independent PSD configuration.
    params.psdAnalysis = CdimPsdAnalysis::analyze(psdInput, psdConfig);
    params.longitudinalNoiseVariancePx2 =
        params.psdAnalysis.longitudinal.noiseVariancePx2;
    params.transverseNoiseVariancePx2 =
        params.psdAnalysis.transverse.noiseVariancePx2;
    params.longitudinalAtmosphericVariancePx2 =
        params.psdAnalysis.longitudinal.atmosphericVariancePx2;
    params.transverseAtmosphericVariancePx2 =
        params.psdAnalysis.transverse.atmosphericVariancePx2;

    const bool usePsdCorrection = params.psdAnalysis.valid;
    params.varianceNoiseCorrectionValid = usePsdCorrection;
    params.varianceNoiseCorrectionApplied = usePsdCorrection;
    params.longitudinalVariancePx2 = usePsdCorrection
                                        ? params.longitudinalAtmosphericVariancePx2
                                        : varLongitudinalPx;
    params.transverseVariancePx2 = usePsdCorrection
                                       ? params.transverseAtmosphericVariancePx2
                                       : varTransversePx;

    const double pixelScaleRad = pixelSize / f;
    const double sigmaLongitudinal2 =
        params.longitudinalVariancePx2 * pixelScaleRad * pixelScaleRad;
    const double sigmaTransverse2 =
        params.transverseVariancePx2 * pixelScaleRad * pixelScaleRad;
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
        const QList<DifferentialSample> tau0Samples = tau0WindowSamples(samples);
        const double baselineAngleRad = baselineAngleDeg * kPi / 180.0;
        const double baselineCos = std::cos(baselineAngleRad);
        const double baselineSin = std::sin(baselineAngleRad);
        const auto makeAperture = [&](bool cameraOne) {
            GdimmTau0::TimedCentroids aperture;
            aperture.hasWhiteNoiseEstimate = false;
            aperture.xWhiteNoiseVarianceRad2 = 0.0;
            aperture.yWhiteNoiseVarianceRad2 = 0.0;

            bool useHardwareTimestamps = !tau0Samples.isEmpty();
            quint64 previousTimestamp = 0;
            for (const DifferentialSample& sample : tau0Samples) {
                const quint64 timestamp = cameraOne
                                              ? sample.cameraTimestamp1
                                              : sample.cameraTimestamp2;
                if (timestamp == 0 ||
                    (previousTimestamp > 0 && timestamp <= previousTimestamp)) {
                    useHardwareTimestamps = false;
                    break;
                }
                previousTimestamp = timestamp;
            }

            const quint64 firstHardwareTimestamp =
                useHardwareTimestamps
                    ? (cameraOne ? tau0Samples.first().cameraTimestamp1
                                 : tau0Samples.first().cameraTimestamp2)
                    : 0;
            const qint64 firstHostTimestamp =
                tau0Samples.isEmpty() ? 0 : tau0Samples.first().timestampMs;
            aperture.timestampsSeconds.reserve(static_cast<std::size_t>(tau0Samples.size()));
            aperture.x.reserve(static_cast<std::size_t>(tau0Samples.size()));
            aperture.y.reserve(static_cast<std::size_t>(tau0Samples.size()));
            for (const DifferentialSample& sample : tau0Samples) {
                const double timestampSeconds = useHardwareTimestamps
                                                    ? static_cast<double>(
                                                          (cameraOne
                                                               ? sample.cameraTimestamp1
                                                               : sample.cameraTimestamp2) -
                                                          firstHardwareTimestamp) *
                                                          MARS_GIGE_TIMESTAMP_TICK_US * 1e-6
                                                    : static_cast<double>(
                                                          sample.timestampMs - firstHostTimestamp) *
                                                          1e-3;
                const double centroidX = cameraOne ? sample.centroid1X : sample.centroid2X;
                const double centroidY = cameraOne ? sample.centroid1Y : sample.centroid2Y;
                aperture.timestampsSeconds.push_back(timestampSeconds);
                aperture.x.push_back(
                    (centroidX * baselineCos + centroidY * baselineSin) * pixelScaleRad);
                aperture.y.push_back(
                    (-centroidX * baselineSin + centroidY * baselineCos) * pixelScaleRad);
            }
            return aperture;
        };

        const GdimmTau0::EstimateResult tau0Estimate = GdimmTau0::estimate(
            {makeAperture(true), makeAperture(false)},
            r0Zenith,
            apertureDiameter,
            outerScaleMeters,
            TAU0_MAX_LAG_MS / 1000.0,
            TAU0_MIN_SAMPLES);

        params.tau0 = tau0Estimate.underResolved
                          ? tau0Estimate.tau0UpperBoundMs
                          : tau0Estimate.tau0Ms;
        params.tau0Valid = tau0Estimate.valid;
        params.tau0UnderResolved = tau0Estimate.underResolved;
        params.tau0ResolutionMs = tau0Estimate.underResolved
                                      ? tau0Estimate.tau0UpperBoundMs
                                      : tau0Estimate.sampleIntervalMs;
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

int ImageProcessorWorker::historyWindowSize() const
{
    QMutexLocker locker(&m_mutex);
    return m_atmosphereHistoryWindowFrames;
}

int ImageProcessorWorker::minimumAtmosphereSamples() const
{
    // Start publishing once there are enough paired samples for a stable
    // estimate. The configured history window remains the rolling window for
    // full-quality results; it must not also delay the first live result.
    return std::min(historyWindowSize(), MIN_HISTORY_WINDOW);
}

int ImageProcessorWorker::pendingCentroidQueueLimit() const
{
    QMutexLocker locker(&m_mutex);
    return std::clamp(static_cast<int>(std::lround(m_targetFrameRateHz)),
                      MIN_HISTORY_WINDOW,
                      MAX_PENDING_PAIR_QUEUE);
}

void ImageProcessorWorker::clearLastValidGlobalCentroid(int cameraIndex)
{
    if (cameraIndex < 0 || cameraIndex >= 2) {
        return;
    }
    m_lastValidGlobalCentroid[cameraIndex] = QPointF();
    m_hasLastValidGlobalCentroid[cameraIndex] = false;
}

void ImageProcessorWorker::clearAllLastValidGlobalCentroids()
{
    clearLastValidGlobalCentroid(0);
    clearLastValidGlobalCentroid(1);
}

void ImageProcessorWorker::resetRoiProcessingHistory()
{
    m_lastAtmospherePublishMs = 0;
    m_lastRoiImagePublishMs[0] = 0;
    m_lastRoiImagePublishMs[1] = 0;
    m_differentialHistory.clear();
    resetPairingState();
}

void ImageProcessorWorker::resetPairingState()
{
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

void ImageProcessorWorker::submitCentroidSample(int cameraIndex,
                                                const CentroidResult& centroid,
                                                quint64 frameId,
                                                quint64 cameraTimestamp,
                                                qint64 timestampMs,
                                                const cv::Mat& roiImage)
{
    if (cameraIndex < 0 || cameraIndex >= 2 || !centroid.valid) {
        return;
    }

    {
        QMutexLocker locker(&m_mutex);
        m_lastValidGlobalCentroid[cameraIndex] = QPointF(centroid.x, centroid.y);
        m_hasLastValidGlobalCentroid[cameraIndex] = true;
    }
    emit centroidReady(cameraIndex,
                       centroid.x,
                       centroid.y,
                       centroid.peakValue,
                       centroid.totalFlux,
                       centroid.background,
                       centroid.noiseSigma,
                       centroid.threshold,
                       centroid.signalPixelCount);

    PendingCentroidSample pending;
    pending.centroid = centroid;
    pending.frameId = frameId;
    pending.cameraTimestamp = cameraTimestamp;
    pending.timestampMs = timestampMs;
    m_pendingCentroids[cameraIndex].append(pending);
    while (m_pendingCentroids[cameraIndex].size() > pendingCentroidQueueLimit()) {
        m_pendingCentroids[cameraIndex].removeFirst();
        ++m_droppedUnpairedSamples;
    }

    if (!appendDifferentialSample()) {
        return;
    }

    emit differentialSampleReady(m_lastPairedSerial, m_droppedUnpairedSamples);
    if (m_differentialHistory.size() < minimumAtmosphereSamples() ||
        m_differentialHistory.size() < historyWindowSize()) {
        emitRoiImageIfDue(cameraIndex, roiImage, timestampMs);
        return;
    }
    if (m_lastAtmospherePublishMs > 0 &&
        (timestampMs - m_lastAtmospherePublishMs) < ATMOSPHERE_PUBLISH_INTERVAL_MS) {
        emitRoiImageIfDue(cameraIndex, roiImage, timestampMs);
        return;
    }

    const AtmosphericParams params = calculateAtmosphere(m_differentialHistory);
    emit psdAnalysisReady(params.psdAnalysis);
    if (params.r0 > 0.0) {
        m_lastAtmospherePublishMs = timestampMs;
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
                             params.r0TransverseCm);
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

    recordFrameMetadata(cameraIndex, frameId, cameraTimestamp);

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
    AutoExposureSpotConfig autoExposureSpotConfig;
    double autoExposureThresholdNative = 16.0;
    {
        QMutexLocker locker(&m_mutex);
        if (cameraIndex >= 0 && cameraIndex < 2 && m_autoExposureMetricsEnabled) {
            shouldEmitAutoExposureSample = true;
            autoExposureSpotConfig = m_autoExposureSpotConfig;
            autoExposureThresholdNative =
                m_roiThresholdAbsolute >= 0.0
                    ? m_roiThresholdAbsolute
                    : static_cast<double>(m_centroidMinimumIntensity);
        }
    }

    const cv::Mat correctedRoiImage = applyHotPixelCorrection(cameraIndex, roi, roiImage);
    // Keep the two image centroid algorithms as the only centroid stage;
    // PSD is applied later to the paired differential samples.
    CentroidResult centroid = calculateCentroid(cameraIndex, roi, correctedRoiImage);
    if (centroid.valid && !(centroid.noiseSigma > 0.0)) {
        cv::Mat intensity = makeCentroidIntensityImage(correctedRoiImage);
        if (!intensity.empty() && !intensity.isContinuous()) {
            intensity = intensity.clone();
        }
        const BackgroundNoiseThresholdEstimator::Estimate estimate =
            BackgroundNoiseThresholdEstimator::estimate(
                intensity.ptr<double>(0),
                static_cast<std::size_t>(intensity.total()),
                {3, 3.0, 1.0});
        if (estimate.valid) {
            centroid.background = estimate.background;
            centroid.noiseSigma = estimate.noiseSigma;
            centroid.threshold = std::max(centroid.threshold, estimate.threshold);
        }
    }

    // Mode 0 intentionally keeps its legacy peak-kernel output unchanged.
    // It has no centroid-stage calculation image, so provide autofocus with
    // the equivalent background-subtracted image without changing the normal
    // calculationImageReady stream.
    cv::Mat autofocusCalculationImage = centroid.calculationImage;
    if (autofocusCalculationImage.empty() && centroid.valid) {
        autofocusCalculationImage =
            makeBackgroundSubtractedCalculationImage(correctedRoiImage, centroid.threshold);
    }

    // Preserve the existing calculation-image preview behavior for modes that
    // do not produce a dedicated threshold-subtracted image.
    if (centroid.valid && centroid.calculationImage.empty()) {
        centroid.calculationImage = correctedRoiImage;
    }
    if (!centroid.calculationImage.empty()) {
        emit calculationImageReady(cameraIndex, frameId, centroid.calculationImage);
    }

    // This is an observational side channel for autofocus. It uses the same
    // calculation image produced by the centroid stage.
    emit autoFocusRoiMeasurementReady(cameraIndex,
                                      frameId,
                                      autofocusCalculationImage,
                                      centroid.valid,
                                      centroid.x,
                                      centroid.y);
    const bool autoExposureMeasurementUsable =
        centroid.valid && centroid.signalPixelCount <= kMaxMeasurementSignalPixels;
    if (shouldEmitAutoExposureSample) {
        cv::Mat autoExposureIntensity = makeCentroidIntensityImage(correctedRoiImage);
        if (!autoExposureIntensity.empty() && !autoExposureIntensity.isContinuous()) {
            autoExposureIntensity = autoExposureIntensity.clone();
        }

        const double autoExposureDnScale =
            correctedRoiImage.depth() == CV_8U ? 4095.0 / 255.0 : 1.0;
        if (centroid.threshold > 0.0) {
            autoExposureThresholdNative = centroid.threshold;
        }
        autoExposureSpotConfig.thresholdDn = autoExposureThresholdNative;
        autoExposureSpotConfig.hardSaturationDn =
            autoExposureSpotConfig.hardSaturationDn / autoExposureDnScale;

        AutoExposureSpotResult spot;
        if (!autoExposureIntensity.empty() && autoExposureIntensity.isContinuous()) {
            spot = analyzeAutoExposureSpot(autoExposureIntensity.ptr<double>(0),
                                           autoExposureIntensity.cols,
                                           autoExposureIntensity.rows,
                                           autoExposureSpotConfig);
        }

        const bool hasValidSpot =
            spot.quality == AutoExposurePeakQuality::ValidSpotPeak ||
            spot.quality == AutoExposurePeakQuality::SpotSaturated;
        const double aePeakValue = hasValidSpot ? spot.peakDn : 0.0;
        const double aeSupportedPeakValue = hasValidSpot ? spot.supportedPeakDn : 0.0;
        emit autoExposureSampleReady(cameraIndex,
                                     aePeakValue * autoExposureDnScale,
                                     centroid.background * autoExposureDnScale,
                                     centroid.noiseSigma * autoExposureDnScale,
                                     centroid.threshold * autoExposureDnScale,
                                     centroid.signalPixelCount,
                                     static_cast<quint64>(std::max(0, spot.spotSaturatedPixelCount)),
                                     static_cast<int>(spot.quality),
                                     aeSupportedPeakValue * autoExposureDnScale,
                                     static_cast<quint64>(std::max(0, spot.supportPixelCount)),
                                     spot.rejectedPeakDn * autoExposureDnScale,
                                     spot.rejectedCandidateCount,
                                     spot.spotHardSaturated,
                                     centroid.valid,
                                     autoExposureMeasurementUsable,
                                     spot.decisionSample,
                                     frameId,
                                     nowMs);
    }
    if (centroid.valid) {
        CentroidResult absoluteCentroid = centroid;
        absoluteCentroid.x += roi.x;
        absoluteCentroid.y += roi.y;
        submitCentroidSample(cameraIndex,
                             absoluteCentroid,
                             frameId,
                             cameraTimestamp,
                             nowMs,
                             roiImage);
    }

    emitRoiImageIfDue(cameraIndex, roiImage, nowMs);
    finishProcessing(centroid.valid);
}

ImageProcessor::ImageProcessor(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<AtmosphericParams>("AtmosphericParams");
    qRegisterMetaType<CdimPsdAnalysisConfig>("CdimPsdAnalysisConfig");
    qRegisterMetaType<CdimPsdAnalysisResult>("CdimPsdAnalysisResult");
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
    connect(m_worker,
            &ImageProcessorWorker::calculationImageReady,
            this,
            &ImageProcessor::calculationImageReady);
    connect(m_worker,
            &ImageProcessorWorker::autoFocusRoiMeasurementReady,
            this,
            &ImageProcessor::autoFocusRoiMeasurementReady);
    connect(m_worker, &ImageProcessorWorker::atmosphereReady, this, &ImageProcessor::atmosphereReady);
    connect(m_worker, &ImageProcessorWorker::psdAnalysisReady, this, &ImageProcessor::psdAnalysisReady);
    connect(m_worker,
            &ImageProcessorWorker::frameProcessed,
            this,
            [this](int cameraIndex, bool centroidValid, double elapsedMs) {
                Q_UNUSED(cameraIndex);
                emit frameProcessed(cameraIndex, centroidValid, elapsedMs);
            });
    connect(m_worker, &ImageProcessorWorker::syncSampleReady, this, &ImageProcessor::syncSampleReady);
    connect(m_worker,
            &ImageProcessorWorker::unpairedSampleDropped,
            this,
            &ImageProcessor::unpairedSampleDropped);
    connect(m_worker,
            &ImageProcessorWorker::autoExposureSampleReady,
            this,
            &ImageProcessor::autoExposureSampleReady);
    connect(m_worker,
            &ImageProcessorWorker::roiBackgroundThresholdReady,
            this,
            &ImageProcessor::roiBackgroundThresholdReady);
    connect(m_worker,
            &ImageProcessorWorker::acquisitionStopRequested,
            this,
            &ImageProcessor::acquisitionStopRequested);

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
    m_centroidMode = std::clamp(mode, 0, 1);
    QMetaObject::invokeMethod(m_worker,
                              "setCentroidMode",
                              Qt::QueuedConnection,
                              Q_ARG(int, m_centroidMode));
}

void ImageProcessor::setPeakKernelCentroidConfig(int radiusPx,
                                                 double strongHotPixelExcessDn)
{
    m_peakKernelRadiusPx = std::clamp(radiusPx, 1, 20);
    m_strongHotPixelExcessDn = std::clamp(strongHotPixelExcessDn, 1.0, 4095.0);
    QMetaObject::invokeMethod(m_worker,
                              "setPeakKernelCentroidConfig",
                              Qt::QueuedConnection,
                              Q_ARG(int, m_peakKernelRadiusPx),
                              Q_ARG(double, m_strongHotPixelExcessDn));
}

void ImageProcessor::setBackgroundNoiseThresholdConfig(int clipIterations,
                                                        double clipSigma,
                                                        double thresholdSigmaMultiplier)
{
    m_backgroundThresholdClipIterations = std::clamp(clipIterations, 0, 20);
    m_backgroundThresholdClipSigma = std::clamp(clipSigma, 0.01, 20.0);
    m_backgroundThresholdSigmaMultiplier = std::clamp(thresholdSigmaMultiplier, 0.0, 20.0);
    QMetaObject::invokeMethod(m_worker,
                              "setBackgroundNoiseThresholdConfig",
                              Qt::QueuedConnection,
                              Q_ARG(int, m_backgroundThresholdClipIterations),
                              Q_ARG(double, m_backgroundThresholdClipSigma),
                              Q_ARG(double, m_backgroundThresholdSigmaMultiplier));
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

void ImageProcessor::setAtmosphereHistoryWindowFrames(int frames)
{
    m_atmosphereHistoryWindowFrames = std::clamp(frames, 50, 60000);
    QMetaObject::invokeMethod(m_worker,
                              "setAtmosphereHistoryWindowFrames",
                              Qt::QueuedConnection,
                              Q_ARG(int, m_atmosphereHistoryWindowFrames));
}

void ImageProcessor::setPsdAnalysisConfig(const CdimPsdAnalysisConfig& config)
{
    m_psdAnalysisConfig = config;
    QMetaObject::invokeMethod(m_worker,
                              "setPsdAnalysisConfig",
                              Qt::QueuedConnection,
                              Q_ARG(CdimPsdAnalysisConfig, m_psdAnalysisConfig));
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
                                      double pixelSizeUm,
                                      double outerScaleM)
{
    m_apertureDiameterMm = std::max(1e-3, apertureDiameterMm);
    m_baselineSeparationMm = std::max(1e-3, baselineSeparationMm);
    baselineAngleDeg = std::isfinite(baselineAngleDeg) ? baselineAngleDeg : 0.0;
    m_baselineAngleDeg = baselineAngleDeg;
    m_focalLengthCm = std::max(1e-3, focalLengthCm);
    m_zenithAngleDeg = std::clamp(zenithAngleDeg, 0.0, 80.0);
    m_wavelengthNm = std::max(1e-6, lambdaNm);
    m_pixelSizeUm = std::max(1e-6, pixelSizeUm);
    m_outerScaleMeters = std::isfinite(outerScaleM) && outerScaleM > 0.0
                              ? outerScaleM
                              : 20.0;
    QMetaObject::invokeMethod(m_worker,
                              "setOpticalParams",
                              Qt::QueuedConnection,
                              Q_ARG(double, apertureDiameterMm),
                              Q_ARG(double, baselineSeparationMm),
                              Q_ARG(double, baselineAngleDeg),
                              Q_ARG(double, focalLengthCm),
                              Q_ARG(double, zenithAngleDeg),
                              Q_ARG(double, lambdaNm),
                              Q_ARG(double, pixelSizeUm),
                              Q_ARG(double, m_outerScaleMeters));
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

void ImageProcessor::setPairRoisPreservingAtmosphereWindow(const RoiRect rois[2])
{
    if (!rois) {
        return;
    }

    m_currentRoi[0] = rois[0];
    m_currentRoi[1] = rois[1];
    QMetaObject::invokeMethod(m_worker,
                              "setPairRoisPreservingAtmosphereWindow",
                              Qt::QueuedConnection,
                              Q_ARG(RoiRect, rois[0]),
                              Q_ARG(RoiRect, rois[1]));
}

void ImageProcessor::discardActiveAtmosphereWindow()
{
    QMetaObject::invokeMethod(m_worker,
                              "discardActiveAtmosphereWindow",
                              Qt::QueuedConnection);
}

void ImageProcessor::advanceAcquisitionGeneration()
{
    ++(*m_acquisitionGeneration);
    QMetaObject::invokeMethod(m_worker,
                              "advanceAcquisitionGeneration",
                              Qt::QueuedConnection);
}

void ImageProcessor::resetAcquisitionStatistics()
{
    QMetaObject::invokeMethod(m_worker,
                              "resetAcquisitionStatistics",
                              Qt::QueuedConnection);
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
    if (frameCopy.empty()) {
        return;
    }

    QMetaObject::invokeMethod(m_worker,
                              "processFrame",
                              Qt::QueuedConnection,
                              Q_ARG(int, cameraIndex),
                              Q_ARG(cv::Mat, frameCopy),
                              Q_ARG(quint64, frameId),
                              Q_ARG(quint64, cameraTimestamp),
                              Q_ARG(quint64, acquisitionGeneration));
}
