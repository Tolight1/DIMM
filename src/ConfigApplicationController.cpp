#include "ConfigApplicationController.h"

namespace ConfigApplicationController {

ConfigApplicationCallbacks callbacksForChanges(const ConfigApplicationCallbacks& callbacks,
                                                const ConfigChangeSet& changes)
{
    ConfigApplicationCallbacks filtered = callbacks;
    if (!changes.camera) {
        filtered.applyCamera = nullptr;
    }
    if (!changes.autoExposure) {
        filtered.applyAutoExposure = nullptr;
    }
    if (!changes.trigger) {
        filtered.applyTriggerMode = nullptr;
    }
    if (!changes.processing) {
        filtered.applyProcessing = nullptr;
    }
    if (!changes.roiRecentering) {
        filtered.applyRoiRecentering = nullptr;
    }
    if (!changes.fullFrameStarDetection) {
        filtered.applyFullFrameStarDetection = nullptr;
    }
    if (!changes.hotPixel) {
        filtered.applyHotPixelTemplates = nullptr;
    }
    if (!changes.optics) {
        filtered.applyOptics = nullptr;
    }
    if (!changes.alignment) {
        filtered.applyAlignment = nullptr;
    }
    if (!changes.polarisSolver) {
        filtered.applyPolarisSolver = nullptr;
    }
    if (!changes.storage) {
        filtered.applyStorage = nullptr;
    }
    if (!changes.environmentSensor) {
        filtered.applyEnvironmentSensor = nullptr;
    }
    if (!changes.autoAcquisition) {
        filtered.applyAutoAcquisition = nullptr;
    }
    if (!changes.network) {
        filtered.applyNetwork = nullptr;
    }
    if (!changes.any()) {
        filtered.afterApply = nullptr;
    }
    return filtered;
}

void applyPreValidationConfig(const AppConfig& config,
                              const ConfigApplicationCallbacks& callbacks)
{
    if (callbacks.applyCamera) {
        callbacks.applyCamera(config.camera.exposureUs,
                              config.camera.gainDb,
                              config.camera.continuousFrameRateHz);
    }
    if (callbacks.applyAutoExposure) {
        callbacks.applyAutoExposure(config.autoExposure);
    }
    if (callbacks.applyTriggerMode) {
        callbacks.applyTriggerMode(config.trigger.mode);
    }
}

void applyValidatedConfig(const AppConfig& config,
                          const ConfigApplicationCallbacks& callbacks)
{
    if (callbacks.applyProcessing) {
        callbacks.applyProcessing(config.processing.backgroundKernelSize,
                                  config.processing.backgroundSigmaMultiplier,
                                  config.processing.centroidMode,
                                  config.processing.peakKernelRadiusPx,
                                  config.processing.strongHotPixelExcessDn,
                                  config.processing.r0HistoryWindowFrames);
    }
    if (callbacks.applyRoiRecentering) {
        callbacks.applyRoiRecentering(config.roiRecentering.thresholdPx,
                                      config.roiRecentering.requiredFrames,
                                      config.roiRecentering.cooldownMs,
                                      config.roiRecentering.minimumShiftPx);
    }
    if (callbacks.applyFullFrameStarDetection) {
        callbacks.applyFullFrameStarDetection(config.starDetection.sigmaThreshold,
                                              config.starDetection.peakFraction,
                                              config.starDetection.minArea,
                                              config.starDetection.maxArea,
                                              config.starDetection.connectivity);
    }
    if (callbacks.applyHotPixelTemplates) {
        callbacks.applyHotPixelTemplates(config.hotPixel.enabled,
                                         config.hotPixel.camera0MaskPath,
                                         config.hotPixel.camera0ExcessPath,
                                         config.hotPixel.camera1MaskPath,
                                         config.hotPixel.camera1ExcessPath,
                                         config.hotPixel.templateWidth,
                                         config.hotPixel.templateHeight);
    }
    if (callbacks.applyOptics) {
        callbacks.applyOptics(config.optical.apertureDiameterMm,
                              config.optical.baselineSeparationMm,
                              config.optical.baselineAngleDeg,
                              config.optical.focalLengthCm,
                              config.optical.zenithAngleDeg,
                              config.optical.wavelengthNm,
                              config.optical.pixelSizeUm);
    }
    if (callbacks.applyAlignment) {
        callbacks.applyAlignment(config.alignment.autoRadius,
                                 config.alignment.focalLengthMm,
                                 config.alignment.pixelSizeUm,
                                 config.alignment.polarDistanceArcmin,
                                 config.alignment.radiusAdjustPx,
                                 config.alignment.previewRateHz);
    }
    if (callbacks.applyPolarisSolver) {
        callbacks.applyPolarisSolver(config.polarisSolver.enabled,
                                     config.polarisSolver.showMatchedCatalogStars,
                                     config.polarisSolver.maxDetectedStars,
                                     config.polarisSolver.minMatchedStars,
                                     config.polarisSolver.maxRmsPx,
                                     config.polarisSolver.retryIntervalMs,
                                     config.polarisSolver.minMatchedSpatialSpreadPx,
                                     config.polarisSolver.minPolarisSnr,
                                     config.polarisSolver.allowSaturatedPolarisConfirmation);
    }
    if (callbacks.applyStorage) {
        callbacks.applyStorage(config.storage.path,
                               config.storage.interval,
                               config.storage.parameterValidationEnabled,
                               config.storage.syncDiagnosticLoggingEnabled);
    }
    if (callbacks.applyEnvironmentSensor) {
        callbacks.applyEnvironmentSensor(config.environmentSensor);
    }
    if (callbacks.applyAutoAcquisition) {
        callbacks.applyAutoAcquisition(config.autoAcquisition);
    }
    if (callbacks.applyNetwork) {
        callbacks.applyNetwork(config.network.ip, config.network.port);
    }
    if (callbacks.afterApply) {
        callbacks.afterApply();
    }
}

}
