#pragma once

#include <QObject>
#include <QList>
#include <QMutex>
#include <QPointF>
#include <QString>
#include <QThread>
#include <QVector>

#include <atomic>
#include <memory>

#include <opencv2/opencv.hpp>

#include "AutoExposureLogic.h"
#include "CdimPsdAnalysis.h"

struct RoiRect {
    int x = 0;
    int y = 0;
    int w = 64;
    int h = 64;
};

struct CentroidResult {
    double x = 0.0;
    double y = 0.0;
    bool valid = false;
    double peakValue = 0.0;
    double totalFlux = 0.0;
    double background = 0.0;
    double noiseSigma = 0.0;
    double threshold = 0.0;
    quint64 signalPixelCount = 0;
    cv::Mat calculationImage;
};

struct AtmosphericParams {
    double r0 = 0.0;
    double seeing = 0.0;
    double theta0 = 0.0;
    double tau0 = 0.0;
    bool tau0Valid = false;
    bool tau0UnderResolved = false;
    // 正常结果为输入时间序列的采样周期；欠分辨时为 tau0 上界。
    double tau0ResolutionMs = 0.0;

    double longitudinalVariancePx2 = 0.0;
    double transverseVariancePx2 = 0.0;
    double longitudinalVarianceRad2 = 0.0;
    double transverseVarianceRad2 = 0.0;
    double r0LongitudinalCm = 0.0;
    double r0TransverseCm = 0.0;

    // The input path remains pixel². These fields distinguish the measured
    // variance from the optional PSD-corrected variance used below.
    double longitudinalMeasuredVariancePx2 = 0.0;
    double transverseMeasuredVariancePx2 = 0.0;
    double longitudinalNoiseVariancePx2 = 0.0;
    double transverseNoiseVariancePx2 = 0.0;
    double longitudinalAtmosphericVariancePx2 = 0.0;
    double transverseAtmosphericVariancePx2 = 0.0;
    bool varianceNoiseCorrectionApplied = false;
    bool varianceNoiseCorrectionValid = false;
    CdimPsdAnalysisResult psdAnalysis;
};

Q_DECLARE_METATYPE(AtmosphericParams)

struct DifferentialSample {
    double longitudinal = 0.0;
    double transverse = 0.0;
    double centroid1X = 0.0;
    double centroid1Y = 0.0;
    double centroid2X = 0.0;
    double centroid2Y = 0.0;
    quint64 frameId1 = 0;
    quint64 frameId2 = 0;
    quint64 cameraTimestamp1 = 0;
    quint64 cameraTimestamp2 = 0;
    double syncResidualUs = 0.0;
    qint64 timestampMs = 0;
};

