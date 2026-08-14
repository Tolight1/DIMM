#pragma once

#include "AlignmentCoarseEstimator.h"
#include "AlignmentTypes.h"
#include "AlignmentSession.h"
#include "AppConfig.h"
#include "AutoAcquisitionRecoveryController.h"
#include "AutoAcquisitionScheduler.h"
#include "AutoExposureController.h"
#include "CameraManager.h"
#include "CameraTypes.h"
#include "CanvasWidgets.h"
#include "EnvironmentSensorManager.h"
#include "ImageProcessor.h"
#include "PolarisDetectionPipeline.h"
#include "PolarisTrajectory.h"
#include "PolarisSolver.h"
#include "ResultWriter.h"
#include "SettingsDialog.h"
#include "StableCandidateTracker.h"
#include "ui_DIMM.h"

#include <QDateTime>
#include <QDialog>
#include <QMainWindow>
#include <QRect>
#include <QSize>
#include <QTimer>
#include <QVector>
#include <array>
#include <cstdint>
#include <functional>

#include <opencv2/opencv.hpp>

class AlignmentCoarseController;
class FullFrameCanvas;
class RoiStarCanvas;
class ChartWidget;
class CommManager;
class EafFocuserManager;
class FocuserControlWidget;
class PulseGeneratorManager;

class QLineEdit;
class QRadioButton;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QAction;

enum class UiStatusLevel;
class SettingsDialog;

class DIMM : public QMainWindow {
    Q_OBJECT

public:
    explicit DIMM(QWidget* parent = nullptr);
    ~DIMM();

    enum class CaptureState {
        Idle,
        Live,
        Simulation,
        Paused,
        Alignment
    };

    enum class DetailViewMode {
        None = 0,
        RoiOnly = 1,
        ChartsOnly = 2,
        Both = 3
    };

    enum class LiveStartupPhase {
        None,
        LocatePair,
        Tracking
    };

    enum class LiveStartupOrigin {
        Manual,
        AutoAcquisition
    };

    enum class HardwareTriggerStartupStage {
        None,
        WaitingFullFramePair,
        WaitingRoiTrackingPair,
        Running
    };

private slots:
    void onStartCapture();
    void onStartSimulation();
    void onStopCapture();
    void onShowMainPage();
    void onShowRoiPage();
    void onShowSettings();
    void onToggleAlignmentMode();
    void onToggleCoarseAlignment();
    void onConfirmCamera1PolarisCandidate();
    void onConfirmCamera2PolarisCandidate();
    void onToggleRoiImages();
    void onToggleCharts();
    void onSaveConfig();
    void onLoadConfig();
    void onExportData();
    void onExportReport();
    void onConnectAll();
    void onDisconnectAll();
    void onAbout();
    void onUpdateSimulation();
    void onCapturedFramePacket(int cameraIndex, CameraFrame packet);
    void onFrameReady(int cameraIndex);
    void onCameraConnected(int index, QString serial, QString model);
    void onCameraDisconnected(int index);
    void onCameraError(int index, int errorCode, QString message);

private:
    struct PairedCentroidDetail {
        quint64 pairedSampleCount = 0;
        quint64 frameId1 = 0;
        quint64 frameId2 = 0;
        quint64 cameraTimestamp1 = 0;
        quint64 cameraTimestamp2 = 0;
        double centroid1X = 0.0;
        double centroid1Y = 0.0;
        double centroid2X = 0.0;
        double centroid2Y = 0.0;
        double longitudinal = 0.0;
        double transverse = 0.0;
        double syncResidualUs = 0.0;
        qint64 timestampMs = 0;
    };

