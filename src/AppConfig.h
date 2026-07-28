#pragma once

#include <QString>
#include <QtGlobal>

struct CameraConfig {
    double exposureUs = 1000.0;
    double gainDb = 10.0;
    double continuousFrameRateHz = 200.0;
};

struct AutoExposureConfig {
    bool enabled = false;
    bool useFittedPeak = false;

    double lowThreshold = 80.0;
    double highThreshold = 220.0;
    double darkRatio = 1.2;
    double brightRatio = 0.8;

    double targetPeakLowDn = 3200.0;
    double targetPeakHighDn = 3600.0;
    double nearSaturationDn = 3800.0;
    double hardSaturationDn = 4090.0;
    int saturatedPixelCount = 1;

    double darkSnrWarning = 8.0;
    double darkSnrCritical = 5.0;
    double minValidCentroidRatio = 0.50;
    double starLostValidRatio = 0.10;
    double brightFrameRatioThreshold = 0.30;
    double darkFrameRatioThreshold = 0.50;

    int sampleWindowSec = 60;
    int brightPersistenceSec = 5;
    int darkPersistenceSec = 60;
    int starLostPersistenceSec = 120;
    int trendConflictPersistenceSec = 30;
    int safePersistenceSec = 60;
    int cooldownSec = 180;

    double minExposureUs = 500.0;
    double maxExposureUs = 20000.0;
    int maxTemplateStepPerAdjust = 1;
    double maxExposureChangeRatioUp = 1.30;
    double maxExposureChangeRatioDown = 0.70;
    double cameraAgreementRatio = 0.50;
};

struct ProcessingConfig {
    int kernelSize = 3;
    double sigma = 1.0;
    int method = 1;
};

struct RoiRecenteringConfig {
    double thresholdPx = 16.0;
    int requiredFrames = 5;
    qint64 cooldownMs = 3000;
    double minimumShiftPx = 8.0;
};

struct StarDetectionConfig {
    double thresholdAbsolute = -1.0;
    double sigmaThreshold = 4.0;
    double peakFraction = 0.20;
    double minimumIntensity = 16.0;
    int minArea = 1;
    int maxArea = 1000;
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
};

struct TriggerConfig {
    int mode = 0;
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
    PulseGeneratorConfig pulseGenerator;
    NetworkConfig network;
};
