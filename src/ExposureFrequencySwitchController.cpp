#include "ExposureFrequencySwitchController.h"

#include "FrameRateChangePolicy.h"

#include <cmath>

ExposureFrequencySwitchPlan ExposureFrequencySwitchController::plan(
    double oldExposureAUs,
    double oldExposureBUs,
    double newExposureAUs,
    double newExposureBUs,
    double activeFrequencyHz,
    const QVector<ExposureFrameRateWindow>& windows,
    bool frequencySwitchEnabled)
{
    const bool exposureChanged = std::abs(oldExposureAUs - newExposureAUs) > 0.5 ||
                                 std::abs(oldExposureBUs - newExposureBUs) > 0.5;
    if (!frequencySwitchEnabled) {
        return {exposureChanged ? ExposureFrequencySwitchAction::ExposureOnly
                                : ExposureFrequencySwitchAction::NoChange,
                activeFrequencyHz};
    }
    const auto selection = selectConfiguredCommonRoiFrameRateForPairUs(
        newExposureAUs, newExposureBUs, windows);
    if (!selection.has_value() || !std::isfinite(selection->frameRateHz) ||
        selection->frameRateHz <= 0.0) {
        return {exposureChanged ? ExposureFrequencySwitchAction::ExposureOnly
                                : ExposureFrequencySwitchAction::NoChange,
                activeFrequencyHz};
    }
    const bool frequencyChanged = needsFrameRateChange(activeFrequencyHz,
                                                        selection->frameRateHz);
    return {frequencyChanged ? ExposureFrequencySwitchAction::FrequencyAndExposure
                             : (exposureChanged ? ExposureFrequencySwitchAction::ExposureOnly
                                                : ExposureFrequencySwitchAction::NoChange),
            selection->frameRateHz};
}

ExposureFrequencySwitchResult ExposureFrequencySwitchController::switchForExposure(
    const ExposureFrequencySwitchPlan& plan,
    const ExposureFrequencySwitchCallbacks& callbacks,
    QString* reason)
{
    if (plan.action != ExposureFrequencySwitchAction::FrequencyAndExposure) {
        return ExposureFrequencySwitchResult::NoFrequencyChange;
    }
    if (!callbacks.stopTriggerOutput || !callbacks.applyFrequency || !callbacks.applyExposures ||
        !callbacks.startTriggerOutput || !callbacks.rollback || !callbacks.flushQueues) {
        if (reason) {
            *reason = QStringLiteral("曝光频率切换回调未完整配置");
        }
        return ExposureFrequencySwitchResult::Failed;
    }

    QString operationReason;
    bool triggerStopped = false;
    const auto fail = [&]() {
        bool rollbackComplete = false;
        if (triggerStopped) {
            rollbackComplete = callbacks.rollback(nullptr);
            callbacks.flushQueues();
        }
        if (reason) {
            *reason = operationReason;
        }
        return rollbackComplete ? ExposureFrequencySwitchResult::RolledBack
                                : ExposureFrequencySwitchResult::Failed;
    };

    if (!callbacks.stopTriggerOutput(&operationReason)) {
        return fail();
    }
    triggerStopped = true;
    if (!callbacks.applyFrequency(plan.targetFrequencyHz, &operationReason) ||
        !callbacks.applyExposures(&operationReason) ||
        !callbacks.startTriggerOutput(plan.targetFrequencyHz, &operationReason)) {
        return fail();
    }
    callbacks.flushQueues();
    return ExposureFrequencySwitchResult::Applied;
}

bool ExposureFrequencySwitchController::acceptsMeasuredFrameRates(
    double targetFrequencyHz,
    double camera0FrequencyHz,
    double camera1FrequencyHz)
{
    if (!std::isfinite(targetFrequencyHz) || targetFrequencyHz <= 0.0 ||
        !std::isfinite(camera0FrequencyHz) || !std::isfinite(camera1FrequencyHz)) {
        return false;
    }
    const double minimumHz = targetFrequencyHz * 0.85;
    const double maximumHz = targetFrequencyHz * 1.05;
    return camera0FrequencyHz >= minimumHz && camera0FrequencyHz <= maximumHz &&
           camera1FrequencyHz >= minimumHz && camera1FrequencyHz <= maximumHz;
}