    struct CaptureRuntimeContext {
        int frameCount = 0;
        int frameCountPerCamera[2] = {0, 0};
        quint64 processedFrameCount = 0;
        quint64 processedFrameCountPerCamera[2] = {0, 0};
        quint64 validCentroidCount = 0;
        quint64 validCentroidCountPerCamera[2] = {0, 0};
        double latestProcessingLatencyMs = 0.0;
        double averageProcessingLatencyMs = 0.0;
        quint64 syncSampleCount = 0;
        quint64 syncJitterSampleCount = 0;
        double latestSyncResidualUs = 0.0;
        double latestSyncJitterUs = 0.0;
        double averageSyncJitterUs = 0.0;
        double maxSyncJitterUs = 0.0;
        quint64 pairedSampleCount = 0;
        quint64 droppedUnpairedSampleCount = 0;
        int simulationFrameIndex = 0;
        int lastSimulationPreviewFrame = -1;
        qint64 lastLivePreviewUpdateMs[2] = {-1, -1};
        qint64 lastMeasurementUiUpdateMs = -1;
        bool hasValidAtmosphere = false;
        quint64 latestAtmosphereTimestampMs = 0;
        double centroidX[2] = {0.0, 0.0};
        double centroidY[2] = {0.0, 0.0};
        double peakBrightness[2] = {0.0, 0.0};
        bool hasValidCentroid[2] = {false, false};
        int lostCentroidFrameCount[2] = {0, 0};
        qint64 lostCentroidSinceMs[2] = {-1, -1};
        bool initialCentroidSettlePending = false;
        int initialCentroidSettleFrameCount = 0;
        int roiRecenteringCandidateFrameCount = 0;
        QSize frameSize[2] = {QSize(5120, 5120), QSize(5120, 5120)};
        AtmosphericParams latestAtmosphere;
        int chartMinuteKey = -1;
        int chartSecond = -1;
        bool simulationRoiSeeded = false;
        bool initialRoiConfirmed[2] = {false, false};
        RoiRect pendingInitialRoi[2];
        bool pendingInitialRoiReady[2] = {false, false};
        QPointF lastTargetPosition[2];
        bool hasLastTargetPosition[2] = {false, false};
        QPointF confirmedPolarisPosition[2];
        bool hasConfirmedPolarisPosition[2] = {false, false};
        double confirmedPolarisPhaseRad[2] = {0.0, 0.0};
        bool hasConfirmedPolarisPhase[2] = {false, false};
        QDateTime confirmedPolarisTimeUtc[2];
        int selectedInitialCandidateIndex[2] = {-1, -1};
        bool pendingInitialCandidateSelectionRequired[2] = {false, false};
        qint64 lastInitialCandidatePromptMs[2] = {-1, -1};
        qint64 liveRelocalizationStartedMs = -1;
        cv::Mat liveRelocalizationPreviewFrame[2];
        QVector<PairedCentroidDetail> pendingPairedCentroidDetails;
    };

