#pragma once
#include "AppConfig.h"

#include <QDialog>
#include <QString>
#include <QtGlobal>
#include <functional>
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QTabWidget;
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
    void addSettingsPage(QWidget* page, const QString& title);
    void setPulseGeneratorState(bool enabled,
                                const QString& portName,
                                int baudRate,
                                int terminalId,
                                double frequencyHz,
                                quint32 pulseCount,
                                double dutyPercent,
                                bool remoteControl);
    void setCommittedConfig(const AppConfig& config);

    std::function<void(double exposure, double gain, double continuousFrameRateHz)> onApplyCamera;
    std::function<void(const AutoExposureConfig& config)> onApplyAutoExposure;
    std::function<void(int backgroundKernelSize,
                       double backgroundSigmaMultiplier,
                       int centroidMode,
                       int peakKernelRadiusPx,
                       double strongHotPixelExcessDn,
                       int r0HistoryWindowFrames)> onApplyProcessing;
    std::function<void(double thresholdPx,
                       int requiredFrames,
                       qint64 cooldownMs,
                       double minimumShiftPx)> onApplyRoiRecentering;
    std::function<void(double sigmaThreshold,
                       double peakFraction,
                       int minArea,
                       int maxArea,
                       int connectivity)> onApplyFullFrameStarDetection;
    std::function<void(bool enabled,
                       QString camera0MaskPath,
                       QString camera0ExcessPath,
                       QString camera1MaskPath,
                       QString camera1ExcessPath,
                       int templateWidth,
                       int templateHeight)> onApplyHotPixelTemplates;
    std::function<void(double apertureDiameterMm,
                       double baselineSeparationMm,
                       double baselineAngleDeg,
                       double focalLengthCm,
                       double zenithAngleDeg,
                       double lambdaNm,
                       double pixelSizeUm)> onApplyOptics;
    std::function<void(bool autoRadius,
                       double focalLengthMm,
                       double pixelSizeUm,
                       double polarDistanceArcmin,
                       double radiusAdjustPx,
                       double previewRateHz)> onApplyAlignment;
    std::function<void(bool enabled,
                       bool showMatchedCatalogStars,
                       int maxDetectedStars,
                       int minMatchedStars,
                       double maxRmsPx,
                       int retryIntervalMs,
                       double minMatchedSpatialSpreadPx,
                       double minPolarisSnr,
                       bool allowSaturatedPolarisConfirmation)> onApplyPolarisSolver;
    std::function<void(QString path,
                       int interval,
                       bool parameterValidationEnabled,
                       bool syncDiagnosticLoggingEnabled)> onApplyStorage;
    std::function<void(int mode)> onApplyTriggerMode;
    std::function<void(const EnvironmentSensorConfig& config)> onApplyEnvironmentSensor;
    std::function<void(const AutoAcquisitionConfig& config)> onApplyAutoAcquisition;
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
    std::function<void(const AppConfig& config, const ConfigChangeSet& changes)> onAfterApply;

    QLineEdit* exposureEdit = nullptr;
    QLineEdit* gainEdit = nullptr;
    QLineEdit* continuousFrameRateEdit = nullptr;
    QCheckBox* autoExposureCheck = nullptr;
    QCheckBox* autoExpTrendConflictCheck = nullptr;
    QLineEdit* autoExpTargetPeakLowEdit = nullptr;
    QLineEdit* autoExpTargetPeakHighEdit = nullptr;
    QLineEdit* autoExpExposureHysteresisEdit = nullptr;
    QLineEdit* autoExpDarkAdjustmentTargetEdit = nullptr;
    QLineEdit* autoExpBrightAdjustmentTargetEdit = nullptr;
    QLineEdit* autoExpHardSaturationEdit = nullptr;
    QLineEdit* autoExpSaturatedPixelCountEdit = nullptr;
    QLineEdit* autoExpDarkSnrWarningEdit = nullptr;
    QLineEdit* autoExpDarkSnrCriticalEdit = nullptr;
    QLineEdit* autoExpMinValidCentroidRatioEdit = nullptr;
    QLineEdit* autoExpStarLostValidRatioEdit = nullptr;
    QLineEdit* autoExpBrightFrameRatioEdit = nullptr;
    QLineEdit* autoExpDarkFrameRatioEdit = nullptr;
    QLineEdit* autoExpStableFrameRatioEdit = nullptr;
    QLineEdit* autoExpHardSaturationFrameRatioEdit = nullptr;
    QLineEdit* autoExpSampleWindowSecEdit = nullptr;
    QLineEdit* autoExpSampleIntervalMsEdit = nullptr;
    QLineEdit* autoExpMinDecisionSampleCountEdit = nullptr;
    QLineEdit* autoExpStepUsEdit = nullptr;
    QLineEdit* autoExpInitialExposureUsEdit = nullptr;
    QLineEdit* autoExpDecisionCooldownMinEdit = nullptr;
    QLineEdit* autoExpTrendConflictPersistenceSecEdit = nullptr;
    QLineEdit* autoExpMinEdit = nullptr;
    QLineEdit* autoExpMaxEdit = nullptr;
    QLineEdit* autoExpMaxChangeUpEdit = nullptr;
    QLineEdit* autoExpMaxChangeDownEdit = nullptr;
    QLineEdit* autoExpCameraAgreementRatioEdit = nullptr;
    QLineEdit* autoExpPeakSupportRadiusEdit = nullptr;
    QLineEdit* autoExpPeakSupportFractionEdit = nullptr;
    QLineEdit* autoExpMinPeakSupportPixelsEdit = nullptr;
    QLineEdit* autoExpMinNeighborPeakRatioEdit = nullptr;
    QLineEdit* autoExpMaxPeakCandidateCountEdit = nullptr;
    QLineEdit* autoExpSupportedPeakPercentileEdit = nullptr;
    QLineEdit* autoExpExposureSettleMsEdit = nullptr;
    QLineEdit* autoExpMinExposureDeltaEdit = nullptr;
    QLineEdit* autoExpMinExposureChangeRatioEdit = nullptr;
    QLineEdit* storagePathEdit = nullptr;
    QLineEdit* saveIntervalEdit = nullptr;
    QCheckBox* parameterValidationCheck = nullptr;
    QCheckBox* syncDiagnosticLogCheck = nullptr;
    QRadioButton* triggerContinuous = nullptr;
    QRadioButton* triggerHardware = nullptr;
    QCheckBox* envSensorEnableCheck = nullptr;
    QLineEdit* envSensorPortEdit = nullptr;
    QComboBox* envSensorBaudCombo = nullptr;
    QLineEdit* envSensorAddressEdit = nullptr;
    QLineEdit* envSensorPollIntervalEdit = nullptr;
    QComboBox* centroidModeCombo = nullptr;
    QLineEdit* procKernelSize = nullptr;
    QLineEdit* procSigma = nullptr;
    QLineEdit* peakKernelRadiusEdit = nullptr;
    QLineEdit* strongHotPixelExcessEdit = nullptr;
    QLineEdit* r0HistoryWindowFramesEdit = nullptr;
    QLineEdit* roiRecenterThresholdEdit = nullptr;
    QLineEdit* roiRecenterRequiredFramesEdit = nullptr;
    QLineEdit* roiRecenterCooldownMsEdit = nullptr;
    QLineEdit* roiRecenterMinimumShiftEdit = nullptr;
    QLineEdit* starSigmaThresholdEdit = nullptr;
    QLineEdit* starPeakFractionEdit = nullptr;
    QLineEdit* starMinAreaEdit = nullptr;
    QLineEdit* starMaxAreaEdit = nullptr;
    QComboBox* starConnectivityCombo = nullptr;
    QCheckBox* hotPixelEnableCheck = nullptr;
    QLineEdit* hotPixelCam0MaskEdit = nullptr;
    QLineEdit* hotPixelCam0ExcessEdit = nullptr;
    QLineEdit* hotPixelCam1MaskEdit = nullptr;
    QLineEdit* hotPixelCam1ExcessEdit = nullptr;
    QLineEdit* hotPixelTemplateWidthEdit = nullptr;
    QLineEdit* hotPixelTemplateHeightEdit = nullptr;
    QLineEdit* opticsD = nullptr;
    QLineEdit* opticsBaseline = nullptr;
    QLineEdit* opticsBaselineAngle = nullptr;
    QLineEdit* opticsF = nullptr;
    QLineEdit* opticsZenith = nullptr;
    QLineEdit* detectorPixelSize = nullptr;
    QLineEdit* detectorWavelength = nullptr;
    QCheckBox* alignmentAutoRadiusCheck = nullptr;
    QCheckBox* alignmentAutoSolveCheck = nullptr;
    QCheckBox* alignmentShowMatchedCatalogStarsCheck = nullptr;
    QLineEdit* alignmentFocalLengthEdit = nullptr;
    QLineEdit* alignmentPixelSizeEdit = nullptr;
    QLineEdit* alignmentPolarDistanceEdit = nullptr;
    QLineEdit* alignmentRadiusAdjustEdit = nullptr;
    QLineEdit* alignmentPreviewRateEdit = nullptr;
    QLineEdit* alignmentMaxDetectedStarsEdit = nullptr;
    QLineEdit* alignmentMinMatchedStarsEdit = nullptr;
    QLineEdit* alignmentMaxRmsEdit = nullptr;
    QLineEdit* alignmentRetryIntervalEdit = nullptr;
    QLineEdit* alignmentMinSpatialSpreadEdit = nullptr;
    QLineEdit* alignmentMinPolarisSnrEdit = nullptr;
    QCheckBox* alignmentAllowSaturatedPolarisCheck = nullptr;
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
    QCheckBox* autoAcquisitionEnableCheck = nullptr;
    QLineEdit* autoAcquisitionLatitudeEdit = nullptr;
    QLineEdit* autoAcquisitionLongitudeEdit = nullptr;
    QLineEdit* autoAcquisitionStartOffsetEdit = nullptr;
    QLineEdit* autoAcquisitionStopOffsetEdit = nullptr;
    QLineEdit* autoAcquisitionRecoveryScanIntervalEdit = nullptr;
    QCheckBox* autoAcquisitionTestOverrideCheck = nullptr;
    QLineEdit* autoAcquisitionTestStartEdit = nullptr;
    QLineEdit* autoAcquisitionTestStopEdit = nullptr;
    QLabel* autoAcquisitionNextStartLabel = nullptr;
    QLabel* autoAcquisitionNextStopLabel = nullptr;

    // Keep new public data members appended because several translation units
    // access SettingsDialog fields directly.
    std::function<bool(QString portName,
                       int baudRate,
                       int terminalId,
                       bool remoteControl,
                       QString* errorMessage)> onSetPulseControlSource;
    QPushButton* pulseApplyConfigBtn = nullptr;

private:
    void updateApplyStatus(const QString& text, const QString& color);
    void updateApplyStatus(const QString& text, UiStatusLevel level);
    bool applyCommittedPulseSettings(bool requireEnabledPort = false);
    bool applySettings();

    QTabWidget* m_tabWidget = nullptr;
    double m_committedPulseFrequencyHz = 200.0;
    quint32 m_committedPulseCount = 2000000U;
    double m_committedPulseDutyPercent = 50.0;
    bool m_committedPulseRemoteControl = true;
    AppConfig m_committedConfig;
    bool m_hasCommittedConfig = false;
};
