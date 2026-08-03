#include "AppConfigPersistence.h"

#include <QSettings>
#include <QString>

namespace {

void saveCamera(QSettings& settings, const CameraConfig& config)
{
    settings.setValue(QStringLiteral("camera/exposureUs"), config.exposureUs);
    settings.setValue(QStringLiteral("camera/gainDb"), config.gainDb);
    settings.setValue(QStringLiteral("camera/continuousFrameRateHz"), config.continuousFrameRateHz);
}

void loadCamera(QSettings& settings, AppConfig* config, const AppConfig& defaults)
{
    config->camera.exposureUs =
        settings.value(QStringLiteral("camera/exposureUs"), defaults.camera.exposureUs).toDouble();
    config->camera.gainDb =
        settings.value(QStringLiteral("camera/gainDb"), defaults.camera.gainDb).toDouble();
    config->camera.continuousFrameRateHz =
        settings.value(QStringLiteral("camera/continuousFrameRateHz"),
                       defaults.camera.continuousFrameRateHz).toDouble();
}

void saveAutoExposure(QSettings& settings, const AutoExposureConfig& config)
{
    settings.setValue(QStringLiteral("autoExposure/enabled"), config.enabled);
    settings.setValue(QStringLiteral("autoExposure/trendConflictEnabled"), config.trendConflictEnabled);
    settings.setValue(QStringLiteral("autoExposure/targetPeakLowDn"), config.targetPeakLowDn);
    settings.setValue(QStringLiteral("autoExposure/targetPeakHighDn"), config.targetPeakHighDn);
    settings.setValue(QStringLiteral("autoExposure/exposureHysteresisDn"), config.exposureHysteresisDn);
    settings.setValue(QStringLiteral("autoExposure/hardSaturationDn"), config.hardSaturationDn);
    settings.setValue(QStringLiteral("autoExposure/saturatedPixelCount"), config.saturatedPixelCount);
    settings.setValue(QStringLiteral("autoExposure/darkSnrWarning"), config.darkSnrWarning);
    settings.setValue(QStringLiteral("autoExposure/darkSnrCritical"), config.darkSnrCritical);
    settings.setValue(QStringLiteral("autoExposure/minValidCentroidRatio"), config.minValidCentroidRatio);
    settings.setValue(QStringLiteral("autoExposure/starLostValidRatio"), config.starLostValidRatio);
    settings.setValue(QStringLiteral("autoExposure/brightFrameRatioThreshold"),
                      config.brightFrameRatioThreshold);
    settings.setValue(QStringLiteral("autoExposure/darkFrameRatioThreshold"),
                      config.darkFrameRatioThreshold);
    settings.setValue(QStringLiteral("autoExposure/stableFrameRatioThreshold"),
                      config.stableFrameRatioThreshold);
    settings.setValue(QStringLiteral("autoExposure/hardSaturationFrameRatioThreshold"),
                      config.hardSaturationFrameRatioThreshold);
    settings.setValue(QStringLiteral("autoExposure/sampleWindowSec"), config.sampleWindowSec);
    settings.setValue(QStringLiteral("autoExposure/autoExposureSampleIntervalMs"),
                      config.autoExposureSampleIntervalMs);
    settings.setValue(QStringLiteral("autoExposure/minDecisionSampleCount"),
                      config.minDecisionSampleCount);
    settings.setValue(QStringLiteral("autoExposure/trendConflictPersistenceSec"),
                      config.trendConflictPersistenceSec);
    settings.setValue(QStringLiteral("autoExposure/minExposureUs"), config.minExposureUs);
    settings.setValue(QStringLiteral("autoExposure/maxExposureUs"), config.maxExposureUs);
    settings.setValue(QStringLiteral("autoExposure/maxExposureChangeRatioUp"),
                      config.maxExposureChangeRatioUp);
    settings.setValue(QStringLiteral("autoExposure/maxExposureChangeRatioDown"),
                      config.maxExposureChangeRatioDown);
    settings.setValue(QStringLiteral("autoExposure/cameraAgreementRatio"),
                      config.cameraAgreementRatio);
    settings.setValue(QStringLiteral("autoExposure/peakSupportRadiusPx"), config.peakSupportRadiusPx);
    settings.setValue(QStringLiteral("autoExposure/peakSupportFraction"), config.peakSupportFraction);
    settings.setValue(QStringLiteral("autoExposure/minPeakSupportPixelCount"),
                      config.minPeakSupportPixelCount);
    settings.setValue(QStringLiteral("autoExposure/minNeighborPeakRatio"),
                      config.minNeighborPeakRatio);
    settings.setValue(QStringLiteral("autoExposure/maxPeakCandidateCount"),
                      config.maxPeakCandidateCount);
    settings.setValue(QStringLiteral("autoExposure/supportedPeakPercentile"),
                      config.supportedPeakPercentile);
    settings.setValue(QStringLiteral("autoExposure/exposureSettleMs"), config.exposureSettleMs);
    settings.setValue(QStringLiteral("autoExposure/minExposureDeltaUs"), config.minExposureDeltaUs);
    settings.setValue(QStringLiteral("autoExposure/minExposureChangeRatio"),
                      config.minExposureChangeRatio);
}

void loadAutoExposure(QSettings& settings, AppConfig* config, const AppConfig& defaults)
{
    auto& target = config->autoExposure;
    const auto& fallback = defaults.autoExposure;
    target.enabled = settings.value(QStringLiteral("autoExposure/enabled"), fallback.enabled).toBool();
    target.trendConflictEnabled =
        settings.value(QStringLiteral("autoExposure/trendConflictEnabled"),
                       fallback.trendConflictEnabled).toBool();
    target.targetPeakLowDn =
        settings.value(QStringLiteral("autoExposure/targetPeakLowDn"), fallback.targetPeakLowDn).toDouble();
    target.targetPeakHighDn =
        settings.value(QStringLiteral("autoExposure/targetPeakHighDn"), fallback.targetPeakHighDn).toDouble();
    target.exposureHysteresisDn =
        settings.value(QStringLiteral("autoExposure/exposureHysteresisDn"),
                       fallback.exposureHysteresisDn).toDouble();
    target.hardSaturationDn =
        settings.value(QStringLiteral("autoExposure/hardSaturationDn"), fallback.hardSaturationDn).toDouble();
    target.saturatedPixelCount =
        settings.value(QStringLiteral("autoExposure/saturatedPixelCount"), fallback.saturatedPixelCount).toInt();
    target.darkSnrWarning =
        settings.value(QStringLiteral("autoExposure/darkSnrWarning"), fallback.darkSnrWarning).toDouble();
    target.darkSnrCritical =
        settings.value(QStringLiteral("autoExposure/darkSnrCritical"), fallback.darkSnrCritical).toDouble();
    target.minValidCentroidRatio =
        settings.value(QStringLiteral("autoExposure/minValidCentroidRatio"),
                       fallback.minValidCentroidRatio).toDouble();
    target.starLostValidRatio =
        settings.value(QStringLiteral("autoExposure/starLostValidRatio"),
                       fallback.starLostValidRatio).toDouble();
    target.brightFrameRatioThreshold =
        settings.value(QStringLiteral("autoExposure/brightFrameRatioThreshold"),
                       fallback.brightFrameRatioThreshold).toDouble();
    target.darkFrameRatioThreshold =
        settings.value(QStringLiteral("autoExposure/darkFrameRatioThreshold"),
                       fallback.darkFrameRatioThreshold).toDouble();
    target.stableFrameRatioThreshold =
        settings.value(QStringLiteral("autoExposure/stableFrameRatioThreshold"),
                       fallback.stableFrameRatioThreshold).toDouble();
    target.hardSaturationFrameRatioThreshold =
        settings.value(QStringLiteral("autoExposure/hardSaturationFrameRatioThreshold"),
                       fallback.hardSaturationFrameRatioThreshold).toDouble();
    target.sampleWindowSec =
        settings.value(QStringLiteral("autoExposure/sampleWindowSec"), fallback.sampleWindowSec).toInt();
    target.autoExposureSampleIntervalMs =
        settings.value(QStringLiteral("autoExposure/autoExposureSampleIntervalMs"),
                       fallback.autoExposureSampleIntervalMs).toInt();
    target.minDecisionSampleCount =
        settings.value(QStringLiteral("autoExposure/minDecisionSampleCount"),
                       fallback.minDecisionSampleCount).toInt();
    target.trendConflictPersistenceSec =
        settings.value(QStringLiteral("autoExposure/trendConflictPersistenceSec"),
                       fallback.trendConflictPersistenceSec).toInt();
    target.minExposureUs =
        settings.value(QStringLiteral("autoExposure/minExposureUs"), fallback.minExposureUs).toDouble();
    target.maxExposureUs =
        settings.value(QStringLiteral("autoExposure/maxExposureUs"), fallback.maxExposureUs).toDouble();
    target.maxExposureChangeRatioUp =
        settings.value(QStringLiteral("autoExposure/maxExposureChangeRatioUp"),
                       fallback.maxExposureChangeRatioUp).toDouble();
    target.maxExposureChangeRatioDown =
        settings.value(QStringLiteral("autoExposure/maxExposureChangeRatioDown"),
                       fallback.maxExposureChangeRatioDown).toDouble();
    target.cameraAgreementRatio =
        settings.value(QStringLiteral("autoExposure/cameraAgreementRatio"),
                       fallback.cameraAgreementRatio).toDouble();
    target.peakSupportRadiusPx =
        settings.value(QStringLiteral("autoExposure/peakSupportRadiusPx"),
                       fallback.peakSupportRadiusPx).toInt();
    target.peakSupportFraction =
        settings.value(QStringLiteral("autoExposure/peakSupportFraction"),
                       fallback.peakSupportFraction).toDouble();
    target.minPeakSupportPixelCount =
        settings.value(QStringLiteral("autoExposure/minPeakSupportPixelCount"),
                       fallback.minPeakSupportPixelCount).toInt();
    target.minNeighborPeakRatio =
        settings.value(QStringLiteral("autoExposure/minNeighborPeakRatio"),
                       fallback.minNeighborPeakRatio).toDouble();
    target.maxPeakCandidateCount =
        settings.value(QStringLiteral("autoExposure/maxPeakCandidateCount"),
                       fallback.maxPeakCandidateCount).toInt();
    target.supportedPeakPercentile =
        settings.value(QStringLiteral("autoExposure/supportedPeakPercentile"),
                       fallback.supportedPeakPercentile).toDouble();
    target.exposureSettleMs =
        settings.value(QStringLiteral("autoExposure/exposureSettleMs"), fallback.exposureSettleMs).toInt();
    target.minExposureDeltaUs =
        settings.value(QStringLiteral("autoExposure/minExposureDeltaUs"),
                       fallback.minExposureDeltaUs).toDouble();
    target.minExposureChangeRatio =
        settings.value(QStringLiteral("autoExposure/minExposureChangeRatio"),
                       fallback.minExposureChangeRatio).toDouble();
}

void saveSimpleGroups(QSettings& settings, const AppConfig& config)
{
    settings.setValue(QStringLiteral("processing/backgroundKernelSize"),
                      config.processing.backgroundKernelSize);
    settings.setValue(QStringLiteral("processing/backgroundSigmaMultiplier"),
                      config.processing.backgroundSigmaMultiplier);
    settings.setValue(QStringLiteral("processing/centroidMode"), config.processing.centroidMode);
    settings.setValue(QStringLiteral("processing/peakKernelMethod"), config.processing.peakKernelMethod);
    settings.setValue(QStringLiteral("processing/peakKernelRadiusPx"),
                      config.processing.peakKernelRadiusPx);
    settings.setValue(QStringLiteral("processing/strongHotPixelExcessDn"),
                      config.processing.strongHotPixelExcessDn);
    settings.setValue(QStringLiteral("roiRecentering/thresholdPx"), config.roiRecentering.thresholdPx);
    settings.setValue(QStringLiteral("roiRecentering/requiredFrames"), config.roiRecentering.requiredFrames);
    settings.setValue(QStringLiteral("roiRecentering/cooldownMs"), config.roiRecentering.cooldownMs);
    settings.setValue(QStringLiteral("roiRecentering/minimumShiftPx"), config.roiRecentering.minimumShiftPx);
    settings.setValue(QStringLiteral("starDetection/thresholdAbsolute"),
                      config.starDetection.thresholdAbsolute);
    settings.setValue(QStringLiteral("starDetection/sigmaThreshold"), config.starDetection.sigmaThreshold);
    settings.setValue(QStringLiteral("starDetection/peakFraction"), config.starDetection.peakFraction);
    settings.setValue(QStringLiteral("starDetection/minimumIntensity"),
                      config.starDetection.minimumIntensity);
    settings.setValue(QStringLiteral("starDetection/minArea"), config.starDetection.minArea);
    settings.setValue(QStringLiteral("starDetection/maxArea"), config.starDetection.maxArea);
    settings.setValue(QStringLiteral("hotPixel/enabled"), config.hotPixel.enabled);
    settings.setValue(QStringLiteral("hotPixel/camera0MaskPath"), config.hotPixel.camera0MaskPath);
    settings.setValue(QStringLiteral("hotPixel/camera0ExcessPath"), config.hotPixel.camera0ExcessPath);
    settings.setValue(QStringLiteral("hotPixel/camera1MaskPath"), config.hotPixel.camera1MaskPath);
    settings.setValue(QStringLiteral("hotPixel/camera1ExcessPath"), config.hotPixel.camera1ExcessPath);
    settings.setValue(QStringLiteral("hotPixel/templateWidth"), config.hotPixel.templateWidth);
    settings.setValue(QStringLiteral("hotPixel/templateHeight"), config.hotPixel.templateHeight);
    settings.setValue(QStringLiteral("optical/apertureDiameterMm"), config.optical.apertureDiameterMm);
    settings.setValue(QStringLiteral("optical/baselineSeparationMm"), config.optical.baselineSeparationMm);
    settings.setValue(QStringLiteral("optical/baselineAngleDeg"), config.optical.baselineAngleDeg);
    settings.setValue(QStringLiteral("optical/focalLengthCm"), config.optical.focalLengthCm);
    settings.setValue(QStringLiteral("optical/zenithAngleDeg"), config.optical.zenithAngleDeg);
    settings.setValue(QStringLiteral("optical/wavelengthNm"), config.optical.wavelengthNm);
    settings.setValue(QStringLiteral("optical/pixelSizeUm"), config.optical.pixelSizeUm);
    settings.setValue(QStringLiteral("alignment/autoRadius"), config.alignment.autoRadius);
    settings.setValue(QStringLiteral("alignment/focalLengthMm"), config.alignment.focalLengthMm);
    settings.setValue(QStringLiteral("alignment/pixelSizeUm"), config.alignment.pixelSizeUm);
    settings.setValue(QStringLiteral("alignment/polarDistanceArcmin"), config.alignment.polarDistanceArcmin);
    settings.setValue(QStringLiteral("alignment/radiusAdjustPx"), config.alignment.radiusAdjustPx);
    settings.setValue(QStringLiteral("alignment/previewRateHz"), config.alignment.previewRateHz);
    settings.setValue(QStringLiteral("polarisSolver/enabled"), config.polarisSolver.enabled);
    settings.setValue(QStringLiteral("polarisSolver/showMatchedCatalogStars"),
                      config.polarisSolver.showMatchedCatalogStars);
    settings.setValue(QStringLiteral("polarisSolver/maxDetectedStars"), config.polarisSolver.maxDetectedStars);
    settings.setValue(QStringLiteral("polarisSolver/minMatchedStars"), config.polarisSolver.minMatchedStars);
    settings.setValue(QStringLiteral("polarisSolver/maxRmsPx"), config.polarisSolver.maxRmsPx);
    settings.setValue(QStringLiteral("polarisSolver/retryIntervalMs"), config.polarisSolver.retryIntervalMs);
    settings.setValue(QStringLiteral("polarisSolver/minMatchedSpatialSpreadPx"),
                      config.polarisSolver.minMatchedSpatialSpreadPx);
    settings.setValue(QStringLiteral("polarisSolver/minPolarisSnr"), config.polarisSolver.minPolarisSnr);
    settings.setValue(QStringLiteral("polarisSolver/allowSaturatedPolarisConfirmation"),
                      config.polarisSolver.allowSaturatedPolarisConfirmation);
    settings.setValue(QStringLiteral("storage/path"), config.storage.path);
    settings.setValue(QStringLiteral("storage/interval"), config.storage.interval);
    settings.setValue(QStringLiteral("storage/parameterValidationEnabled"),
                      config.storage.parameterValidationEnabled);
    settings.setValue(QStringLiteral("storage/syncDiagnosticLoggingEnabled"),
                      config.storage.syncDiagnosticLoggingEnabled);
    settings.setValue(QStringLiteral("trigger/mode"), config.trigger.mode);
    settings.setValue(QStringLiteral("environmentSensor/enabled"), config.environmentSensor.enabled);
    settings.setValue(QStringLiteral("environmentSensor/portName"), config.environmentSensor.portName);
    settings.setValue(QStringLiteral("environmentSensor/baudRate"), config.environmentSensor.baudRate);
    settings.setValue(QStringLiteral("environmentSensor/deviceAddress"), config.environmentSensor.deviceAddress);
    settings.setValue(QStringLiteral("environmentSensor/pollIntervalMs"),
                      config.environmentSensor.pollIntervalMs);
    settings.setValue(QStringLiteral("pulseGenerator/enabled"), config.pulseGenerator.enabled);
    settings.setValue(QStringLiteral("pulseGenerator/portName"), config.pulseGenerator.portName);
    settings.setValue(QStringLiteral("pulseGenerator/baudRate"), config.pulseGenerator.baudRate);
    settings.setValue(QStringLiteral("pulseGenerator/terminalId"), config.pulseGenerator.terminalId);
    settings.setValue(QStringLiteral("pulseGenerator/frequencyHz"), config.pulseGenerator.frequencyHz);
    settings.setValue(QStringLiteral("pulseGenerator/pulseCount"), config.pulseGenerator.pulseCount);
    settings.setValue(QStringLiteral("pulseGenerator/dutyPercent"), config.pulseGenerator.dutyPercent);
    settings.setValue(QStringLiteral("pulseGenerator/remoteControl"), config.pulseGenerator.remoteControl);
    settings.setValue(QStringLiteral("autoAcquisition/enabled"), config.autoAcquisition.enabled);
    settings.setValue(QStringLiteral("autoAcquisition/latitudeDeg"), config.autoAcquisition.latitudeDeg);
    settings.setValue(QStringLiteral("autoAcquisition/longitudeDeg"), config.autoAcquisition.longitudeDeg);
    settings.setValue(QStringLiteral("autoAcquisition/startOffsetMinutesAfterSunset"),
                      config.autoAcquisition.startOffsetMinutesAfterSunset);
    settings.setValue(QStringLiteral("autoAcquisition/stopOffsetMinutesBeforeSunrise"),
                      config.autoAcquisition.stopOffsetMinutesBeforeSunrise);
    settings.setValue(QStringLiteral("autoAcquisition/testTimeOverrideEnabled"),
                      config.autoAcquisition.testTimeOverrideEnabled);
    settings.setValue(QStringLiteral("autoAcquisition/testStartTime"),
                      config.autoAcquisition.testStartTime);
    settings.setValue(QStringLiteral("autoAcquisition/testStopTime"),
                      config.autoAcquisition.testStopTime);
    settings.setValue(QStringLiteral("network/ip"), config.network.ip);
    settings.setValue(QStringLiteral("network/port"), config.network.port);
}

void loadSimpleGroups(QSettings& settings, AppConfig* config, const AppConfig& defaults)
{
    auto& target = *config;
    target.processing.backgroundKernelSize =
        settings.value(QStringLiteral("processing/backgroundKernelSize"),
                       defaults.processing.backgroundKernelSize).toInt();
    target.processing.backgroundSigmaMultiplier =
        settings.value(QStringLiteral("processing/backgroundSigmaMultiplier"),
                       defaults.processing.backgroundSigmaMultiplier).toDouble();
    target.processing.centroidMode =
        settings.value(QStringLiteral("processing/centroidMode"),
                       defaults.processing.centroidMode).toInt();
    target.processing.peakKernelMethod =
        settings.value(QStringLiteral("processing/peakKernelMethod"),
                       defaults.processing.peakKernelMethod).toInt();
    target.processing.peakKernelRadiusPx =
        settings.value(QStringLiteral("processing/peakKernelRadiusPx"),
                       defaults.processing.peakKernelRadiusPx).toInt();
    target.processing.strongHotPixelExcessDn =
        settings.value(QStringLiteral("processing/strongHotPixelExcessDn"),
                       defaults.processing.strongHotPixelExcessDn).toDouble();
    target.roiRecentering.thresholdPx =
        settings.value(QStringLiteral("roiRecentering/thresholdPx"),
                       defaults.roiRecentering.thresholdPx).toDouble();
    target.roiRecentering.requiredFrames =
        settings.value(QStringLiteral("roiRecentering/requiredFrames"),
                       defaults.roiRecentering.requiredFrames).toInt();
    target.roiRecentering.cooldownMs =
        settings.value(QStringLiteral("roiRecentering/cooldownMs"),
                       defaults.roiRecentering.cooldownMs).toLongLong();
    target.roiRecentering.minimumShiftPx =
        settings.value(QStringLiteral("roiRecentering/minimumShiftPx"),
                       defaults.roiRecentering.minimumShiftPx).toDouble();
    target.starDetection.thresholdAbsolute =
        settings.value(QStringLiteral("starDetection/thresholdAbsolute"),
                       defaults.starDetection.thresholdAbsolute).toDouble();
    target.starDetection.sigmaThreshold =
        settings.value(QStringLiteral("starDetection/sigmaThreshold"),
                       defaults.starDetection.sigmaThreshold).toDouble();
    target.starDetection.peakFraction =
        settings.value(QStringLiteral("starDetection/peakFraction"),
                       defaults.starDetection.peakFraction).toDouble();
    target.starDetection.minimumIntensity =
        settings.value(QStringLiteral("starDetection/minimumIntensity"),
                       defaults.starDetection.minimumIntensity).toDouble();
    target.starDetection.minArea =
        settings.value(QStringLiteral("starDetection/minArea"), defaults.starDetection.minArea).toInt();
    target.starDetection.maxArea =
        settings.value(QStringLiteral("starDetection/maxArea"), defaults.starDetection.maxArea).toInt();
    target.hotPixel.enabled =
        settings.value(QStringLiteral("hotPixel/enabled"), defaults.hotPixel.enabled).toBool();
    target.hotPixel.camera0MaskPath =
        settings.value(QStringLiteral("hotPixel/camera0MaskPath"),
                       defaults.hotPixel.camera0MaskPath).toString();
    target.hotPixel.camera0ExcessPath =
        settings.value(QStringLiteral("hotPixel/camera0ExcessPath"),
                       defaults.hotPixel.camera0ExcessPath).toString();
    target.hotPixel.camera1MaskPath =
        settings.value(QStringLiteral("hotPixel/camera1MaskPath"),
                       defaults.hotPixel.camera1MaskPath).toString();
    target.hotPixel.camera1ExcessPath =
        settings.value(QStringLiteral("hotPixel/camera1ExcessPath"),
                       defaults.hotPixel.camera1ExcessPath).toString();
    target.hotPixel.templateWidth =
        settings.value(QStringLiteral("hotPixel/templateWidth"), defaults.hotPixel.templateWidth).toInt();
    target.hotPixel.templateHeight =
        settings.value(QStringLiteral("hotPixel/templateHeight"), defaults.hotPixel.templateHeight).toInt();
    target.optical.apertureDiameterMm =
        settings.value(QStringLiteral("optical/apertureDiameterMm"),
                       defaults.optical.apertureDiameterMm).toDouble();
    target.optical.baselineSeparationMm =
        settings.value(QStringLiteral("optical/baselineSeparationMm"),
                       defaults.optical.baselineSeparationMm).toDouble();
    target.optical.baselineAngleDeg =
        settings.value(QStringLiteral("optical/baselineAngleDeg"),
                       defaults.optical.baselineAngleDeg).toDouble();
    target.optical.focalLengthCm =
        settings.value(QStringLiteral("optical/focalLengthCm"), defaults.optical.focalLengthCm).toDouble();
    target.optical.zenithAngleDeg =
        settings.value(QStringLiteral("optical/zenithAngleDeg"), defaults.optical.zenithAngleDeg).toDouble();
    target.optical.wavelengthNm =
        settings.value(QStringLiteral("optical/wavelengthNm"), defaults.optical.wavelengthNm).toDouble();
    target.optical.pixelSizeUm =
        settings.value(QStringLiteral("optical/pixelSizeUm"), defaults.optical.pixelSizeUm).toDouble();
    target.alignment.autoRadius =
        settings.value(QStringLiteral("alignment/autoRadius"), defaults.alignment.autoRadius).toBool();
    target.alignment.focalLengthMm =
        settings.value(QStringLiteral("alignment/focalLengthMm"), defaults.alignment.focalLengthMm).toDouble();
    target.alignment.pixelSizeUm =
        settings.value(QStringLiteral("alignment/pixelSizeUm"), defaults.alignment.pixelSizeUm).toDouble();
    target.alignment.polarDistanceArcmin =
        settings.value(QStringLiteral("alignment/polarDistanceArcmin"),
                       defaults.alignment.polarDistanceArcmin).toDouble();
    target.alignment.radiusAdjustPx =
        settings.value(QStringLiteral("alignment/radiusAdjustPx"), defaults.alignment.radiusAdjustPx).toDouble();
    target.alignment.previewRateHz =
        settings.value(QStringLiteral("alignment/previewRateHz"), defaults.alignment.previewRateHz).toDouble();
    target.polarisSolver.enabled =
        settings.value(QStringLiteral("polarisSolver/enabled"), defaults.polarisSolver.enabled).toBool();
    target.polarisSolver.showMatchedCatalogStars =
        settings.value(QStringLiteral("polarisSolver/showMatchedCatalogStars"),
                       defaults.polarisSolver.showMatchedCatalogStars).toBool();
    target.polarisSolver.maxDetectedStars =
        settings.value(QStringLiteral("polarisSolver/maxDetectedStars"),
                       defaults.polarisSolver.maxDetectedStars).toInt();
    target.polarisSolver.minMatchedStars =
        settings.value(QStringLiteral("polarisSolver/minMatchedStars"),
                       defaults.polarisSolver.minMatchedStars).toInt();
    target.polarisSolver.maxRmsPx =
        settings.value(QStringLiteral("polarisSolver/maxRmsPx"), defaults.polarisSolver.maxRmsPx).toDouble();
    target.polarisSolver.retryIntervalMs =
        settings.value(QStringLiteral("polarisSolver/retryIntervalMs"),
                       defaults.polarisSolver.retryIntervalMs).toInt();
    target.polarisSolver.minMatchedSpatialSpreadPx =
        settings.value(QStringLiteral("polarisSolver/minMatchedSpatialSpreadPx"),
                       defaults.polarisSolver.minMatchedSpatialSpreadPx).toDouble();
    target.polarisSolver.minPolarisSnr =
        settings.value(QStringLiteral("polarisSolver/minPolarisSnr"),
                       defaults.polarisSolver.minPolarisSnr).toDouble();
    target.polarisSolver.allowSaturatedPolarisConfirmation =
        settings.value(QStringLiteral("polarisSolver/allowSaturatedPolarisConfirmation"),
                       defaults.polarisSolver.allowSaturatedPolarisConfirmation).toBool();
    target.storage.path = settings.value(QStringLiteral("storage/path"), defaults.storage.path).toString();
    target.storage.interval =
        settings.value(QStringLiteral("storage/interval"), defaults.storage.interval).toInt();
    target.storage.parameterValidationEnabled =
        settings.value(QStringLiteral("storage/parameterValidationEnabled"),
                       defaults.storage.parameterValidationEnabled).toBool();
    target.storage.syncDiagnosticLoggingEnabled =
        settings.value(QStringLiteral("storage/syncDiagnosticLoggingEnabled"),
                       defaults.storage.syncDiagnosticLoggingEnabled).toBool();
    target.trigger.mode = settings.value(QStringLiteral("trigger/mode"), defaults.trigger.mode).toInt();
    target.environmentSensor.enabled =
        settings.value(QStringLiteral("environmentSensor/enabled"),
                       defaults.environmentSensor.enabled).toBool();
    target.environmentSensor.portName =
        settings.value(QStringLiteral("environmentSensor/portName"),
                       defaults.environmentSensor.portName).toString();
    target.environmentSensor.baudRate =
        settings.value(QStringLiteral("environmentSensor/baudRate"),
                       defaults.environmentSensor.baudRate).toInt();
    target.environmentSensor.deviceAddress =
        settings.value(QStringLiteral("environmentSensor/deviceAddress"),
                       defaults.environmentSensor.deviceAddress).toInt();
    target.environmentSensor.pollIntervalMs =
        settings.value(QStringLiteral("environmentSensor/pollIntervalMs"),
                       defaults.environmentSensor.pollIntervalMs).toInt();
    target.pulseGenerator.enabled =
        settings.value(QStringLiteral("pulseGenerator/enabled"),
                       defaults.pulseGenerator.enabled).toBool();
    target.pulseGenerator.portName =
        settings.value(QStringLiteral("pulseGenerator/portName"),
                       defaults.pulseGenerator.portName).toString();
    target.pulseGenerator.baudRate =
        settings.value(QStringLiteral("pulseGenerator/baudRate"),
                       defaults.pulseGenerator.baudRate).toInt();
    target.pulseGenerator.terminalId =
        settings.value(QStringLiteral("pulseGenerator/terminalId"),
                       defaults.pulseGenerator.terminalId).toInt();
    target.pulseGenerator.frequencyHz =
        settings.value(QStringLiteral("pulseGenerator/frequencyHz"),
                       defaults.pulseGenerator.frequencyHz).toDouble();
    target.pulseGenerator.pulseCount =
        settings.value(QStringLiteral("pulseGenerator/pulseCount"),
                       defaults.pulseGenerator.pulseCount).toUInt();
    target.pulseGenerator.dutyPercent =
        settings.value(QStringLiteral("pulseGenerator/dutyPercent"),
                       defaults.pulseGenerator.dutyPercent).toDouble();
    target.pulseGenerator.remoteControl =
        settings.value(QStringLiteral("pulseGenerator/remoteControl"),
                       defaults.pulseGenerator.remoteControl).toBool();
    target.autoAcquisition.enabled =
        settings.value(QStringLiteral("autoAcquisition/enabled"),
                       defaults.autoAcquisition.enabled).toBool();
    target.autoAcquisition.latitudeDeg =
        settings.value(QStringLiteral("autoAcquisition/latitudeDeg"),
                       defaults.autoAcquisition.latitudeDeg).toDouble();
    target.autoAcquisition.longitudeDeg =
        settings.value(QStringLiteral("autoAcquisition/longitudeDeg"),
                       defaults.autoAcquisition.longitudeDeg).toDouble();
    target.autoAcquisition.startOffsetMinutesAfterSunset =
        settings.value(QStringLiteral("autoAcquisition/startOffsetMinutesAfterSunset"),
                       defaults.autoAcquisition.startOffsetMinutesAfterSunset).toInt();
    target.autoAcquisition.stopOffsetMinutesBeforeSunrise =
        settings.value(QStringLiteral("autoAcquisition/stopOffsetMinutesBeforeSunrise"),
                       defaults.autoAcquisition.stopOffsetMinutesBeforeSunrise).toInt();
    target.autoAcquisition.testTimeOverrideEnabled =
        settings.value(QStringLiteral("autoAcquisition/testTimeOverrideEnabled"),
                       defaults.autoAcquisition.testTimeOverrideEnabled).toBool();
    target.autoAcquisition.testStartTime =
        settings.value(QStringLiteral("autoAcquisition/testStartTime"),
                       defaults.autoAcquisition.testStartTime).toTime();
    target.autoAcquisition.testStopTime =
        settings.value(QStringLiteral("autoAcquisition/testStopTime"),
                       defaults.autoAcquisition.testStopTime).toTime();
    target.network.ip = settings.value(QStringLiteral("network/ip"), defaults.network.ip).toString();
    target.network.port =
        static_cast<quint16>(settings.value(QStringLiteral("network/port"),
                                            defaults.network.port).toUInt());
}

} // namespace

namespace AppConfigPersistence {

AppConfig load(const AppConfig& defaults)
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("settings"));
    AppConfig config = defaults;
    loadCamera(settings, &config, defaults);
    loadAutoExposure(settings, &config, defaults);
    loadSimpleGroups(settings, &config, defaults);
    settings.endGroup();
    return config;
}

void save(const AppConfig& config)
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("settings"));
    saveCamera(settings, config.camera);
    saveAutoExposure(settings, config.autoExposure);
    saveSimpleGroups(settings, config);
    settings.endGroup();
    settings.sync();
}

}