    void registerMetaTypes();
    void setupServiceManagers();
    void setupStatusBarUi();
    void setupMainWindowUi();
    void setupRuntimeActions();
    void initializeCaptureServices();
    void setupPreviewCanvases();
    void setupFullFramePreviewCanvases();
    void setupRoiPreviewCanvases();
    void setupChartCanvases();
    void setupSettingsCallbacks();
    void setupCameraSettingsCallbacks();
    void setupAutoExposureSettingsCallbacks();
    void setupTriggerSettingsCallbacks();
    void setupEnvironmentSettingsCallbacks();
    void setupPulseGeneratorSettingsCallbacks();
    void setupAutoAcquisitionSettingsCallbacks();
    void setupProcessingSettingsCallbacks();
    void setupOpticsSettingsCallbacks();
    void setupAlignmentSettingsCallbacks();
    void setupStorageSettingsCallbacks();
    void setupNetworkSettingsCallbacks();
    void setupCameraConnections();
    void setupImageProcessorConnections();
    void setupCentroidProcessorConnection();
    void setupAutoExposureProcessorConnection();
    void setupDifferentialSampleProcessorConnections();
    void setupFrameProcessedProcessorConnection();
    void setupSyncSampleProcessorConnection();
    void setupRoiImageProcessorConnection();
    void setupAtmosphereProcessorConnection();
    void setupCanvasMouseStatusConnections();
    void setupRuntimeTimers();
    void setupCommConnections();
    void setupReportTimer();
    void setupConnections();
    void updateParams();
    void refreshUi();
    void refreshStatusUi();
    void refreshCameraUi();
    void refreshMeasurementUi();
    void refreshPanelUi();
    void refreshActionStates();
    void syncCameraSelectionUi();
    QString currentPreviewModeText() const;
    void setStatusMessage(const QString& text, const QString& color = "#e0e0e0");
    void setStatusMessage(const QString& text, UiStatusLevel level);
    void setFullFrameThresholdDisplay(int cameraIndex,
                                      double otsuThreshold,
                                      double actualThreshold);
    void setRoiThresholdDisplay(int cameraIndex,
                                double otsuThreshold,
                                double actualThreshold);
    void setAlignmentSolveLabel(int cameraIndex, const QString& text, UiStatusLevel level);
    void logPolarisSolveResult(const PolarisSolveResult& result) const;
    void setDetailViewMode(DetailViewMode mode);
    void resetMeasurementState();
    void updateCaptureState(CaptureState state);
    void updateCommState(bool connected);
    bool hasAnyOpenCamera() const;
    int openCameraCount() const;
    bool hasActiveCapture() const;
    bool isLiveCaptureActive() const;
    bool isSimulationCaptureActive() const;
    void handleLiveFramePacket(int cameraIndex, const CameraFrame& packet);
    bool canReportMeasurements() const;
    std::uint32_t monitoringDeviceStatus(const CaptureRuntimeContext& runtime,
                                         qint64 nowMs,
                                         double frameRateHz,
                                         bool frameRateValid);
    QString captureModeName() const;
    QString captureModeLabel() const;
    QString resultSubdirectoryName() const;
    CaptureRuntimeContext& activeRuntime();
    const CaptureRuntimeContext& activeRuntime() const;
    CaptureRuntimeContext& runtimeForState(CaptureState state);
    const CaptureRuntimeContext& runtimeForState(CaptureState state) const;
    bool hasValidCentroidsForRoiUpdate() const;
    void appendActualRoiTrackPoint(int cameraIndex, const RoiRect& roi);
    void updateActualRoiTrackOverlay(int cameraIndex);
    void clearActualRoiTracks();
    bool isCentroidNearCurrentRoiEdge(int cameraIndex, double x, double y) const;
    bool isCentroidTooFarFromCurrentRoiCenter(int cameraIndex) const;
    bool tryApplyInitialCentroidSettleRoi();
    bool shouldUpdateRoiForRecentering();
    void requestLiveFullFrameRelocalization(const QString& reason);
    void handleLiveRoiCentroidLoss(int cameraIndex);
    bool isUsableCentroidSample(int cameraIndex,
                                double x,
                                double y,
                                double peakValue,
                                double totalFlux,
                                double background,
                                double threshold,
                                quint64 signalPixelCount,
                                bool requireCentered = false) const;
    RoiRect sanitizeRoi(const RoiRect& roi, int cameraIndex = 0) const;
    RoiRect buildCameraCentroidRoi(int cameraIndex) const;
    void applyRoiSummary(const RoiRect& roi, const QString& cameraLabel = QStringLiteral("相机1"));
    void updateMinuteRoi(bool force = false);
    void hideLegacyRoiScheduleUi();
    QString roiRuleDescription() const;
    bool configureLiveCameras(QString* reason = nullptr);
    bool applyContinuousCameraFrameRate(QString* reason = nullptr);
    bool startAlignmentMode(QString* reason = nullptr);
    void stopAlignmentMode();
    bool prepareAlignmentCamerasForPreview(QString* reason);
    void restoreCamerasAfterAlignment();
    void showAlignmentModeStarted();
    void showAlignmentModeStopped();
    void resetAlignmentRuntimeForStart();
    void resetAlignmentRuntimeForStop();
    void clearAlignmentCanvasesForStart();
    void clearAlignmentCanvasesForStop();
    void handleAlignmentFramePacket(int cameraIndex, const CameraFrame& packet);
    bool handleManualAlignmentFrameTracking(int cameraIndex, const cv::Mat& frame);
    bool handleAutomaticAlignmentFrameTracking(int cameraIndex, const cv::Mat& frame, qint64 nowMs);
    bool prepareAlignmentFramePreview(int cameraIndex, const CameraFrame& packet);
    void finishAlignmentFramePreview(int cameraIndex, const CameraFrame& packet, qint64 nowMs);
    void showMissingAlignmentFrameForSolve(int cameraIndex);
    void showSubmittedAlignmentSolve(int cameraIndex, bool force);
    void updateAlignmentOverlay(int cameraIndex, const CameraFrame& packet);
    bool trackAlignmentPolarisLocally(int cameraIndex,
                                      const cv::Mat& frame,
                                      QPointF* trackedPosition,
                                      double* peakValue);
    QVector<PolarisDetectionPipeline::InitialStarCandidate> collectAlignmentStarCandidates(
        int cameraIndex,
        const cv::Mat& frame,
        const PolarisSolveResult& solved,
        bool hasCurrentSolverResult,
        bool allowGuiCandidateDetection,
        cv::Mat* mono8,
        double* peakValue,
        double* actualThresholdValue,
        double* otsuThresholdValue) const;
    bool handleAlignmentCandidateSelection(
        int cameraIndex,
        FullFrameCanvas* targetCanvas,
        FullFrameCanvas::AlignmentOverlay* overlay,
        const QVector<PolarisDetectionPipeline::InitialStarCandidate>& candidates,
        QPointF* selectedStar);
    PolarisDetectionPipeline::InitialStarSelection selectAlignmentInitialCandidate(
        int cameraIndex,
        const QVector<PolarisDetectionPipeline::InitialStarCandidate>& candidates,
        bool manualSelectionRequested);
    bool handleManualAlignmentCandidatePrompt(
        int cameraIndex,
        FullFrameCanvas* targetCanvas,
        FullFrameCanvas::AlignmentOverlay* overlay,
        const QVector<PolarisDetectionPipeline::InitialStarCandidate>& candidates,
        PolarisDetectionPipeline::InitialStarSelection* selection);
    void applyAlignmentSelectedCandidate(
        int cameraIndex,
        FullFrameCanvas* targetCanvas,
        const QVector<PolarisDetectionPipeline::InitialStarCandidate>& candidates,
        const PolarisDetectionPipeline::InitialStarSelection& selection,
        bool manualSelectionRequested,
        QPointF* selectedStar);
    bool promptAlignmentCandidateSelection(
        int cameraIndex,
        const QVector<PolarisDetectionPipeline::InitialStarCandidate>& candidates,
        int* chosenCandidateIndex);
    void applyManualAlignmentConfirmation(int cameraIndex, const QPointF& star);
    void updateConfirmedPolarisFromFallbackCentroid(int cameraIndex,
                                                    const cv::Mat& frame,
                                                    bool allowGuiCandidateDetection,
                                                    cv::Mat* mono8,
                                                    QPointF* star,
                                                    double* peakValue);
    PolarisSolverConfig buildPolarisSolverConfig() const;
    void onPolarisSolveFinished(PolarisSolveResult result);
    void onPolarisSolveStatusChanged(int cameraIndex,
                                     PolarisSolveStatus status,
                                     QString message,
                                     quint64 generation);
    void resetCoarseAlignmentRuntime();
    void clearCoarseAlignmentOverlays();
    CoarseAlignmentConfig buildCoarseAlignmentConfig() const;
    void submitCoarseAlignmentFrame(int cameraIndex, const CameraFrame& packet, qint64 nowMs);
    void onCoarseAlignmentEstimateReady(CoarseAlignmentEstimate estimate);
    void updateCoarseAlignmentOverlay(int cameraIndex);
    double fallbackAlignmentOrbitRadiusPx() const;
    double alignmentOrbitRadiusPx() const;
    bool startDualCameraLocalization(QString* reason = nullptr);
    bool applyLiveHardwareRois(const RoiRect rois[2],
                               QString* reason = nullptr,
                               RoiRect appliedRois[2] = nullptr);
    bool applyLiveFullFrameForRelocalization(QString* reason = nullptr);
    void advanceLiveAcquisitionGeneration();
    void resetLiveFrameAcceptanceGates();
    bool validateAndCacheLiveRoiCapabilities(QString* reason = nullptr);
    bool readLivePairRoiPosition(RoiPosition positions[2], QString* reason = nullptr);
    RoiRect buildLiveCameraRoi(int cameraIndex, const RoiRect& desiredRoi) const;
    bool selectLiveRelocalizationCentroid(
        int cameraIndex,
        const cv::Mat& mono8,
        QPointF* centroid,
        double* peakValue,
        QString* selectionSource = nullptr,
        QString* failureReason = nullptr);
    QVector<PolarisDetectionPipeline::InitialStarCandidate> stabilizeInitialCandidates(
        int cameraIndex,
        const QVector<PolarisDetectionPipeline::InitialStarCandidate>& candidates);
    void clearStableCandidateTrackers();
    bool maybeSeedRoiFromFrame(int cameraIndex, const cv::Mat& frame);
    void handleLiveRelocalizationWatchdog(qint64 nowMs);
    void updateFullFrameRoiOverlay(int cameraIndex);
    void showDeferredLiveRelocalizationPreview();
    void clearPendingLiveRelocalizationRois();
    bool commitPairedInitialRoisIfReady();
    bool startHardwarePulseStage(double frequencyHz, const QString& stageLabel, QString* reason = nullptr);
    bool startFullFrameLocalizationPulse(QString* reason = nullptr);
    bool isFullFrameLocalizationPulseRunning() const;
    bool switchToRoiTrackingPulse(QString* reason = nullptr);
    void initResultFile();
    void initDetailResultFile();
    void initSyncDiagnosticFile();
    void closeResultFile();
    void saveResultRow(int frame);
    void saveDetailResultRows(int frame, const QVector<PairedCentroidDetail>& details);
    void flushPendingWrites();
    void resetSyncDiagnostics();
    void recordSyncDiagnosticEvent(const QString& event,
                                   int cameraIndex,
                                   const CameraFrame& packet,
                                   const QString& note = QString());
    void recordSyncUnpairedDropDiagnostic(int droppedCameraIndex,
                                          quint64 cam0FrameId,
                                          quint64 cam1FrameId,
                                          qint64 frameIdOffset,
                                          qint64 alignedFrameId0,
                                          qint64 alignedFrameId1,
                                          quint64 cam0Timestamp,
                                          quint64 cam1Timestamp,
                                          quint64 droppedUnpairedSamples);
    bool stopLiveCapture();
    void stopSimulationCapture();
    bool startSimulationCapture();
    cv::Mat buildSimulationFrame(int cameraIndex) const;
    void updateCurrentRoi();
    void on1hzTick();
    void evaluateAutoAcquisitionSchedule();
    void setAutoAcquisitionStatus(const QString& text,
                                  UiStatusLevel level,
                                  const QString& throttleKey = QString());
    void stopAutoAcquisitionScanUntilNextInterval(const QString& reason,
                                                  bool manualSelectionRequired);
    void noteManualAutoAcquisitionStopIfNeeded();
    void updateCameraInfo();
    void matchRoiTimeSlot();
    void onCommCommand(uint8_t cmd);
    void reportMeasurement();
    void handleAutoExposureSample(const AutoExposureFrameSample& sample);
    void resetAutoExposureState(bool applyInitialExposure = false);
    bool isAutoExposureRoiRelocalizationGraceActive(qint64 nowMs) const;
    AppConfig currentAppConfig() const;
    void applyStartupConfig(const AppConfig& config);
    void savePersistentSettings(const AppConfig& config, const ConfigChangeSet& changes);
    QString autoExposureStateName(AutoExposureState state) const;
    QString autoExposureStateShortText(AutoExposureState state) const;
    QString autoExposureUiStatusText() const;
    QString autoExposureAdjustDirectionText() const;
    QString csvSafeField(QString value) const;
    QVector<int> scanHotPixelExposureTemplates() const;
    QVector<int> scanHotPixelExposureTemplatesForCamera(int cameraIndex) const;
    int selectHotPixelTemplateExposureForCurrentExposure(double currentExposure) const;
    int selectHotPixelTemplateExposureForCameraExposure(int cameraIndex, double currentExposure) const;
    bool resolveHotPixelTemplatePathsForExposure(int exposureUs,
                                                 QString* camera0Mask,
                                                 QString* camera0Excess,
                                                 QString* camera1Mask,
                                                 QString* camera1Excess) const;
    bool resolveHotPixelTemplatePathsForCameraExposure(int cameraIndex,
                                                       int exposureUs,
                                                       QString* maskPath,
                                                       QString* excessPath) const;
    bool applyExposureAndHotPixelTemplate(int exposureUs, QString* reason = nullptr);
    bool applyExposureAndHotPixelTemplate(int cameraIndex, int exposureUs, QString* reason = nullptr);
    void refreshHotPixelTemplates();
    bool isSettingsApplyAllowed() const;
    bool canStartLiveCapture(QString* reason = nullptr) const;
    bool canConnectOrDisconnectCameras(QString* reason = nullptr) const;
    bool isPulseBoardResponseTimeout(const QString& reason) const;
    void setPulseBoardResponseTimeoutStatus(const QString& text,
                                            UiStatusLevel level = UiStatusLevel::Warning);
    void scheduleHardwareTriggerStartupCheck();
    void checkHardwareTriggerStartup();