class ImageProcessorWorker : public QObject {
    Q_OBJECT
public:
    explicit ImageProcessorWorker(std::shared_ptr<std::atomic<quint64>> acquisitionGeneration,
                                  QObject* parent = nullptr);

signals:
    void centroidReady(int cameraIndex,
                       double x,
                       double y,
                       double peakValue,
                       double totalFlux,
                       double background,
                       double noiseSigma,
                       double threshold,
                       quint64 signalPixelCount);
    void differentialSampleReady(quint64 pairedSampleCount, quint64 droppedUnpairedCount);
    void differentialSampleDetailReady(quint64 pairedSampleCount,
                                       quint64 frameId1,
                                       quint64 frameId2,
                                       quint64 cameraTimestamp1,
                                       quint64 cameraTimestamp2,
                                       double centroid1X,
                                       double centroid1Y,
                                       double centroid2X,
                                       double centroid2Y,
                                       double longitudinal,
                                       double transverse,
                                       double syncResidualUs,
                                       qint64 timestampMs);
    void roiImageReady(int cameraIndex, cv::Mat roiImage);
    void calculationImageReady(int cameraIndex, quint64 frameId, cv::Mat calculationImage);
    void autoFocusRoiMeasurementReady(int cameraIndex,
                                      quint64 frameId,
                                      cv::Mat calculationImage,
                                      bool centroidValid,
                                      double centroidX,
                                      double centroidY);
    void atmosphereReady(double r0,
                         double seeing,
                         double theta0,
                         double tau0,
                         bool tau0Valid,
                         bool tau0UnderResolved,
                         double tau0ResolutionMs,
                         double longitudinalVariancePx2,
                         double transverseVariancePx2,
                         double longitudinalVarianceRad2,
                         double transverseVarianceRad2,
                         double r0LongitudinalCm,
                         double r0TransverseCm);
    void psdAnalysisReady(CdimPsdAnalysisResult result);
    void frameProcessed(int cameraIndex, bool centroidValid, double elapsedMs);
    void syncSampleReady(double syncResidualUs);
    void unpairedSampleDropped(int droppedCameraIndex,
                               quint64 cam0FrameId,
                               quint64 cam1FrameId,
                               qint64 frameIdOffset,
                               qint64 alignedFrameId0,
                               qint64 alignedFrameId1,
                               quint64 cam0Timestamp,
                               quint64 cam1Timestamp,
                               quint64 droppedUnpairedSamples);
    void autoExposureSampleReady(int cameraIndex,
                                 double peakValue,
                                 double background,
                                 double noiseSigma,
                                 double threshold,
                                 quint64 signalPixelCount,
                                 quint64 saturatedPixelCount,
                                 int peakQuality,
                                 double supportedPeakValue,
                                 quint64 peakSupportPixelCount,
                                 double rejectedPeakValue,
                                 int rejectedCandidateCount,
                                 bool spotHardSaturated,
                                 bool centroidValid,
                                 bool measurementUsable,
                                 bool decisionSample,
                                 quint64 frameId,
                                 qint64 timestampMs);
    void roiBackgroundThresholdReady(int cameraIndex,
                                     double background,
                                     double noiseSigma,
                                     double threshold);
    void acquisitionStopRequested(const QString& reason);

public slots:
    void setCentroidMethod(int method);
    void setCentroidMode(int mode);
    void setPeakKernelCentroidConfig(int radiusPx, double strongHotPixelExcessDn);
    void setBackgroundNoiseThresholdConfig(int clipIterations,
                                           double clipSigma,
                                           double thresholdSigmaMultiplier);
    void setThreshold(double threshold);
    void setRoiCentroidConfig(double thresholdAbsolute,
                              double sigmaThreshold,
                              double minimumIntensity,
                              int minimumSignalPixels,
                              double noiseTrimFraction);
    void configureHotPixelTemplates(const QString& camera0MaskPath,
                                    const QString& camera0ExcessPath,
                                    const QString& camera1MaskPath,
                                    const QString& camera1ExcessPath,
                                    int templateWidth,
                                    int templateHeight);
    void setOpticalParams(double apertureDiameterMm,
                          double baselineSeparationMm,
                          double baselineAngleDeg,
                          double focalLengthCm,
                          double zenithAngleDeg,
                          double lambdaNm,
                          double pixelSizeUm,
                          double outerScaleM);
    void setTargetFrameRateHz(double frameRateHz);
    void setAtmosphereHistoryWindowFrames(int frames);
    void setPsdAnalysisConfig(const CdimPsdAnalysisConfig& config);
    void setAutoExposureMetricConfig(bool enabled,
                                     double hardSaturationDn,
                                     int sampleIntervalMs,
                                     int peakSupportRadiusPx,
                                     double peakSupportFraction,
                                     int minPeakSupportPixelCount,
                                     double minNeighborPeakRatio,
                                     int maxPeakCandidateCount,
                                     double supportedPeakPercentile,
                                     int saturatedPixelCount);
    void setCurrentRoi(int cameraIndex, const RoiRect& roi);
    void setPairRois(RoiRect roi0, RoiRect roi1);
    void setPairRoisPreservingAtmosphereWindow(RoiRect roi0, RoiRect roi1);
    void discardActiveAtmosphereWindow();
    void advanceAcquisitionGeneration();
    void resetAcquisitionStatistics();
    void processFrame(int cameraIndex,
                      cv::Mat frame,
                      quint64 frameId = 0,
                      quint64 cameraTimestamp = 0,
                      quint64 acquisitionGeneration = 0);

private:
    mutable QMutex m_mutex;
    int m_centroidMode = 0;
    int m_peakKernelRadiusPx = 3;
    double m_strongHotPixelExcessDn = 100.0;
    int m_backgroundThresholdClipIterations = 3;
    double m_backgroundThresholdClipSigma = 3.0;
    double m_backgroundThresholdSigmaMultiplier = 1.0;
    double m_threshold = 0.0;
    int m_centroidWindowRadius = 15;
    double m_centroidSigmaThreshold = 4.0;
    double m_centroidPeakFraction = 0.20;
    int m_centroidMinimumIntensity = 16;
    int m_centroidMinimumSignalPixels = 3;
    double m_roiThresholdAbsolute = -1.0;
    QPointF m_lastValidGlobalCentroid[2];
    bool m_hasLastValidGlobalCentroid[2] = {false, false};
    double m_roiNoiseTrimFraction = 0.10;
    double m_apertureDiameter = 56e-3;
    double m_baselineSeparation = 250e-3;
    double m_baselineAngleDeg = 0.0;
    double m_f = 0.269;
    double m_zenithAngleDeg = 49.6;
    double m_lambda = 500e-9;
    double m_pixelSize = 2.5e-6;
    double m_outerScaleMeters = 20.0;
    double m_targetFrameRateHz = 200.0;
    int m_atmosphereHistoryWindowFrames = 5000;
    CdimPsdAnalysisConfig m_psdAnalysisConfig;
    bool m_autoExposureMetricsEnabled = false;
    double m_autoExposureHardSaturationDn = 4090.0;
    AutoExposureSpotConfig m_autoExposureSpotConfig;
    RoiRect m_currentRoi[2];
    std::shared_ptr<std::atomic<quint64>> m_acquisitionGeneration;
    bool m_syncCalibrated = false;
    bool m_timestampOffsetCalibrated = false;
    quint64 m_firstRawFrameId[2] = {0, 0};
    quint64 m_firstRawTimestamp[2] = {0, 0};
    qint64 m_frameIdOffset = 0;
    long double m_timestampOffsetTicks = 0.0L;
    quint64 m_lastPairedSerial = 0;
    quint64 m_droppedUnpairedSamples = 0;
    qint64 m_lastAtmospherePublishMs = 0;
    qint64 m_lastRoiImagePublishMs[2] = {0, 0};
    quint64 m_diagnosticUnpairedDropLogCount = 0;
    QList<DifferentialSample> m_differentialHistory;

