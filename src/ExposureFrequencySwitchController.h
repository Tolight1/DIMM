#pragma once

#include "ExposureFrameRateRules.h"

#include <functional>

#include <QString>

enum class ExposureFrequencySwitchAction {
    NoChange,
    ExposureOnly,
    FrequencyAndExposure
};

struct ExposureFrequencySwitchPlan {
    ExposureFrequencySwitchAction action = ExposureFrequencySwitchAction::NoChange;
    double targetFrequencyHz = 0.0;
};

enum class ExposureFrequencySwitchResult {
    NoFrequencyChange,
    Applied,
    RolledBack,
    Failed,
};

struct ExposureFrequencySwitchCallbacks {
    std::function<bool(QString* reason)> stopTriggerOutput;
    std::function<bool(double frequencyHz, QString* reason)> applyFrequency;
    std::function<bool(QString* reason)> applyExposures;
    std::function<bool(double frequencyHz, QString* reason)> startTriggerOutput;
    std::function<bool(QString* reason)> rollback;
    std::function<void()> flushQueues;
};

class ExposureFrequencySwitchController
{
public:
    static ExposureFrequencySwitchPlan plan(double oldExposureAUs,
                                            double oldExposureBUs,
                                            double newExposureAUs,
                                            double newExposureBUs,
                                            double activeFrequencyHz,
                                            const QVector<ExposureFrameRateWindow>& windows,
                                            bool frequencySwitchEnabled = true);

    static ExposureFrequencySwitchResult switchForExposure(
        const ExposureFrequencySwitchPlan& plan,
        const ExposureFrequencySwitchCallbacks& callbacks,
        QString* reason = nullptr);

    static bool acceptsMeasuredFrameRates(double targetFrequencyHz,
                                          double camera0FrequencyHz,
                                          double camera1FrequencyHz);
};