    void beginHardwareTriggerStartupStage(
        HardwareTriggerStartupStage stage);

    void recordHardwareTriggerStartupFrame(
        int cameraIndex,
        bool frameLooksLikeHardwareRoi);

    void confirmHardwareTriggerStartupIfReady();
    void handleHardwareTriggerStartupFailure(const QString& detail);
    bool shouldRetryFailedLiveStartup() const;
    void retryFailedLiveStartup();
    void resetLiveStartupRecoveryState(bool resetRetryCount);
    void requestAlignmentPolarisSelection(int cameraIndex);
    void requestAutomaticPolarisSolve(int cameraIndex, bool force);
    void requestAutomaticPolarisSolveBoth();

    Ui_DIMM* ui = nullptr;
    QTimer* m_simulationTimer = nullptr;
    QAction* m_actionStartSimulation = nullptr;
    QAction* m_actionAlignmentMode = nullptr;
    QAction* m_actionConfirmCamera1Polaris = nullptr;
    QAction* m_actionConfirmCamera2Polaris = nullptr;
    QAction* m_actionRetryCamera1PolarisSolve = nullptr;
    QAction* m_actionRetryCamera2PolarisSolve = nullptr;
    QAction* m_actionRetryBothPolarisSolve = nullptr;
    QAction* m_actionToggleCoarseAlignment = nullptr;
    QPushButton* m_btnToggleCoarseAlignment = nullptr;
    QPushButton* m_btnConfirmCamera1Polaris = nullptr;
    QPushButton* m_btnConfirmCamera2Polaris = nullptr;
    QPushButton* m_btnRetryCamera1PolarisSolve = nullptr;
    QPushButton* m_btnRetryCamera2PolarisSolve = nullptr;
    QPushButton* m_btnRetryBothPolarisSolve = nullptr;
    QVector<PolarisDetectionPipeline::InitialStarCandidate> m_alignmentCachedCandidates[kCameraCount];
    qint64 m_alignmentLastCandidateDetectionMs[kCameraCount] = {-1, -1};
    double m_alignmentOtsuThreshold[kCameraCount] = {-1.0, -1.0};
    double m_alignmentActualThreshold[kCameraCount] = {-1.0, -1.0};
    CaptureState m_captureState = CaptureState::Idle;
    DetailViewMode m_detailViewMode = DetailViewMode::RoiOnly;
    bool m_commConnected = false;
    bool m_commConnecting = false;
    bool m_connectingCameras = false;
    QString m_statusText = QStringLiteral("状态: 就绪");
    QString m_statusColor = QStringLiteral("#e0e0e0");

