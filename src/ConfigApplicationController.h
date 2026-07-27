#pragma once

#include "AppConfig.h"

#include <functional>

struct ConfigApplicationCallbacks {
    std::function<void(double exposure, double gain, double continuousFrameRateHz)> applyCamera;
    std::function<void(bool enabled,
                       double lowThreshold,
                       double highThreshold,
                       double darkRatio,
                       double brightRatio,
                       double minExposure,
                       double maxExposure)> applyAutoExposure;
    std::function<void(int mode)> applyTriggerMode;
    std::function<void(int kernelSize, double sigma, int method)> applyProcessing;
    std::function<void(double thresholdPx,
                       int requiredFrames,
                       qint64 cooldownMs,
                       double minimumShiftPx)> applyRoiRecentering;
    std::function<void(double thresholdAbsolute,
                       double sigmaThreshold,
                       double peakFraction,
                       double minimumIntensity,
                       int minArea,
                       int maxArea)> applyFullFrameStarDetection;
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
    std::function<void(QString path, int interval)> applyStorage;
    std::function<void(QString ip, quint16 port)> applyNetwork;
    std::function<void()> afterApply;
};

namespace ConfigApplicationController {
void applyPreValidationConfig(const AppConfig& config,
                              const ConfigApplicationCallbacks& callbacks);
void applyValidatedConfig(const AppConfig& config,
                          const ConfigApplicationCallbacks& callbacks);
}