    struct HotPixelTemplate {
        bool enabled = false;
        QString maskPath;
        QString excessPath;
        int width = 0;
        int height = 0;
    };

    struct HotPixelRoiCache {
        bool valid = false;
        RoiRect roi;
        QVector<uchar> mask;
        QVector<quint16> excess;
    };

    struct PendingCentroidSample {
        CentroidResult centroid;
        quint64 frameId = 0;
        quint64 cameraTimestamp = 0;
        qint64 timestampMs = 0;
    };

    HotPixelTemplate m_hotPixelTemplates[2];
    HotPixelRoiCache m_hotPixelCaches[2];
    QList<PendingCentroidSample> m_pendingCentroids[2];

    static constexpr double TAU0_HISTORY_WINDOW_SECONDS = 3.0;
    static constexpr double TAU0_MAX_LAG_MS = 200.0;
    static constexpr int TAU0_MIN_SAMPLES = 30;
    static constexpr int MIN_HISTORY_WINDOW = 50;
    static constexpr int MAX_HISTORY_WINDOW = 60000;
    static constexpr int MAX_PENDING_PAIR_QUEUE = 1000;
    static constexpr int MIN_ROI_SIZE = 16;
    static constexpr qint64 ATMOSPHERE_PUBLISH_INTERVAL_MS = 1000;
    static constexpr qint64 ROI_IMAGE_PUBLISH_INTERVAL_MS = 100;
    static constexpr double MARS_GIGE_TIMESTAMP_TICK_US = 0.008;

    cv::Mat preprocess(const cv::Mat& image);
    cv::Mat applyHotPixelCorrection(int cameraIndex, const RoiRect& roi, const cv::Mat& roiImage);
    HotPixelRoiCache hotPixelCacheSnapshot(int cameraIndex, const RoiRect& roi) const;
    CentroidResult calculateCentroid(int cameraIndex, const RoiRect& roi, const cv::Mat& roiImage);
    CentroidResult backgroundThresholdKernelCentroid(int cameraIndex,
                                                     const RoiRect& roi,
                                                     const cv::Mat& image);
    CentroidResult backgroundSubtractedFullRoiCentroid(int cameraIndex,
                                                       const RoiRect& roi,
                                                       const cv::Mat& image);
    CentroidResult peakKernelCentroid(int cameraIndex, const RoiRect& roi, const cv::Mat& image);
    void clearLastValidGlobalCentroid(int cameraIndex);
    void clearAllLastValidGlobalCentroids();
    AtmosphericParams calculateAtmosphere(const QList<DifferentialSample>& samples);
    QList<DifferentialSample> tau0WindowSamples(
        const QList<DifferentialSample>& samples) const;