    SettingsDialog* m_settingsDialog = nullptr;
    EafFocuserManager* m_focuserManager = nullptr;
    FocuserControlWidget* m_focuserControlWidget = nullptr;
    EnvironmentSensorManager* m_environmentSensor = nullptr;
    EnvironmentSensorData m_latestEnvironment;
    EnvironmentSensorConfig m_environmentSensorConfig;
    QLabel* m_lblStatusState = nullptr;
    QLabel* m_lblStatusROI = nullptr;
    QLabel* m_lblStatusFrames = nullptr;
    QLabel* m_lblFullFrameCam1 = nullptr;
    QLabel* m_lblFullFrameCam2 = nullptr;
    QLabel* m_lblFullFrameThresholdCam1 = nullptr;
    QLabel* m_lblFullFrameThresholdCam2 = nullptr;
    QLabel* m_lblRoiThresholdCam1 = nullptr;
    QLabel* m_lblRoiThresholdCam2 = nullptr;
    QLabel* m_lblAlignmentSolveCam1 = nullptr;
    QLabel* m_lblAlignmentSolveCam2 = nullptr;

    CameraManager* m_cameraManager = nullptr;
    ImageProcessor* m_imageProcessor = nullptr;
    FullFrameCanvas* m_fullFrameCanvas1 = nullptr;
    FullFrameCanvas* m_fullFrameCanvas2 = nullptr;
    RoiStarCanvas* m_cam1RoiCanvas = nullptr;
    RoiStarCanvas* m_cam2RoiCanvas = nullptr;
    ChartWidget* m_r0Chart = nullptr;
    ChartWidget* m_seeingChart = nullptr;

