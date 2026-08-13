#pragma once

#include "AppConfig.h"

#include <functional>

struct ConfigApplicationCallbacks {
    std::function<void(double exposure, double gain, double continuousFrameRateHz)> applyCamera;
    std::function<void(const AutoExposureConfig& config)> applyAutoExposure;
    std::function<void(int mode)> applyTriggerMode;
    std::function<void(int backgroundKernelSize,
                       double backgroundSigmaMultiplier,
                       int centroidMode,
                       int peakKernelRadiusPx,
                       double strongHotPixelExcessDn,
                       int r0HistoryWindowFrames)> applyProcessing;
    std::function<void(double thresholdPx,
                       int requiredFrames,
                       qint64 cooldownMs,
                       double minimumShiftPx)> applyRoiRecentering;
    std::function<void(double sigmaThreshold,
                       double peakFraction,
                       int minArea,
                       int maxArea,
                       int connectivity)> applyFullFrameStarDetection;
    std::function<void(bool enabled,
                       QString camera0MaskPath,
                       QString camera0ExcessPath,
                       QString camera1MaskPath,
                       QString camera1ExcessPath,
                       int templateWidth,
                       int templateHeight)> applyHotPixelTemplates;
    std::function<void(double apertureDiameterMm,
                       double baselineSeparationMm,
                       double baselineAngleDeg,
                       double focalLengthCm,
                       double zenithAngleDeg,
                       double lambdaNm,
                       double pixelSizeUm)> applyOptics;
    std::function<void(bool autoRadius,
                       double focalLengthMm,
                       double pixelSizeUm,
                       double polarDistanceArcmin,
                       double radiusAdjustPx,
                       double previewRateHz)> applyAlignment;
    std::function<void(bool enabled,
                       bool showMatchedCatalogStars,
                       int maxDetectedStars,
                       int minMatchedStars,
                       double maxRmsPx,
                       int retryIntervalMs,
                       double minMatchedSpatialSpreadPx,
                       double minPolarisSnr,
                       bool allowSaturatedPolarisConfirmation)> applyPolarisSolver;
    std::function<void(QString path,
                       int interval,
                       bool parameterValidationEnabled,
                       bool syncDiagnosticLoggingEnabled)> applyStorage;
    std::function<void(const EnvironmentSensorConfig& config)> applyEnvironmentSensor;
    std::function<void(const AutoAcquisitionConfig& config)> applyAutoAcquisition;
    std::function<void(QString ip, quint16 port)> applyNetwork;
    std::function<void()> afterApply;
};

namespace ConfigApplicationController {
ConfigApplicationCallbacks callbacksForChanges(const ConfigApplicationCallbacks& callbacks,
                                                const ConfigChangeSet& changes);
void applyPreValidationConfig(const AppConfig& config,
                              const ConfigApplicationCallbacks& callbacks);
void applyValidatedConfig(const AppConfig& config,
                          const ConfigApplicationCallbacks& callbacks);
}
