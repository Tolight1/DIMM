#include "AutoFocusSettings.h"

#include <QSettings>

namespace {

QString cameraKey(int cameraIndex, const QString& name)
{
    return QStringLiteral("autofocus/camera%1/%2").arg(cameraIndex + 1).arg(name);
}

QString valueKey(const QString& name)
{
    return QStringLiteral("autofocus/%1").arg(name);
}

} // namespace

AutoFocusConfig AutoFocusSettings::load(QSettings& settings)
{
    AutoFocusConfig config;
    config.masterEnabled = settings.value(valueKey(QStringLiteral("master_enabled")), false).toBool();
    for (int camera = 0; camera < 2; ++camera) {
        config.cameraEnabled[camera] =
            settings.value(cameraKey(camera, QStringLiteral("enabled")), false).toBool();
        config.reference[camera].hfr =
            settings.value(cameraKey(camera, QStringLiteral("reference_hfr")), 0.0).toDouble();
        config.reference[camera].rms =
            settings.value(cameraKey(camera, QStringLiteral("reference_rms")), 0.0).toDouble();
        config.reasonableHfrMinimum[camera] = settings.value(
            cameraKey(camera, QStringLiteral("reasonable_hfr_minimum")),
            config.reference[camera].hfr > 0.0 ? config.reference[camera].hfr : 3.0).toDouble();
        config.autoCalibrateHfr[camera] =
            settings.value(cameraKey(camera, QStringLiteral("auto_calibrate_hfr")), false).toBool();
        config.autoCalibrateRms[camera] =
            settings.value(cameraKey(camera, QStringLiteral("auto_calibrate_rms")), false).toBool();
    }
    config.framesPerState = settings.value(valueKey(QStringLiteral("frames_per_state")), 0).toInt();
    config.settleTimeMs = settings.value(valueKey(QStringLiteral("settle_time_ms")), 0).toInt();
    config.focuserStageTimeoutMs =
        settings.value(valueKey(QStringLiteral("focuser_stage_timeout_ms")), 30000).toInt();
    config.temperatureTriggerThreshold =
        settings.value(valueKey(QStringLiteral("temperature_trigger_threshold")), 0.0).toDouble();
    config.statisticsMode = static_cast<AutoFocusStatisticsMode>(
        settings.value(valueKey(QStringLiteral("statistics_mode")),
                       static_cast<int>(AutoFocusStatisticsMode::Mean))
            .toInt());
    config.trimRatio = settings.value(valueKey(QStringLiteral("trim_ratio")), 0.0).toDouble();
    config.focusStep = settings.value(valueKey(QStringLiteral("focus_step")), 0).toInt();
    config.maximumSearchRange =
        settings.value(valueKey(QStringLiteral("maximum_search_range")), 0).toInt();
    config.maximumIterations =
        settings.value(valueKey(QStringLiteral("maximum_iterations")), 0).toInt();
    config.hfrImprovementThreshold =
        settings.value(valueKey(QStringLiteral("hfr_improvement_threshold")), 0.0).toDouble();
    config.directionImprovementThreshold = settings.value(
        valueKey(QStringLiteral("direction_improvement_threshold")),
        config.hfrImprovementThreshold).toDouble();
    config.directionWorseningThreshold = settings.value(
        valueKey(QStringLiteral("direction_worsening_threshold")),
        config.hfrImprovementThreshold).toDouble();
    config.initialMetricTolerance =
        settings.value(valueKey(QStringLiteral("initial_metric_tolerance")), 0.10).toDouble();
    config.callbackHfrTolerance =
        settings.value(valueKey(QStringLiteral("callback_hfr_tolerance")), 0.05).toDouble();
    config.startupSearchRadius = settings.value(
        valueKey(QStringLiteral("startup_search_radius")),
        config.maximumSearchRange).toInt();
    config.hfrFinalTolerance =
        settings.value(valueKey(QStringLiteral("hfr_final_tolerance")), 0.0).toDouble();
    config.focusStabilityThreshold =
        settings.value(valueKey(QStringLiteral("focus_stability_threshold")), 0.0).toDouble();
    config.rmsAuxiliaryThreshold =
        settings.value(valueKey(QStringLiteral("rms_auxiliary_threshold")), 0.0).toDouble();
    const QString callbackStepKey = valueKey(QStringLiteral("callback_step"));
    const QString legacyCallbackStepKey =
        valueKey(QStringLiteral("reverse_backlash_probe_step"));
    config.callbackStep = settings.contains(callbackStepKey)
                              ? settings.value(callbackStepKey).toInt()
                              : settings.value(legacyCallbackStepKey, 0).toInt();
    config.dataLoggingEnabled =
        settings.value(valueKey(QStringLiteral("data_logging_enabled")), false).toBool();
    return config;
}

