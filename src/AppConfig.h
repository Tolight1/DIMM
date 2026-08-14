#pragma once

#include "ConnectedDomain.h"

#include <QString>
#include <QTime>
#include <QtGlobal>

struct CameraConfig {
    double exposureUs = 1000.0;
    double gainDb = 10.0;
    double continuousFrameRateHz = 200.0;
};

struct AutoExposureConfig {
    bool enabled = false;

    double lowThreshold = 80.0;
    double highThreshold = 220.0;
    double darkRatio = 1.2;
    double brightRatio = 0.8;

    double targetPeakLowDn = 500.0;
    double targetPeakHighDn = 3600.0;
    double exposureHysteresisDn = 300.0;
    double hardSaturationDn = 4090.0;
    int saturatedPixelCount = 1;

    double darkSnrWarning = 8.0;
    double darkSnrCritical = 5.0;
    double minValidCentroidRatio = 0.50;
    double starLostValidRatio = 0.10;
    double brightFrameRatioThreshold = 0.30;
    double darkFrameRatioThreshold = 0.50;
    bool trendConflictEnabled = true;
    double stableFrameRatioThreshold = 0.70;
    int autoExposureSampleIntervalMs = 500;
    int minDecisionSampleCount = 500;
    double autoExposureStepUs = 200.0;
    double initialExposureUs = 4000.0;
    int autoExposureDecisionCooldownMin = 30;
    double hardSaturationFrameRatioThreshold = 0.05;
    int peakSupportRadiusPx = 2;
    double peakSupportFraction = 0.50;
    int minPeakSupportPixelCount = 3;
    double minNeighborPeakRatio = 0.35;
    int maxPeakCandidateCount = 8;
    double supportedPeakPercentile = 0.95;
    int exposureSettleMs = 750;
    double minExposureDeltaUs = 10.0;
    double minExposureChangeRatio = 0.02;

    int sampleWindowSec = 10;
    int trendConflictPersistenceSec = 30;

    double minExposureUs = 500.0;
    double maxExposureUs = 20000.0;
    double maxExposureChangeRatioUp = 1.30;
    double maxExposureChangeRatioDown = 0.70;
    double cameraAgreementRatio = 0.50;
};

struct ProcessingConfig {
    int backgroundKernelSize = 5;
    double backgroundSigmaMultiplier = 4.0;
    int centroidMode = 1;
    int peakKernelRadiusPx = 3;
    double strongHotPixelExcessDn = 100.0;
    int r0HistoryWindowFrames = 5000;
};

struct RoiRecenteringConfig {
    double thresholdPx = 16.0;
    int requiredFrames = 5;
    qint64 cooldownMs = 3000;
    double minimumShiftPx = 8.0;
};

struct StarDetectionConfig {
    double sigmaThreshold = 4.0;
    double peakFraction = 0.20;
    int minArea = ConnectedDomain::kMinimumComponentArea;
    int maxArea = 1000;
    int connectivity = ConnectedDomain::kDefaultConnectivity;
};

struct HotPixelConfig {
    bool enabled = false;
    QString camera0MaskPath;
    QString camera0ExcessPath;
    QString camera1MaskPath;
    QString camera1ExcessPath;
    int templateWidth = 0;
    int templateHeight = 0;
};

struct OpticalConfig {
    double apertureDiameterMm = 100.0;
    double baselineSeparationMm = 200.0;
    double baselineAngleDeg = 0.0;
    double focalLengthCm = 269.0;
    double zenithAngleDeg = 0.0;
    double wavelengthNm = 500.0;
    double pixelSizeUm = 2.5;
};

struct AlignmentConfig {
    bool autoRadius = true;
    double focalLengthMm = 269.0;
    double pixelSizeUm = 2.5;
    double polarDistanceArcmin = 37.6;
    double radiusAdjustPx = 0.0;
    double previewRateHz = 1.0;
};