    QString m_dataPath = "D:/C-DIMM/data";
    int m_saveInterval = 1;
    bool m_parameterValidationEnabled = false;
    bool m_syncDiagnosticLoggingEnabled = false;
    int m_resultRowsSeen = 0;
    ResultWriter m_resultWriter;
    ResultWriter m_detailResultWriter;
    ResultWriter m_syncDiagnosticWriter;
    QString m_resultFilePath;
    QString m_detailResultFilePath;
    QString m_syncDiagnosticFilePath;
    CaptureState m_resultFileState = CaptureState::Idle;
    QTimer* m_fileFlushTimer = nullptr;
    quint64 m_diagnosticLastCapturedFrameId[2] = {0, 0};
    quint64 m_diagnosticCapturedPacketCount[2] = {0, 0};
    quint64 m_diagnosticCapturedPacketGapCount[2] = {0, 0};
    quint64 m_diagnosticLastLiveFrameId[2] = {0, 0};
    quint64 m_diagnosticLivePacketCount[2] = {0, 0};
    quint64 m_diagnosticLivePacketGapCount[2] = {0, 0};

    CaptureRuntimeContext m_liveRuntime;
    CaptureRuntimeContext m_simulationRuntime;
    double m_configExposureUs = 1000.0;
    double m_cameraExposureUs[2] = {1000.0, 1000.0};
    double m_configGainDb = 10.0;
    double m_configContinuousFrameRateHz = 200.0;
    double m_lastContinuousFrameRateReadback[2] = {0.0, 0.0};
    qint64 m_liveFrameAcceptAfterMs = -1;
    quint64 m_lastAcceptedLiveFrameId[2] = {0, 0};
    qint64 m_lastAcceptedLiveFrameMs[2] = {-1, -1};
    qint64 m_lastAcceptedContinuousFrameMs[2] = {-1, -1};
    quint64 m_liveAcquisitionGeneration = 1;
    quint64 m_roiUpdateCount = 0;
    qint64 m_lastRoiUpdateMs = -1;
    QString m_lastRoiUpdateReason;
    int m_configTriggerMode = 0;
    bool m_hotPixelTemplatesEnabled = false;
    QString m_hotPixelCamera0MaskPath;
    QString m_hotPixelCamera0ExcessPath;
    QString m_hotPixelCamera1MaskPath;
    QString m_hotPixelCamera1ExcessPath;
    int m_hotPixelTemplateWidth = 0;
    int m_hotPixelTemplateHeight = 0;
    bool m_liveHardwareRoiActive = false;
    LiveStartupPhase m_liveStartupPhase = LiveStartupPhase::None;
    bool m_liveRoiCapabilitiesValid = false;
    RoiCapability m_liveRoiCapabilities[2];
    double m_roiRecenteringThresholdPx = 16.0;
    int m_roiRecenteringRequiredFrames = 5;
    qint64 m_roiRecenteringCooldownMs = 3000;
    double m_roiRecenteringMinimumShiftPx = 8.0;
    bool m_pulseGeneratorEnabled = false;
    QString m_pulseGeneratorPort = QStringLiteral("COM9");
    int m_pulseGeneratorBaudRate = 19200;
    int m_pulseGeneratorTerminalId = 1;
    double m_pulseGeneratorFrequencyHz = 200.0;
    quint32 m_pulseGeneratorPulseCount = 2000000U;
    double m_pulseGeneratorDutyPercent = 50.0;
    bool m_pulseGeneratorRemoteControl = true;

