#pragma once

#include "CameraManager.h"
#include "ImageProcessor.h"
#include "ui_DIMM.h"

#include <QDialog>
#include <QFile>
#include <QMainWindow>
#include <QSize>
#include <QTextStream>
#include <QTimer>
#include <QVector>
#include <functional>

#include <opencv2/opencv.hpp>

class FullFrameCanvas;
class RoiStarCanvas;
class ChartWidget;
class CommManager;
class PulseGeneratorManager;

class QLineEdit;
class QRadioButton;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QAction;

enum class UiStatusLevel {
    Muted,
    Info,
    Success,
    Warning,
    Error
};

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);
    void setPulseGeneratorState(bool enabled,
                                const QString& portName,
                                int baudRate,
                                int terminalId,
                                double frequencyHz,
                                quint32 pulseCount,
                                double dutyPercent,
                                bool remoteControl);

    std::function<void(double exposure, double gain, double continuousFrameRateHz)> onApplyCamera;
    std::function<void(bool enabled,
                       double lowThreshold,
                       double highThreshold,
                       double darkRatio,
                       double brightRatio,
                       double minExposure,
                       double maxExposure)> onApplyAutoExposure;
    std::function<void(int kernelSize, double sigma, int method)> onApplyProcessing;
    std::function<void(double thresholdPx,
                       int requiredFrames,
                       qint64 cooldownMs,
                       double minimumShiftPx)> onApplyRoiRecentering;
    std::function<void(double thresholdAbsolute,
                       double sigmaThreshold,
                       double peakFraction,
                       double minimumIntensity,
                       int minArea,
                       int maxArea)> onApplyFullFrameStarDetection;
    std::function<void(bool enabled,
                       QString camera0MaskPath,
                       QString camera0ExcessPath,
                       QString camera1MaskPath,
                       QString camera1ExcessPath,
                       int templateWidth,
                       int templateHeight)> onApplyHotPixelTemplates;
    std::function<void(double apertureDiameterMm,
                       double baselineSeparationMm,
                       double focalLengthMm,
                       double zenithAngleDeg,
                       double lambdaNm,
                       double pixelSizeUm)> onApplyOptics;
    std::function<void(bool autoRadius,
                       double focalLengthMm,
                       double pixelSizeUm,
                       double polarDistanceArcmin,
                       double radiusAdjustPx,
                       double previewRateHz)> onApplyAlignment;
    std::function<void(QString path, int interval)> onApplyStorage;
    std::function<void(int mode)> onApplyTriggerMode;
    std::function<bool(bool enabled,
                       QString portName,
                       int baudRate,
                       int terminalId,
                       double frequencyHz,
                       quint32 pulseCount,
                       double dutyPercent,
                       bool remoteControl,
                       QString* errorMessage)> onApplyPulseGenerator;
    std::function<bool(QString portName,
                       int baudRate,
                       int terminalId,
                       double frequencyHz,
                       quint32 pulseCount,
                       double dutyPercent,
                       bool remoteControl,
                       QString* errorMessage)> onStartPulseOutput;
    std::function<bool(QString* errorMessage)> onStopPulseOutput;
    std::function<void(QString ip, quint16 port)> onApplyNetwork;
    std::function<void(QString ip, quint16 port)> onConnectNetwork;
    std::function<void()> onAfterApply;

    QLineEdit* exposureEdit = nullptr;
    QLineEdit* gainEdit = nullptr;
    QLineEdit* continuousFrameRateEdit = nullptr;
    QCheckBox* autoExposureCheck = nullptr;
    QLineEdit* autoExpLowEdit = nullptr;
    QLineEdit* autoExpHighEdit = nullptr;
    QLineEdit* autoExpDarkRatioEdit = nullptr;
    QLineEdit* autoExpBrightRatioEdit = nullptr;
    QLineEdit* autoExpMinEdit = nullptr;
    QLineEdit* autoExpMaxEdit = nullptr;
    QLineEdit* storagePathEdit = nullptr;
    QLineEdit* saveIntervalEdit = nullptr;
    QRadioButton* triggerContinuous = nullptr;
    QRadioButton* triggerHardware = nullptr;
    QRadioButton* procGravity = nullptr;
    QRadioButton* procGaussian = nullptr;
    QLineEdit* procKernelSize = nullptr;
    QLineEdit* procSigma = nullptr;
    QLineEdit* roiRecenterThresholdEdit = nullptr;
    QLineEdit* roiRecenterRequiredFramesEdit = nullptr;
    QLineEdit* roiRecenterCooldownMsEdit = nullptr;
    QLineEdit* roiRecenterMinimumShiftEdit = nullptr;
    QLineEdit* starThresholdAbsoluteEdit = nullptr;
    QLineEdit* starSigmaThresholdEdit = nullptr;
    QLineEdit* starPeakFractionEdit = nullptr;
    QLineEdit* starMinimumIntensityEdit = nullptr;
    QLineEdit* starMinAreaEdit = nullptr;
    QLineEdit* starMaxAreaEdit = nullptr;
    QCheckBox* hotPixelEnableCheck = nullptr;
    QLineEdit* hotPixelCam0MaskEdit = nullptr;
    QLineEdit* hotPixelCam0ExcessEdit = nullptr;
    QLineEdit* hotPixelCam1MaskEdit = nullptr;
    QLineEdit* hotPixelCam1ExcessEdit = nullptr;
    QLineEdit* hotPixelTemplateWidthEdit = nullptr;
    QLineEdit* hotPixelTemplateHeightEdit = nullptr;
    QLineEdit* opticsD = nullptr;
    QLineEdit* opticsBaseline = nullptr;
    QLineEdit* opticsF = nullptr;
    QLineEdit* opticsZenith = nullptr;
    QLineEdit* detectorPixelSize = nullptr;
    QLineEdit* detectorWavelength = nullptr;
    QCheckBox* alignmentAutoRadiusCheck = nullptr;
    QLineEdit* alignmentFocalLengthEdit = nullptr;
    QLineEdit* alignmentPixelSizeEdit = nullptr;
    QLineEdit* alignmentPolarDistanceEdit = nullptr;
    QLineEdit* alignmentRadiusAdjustEdit = nullptr;
    QLineEdit* alignmentPreviewRateEdit = nullptr;
    QLineEdit* netIpEdit = nullptr;
    QLineEdit* netPortEdit = nullptr;
    QPushButton* netConnectBtn = nullptr;
    QLabel* netStatusLabel = nullptr;
    QLabel* applyStatusLabel = nullptr;
    QCheckBox* pulseEnableCheck = nullptr;
    QLineEdit* pulsePortEdit = nullptr;
    QComboBox* pulseBaudCombo = nullptr;
    QLineEdit* pulseTerminalEdit = nullptr;
    QLineEdit* pulseFreqEdit = nullptr;
    QLineEdit* pulseCountEdit = nullptr;
    QLineEdit* pulseDutyEdit = nullptr;
    QRadioButton* pulseSourceLocal = nullptr;
    QRadioButton* pulseSourceRemote = nullptr;
    QPushButton* pulseApplyFreqBtn = nullptr;
    QPushButton* pulseApplyCountBtn = nullptr;
    QPushButton* pulseApplyDutyBtn = nullptr;
    QPushButton* pulseApplySourceBtn = nullptr;
    QPushButton* pulseStartBtn = nullptr;
    QPushButton* pulseStopBtn = nullptr;