void AutoFocusSettings::save(QSettings& settings, const AutoFocusConfig& config)
{
    settings.setValue(valueKey(QStringLiteral("master_enabled")), config.masterEnabled);
    for (int camera = 0; camera < 2; ++camera) {
        settings.setValue(cameraKey(camera, QStringLiteral("enabled")), config.cameraEnabled[camera]);
        settings.setValue(cameraKey(camera, QStringLiteral("reference_hfr")), config.reference[camera].hfr);
        settings.setValue(cameraKey(camera, QStringLiteral("reference_rms")), config.reference[camera].rms);
        settings.setValue(cameraKey(camera, QStringLiteral("reasonable_hfr_minimum")),
                          config.reasonableHfrMinimum[camera]);
        settings.setValue(cameraKey(camera, QStringLiteral("auto_calibrate_hfr")),
                          config.autoCalibrateHfr[camera]);
        settings.setValue(cameraKey(camera, QStringLiteral("auto_calibrate_rms")),
                          config.autoCalibrateRms[camera]);
        settings.remove(cameraKey(camera, QStringLiteral("reference_tenengrad")));
        settings.remove(cameraKey(camera, QStringLiteral("auto_calibrate_tenengrad")));
    }
    settings.setValue(valueKey(QStringLiteral("frames_per_state")), config.framesPerState);
    settings.setValue(valueKey(QStringLiteral("settle_time_ms")), config.settleTimeMs);
    settings.setValue(valueKey(QStringLiteral("focuser_stage_timeout_ms")),
                      config.focuserStageTimeoutMs);
    settings.setValue(valueKey(QStringLiteral("temperature_trigger_threshold")),
                      config.temperatureTriggerThreshold);
    settings.setValue(valueKey(QStringLiteral("statistics_mode")),
                      static_cast<int>(config.statisticsMode));
    settings.setValue(valueKey(QStringLiteral("trim_ratio")), config.trimRatio);
    settings.setValue(valueKey(QStringLiteral("focus_step")), config.focusStep);
    settings.setValue(valueKey(QStringLiteral("maximum_search_range")), config.maximumSearchRange);
    settings.setValue(valueKey(QStringLiteral("maximum_iterations")), config.maximumIterations);
    settings.setValue(valueKey(QStringLiteral("hfr_improvement_threshold")),
                      config.hfrImprovementThreshold);
    settings.setValue(valueKey(QStringLiteral("direction_improvement_threshold")),
                      config.directionImprovementThreshold);
    settings.setValue(valueKey(QStringLiteral("direction_worsening_threshold")),
                      config.directionWorseningThreshold);
    settings.setValue(valueKey(QStringLiteral("initial_metric_tolerance")),
                      config.initialMetricTolerance);
    settings.setValue(valueKey(QStringLiteral("callback_hfr_tolerance")),
                      config.callbackHfrTolerance);
    settings.setValue(valueKey(QStringLiteral("startup_search_radius")),
                      config.startupSearchRadius);
    settings.setValue(valueKey(QStringLiteral("hfr_final_tolerance")), config.hfrFinalTolerance);
    settings.setValue(valueKey(QStringLiteral("focus_stability_threshold")),
                      config.focusStabilityThreshold);
    settings.setValue(valueKey(QStringLiteral("rms_auxiliary_threshold")),
                      config.rmsAuxiliaryThreshold);
    settings.remove(valueKey(QStringLiteral("tenengrad_auxiliary_threshold")));
    settings.setValue(valueKey(QStringLiteral("callback_step")), config.callbackStep);
    settings.remove(valueKey(QStringLiteral("search_dead_zone_probe_step")));
    settings.remove(valueKey(QStringLiteral("search_dead_zone_response_threshold")));
    settings.remove(valueKey(QStringLiteral("search_dead_zone_maximum_travel")));
    settings.remove(valueKey(QStringLiteral("reverse_backlash_probe_step")));
    settings.remove(valueKey(QStringLiteral("reverse_backlash_response_threshold")));
    settings.remove(valueKey(QStringLiteral("reverse_backlash_maximum_travel")));
    settings.setValue(valueKey(QStringLiteral("data_logging_enabled")), config.dataLoggingEnabled);
}

QString AutoFocusSettings::validationError(const AutoFocusConfig& config)
{
    return QString::fromStdString(AutoFocusController::validateConfig(config));
}

bool AutoFocusSettings::manualStartAvailable(bool trackingAvailable)
{
    return trackingAvailable;
}
