#pragma once

#include "CameraTypes.h"
#include "PolarisCatalog.h"

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

#include <atomic>
#include <memory>

#include <opencv2/opencv.hpp>

class PolarisSolverWorker;
class QThread;

struct DetectedStar {
    int detectionIndex = 0;
    QPointF centroidPx;
    QRectF boundingBoxPx;
    int areaPx = 0;
    double peak = 0.0;
    double flux = 0.0;
    double background = 0.0;
    double snr = 0.0;
    bool saturated = false;
};

struct PolarisHotPixelTemplateConfig {
    bool enabled = false;
    QString maskPath;
    QString excessPath;
    int templateWidth = 0;
    int templateHeight = 0;
};

struct PolarisSolverConfig {
    bool enabled = true;
    int cameraIndex = 0;
    int maxDetectedStars = 20;
    int minMatchedStars = 5;
    double maxRmsPx = 3.0;
    double nominalPlateScaleArcsecPx = 1.9179;
    double minPlateScaleArcsecPx = 1.70;
    double maxPlateScaleArcsecPx = 2.15;
    double initialMatchTolerancePx = 8.0;
    double refinedMatchTolerancePx = 4.0;
    double observationEpochYear = 2026.5;
    double starThresholdSigma = 4.0;
    double starPeakFraction = 0.20;
    int minStarAreaPx = 1;
    int maxStarAreaPx = 1000;
    int retryIntervalMs = 3000;
    int lostTrackRetryCount = 3;
    bool showMatchedCatalogStars = true;
    double minScoreMargin = 5.0;
    double minMatchedSpatialSpreadPx = 50.0;
    double minPolarisSnr = 5.0;
    bool allowSaturatedPolarisConfirmation = false;
    PolarisHotPixelTemplateConfig hotPixelTemplates[kCameraCount];
};

enum class PolarisSolveStatus {
    Idle,
    WaitingFrame,
    DetectingStars,
    MatchingCatalog,
    Solved,
    Tracking,
    ManualConfirmed,
    InsufficientStars,
    NoCatalogMatch,
    LowConfidence,
    Cancelled,
    Error
};

struct CatalogImageMatch {
    quint64 sourceId = 0;
    QString name;
    QPointF catalogPlaneRad;
    QPointF predictedPixel;
    QPointF detectedPixel;
    double residualPx = 0.0;
    bool isPolaris = false;
};

struct PolarisSolveTiming {
    double detectionMs = 0.0;
    double matchingMs = 0.0;
    double totalMs = 0.0;
};

struct PolarisSolveStats {
    int catalogStarCount = 0;
    int catalogTriangleCount = 0;
    int imageTriangleCount = 0;
    int candidateTriangleCount = 0;
    int testedTransformCount = 0;
    int solutionClusterCount = 0;
    int bestSolutionSupportCount = 0;
    int secondBestSolutionSupportCount = 0;
};

struct PolarisSolveResult {
    bool valid = false;
    PolarisSolveStatus status = PolarisSolveStatus::Idle;
    QString message;
    int detectedStarCount = 0;
    int matchedStarCount = 0;
    double observationEpochYear = 0.0;
    QString coordinateModel;
    double rmsPx = 0.0;
    double maxResidualPx = 0.0;
    double matchedSpatialSpreadPx = 0.0;
    double plateScaleArcsecPx = 0.0;
    double rotationDeg = 0.0;
    bool mirrored = false;
    double bestScore = 0.0;
    double secondBestScore = 0.0;
    double scoreMargin = 0.0;
    bool showMatchedCatalogStars = true;
    bool hasPolarisPixel = false;
    QPointF polarisPixel;
    bool hasPredictedPolarisPixel = false;
    QPointF predictedPolarisPixel;
    bool hasDetectedPolarisPixel = false;
    QPointF detectedPolarisPixel;
    bool polarisDetectionQualityRejected = false;
    bool hasNorthCelestialPolePixel = false;
    QPointF northCelestialPolePixel;
    double polarisPolarRadiusPx = 0.0;
    quint64 taskId = 0;
    QVector<DetectedStar> detections;
    QVector<CatalogImageMatch> matches;
    PolarisSolveTiming timing;
    PolarisSolveStats stats;
    quint64 generation = 0;
    quint64 frameId = 0;
    int cameraIndex = -1;
};

QVector<DetectedStar> detectStarsFromFrame(const cv::Mat& frame,
                                           const PolarisSolverConfig& config,
                                           const std::shared_ptr<std::atomic_bool>& cancelled = {});
PolarisSolveResult solveFrame(const cv::Mat& frame,
                              const PolarisSolverConfig& config);
PolarisSolveResult solveFrame(const cv::Mat& frame,
                              const PolarisSolverConfig& config,
                              const std::shared_ptr<std::atomic_bool>& cancelled);
PolarisSolveResult solveDetectedStars(const QVector<DetectedStar>& detections,
                                       const PolarisSolverConfig& config);
PolarisSolveResult solveDetectedStars(const QVector<DetectedStar>& detections,
                                       const PolarisSolverConfig& config,
                                       const std::shared_ptr<std::atomic_bool>& cancelled);

class PolarisSolverController : public QObject {
    Q_OBJECT

public:
    static constexpr int kSolverWorkerThreadCount = 1;

    explicit PolarisSolverController(QObject* parent = nullptr);
    ~PolarisSolverController();

    void submitFrame(int cameraIndex,
                     const cv::Mat& frame,
                     const PolarisSolverConfig& config,
                     quint64 generation,
                     quint64 frameId,
                     bool forceSolve);
    void cancelCamera(int cameraIndex, quint64 generation);
    void cancelAll(quint64 newGeneration);

signals:
    void solveFinished(PolarisSolveResult result);
    void solveStatusChanged(int cameraIndex,
                            PolarisSolveStatus status,
                            QString message,
                            quint64 generation);

private:
    struct PendingSolveTask {
        bool valid = false;
        quint64 taskId = 0;
        int cameraIndex = -1;
        quint64 generation = 0;
        quint64 frameId = 0;
        cv::Mat frame;
        PolarisSolverConfig config;
        std::shared_ptr<std::atomic_bool> cancelled;
    };

    void cancelCurrentSolveTasks();
    void startSolveTask(int cameraIndex,
                        quint64 taskId,
                        const cv::Mat& frame,
                        const PolarisSolverConfig& config,
                        quint64 generation,
                        quint64 frameId,
                        const std::shared_ptr<std::atomic_bool>& cancelled = {});

    QThread* m_workerThread = nullptr;
    PolarisSolverWorker* m_worker = nullptr;
    bool m_taskRunning[kCameraCount] = {false, false};
    PendingSolveTask m_pendingLatestTask[kCameraCount];
    std::shared_ptr<std::atomic_bool> m_cancelledTokens[kCameraCount];
    quint64 m_nextTaskId = 0;
    quint64 m_activeTaskId[kCameraCount] = {0, 0};
    quint64 m_generation = 0;
};

Q_DECLARE_METATYPE(PolarisSolveResult)