private:
    void updateApplyStatus(const QString& text, const QString& color);
    void updateApplyStatus(const QString& text, UiStatusLevel level);
    bool applyCommittedPulseSettings(bool requireEnabledPort = false);
    bool applySettings();

    double m_committedPulseFrequencyHz = 200.0;
    quint32 m_committedPulseCount = 2000000U;
    double m_committedPulseDutyPercent = 50.0;
    bool m_committedPulseRemoteControl = true;
};

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

private slots:
    void onStartCapture();
    void onStartSimulation();
    void onStopCapture();
    void onShowMainPage();
    void onShowRoiPage();
    void onShowSettings();
    void onToggleAlignmentMode();
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
    struct CaptureRuntimeContext {
        int frameCount = 0;
        int frameCountPerCamera[2] = {0, 0};
        quint64 processedFrameCount = 0;
        quint64 processedFrameCountPerCamera[2] = {0, 0};
        quint64 validCentroidCount = 0;
        quint64 validCentroidCountPerCamera[2] = {0, 0};
        quint64 detectedCentroidCount = 0;
        quint64 detectedCentroidCountPerCamera[2] = {0, 0};
        quint64 usableCentroidCount = 0;
        quint64 usableCentroidCountPerCamera[2] = {0, 0};
        quint64 qualityRejectedCountPerCamera[2] = {0, 0};
        quint64 edgeRejectedCountPerCamera[2] = {0, 0};
        quint64 roiInvalidRejectedCountPerCamera[2] = {0, 0};
        quint64 noCentroidRejectedCountPerCamera[2] = {0, 0};
        double latestProcessingLatencyMs = 0.0;
        double averageProcessingLatencyMs = 0.0;
        quint64 syncSampleCount = 0;
        quint64 syncOffsetSampleCount = 0;
        quint64 syncJitterSampleCount = 0;
        double latestSyncDeltaRawUs = 0.0;
        double syncOffsetUs = 0.0;
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
        double centroidX[2] = {0.0, 0.0};
        double centroidY[2] = {0.0, 0.0};
        double peakBrightness[2] = {0.0, 0.0};
        bool hasValidCentroid[2] = {false, false};
        int lostCentroidFrameCount[2] = {0, 0};
        qint64 lostCentroidSinceMs[2] = {-1, -1};
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
        int selectedInitialCandidateIndex[2] = {-1, -1};
        bool pendingInitialCandidateSelectionRequired[2] = {false, false};
        qint64 lastInitialCandidatePromptMs[2] = {-1, -1};
        qint64 liveRelocalizationStartedMs = -1;
        cv::Mat liveRelocalizationPreviewFrame[2];
    };

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
    QString captureModeName() const;
    QString captureModeLabel() const;
    QString resultSubdirectoryName() const;
    CaptureRuntimeContext& activeRuntime();
    const CaptureRuntimeContext& activeRuntime() const;
    CaptureRuntimeContext& runtimeForState(CaptureState state);
    const CaptureRuntimeContext& runtimeForState(CaptureState state) const;
    bool hasValidCentroidsForRoiUpdate() const;
    bool isCentroidNearCurrentRoiEdge(int cameraIndex, double x, double y) const;
    bool isCentroidTooFarFromCurrentRoiCenter(int cameraIndex) const;
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
    void recordLiveRoiUpdate(const RoiRect rois[2], const QString& reason);
    void updateMinuteRoi(bool force = false);
    void hideLegacyRoiScheduleUi();
    QString roiRuleDescription() const;
    bool configureLiveCameras(QString* reason = nullptr);
    bool applyContinuousCameraFrameRate(QString* reason = nullptr);
    bool startAlignmentMode(QString* reason = nullptr);
    void stopAlignmentMode();
    void handleAlignmentFramePacket(int cameraIndex, const CameraFrame& packet);
    void updateAlignmentOverlay(int cameraIndex, const cv::Mat& frame);
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
    bool selectLiveRelocalizationCentroid(int cameraIndex,
                                          const cv::Mat& mono8,
                                          QPointF* centroid,
                                          double* peakValue);
    bool maybeSeedRoiFromFrame(int cameraIndex, const cv::Mat& frame);
    void handleLiveRelocalizationWatchdog(qint64 nowMs);
    void updateFullFrameRoiOverlay(int cameraIndex);
    void showDeferredLiveRelocalizationPreview();
    void clearPendingLiveRelocalizationRois();
    bool commitPairedInitialRoisIfReady();
    bool startHardwarePulseStage(double frequencyHz, const QString& stageLabel, QString* reason = nullptr);
    bool startFullFrameLocalizationPulse(QString* reason = nullptr);
    bool switchToRoiTrackingPulse(QString* reason = nullptr);
    void initResultFile();
    void closeResultFile();
    void saveResultRow(int frame);
    void flushPendingWrites();
    bool stopLiveCapture();
    void stopSimulationCapture();
    bool startSimulationCapture();
    cv::Mat buildSimulationFrame(int cameraIndex) const;
    void updateCurrentRoi();
    void on1hzTick();
    void updateCameraInfo();
    void matchRoiTimeSlot();
    void onCommCommand(uint8_t cmd);
    void reportMeasurement();
    void reportDeviceStatus();
    void applyAutoExposure(int cameraIndex, double peakValue);
    QVector<int> scanHotPixelExposureTemplates() const;
    int selectTemplateExposureForPeak(double currentExposure, double peakValue) const;
    bool resolveHotPixelTemplatePathsForExposure(int exposureUs,
                                                 QString* camera0Mask,
                                                 QString* camera0Excess,
                                                 QString* camera1Mask,
                                                 QString* camera1Excess) const;
    bool applyExposureAndHotPixelTemplate(int exposureUs, QString* reason = nullptr);
    bool isSettingsApplyAllowed() const;
    bool canStartLiveCapture(QString* reason = nullptr) const;
    bool canConnectOrDisconnectCameras(QString* reason = nullptr) const;
    void scheduleHardwareTriggerStartupCheck();
    void checkHardwareTriggerStartup();
    void requestAlignmentPolarisSelection(int cameraIndex);

    Ui_DIMM* ui = nullptr;
    QTimer* m_simulationTimer = nullptr;
    QAction* m_actionStartSimulation = nullptr;
    QAction* m_actionAlignmentMode = nullptr;
    QAction* m_actionConfirmCamera1Polaris = nullptr;
    QAction* m_actionConfirmCamera2Polaris = nullptr;
    CaptureState m_captureState = CaptureState::Idle;
    DetailViewMode m_detailViewMode = DetailViewMode::RoiOnly;
    bool m_commConnected = false;
    bool m_commConnecting = false;
    bool m_connectingCameras = false;
    QString m_statusText = QStringLiteral("状态: 就绪");
    QString m_statusColor = QStringLiteral("#e0e0e0");

    SettingsDialog* m_settingsDialog = nullptr;
    QLabel* m_lblStatusState = nullptr;
    QLabel* m_lblStatusROI = nullptr;
    QLabel* m_lblStatusFrames = nullptr;
    QLabel* m_lblFullFrameCam1 = nullptr;
    QLabel* m_lblFullFrameCam2 = nullptr;

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
    int m_resultRowsSeen = 0;
    QFile* m_resultFile = nullptr;
    QTextStream* m_resultStream = nullptr;
    QString m_resultFilePath;
    CaptureState m_resultFileState = CaptureState::Idle;
    QStringList m_pendingWrites;
    QTimer* m_fileFlushTimer = nullptr;

    CaptureRuntimeContext m_liveRuntime;
    CaptureRuntimeContext m_simulationRuntime;
    double m_configExposureUs = 2000.0;
    double m_configGainDb = 10.0;
    double m_configContinuousFrameRateHz = 200.0;
    double m_lastContinuousFrameRateReadback[2] = {0.0, 0.0};
    qint64 m_liveFrameAcceptAfterMs = -1;
    quint64 m_lastAcceptedLiveFrameId[2] = {0, 0};
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
    QString m_pulseGeneratorPort = QStringLiteral("COM6");
    int m_pulseGeneratorBaudRate = 19200;
    int m_pulseGeneratorTerminalId = 1;
    double m_pulseGeneratorFrequencyHz = 200.0;
    quint32 m_pulseGeneratorPulseCount = 2000000U;
    double m_pulseGeneratorDutyPercent = 50.0;
    bool m_pulseGeneratorRemoteControl = true;

    bool m_autoExposureEnabled = false;
    double m_autoExposureLowThreshold = 80.0;
    double m_autoExposureHighThreshold = 220.0;
    double m_autoExposureDarkRatio = 1.2;
    double m_autoExposureBrightRatio = 0.8;
    double m_autoExposureMinUs = 500.0;
    double m_autoExposureMaxUs = 20000.0;
    int m_autoExposureIntervalMs = 4 * 60 * 60 * 1000;
    QVector<double> m_autoExposurePeakSamples[2];
    qint64 m_lastAutoExposureCheckMs = -1;
    int m_hotPixelTemplateExposureUs = 2000;
    bool m_alignmentAutoRadius = true;
    double m_alignmentFocalLengthMm = 269.0;
    double m_alignmentPixelSizeUm = 2.5;
    double m_alignmentPolarisPolarDistanceArcmin = 37.6;
    double m_alignmentRadiusAdjustPx = 0.0;
    double m_alignmentPreviewRateHz = 1.0;
    qint64 m_alignmentLastPreviewMs[2] = {-1, -1};
    bool m_alignmentSelectionRequested[2] = {false, false};

    QTimer* m_1hzTimer = nullptr;
    QTimer* m_hardwareTriggerStartupTimer = nullptr;

    CommManager* m_commManager = nullptr;
    PulseGeneratorManager* m_pulseGenerator = nullptr;
    QTimer* m_reportTimer = nullptr;
    bool m_reporting = false;
    uint32_t m_startTimeMs = 0;
};
