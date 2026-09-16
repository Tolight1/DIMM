#include "AppConfigSnapshot.h"

#include <QDir>
#include <QSaveFile>

namespace {

QString number(double value)
{
    return QString::number(value, 'g', 17);
}

QString boolean(bool value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

QString escape(const QString& value)
{
    QString escaped = value;
    escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    escaped.replace(QStringLiteral("\r"), QStringLiteral("\\r"));
    escaped.replace(QStringLiteral("\n"), QStringLiteral("\\n"));
    return escaped;
}

bool isSafeFileName(const QString& value)
{
    return !value.isEmpty() && !QFileInfo(value).isAbsolute() &&
           !value.contains(QLatin1Char('/')) && !value.contains(QLatin1Char('\\')) &&
           value != QStringLiteral(".") && value != QStringLiteral("..");
}

} // namespace

namespace AppConfigSnapshot {

QString initialFileName()
{
    return QStringLiteral("settings_initial.txt");
}

QString changedFileName(int sequence)
{
    return QStringLiteral("settings_changed_%1.txt").arg(qMax(1, sequence), 3, 10, QLatin1Char('0'));
}

QString changeState(const ConfigChangeSet& changes)
{
    QStringList groups;
    if (changes.camera) groups.append(QStringLiteral("camera"));
    if (changes.autoExposure) groups.append(QStringLiteral("autoExposure"));
    if (changes.trigger) groups.append(QStringLiteral("trigger"));
    if (changes.processing) groups.append(QStringLiteral("processing"));
    if (changes.roiRecentering) groups.append(QStringLiteral("roiRecentering"));
    if (changes.fullFrameStarDetection) groups.append(QStringLiteral("starDetection"));
    if (changes.hotPixel) groups.append(QStringLiteral("hotPixel"));
    if (changes.optics) groups.append(QStringLiteral("optics"));
    if (changes.alignment) groups.append(QStringLiteral("alignment"));
    if (changes.polarisSolver) groups.append(QStringLiteral("polarisSolver"));
    if (changes.storage) groups.append(QStringLiteral("storage"));
    if (changes.environmentSensor) groups.append(QStringLiteral("environmentSensor"));
    if (changes.pulseGenerator) groups.append(QStringLiteral("pulseGenerator"));
    if (changes.autoAcquisition) groups.append(QStringLiteral("autoAcquisition"));
    if (changes.network) groups.append(QStringLiteral("network"));
    return QStringLiteral("changed:") + groups.join(QLatin1Char(','));
}

QString toUtf8Text(const AppConfig& config)
{
    QStringList lines;
    const auto add = [&lines](const QString& key, const QString& value) {
        lines.append(key + QLatin1Char('=') + value);
    };

    add(QStringLiteral("camera/exposureUs"), number(config.camera.exposureUs));
    add(QStringLiteral("camera/gainDb"), number(config.camera.gainDb));
    add(QStringLiteral("camera/continuousFrameRateHz"), number(config.camera.continuousFrameRateHz));

    add(QStringLiteral("autoExposure/enabled"), boolean(config.autoExposure.enabled));
    add(QStringLiteral("autoExposure/exposureFrequencySwitchEnabled"),
        boolean(config.autoExposure.exposureFrequencySwitchEnabled));
    add(QStringLiteral("autoExposure/trendConflictEnabled"), boolean(config.autoExposure.trendConflictEnabled));
    add(QStringLiteral("autoExposure/targetPeakLowDn"), number(config.autoExposure.targetPeakLowDn));
    add(QStringLiteral("autoExposure/targetPeakHighDn"), number(config.autoExposure.targetPeakHighDn));
    add(QStringLiteral("autoExposure/exposureHysteresisDn"), number(config.autoExposure.exposureHysteresisDn));
    add(QStringLiteral("autoExposure/hardSaturationDn"), number(config.autoExposure.hardSaturationDn));
    add(QStringLiteral("autoExposure/saturatedPixelCount"), QString::number(config.autoExposure.saturatedPixelCount));
    add(QStringLiteral("autoExposure/darkSnrWarning"), number(config.autoExposure.darkSnrWarning));
    add(QStringLiteral("autoExposure/darkSnrCritical"), number(config.autoExposure.darkSnrCritical));
    add(QStringLiteral("autoExposure/trackingLostSnr"), number(config.autoExposure.trackingLostSnr));
    add(QStringLiteral("autoExposure/minValidCentroidRatio"), number(config.autoExposure.minValidCentroidRatio));
    add(QStringLiteral("autoExposure/starLostValidRatio"), number(config.autoExposure.starLostValidRatio));
    add(QStringLiteral("autoExposure/brightFrameRatioThreshold"), number(config.autoExposure.brightFrameRatioThreshold));
    add(QStringLiteral("autoExposure/darkFrameRatioThreshold"), number(config.autoExposure.darkFrameRatioThreshold));
    add(QStringLiteral("autoExposure/stableFrameRatioThreshold"), number(config.autoExposure.stableFrameRatioThreshold));
    add(QStringLiteral("autoExposure/hardSaturationFrameRatioThreshold"), number(config.autoExposure.hardSaturationFrameRatioThreshold));
    add(QStringLiteral("autoExposure/sampleWindowSec"), QString::number(config.autoExposure.sampleWindowSec));
    add(QStringLiteral("autoExposure/autoExposureSampleIntervalMs"), QString::number(config.autoExposure.autoExposureSampleIntervalMs));
    add(QStringLiteral("autoExposure/minDecisionSampleCount"), QString::number(config.autoExposure.minDecisionSampleCount));
    add(QStringLiteral("autoExposure/autoExposureStepUs"), number(config.autoExposure.autoExposureStepUs));
    add(QStringLiteral("autoExposure/initialExposureUs"), number(config.autoExposure.initialExposureUs));
    add(QStringLiteral("autoExposure/autoExposureDecisionCooldownMin"), QString::number(config.autoExposure.autoExposureDecisionCooldownMin));
    add(QStringLiteral("autoExposure/trendConflictPersistenceSec"), QString::number(config.autoExposure.trendConflictPersistenceSec));
    add(QStringLiteral("autoExposure/minExposureUs"), number(config.autoExposure.minExposureUs));
    add(QStringLiteral("autoExposure/maxExposureUs"), number(config.autoExposure.maxExposureUs));
    add(QStringLiteral("autoExposure/exposureFrameRateWindows"), escape(config.autoExposure.exposureFrameRateWindows));
    add(QStringLiteral("autoExposure/maxExposureChangeRatioUp"), number(config.autoExposure.maxExposureChangeRatioUp));
    add(QStringLiteral("autoExposure/maxExposureChangeRatioDown"), number(config.autoExposure.maxExposureChangeRatioDown));
    add(QStringLiteral("autoExposure/cameraAgreementRatio"), number(config.autoExposure.cameraAgreementRatio));
    add(QStringLiteral("autoExposure/peakSupportRadiusPx"), QString::number(config.autoExposure.peakSupportRadiusPx));
    add(QStringLiteral("autoExposure/peakSupportFraction"), number(config.autoExposure.peakSupportFraction));
    add(QStringLiteral("autoExposure/minPeakSupportPixelCount"), QString::number(config.autoExposure.minPeakSupportPixelCount));
    add(QStringLiteral("autoExposure/minNeighborPeakRatio"), number(config.autoExposure.minNeighborPeakRatio));
    add(QStringLiteral("autoExposure/maxPeakCandidateCount"), QString::number(config.autoExposure.maxPeakCandidateCount));
    add(QStringLiteral("autoExposure/supportedPeakPercentile"), number(config.autoExposure.supportedPeakPercentile));
    add(QStringLiteral("autoExposure/exposureSettleMs"), QString::number(config.autoExposure.exposureSettleMs));
    add(QStringLiteral("autoExposure/minExposureDeltaUs"), number(config.autoExposure.minExposureDeltaUs));
    add(QStringLiteral("autoExposure/minExposureChangeRatio"), number(config.autoExposure.minExposureChangeRatio));

    add(QStringLiteral("processing/backgroundThresholdClipIterations"), QString::number(config.processing.backgroundThresholdClipIterations));
    add(QStringLiteral("processing/backgroundThresholdClipSigma"), number(config.processing.backgroundThresholdClipSigma));
    add(QStringLiteral("processing/backgroundThresholdSigmaMultiplier"), number(config.processing.backgroundThresholdSigmaMultiplier));
    add(QStringLiteral("processing/centroidMode"), QString::number(config.processing.centroidMode));
    add(QStringLiteral("processing/peakKernelRadiusPx"), QString::number(config.processing.peakKernelRadiusPx));
    add(QStringLiteral("processing/strongHotPixelExcessDn"), number(config.processing.strongHotPixelExcessDn));
    add(QStringLiteral("processing/r0HistoryWindowFrames"), QString::number(config.processing.r0HistoryWindowFrames));
    add(QStringLiteral("processing/psd/enabled"), boolean(config.processing.psdAnalysis.enabled));
    add(QStringLiteral("processing/psd/mode"),
        QString::number(static_cast<int>(config.processing.psdAnalysis.psdMode)));
    add(QStringLiteral("processing/psd/noiseDetectionMode"),
        QString::number(static_cast<int>(config.processing.psdAnalysis.noiseDetectionMode)));
    add(QStringLiteral("processing/psd/welchSegmentLength"),
        QString::number(config.processing.psdAnalysis.welchSegmentLength));
    add(QStringLiteral("processing/psd/welchOverlap"),
        number(config.processing.psdAnalysis.welchOverlap));
    add(QStringLiteral("processing/psd/nfft"),
        QString::number(config.processing.psdAnalysis.nfft));
    add(QStringLiteral("processing/psd/noiseCandidateStartNyquist"),
        number(config.processing.psdAnalysis.noiseCandidateStartNyquist));
    add(QStringLiteral("processing/psd/noiseCandidateEndNyquist"),
        number(config.processing.psdAnalysis.noiseCandidateEndNyquist));
    add(QStringLiteral("processing/psd/minimumNoiseBandBins"),
        QString::number(config.processing.psdAnalysis.minimumNoiseBandBins));
    add(QStringLiteral("processing/psd/minimumNoiseBandNyquistWidth"),
        number(config.processing.psdAnalysis.minimumNoiseBandNyquistWidth));
    add(QStringLiteral("processing/psd/fitNoiseDominanceKappa"),
        number(config.processing.psdAnalysis.fitNoiseDominanceKappa));

    add(QStringLiteral("roiRecentering/thresholdPx"), number(config.roiRecentering.thresholdPx));
    add(QStringLiteral("roiRecentering/requiredFrames"), QString::number(config.roiRecentering.requiredFrames));
    add(QStringLiteral("roiRecentering/cooldownMs"), QString::number(config.roiRecentering.cooldownMs));
    add(QStringLiteral("roiRecentering/minimumShiftPx"), number(config.roiRecentering.minimumShiftPx));

    add(QStringLiteral("starDetection/sigmaThreshold"), number(config.starDetection.sigmaThreshold));
    add(QStringLiteral("starDetection/peakFraction"), number(config.starDetection.peakFraction));
    add(QStringLiteral("starDetection/minArea"), QString::number(config.starDetection.minArea));
    add(QStringLiteral("starDetection/maxArea"), QString::number(config.starDetection.maxArea));
    add(QStringLiteral("starDetection/connectivity"), QString::number(config.starDetection.connectivity));

    add(QStringLiteral("hotPixel/enabled"), boolean(config.hotPixel.enabled));
    add(QStringLiteral("hotPixel/camera0MaskPath"), escape(config.hotPixel.camera0MaskPath));
    add(QStringLiteral("hotPixel/camera0ExcessPath"), escape(config.hotPixel.camera0ExcessPath));
    add(QStringLiteral("hotPixel/camera1MaskPath"), escape(config.hotPixel.camera1MaskPath));
    add(QStringLiteral("hotPixel/camera1ExcessPath"), escape(config.hotPixel.camera1ExcessPath));
    add(QStringLiteral("hotPixel/templateWidth"), QString::number(config.hotPixel.templateWidth));
    add(QStringLiteral("hotPixel/templateHeight"), QString::number(config.hotPixel.templateHeight));

    add(QStringLiteral("optical/apertureDiameterMm"), number(config.optical.apertureDiameterMm));
    add(QStringLiteral("optical/baselineSeparationMm"), number(config.optical.baselineSeparationMm));
    add(QStringLiteral("optical/baselineAngleDeg"), number(config.optical.baselineAngleDeg));
    add(QStringLiteral("optical/focalLengthCm"), number(config.optical.focalLengthCm));
    add(QStringLiteral("optical/zenithAngleDeg"), number(config.optical.zenithAngleDeg));
    add(QStringLiteral("optical/wavelengthNm"), number(config.optical.wavelengthNm));
    add(QStringLiteral("optical/pixelSizeUm"), number(config.optical.pixelSizeUm));
    add(QStringLiteral("optical/outerScaleM"), number(config.optical.outerScaleM));

    add(QStringLiteral("alignment/autoRadius"), boolean(config.alignment.autoRadius));
    add(QStringLiteral("alignment/focalLengthMm"), number(config.alignment.focalLengthMm));
    add(QStringLiteral("alignment/pixelSizeUm"), number(config.alignment.pixelSizeUm));
    add(QStringLiteral("alignment/polarDistanceArcmin"), number(config.alignment.polarDistanceArcmin));
    add(QStringLiteral("alignment/radiusAdjustPx"), number(config.alignment.radiusAdjustPx));
    add(QStringLiteral("alignment/previewRateHz"), number(config.alignment.previewRateHz));

    add(QStringLiteral("polarisSolver/enabled"), boolean(config.polarisSolver.enabled));
    add(QStringLiteral("polarisSolver/showMatchedCatalogStars"), boolean(config.polarisSolver.showMatchedCatalogStars));
    add(QStringLiteral("polarisSolver/maxDetectedStars"), QString::number(config.polarisSolver.maxDetectedStars));
    add(QStringLiteral("polarisSolver/minMatchedStars"), QString::number(config.polarisSolver.minMatchedStars));
    add(QStringLiteral("polarisSolver/maxRmsPx"), number(config.polarisSolver.maxRmsPx));
    add(QStringLiteral("polarisSolver/retryIntervalMs"), QString::number(config.polarisSolver.retryIntervalMs));
    add(QStringLiteral("polarisSolver/minMatchedSpatialSpreadPx"), number(config.polarisSolver.minMatchedSpatialSpreadPx));
    add(QStringLiteral("polarisSolver/minPolarisSnr"), number(config.polarisSolver.minPolarisSnr));
    add(QStringLiteral("polarisSolver/allowSaturatedPolarisConfirmation"), boolean(config.polarisSolver.allowSaturatedPolarisConfirmation));

    add(QStringLiteral("storage/path"), escape(config.storage.path));
    add(QStringLiteral("storage/interval"), QString::number(config.storage.interval));
    add(QStringLiteral("storage/parameterValidationEnabled"), boolean(config.storage.parameterValidationEnabled));
    add(QStringLiteral("storage/syncDiagnosticLoggingEnabled"), boolean(config.storage.syncDiagnosticLoggingEnabled));
    add(QStringLiteral("trigger/mode"), QString::number(config.trigger.mode));

    add(QStringLiteral("environmentSensor/enabled"), boolean(config.environmentSensor.enabled));
    add(QStringLiteral("environmentSensor/portName"), escape(config.environmentSensor.portName));
    add(QStringLiteral("environmentSensor/baudRate"), QString::number(config.environmentSensor.baudRate));
    add(QStringLiteral("environmentSensor/dataBits"), QString::number(config.environmentSensor.dataBits));
    add(QStringLiteral("environmentSensor/stopBits"), QString::number(config.environmentSensor.stopBits));
    add(QStringLiteral("environmentSensor/readTimeoutMs"), QString::number(config.environmentSensor.readTimeoutMs));
    add(QStringLiteral("environmentSensor/writeTimeoutMs"), QString::number(config.environmentSensor.writeTimeoutMs));
    add(QStringLiteral("environmentSensor/pollIntervalMs"), QString::number(config.environmentSensor.pollIntervalMs));
    add(QStringLiteral("environmentSensor/deviceAddress"), QString::number(config.environmentSensor.deviceAddress));

    add(QStringLiteral("pulseGenerator/enabled"), boolean(config.pulseGenerator.enabled));
    add(QStringLiteral("pulseGenerator/portName"), escape(config.pulseGenerator.portName));
    add(QStringLiteral("pulseGenerator/baudRate"), QString::number(config.pulseGenerator.baudRate));
    add(QStringLiteral("pulseGenerator/terminalId"), QString::number(config.pulseGenerator.terminalId));
    add(QStringLiteral("pulseGenerator/frequencyHz"), number(config.pulseGenerator.frequencyHz));
    add(QStringLiteral("pulseGenerator/pulseCount"), QString::number(config.pulseGenerator.pulseCount));
    add(QStringLiteral("pulseGenerator/dutyPercent"), number(config.pulseGenerator.dutyPercent));
    add(QStringLiteral("pulseGenerator/remoteControl"), boolean(config.pulseGenerator.remoteControl));

    add(QStringLiteral("autoAcquisition/enabled"), boolean(config.autoAcquisition.enabled));
    add(QStringLiteral("autoAcquisition/latitudeDeg"), number(config.autoAcquisition.latitudeDeg));
    add(QStringLiteral("autoAcquisition/longitudeDeg"), number(config.autoAcquisition.longitudeDeg));
    add(QStringLiteral("autoAcquisition/startOffsetMinutesAfterSunset"), QString::number(config.autoAcquisition.startOffsetMinutesAfterSunset));
    add(QStringLiteral("autoAcquisition/stopOffsetMinutesBeforeSunrise"), QString::number(config.autoAcquisition.stopOffsetMinutesBeforeSunrise));
    add(QStringLiteral("autoAcquisition/recoveryScanIntervalMinutes"), QString::number(config.autoAcquisition.recoveryScanIntervalMinutes));
    add(QStringLiteral("autoAcquisition/searchMode"), QString::number(static_cast<int>(config.autoAcquisition.searchMode)));
    add(QStringLiteral("autoAcquisition/starFindingAttemptDurationSec"), QString::number(config.autoAcquisition.starFindingAttemptDurationSec));
    add(QStringLiteral("autoAcquisition/testTimeOverrideEnabled"), boolean(config.autoAcquisition.testTimeOverrideEnabled));
    add(QStringLiteral("autoAcquisition/testStartTime"), config.autoAcquisition.testStartTime.toString(QStringLiteral("HH:mm:ss")));
    add(QStringLiteral("autoAcquisition/testStopTime"), config.autoAcquisition.testStopTime.toString(QStringLiteral("HH:mm:ss")));

    add(QStringLiteral("network/ip"), escape(config.network.ip));
    add(QStringLiteral("network/port"), QString::number(config.network.port));
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

bool write(const QString& sessionDirectory,
           const QString& relativeFileName,
           const AppConfig& config,
           QString* errorMessage)
{
    if (!isSafeFileName(relativeFileName)) {
        if (errorMessage) *errorMessage = QStringLiteral("设置快照文件名无效");
        return false;
    }
    const QDir directory(sessionDirectory);
    if (!directory.exists()) {
        if (errorMessage) *errorMessage = QStringLiteral("采集会话目录不存在");
        return false;
    }

    QSaveFile file(directory.filePath(relativeFileName));
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }
    const QByteArray text = toUtf8Text(config).toUtf8();
    if (file.write(text) != text.size() || !file.commit()) {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }
    return true;
}

} // namespace AppConfigSnapshot