struct PolarisSolverSettingsConfig {
    bool enabled = true;
    bool showMatchedCatalogStars = true;
    int maxDetectedStars = 20;
    int minMatchedStars = 5;
    double maxRmsPx = 3.0;
    int retryIntervalMs = 3000;
    double minMatchedSpatialSpreadPx = 50.0;
    double minPolarisSnr = 5.0;
    bool allowSaturatedPolarisConfirmation = false;
};

struct StorageConfig {
    QString path;
    int interval = 1;
    bool parameterValidationEnabled = false;
    bool syncDiagnosticLoggingEnabled = false;
};

struct TriggerConfig {
    int mode = 0;
};

struct EnvironmentSensorConfig {
    bool enabled = true;
    QString portName = QStringLiteral("COM6");
    int baudRate = 9600;
    int dataBits = 8;
    int stopBits = 1;
    int readTimeoutMs = 500;
    int writeTimeoutMs = 500;
    int pollIntervalMs = 1000;
    int deviceAddress = 1;
};

struct PulseGeneratorConfig {
    bool enabled = false;
    QString portName;
    int baudRate = 19200;
    int terminalId = 1;
    double frequencyHz = 200.0;
    quint32 pulseCount = 2000000U;
    double dutyPercent = 50.0;
    bool remoteControl = true;
};

struct AutoAcquisitionConfig {
    bool enabled = false;
    double latitudeDeg = 40.45;
    double longitudeDeg = 116.86;
    int startOffsetMinutesAfterSunset = 30;
    int stopOffsetMinutesBeforeSunrise = 30;
    int recoveryScanIntervalMinutes = 20;
    bool testTimeOverrideEnabled = false;
    QTime testStartTime = QTime(18, 30);
    QTime testStopTime = QTime(6, 0);
};

struct NetworkConfig {
    QString ip;
    quint16 port = 0;
};

struct AppConfig {
    CameraConfig camera;
    AutoExposureConfig autoExposure;
    ProcessingConfig processing;
    RoiRecenteringConfig roiRecentering;
    StarDetectionConfig starDetection;
    HotPixelConfig hotPixel;
    OpticalConfig optical;
    AlignmentConfig alignment;
    PolarisSolverSettingsConfig polarisSolver;
    StorageConfig storage;
    TriggerConfig trigger;
    EnvironmentSensorConfig environmentSensor;
    PulseGeneratorConfig pulseGenerator;
    AutoAcquisitionConfig autoAcquisition;
    NetworkConfig network;
};

struct ConfigChangeSet {
    bool camera = false;
    bool autoExposure = false;
    bool trigger = false;
    bool processing = false;
    bool roiRecentering = false;
    bool fullFrameStarDetection = false;
    bool hotPixel = false;
    bool optics = false;
    bool alignment = false;
    bool polarisSolver = false;
    bool storage = false;
    bool environmentSensor = false;
    bool pulseGenerator = false;
    bool autoAcquisition = false;
    bool network = false;

    bool any() const
    {
        return camera ||
               autoExposure ||
               trigger ||
               processing ||
               roiRecentering ||
               fullFrameStarDetection ||
               hotPixel ||
               optics ||
               alignment ||
               polarisSolver ||
               storage ||
               environmentSensor ||
               pulseGenerator ||
               autoAcquisition ||
               network;
    }

    static ConfigChangeSet all()
    {
        ConfigChangeSet changes;
        changes.camera = true;
        changes.autoExposure = true;
        changes.trigger = true;
        changes.processing = true;
        changes.roiRecentering = true;
        changes.fullFrameStarDetection = true;
        changes.hotPixel = true;
        changes.optics = true;
        changes.alignment = true;
        changes.polarisSolver = true;
        changes.storage = true;
        changes.environmentSensor = true;
        changes.pulseGenerator = true;
        changes.autoAcquisition = true;
        changes.network = true;
        return changes;
    }
};
