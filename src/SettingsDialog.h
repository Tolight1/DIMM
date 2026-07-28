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

    std::function<void(double exposure, double gain, double continuousFrameRateHz)> onApplyCamera;
    std::function<void(const AutoExposureConfig& config)> onApplyAutoExposure;
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
    QCheckBox* autoExpUseFittedPeakCheck = nullptr;
    QLineEdit* autoExpTargetPeakLowEdit = nullptr;
    QLineEdit* autoExpTargetPeakHighEdit = nullptr;
    QLineEdit* autoExpNearSaturationEdit = nullptr;
    QLineEdit* autoExpHardSaturationEdit = nullptr;
    QLineEdit* autoExpSaturatedPixelCountEdit = nullptr;
    QLineEdit* autoExpDarkSnrWarningEdit = nullptr;
    QLineEdit* autoExpDarkSnrCriticalEdit = nullptr;
    QLineEdit* autoExpMinValidCentroidRatioEdit = nullptr;
    QLineEdit* autoExpStarLostValidRatioEdit = nullptr;
    QLineEdit* autoExpBrightFrameRatioEdit = nullptr;
    QLineEdit* autoExpDarkFrameRatioEdit = nullptr;
    QLineEdit* autoExpSampleWindowSecEdit = nullptr;
    QLineEdit* autoExpBrightPersistenceSecEdit = nullptr;
    QLineEdit* autoExpDarkPersistenceSecEdit = nullptr;
    QLineEdit* autoExpStarLostPersistenceSecEdit = nullptr;
    QLineEdit* autoExpTrendConflictPersistenceSecEdit = nullptr;
    QLineEdit* autoExpSafePersistenceSecEdit = nullptr;
    QLineEdit* autoExpCooldownSecEdit = nullptr;
    QLineEdit* autoExpMinEdit = nullptr;
    QLineEdit* autoExpMaxEdit = nullptr;
    QLineEdit* autoExpMaxTemplateStepEdit = nullptr;
    QLineEdit* autoExpMaxChangeUpEdit = nullptr;
    QLineEdit* autoExpMaxChangeDownEdit = nullptr;
    QLineEdit* autoExpCameraAgreementRatioEdit = nullptr;
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
};