    AutoAcquisitionConfig m_autoAcquisitionConfig;
    AutoAcquisitionRecoveryController m_autoAcquisitionRecovery;
    StableCandidateTracker m_stableCandidateTrackers[kCameraCount];
    PolarisTrajectory::RoiTrajectoryAccumulator m_actualRoiTracks[kCameraCount];
    bool m_autoAcquisitionCommandInProgress = false;
    bool m_autoAcquisitionStartedCurrentRun = false;
    QString m_autoAcquisitionActiveWindowId;
    QString m_autoAcquisitionSuppressedWindowId;
    qint64 m_lastAutoAcquisitionAttemptMs = -1;
    QString m_lastAutoAcquisitionStatusKey;
    qint64 m_lastAutoAcquisitionStatusMs = -1;

    LiveStartupOrigin m_liveStartupOrigin =
        LiveStartupOrigin::Manual;

    bool m_liveStartupConfirmed = false;
    bool m_liveStartupRecoveryInProgress = false;
    bool m_pulseBoardResponseTimedOut = false;
    HardwareTriggerStartupStage
        m_hardwareTriggerStartupStage =
            HardwareTriggerStartupStage::None;

    quint64
        m_hardwareTriggerStageBaselineFrameCount[2] =
            {0, 0};

    bool
        m_hardwareTriggerStageFrameSeen[2] =
            {false, false};