    int historyWindowSize() const;
    int minimumAtmosphereSamples() const;
    int pendingCentroidQueueLimit() const;
    void resetRoiProcessingHistory();
    void resetPairingState();
    void recordFrameMetadata(int cameraIndex, quint64 frameId, quint64 cameraTimestamp);
    void submitCentroidSample(int cameraIndex,
                              const CentroidResult& centroid,
                              quint64 frameId,
                              quint64 cameraTimestamp,
                              qint64 timestampMs,
                              const cv::Mat& roiImage);
    bool appendDifferentialSample();
    void emitRoiImageIfDue(int cameraIndex, const cv::Mat& roiImage, qint64 nowMs, bool force = false);
};

class ImageProcessor : public QObject {
    Q_OBJECT
public:
    explicit ImageProcessor(QObject* parent = nullptr);
    ~ImageProcessor();

    void setCentroidMethod(int method);
    void setCentroidMode(int mode);
    void setPeakKernelCentroidConfig(int radiusPx, double strongHotPixelExcessDn);
    void setBackgroundNoiseThresholdConfig(int clipIterations,
                                           double clipSigma,
                                           double thresholdSigmaMultiplier);
    void setThreshold(double threshold);
    void setRoiCentroidConfig(double thresholdAbsolute,
                              double sigmaThreshold,
                              double minimumIntensity,
                              int minimumSignalPixels,
                              double noiseTrimFraction);
    void configureHotPixelTemplates(const QString& camera0MaskPath,
                                    const QString& camera0ExcessPath,
                                    const QString& camera1MaskPath,
                                    const QString& camera1ExcessPath,
                                    int templateWidth,
                                    int templateHeight);
    bool loadProcessingConfig(const QString& path, QString* message = nullptr);
    void setOpticalParams(double apertureDiameterMm,
                          double baselineSeparationMm,
                          double baselineAngleDeg,
                          double focalLengthCm,
                          double zenithAngleDeg,
                          double lambdaNm,
                          double pixelSizeUm,
                          double outerScaleM);
    void setTargetFrameRateHz(double frameRateHz);
    void setAtmosphereHistoryWindowFrames(int frames);
    void setPsdAnalysisConfig(const CdimPsdAnalysisConfig& config);
    void setAutoExposureMetricConfig(bool enabled,
                                     double hardSaturationDn,
                                     int sampleIntervalMs = 1000,
                                     int peakSupportRadiusPx = 2,
                                     double peakSupportFraction = 0.50,
                                     int minPeakSupportPixelCount = 3,
                                     double minNeighborPeakRatio = 0.35,
                                     int maxPeakCandidateCount = 8,
                                     double supportedPeakPercentile = 0.95,
                                     int saturatedPixelCount = 1);
    void setCurrentRoi(int cameraIndex, const RoiRect& roi);
    void setPairRois(const RoiRect rois[2]);
    void setPairRoisPreservingAtmosphereWindow(const RoiRect rois[2]);
    void discardActiveAtmosphereWindow();
    void advanceAcquisitionGeneration();
    void resetAcquisitionStatistics();
    RoiRect getCurrentRoi(int cameraIndex) const;
    int centroidMethod() const { return m_centroidMode; }
    int centroidMode() const { return m_centroidMode; }
    int peakKernelRadiusPx() const { return m_peakKernelRadiusPx; }
    double strongHotPixelExcessDn() const { return m_strongHotPixelExcessDn; }
    int backgroundThresholdClipIterations() const { return m_backgroundThresholdClipIterations; }
    double backgroundThresholdClipSigma() const { return m_backgroundThresholdClipSigma; }
    double backgroundThresholdSigmaMultiplier() const { return m_backgroundThresholdSigmaMultiplier; }
    double apertureDiameterMm() const { return m_apertureDiameterMm; }
    double baselineSeparationMm() const { return m_baselineSeparationMm; }
    double baselineAngleDeg() const { return m_baselineAngleDeg; }
    double focalLengthCm() const { return m_focalLengthCm; }
    double zenithAngleDeg() const { return m_zenithAngleDeg; }
    double wavelengthNm() const { return m_wavelengthNm; }
    double pixelSizeUm() const { return m_pixelSizeUm; }
    double outerScaleMeters() const { return m_outerScaleMeters; }
    int atmosphereHistoryWindowFrames() const { return m_atmosphereHistoryWindowFrames; }
    CdimPsdAnalysisConfig psdAnalysisConfig() const { return m_psdAnalysisConfig; }

public slots:
    void processFrame(int cameraIndex,
                      const cv::Mat& frame,
                      quint64 frameId = 0,
                      quint64 cameraTimestamp = 0,
                      quint64 acquisitionGeneration = 0);

signals:
    void centroidReady(int cameraIndex,
                       double x,
                       double y,
                       double peakValue,
                       double totalFlux,
                       double background,
                       double noiseSigma,
                       double threshold,
                       quint64 signalPixelCount);
    void differentialSampleReady(quint64 pairedSampleCount, quint64 droppedUnpairedCount);
    void differentialSampleDetailReady(quint64 pairedSampleCount,
                                       quint64 frameId1,
                                       quint64 frameId2,
                                       quint64 cameraTimestamp1,
                                       quint64 cameraTimestamp2,
                                       double centroid1X,
                                       double centroid1Y,
                                       double centroid2X,
                                       double centroid2Y,
                                       double longitudinal,
                                       double transverse,
                                       double syncResidualUs,
                                       qint64 timestampMs);
    void roiImageReady(int cameraIndex, cv::Mat roiImage);
    void calculationImageReady(int cameraIndex, quint64 frameId, cv::Mat calculationImage);
    void autoFocusRoiMeasurementReady(int cameraIndex,
                                      quint64 frameId,
                                      cv::Mat calculationImage,
                                      bool centroidValid,
                                      double centroidX,
                                      double centroidY);
    void atmosphereReady(double r0,
                         double seeing,
                         double theta0,
                         double tau0,
                         bool tau0Valid,
                         bool tau0UnderResolved,
                         double tau0ResolutionMs,
                         double longitudinalVariancePx2,
                         double transverseVariancePx2,
                         double longitudinalVarianceRad2,
                         double transverseVarianceRad2,
                         double r0LongitudinalCm,
                         double r0TransverseCm);
    void psdAnalysisReady(CdimPsdAnalysisResult result);
    void frameProcessed(int cameraIndex, bool centroidValid, double elapsedMs);
    void syncSampleReady(double syncResidualUs);
    void unpairedSampleDropped(int droppedCameraIndex,
                               quint64 cam0FrameId,
                               quint64 cam1FrameId,
                               qint64 frameIdOffset,
                               qint64 alignedFrameId0,
                               qint64 alignedFrameId1,
                               quint64 cam0Timestamp,
                               quint64 cam1Timestamp,
                               quint64 droppedUnpairedSamples);
    void autoExposureSampleReady(int cameraIndex,
                                 double peakValue,
                                 double background,
                                 double noiseSigma,
                                 double threshold,
                                 quint64 signalPixelCount,
                                 quint64 saturatedPixelCount,
                                 int peakQuality,
                                 double supportedPeakValue,
                                 quint64 peakSupportPixelCount,
                                 double rejectedPeakValue,
                                 int rejectedCandidateCount,
                                 bool spotHardSaturated,
                                 bool centroidValid,
                                 bool measurementUsable,
                                 bool decisionSample,
                                 quint64 frameId,
                                 qint64 timestampMs);
    void roiBackgroundThresholdReady(int cameraIndex,
                                     double background,
                                     double noiseSigma,
                                     double threshold);
    void acquisitionStopRequested(const QString& reason);

private:
    QThread* m_workerThread = nullptr;
    ImageProcessorWorker* m_worker = nullptr;
    RoiRect m_currentRoi[2];
    std::shared_ptr<std::atomic<quint64>> m_acquisitionGeneration =
        std::make_shared<std::atomic<quint64>>(1);
    int m_centroidMode = 0;
    int m_peakKernelRadiusPx = 3;
    double m_strongHotPixelExcessDn = 100.0;
    int m_backgroundThresholdClipIterations = 3;
    double m_backgroundThresholdClipSigma = 3.0;
    double m_backgroundThresholdSigmaMultiplier = 1.0;
    double m_apertureDiameterMm = 56.0;
    double m_baselineSeparationMm = 250.0;
    double m_baselineAngleDeg = 0.0;
    double m_focalLengthCm = 26.9;
    double m_zenithAngleDeg = 49.6;
    double m_wavelengthNm = 500.0;
    double m_pixelSizeUm = 2.5;
    double m_outerScaleMeters = 20.0;
    double m_targetFrameRateHz = 200.0;
    int m_atmosphereHistoryWindowFrames = 5000;
    CdimPsdAnalysisConfig m_psdAnalysisConfig;
};