    bool m_internalLiveStartupRetry = false;
    qint64 m_lastPulseBoardTimeoutStatusMs = -1;
    static constexpr int kPulseBoardTimeoutStatusThrottleMs = 10000;

    int m_liveStartupRetryCount = 0;
    QString m_liveStartupWindowId;

    QTimer* m_liveStartupRetryTimer = nullptr;

    static constexpr int kHardwareTriggerFirstFrameTimeoutMs = 5000;
    static constexpr int kRoiTrackingFirstFrameTimeoutMs = 3000;
    static constexpr int kLiveStartupRetryDelayMs = 3000;
    static constexpr int kLiveStartupMaxImmediateRetries = 3;

    AutoExposureConfig m_autoExposureConfig;
    AutoExposureController m_autoExposureController;
    AutoExposureTrendSnapshot m_latestAutoExposureTrend;
    AutoExposureState m_autoExposureState = AutoExposureState::Normal;
    AutoExposureState m_cameraAutoExposureState[2] = {AutoExposureState::Normal, AutoExposureState::Normal};
    QString m_autoExposureReason;
    QString m_cameraAutoExposureReason[2];
    quint64 m_autoExposureSequenceId = 0;
    int m_autoExposureTargetExposureUs = 0;
    int m_cameraAutoExposureTargetExposureUs[2] = {0, 0};
    qint64 m_lastAutoExposureAdjustMs = -1;
    quint64 m_autoExposureFramesSinceAdjust = 0;
    bool m_autoExposureAdjustmentSessionActive = true;
    qint64 m_autoExposureCooldownRemainingMs = 0;
    static constexpr int kAutoExposureRoiRelocalizationGraceMs = 3000;
    double m_latestAutoExposurePeakDn[2] = {0.0, 0.0};
    double m_latestAutoExposureSnr[2] = {0.0, 0.0};
    double m_latestAutoExposureValidRatio[2] = {0.0, 0.0};
    double m_latestAutoExposureUsableRatio[2] = {0.0, 0.0};
    mutable QVector<int> m_cachedHotPixelTemplateExposures;
    mutable qint64 m_cachedHotPixelTemplateScanMs = -1;
    int m_hotPixelTemplateExposureUs[2] = {1000, 1000};
    bool m_alignmentAutoRadius = true;
    bool m_alignmentAutoSolveEnabled = true;
    bool m_alignmentShowMatchedCatalogStars = true;
    double m_alignmentFocalLengthMm = 269.0;
    double m_alignmentPixelSizeUm = 2.5;
    double m_alignmentPolarisPolarDistanceArcmin = 37.6;
    double m_alignmentRadiusAdjustPx = 0.0;
    double m_alignmentPreviewRateHz = 1.0;
    int m_alignmentMaxDetectedStars = 20;
    int m_alignmentMinMatchedStars = 5;
    double m_alignmentMaxRmsPx = 3.0;
    int m_alignmentRetryIntervalMs = 3000;
    double m_alignmentMinMatchedSpatialSpreadPx = 50.0;
    double m_alignmentMinPolarisSnr = 5.0;
    bool m_alignmentAllowSaturatedPolarisConfirmation = false;
    PolarisSolverController* m_polarisSolverController = nullptr;
    AlignmentSession m_alignmentSession;
    bool m_alignmentCoarseActive = false;
    qint64 m_alignmentLastCoarseSubmitMs[kCameraCount] = {-1, -1};
    int m_alignmentCoarseSubmitIntervalMs = 1000;
    CoarseAlignmentEstimate m_alignmentCoarseEstimates[kCameraCount];
    AlignmentCoarseController* m_alignmentCoarseController = nullptr;

    QTimer* m_1hzTimer = nullptr;
    QTimer* m_hardwareTriggerStartupTimer = nullptr;

    CommManager* m_commManager = nullptr;
    PulseGeneratorManager* m_pulseGenerator = nullptr;
    QTimer* m_reportTimer = nullptr;
    bool m_reporting = false;
    uint32_t m_startTimeMs = 0;
};
