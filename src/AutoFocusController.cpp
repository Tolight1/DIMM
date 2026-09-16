#include "AutoFocusController.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace {

bool isPositive(double value)
{
    return std::isfinite(value) && value > 0.0;
}

bool isProbability(double value)
{
    return std::isfinite(value) && value >= 0.0 && value < 0.5;
}

bool hasValidReference(const AutoFocusReferenceMetrics& reference)
{
    return isPositive(reference.hfr) && isPositive(reference.rms);
}

double median(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2;
    return values.size() % 2 == 0 ? (values[middle - 1] + values[middle]) / 2.0
                                  : values[middle];
}

double aggregateValues(std::vector<double> values,
                       AutoFocusStatisticsMode mode,
                       double trimRatio)
{
    if (mode == AutoFocusStatisticsMode::Median) {
        return median(std::move(values));
    }
    if (mode == AutoFocusStatisticsMode::TrimmedMean) {
        std::sort(values.begin(), values.end());
        const std::size_t trimCount = static_cast<std::size_t>(
            std::floor(static_cast<double>(values.size()) * trimRatio));
        if (trimCount * 2 >= values.size()) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        values = std::vector<double>(values.begin() + static_cast<std::ptrdiff_t>(trimCount),
                                     values.end() - static_cast<std::ptrdiff_t>(trimCount));
    }
    return std::accumulate(values.begin(), values.end(), 0.0) /
           static_cast<double>(values.size());
}

double standardDeviation(const std::vector<double>& values, double mean)
{
    double sumSquaredDifference = 0.0;
    for (const double value : values) {
        const double difference = value - mean;
        sumSquaredDifference += difference * difference;
    }
    return std::sqrt(sumSquaredDifference / static_cast<double>(values.size()));
}

} // namespace

std::string AutoFocusController::validateConfig(const AutoFocusConfig& config)
{
    if (!config.masterEnabled) {
        return {};
    }
    const bool anyCameraEnabled = config.cameraEnabled[0] || config.cameraEnabled[1];
    if (!anyCameraEnabled) {
        return {};
    }
    if (config.framesPerState <= 0 || config.focusStep <= 0 ||
        config.settleTimeMs < 0 || config.focuserStageTimeoutMs <= 0 ||
        !isPositive(config.temperatureTriggerThreshold) ||
        config.maximumSearchRange < config.focusStep || config.maximumIterations <= 0 ||
        !isPositive(config.directionImprovementThreshold) ||
        !isPositive(config.directionWorseningThreshold) ||
        !isProbability(config.directionImprovementThreshold) ||
        !isProbability(config.directionWorseningThreshold) ||
        !isPositive(config.hfrImprovementThreshold) ||
        !isProbability(config.callbackHfrTolerance) ||
        config.startupSearchRadius < config.focusStep ||
        !isProbability(config.hfrFinalTolerance) ||
        !isPositive(config.focusStabilityThreshold) ||
        !isProbability(config.rmsAuxiliaryThreshold) || config.callbackStep < 0) {
        return "Autofocus motion or threshold configuration is invalid.";
    }
    if (config.statisticsMode == AutoFocusStatisticsMode::TrimmedMean &&
        (!isProbability(config.trimRatio) ||
         static_cast<int>(std::floor(config.framesPerState * config.trimRatio)) * 2 >=
             config.framesPerState)) {
        return "Autofocus trimmed-mean configuration is invalid.";
    }
    for (int camera = 0; camera < 2; ++camera) {
        const AutoFocusReferenceMetrics& reference = config.reference[camera];
        if (config.cameraEnabled[camera] &&
            ((!config.autoCalibrateHfr[camera] && !isPositive(reference.hfr)) ||
             (!config.autoCalibrateRms[camera] && !isPositive(reference.rms)) ||
             !isPositive(config.reasonableHfrMinimum[camera]))) {
            return "Autofocus reference metrics are invalid.";
        }
    }
    return {};
}

AutoFocusMetrics AutoFocusController::aggregate(const std::vector<AutoFocusSample>& samples,
                                                 AutoFocusStatisticsMode mode,
                                                 double trimRatio)
{
    AutoFocusMetrics metrics;
    metrics.rawFrameCount = samples.size();
    std::vector<double> hfr;
    std::vector<double> rms;
    for (const AutoFocusSample& sample : samples) {
        if (!sample.valid || !isPositive(sample.hfr) || !isPositive(sample.rms)) {
            continue;
        }
        hfr.push_back(sample.hfr);
        rms.push_back(sample.rms);
    }
    metrics.validFrameCount = hfr.size();
    if (hfr.empty() || (mode == AutoFocusStatisticsMode::TrimmedMean &&
                        (!isProbability(trimRatio) ||
                         static_cast<std::size_t>(std::floor(hfr.size() * trimRatio)) * 2 >= hfr.size()))) {
        return metrics;
    }
    metrics.hfr = aggregateValues(hfr, mode, trimRatio);
    metrics.rms = aggregateValues(rms, mode, trimRatio);
    metrics.hfrStandardDeviation = standardDeviation(hfr, metrics.hfr);
    metrics.rmsStandardDeviation = standardDeviation(rms, metrics.rms);
    metrics.valid = std::isfinite(metrics.hfr) && std::isfinite(metrics.rms);
    return metrics;
}

AutoFocusController::AutoFocusController(AutoFocusConfig config)
    : m_config(std::move(config))
{
}

void AutoFocusController::setConfig(AutoFocusConfig config)
{
    m_config = std::move(config);
}

AutoFocusAction AutoFocusController::start(int cameraIndex,
                                            double temperature,
                                            AutoFocusTrigger trigger)
{
    if (cameraIndex < 0 || cameraIndex >= static_cast<int>(m_runtime.size()) ||
        !m_config.masterEnabled || !m_config.cameraEnabled[cameraIndex] ||
        !validateConfig(m_config).empty() ||
        !hasValidReference(m_config.reference[cameraIndex]) || !std::isfinite(temperature)) {
        return {AutoFocusActionType::Failed, 0};
    }

    CameraRuntime& runtime = m_runtime[cameraIndex];
    const double lastCheckedTemperature = runtime.lastCheckedTemperature;
    const bool temperatureBaselineValid = runtime.temperatureBaselineValid;
    runtime = {};
    runtime.state = AutoFocusRunState::AwaitingInitialMetrics;
    runtime.trigger = trigger;
    runtime.runSearchRadius = trigger == AutoFocusTrigger::AutoAcquisitionStartup
                                  ? m_config.startupSearchRadius
                                  : m_config.maximumSearchRange;
    const int temperatureSearchDirection =
        m_hasLastAutofocusTemperature[cameraIndex] &&
                temperature < m_lastAutofocusTemperature[cameraIndex]
            ? 1
            : -1;
    runtime.searchDirection = trigger == AutoFocusTrigger::AutoAcquisitionStartup
                                  ? -1
                                  : trigger == AutoFocusTrigger::Temperature
                                        ? temperatureSearchDirection
                                        : (m_lastCompletedMoveDirection[cameraIndex] == 0
                                               ? 1
                                               : m_lastCompletedMoveDirection[cameraIndex]);
    runtime.startTemperature = temperature;
    runtime.lastCheckedTemperature = lastCheckedTemperature;
    runtime.temperatureBaselineValid = temperatureBaselineValid;
    if (trigger == AutoFocusTrigger::AutoAcquisitionStartup ||
        trigger == AutoFocusTrigger::Temperature) {
        m_lastAutofocusTemperature[cameraIndex] = temperature;
        m_hasLastAutofocusTemperature[cameraIndex] = true;
    }
    return {AutoFocusActionType::AwaitingInitialMetrics, 0};
}

AutoFocusAction AutoFocusController::start(int cameraIndex, double temperature, bool manual)
{
    return start(cameraIndex,
                 temperature,
                 manual ? AutoFocusTrigger::Manual : AutoFocusTrigger::Temperature);
}

AutoFocusAction AutoFocusController::startReferenceCalibration(int cameraIndex,
                                                                std::int64_t nowMs)
{
    if (cameraIndex < 0 || cameraIndex >= static_cast<int>(m_runtime.size()) ||
        !m_config.masterEnabled || !m_config.cameraEnabled[cameraIndex] ||
        !validateConfig(m_config).empty()) {
        return {AutoFocusActionType::Failed, 0};
    }
    CameraRuntime& runtime = m_runtime[cameraIndex];
    runtime = {};
    runtime.state = AutoFocusRunState::ReferenceCalibration;
    runtime.readyAfterMs = nowMs + m_config.settleTimeMs;
    runtime.stageDeadlineMs = nowMs + m_config.focuserStageTimeoutMs;
    return {AutoFocusActionType::None, 0};
}

AutoFocusAction AutoFocusController::updateTemperature(int cameraIndex,
                                                        double temperature,
                                                        bool valid)
{
    if (cameraIndex < 0 || cameraIndex >= static_cast<int>(m_runtime.size()) ||
        !std::isfinite(temperature) || !m_config.masterEnabled ||
        !m_config.cameraEnabled[cameraIndex]) {
        return {AutoFocusActionType::None, 0};
    }

    CameraRuntime& runtime = m_runtime[cameraIndex];
    if (!valid) {
        runtime.temperatureBaselineValid = false;
        return {AutoFocusActionType::None, 0};
    }
    if (!runtime.temperatureBaselineValid) {
        runtime.lastCheckedTemperature = temperature;
        runtime.temperatureBaselineValid = true;
        return {AutoFocusActionType::None, 0};
    }
    if (runtime.state != AutoFocusRunState::Idle &&
        runtime.state != AutoFocusRunState::Complete &&
        runtime.state != AutoFocusRunState::Failed) {
        return {AutoFocusActionType::None, 0};
    }
    if (std::abs(temperature - runtime.lastCheckedTemperature) <
        m_config.temperatureTriggerThreshold) {
        return {AutoFocusActionType::None, 0};
    }
    return start(cameraIndex, temperature, AutoFocusTrigger::Temperature);
}

void AutoFocusController::primeTemperatureBaseline(int cameraIndex,
                                                    double temperature,
                                                    bool valid)
{
    if (cameraIndex < 0 || cameraIndex >= static_cast<int>(m_runtime.size())) {
        return;
    }

    CameraRuntime& runtime = m_runtime[cameraIndex];
    if (!valid || !std::isfinite(temperature)) {
        runtime.temperatureBaselineValid = false;
        return;
    }

    if (runtime.state == AutoFocusRunState::Idle || runtime.state == AutoFocusRunState::Complete ||
        runtime.state == AutoFocusRunState::Failed) {
        runtime.lastCheckedTemperature = temperature;
        runtime.temperatureBaselineValid = true;
    }
}

AutoFocusAction AutoFocusController::submitMetrics(int cameraIndex,
                                                    const AutoFocusMetrics& metrics)
{
    if (cameraIndex < 0 || cameraIndex >= static_cast<int>(m_runtime.size()) ||
        !metrics.valid) {
        return {AutoFocusActionType::Failed, 0};
    }

    CameraRuntime& runtime = m_runtime[cameraIndex];
    if (runtime.pendingMoveDirection != 0) {
        m_lastCompletedMoveDirection[cameraIndex] = runtime.pendingMoveDirection;
        runtime.pendingMoveDirection = 0;
    }
    runtime.currentMetrics = metrics;
    runtime.hasCurrentMetrics = true;

    const auto finish = [&runtime](AutoFocusCompletionKind completion) {
        runtime.completionKind = completion;
        runtime.state = AutoFocusRunState::Complete;
        runtime.stageDeadlineMs = 0;
        runtime.lastCheckedTemperature = runtime.startTemperature;
        runtime.temperatureBaselineValid = true;
        return AutoFocusAction{AutoFocusActionType::Finished, 0};
    };

    switch (runtime.state) {
    case AutoFocusRunState::ReferenceCalibration:
        if (m_config.autoCalibrateHfr[cameraIndex]) {
            m_config.reference[cameraIndex].hfr = metrics.hfr;
        }
        if (m_config.autoCalibrateRms[cameraIndex]) {
            m_config.reference[cameraIndex].rms = metrics.rms;
        }
        runtime = {};
        return {AutoFocusActionType::ReferenceCalibrated, 0};
    case AutoFocusRunState::AwaitingInitialMetrics:
        if (reachesReference(cameraIndex, metrics)) {
            return finish(AutoFocusCompletionKind::AlreadyWithinTolerance);
        }
        runtime.previousSearchMetrics = metrics;
        runtime.hasPreviousSearchMetrics = true;
        runtime.provisionalMetrics = metrics;
        runtime.hasProvisionalMetrics = true;
        runtime.hasBestMetrics = false;
        runtime.unqualifiedOvershootHits = 0;
        runtime.improvingThresholdHits = 0;
        runtime.worseningThresholdHits = 0;
        runtime.state = AutoFocusRunState::DirectionSearch;
        return requestMove(runtime, runtime.searchDirection * m_config.focusStep);
    case AutoFocusRunState::DirectionSearch: {
        if (reachesReference(cameraIndex, metrics)) {
            return finish(AutoFocusCompletionKind::ReachedTargetBoundary);
        }

        const bool improving = metrics.hfr <= runtime.previousSearchMetrics.hfr *
                               (1.0 - m_config.directionImprovementThreshold);
        const bool worsening = metrics.hfr >= runtime.previousSearchMetrics.hfr *
                               (1.0 + m_config.directionWorseningThreshold);
        runtime.previousSearchMetrics = metrics;
        runtime.hasPreviousSearchMetrics = true;
        if (improving) {
            ++runtime.improvingThresholdHits;
            runtime.worseningThresholdHits = 0;
            if (runtime.improvingThresholdHits >= 2) {
                runtime.improvingThresholdHits = 0;
                runtime.provisionalMetrics = metrics;
                runtime.hasProvisionalMetrics = true;
                runtime.unqualifiedOvershootHits = 0;
                runtime.state = AutoFocusRunState::Adjusting;
            }
            return requestSearchMove(runtime);
        }
        if (worsening) {
            ++runtime.worseningThresholdHits;
            runtime.improvingThresholdHits = 0;
            if (runtime.worseningThresholdHits >= 2) {
                runtime.searchDirection = -signOf(runtime.searchDirection);
                runtime.improvingThresholdHits = 0;
                runtime.worseningThresholdHits = 0;
                if (runtime.directionCorrectionUsed || m_config.callbackStep == 0) {
                    runtime.provisionalMetrics = metrics;
                    runtime.hasProvisionalMetrics = true;
                    runtime.unqualifiedOvershootHits = 0;
                    runtime.state = AutoFocusRunState::Adjusting;
                    return requestSearchMove(runtime);
                }
                runtime.directionCorrectionUsed = true;
                runtime.state = AutoFocusRunState::Callback;
                runtime.callbackMeasurementPending = true;
                return requestMove(runtime, runtime.searchDirection * m_config.callbackStep);
            }
        }
        return requestSearchMove(runtime);
    }
    case AutoFocusRunState::Callback:
        runtime.callbackMeasurementPending = false;
        if (reachesReference(cameraIndex, metrics)) {
            return finish(AutoFocusCompletionKind::ReturnedToBest);
        }
        runtime.previousSearchMetrics = metrics;
        runtime.hasPreviousSearchMetrics = true;
        runtime.provisionalMetrics = metrics;
        runtime.hasProvisionalMetrics = true;
        runtime.unqualifiedOvershootHits = 0;
        runtime.improvingThresholdHits = 0;
        runtime.worseningThresholdHits = 0;
        runtime.state = AutoFocusRunState::Adjusting;
        return requestSearchMove(runtime);
    case AutoFocusRunState::Adjusting:
        if (reachesReference(cameraIndex, metrics)) {
            return finish(AutoFocusCompletionKind::ReachedTargetBoundary);
        }
        if (runtime.hasBestMetrics) {
            if (metrics.hfr < runtime.bestMetrics.hfr) {
                runtime.bestMetrics = metrics;
                runtime.bestOffset = runtime.currentOffset;
            } else if (metrics.hfr > runtime.bestMetrics.hfr *
                                          (1.0 + m_config.hfrImprovementThreshold)) {
                if (runtime.callbackUsed) {
                    return failRun(runtime);
                }
                runtime.callbackUsed = true;
                runtime.callbackTargetHfr = runtime.bestMetrics.hfr;
                runtime.callbackTargetOffset = runtime.bestOffset;
                runtime.searchDirection = -signOf(runtime.searchDirection);
                runtime.callbackSearchBestHfr = runtime.callbackTargetHfr;
                runtime.callbackSearchHasSample = false;
                return returnToBest(runtime, false);
            }
        } else {
            if (!runtime.hasProvisionalMetrics || metrics.hfr < runtime.provisionalMetrics.hfr) {
                runtime.provisionalMetrics = metrics;
                runtime.hasProvisionalMetrics = true;
                runtime.unqualifiedOvershootHits = 0;
            }
            if (metrics.hfr <= m_config.reasonableHfrMinimum[cameraIndex]) {
                runtime.bestMetrics = metrics;
                runtime.bestOffset = runtime.currentOffset;
                runtime.hasBestMetrics = true;
                runtime.unqualifiedOvershootHits = 0;
            } else if (metrics.hfr > runtime.provisionalMetrics.hfr *
                                          (1.0 + m_config.hfrImprovementThreshold)) {
                ++runtime.unqualifiedOvershootHits;
                if (runtime.unqualifiedOvershootHits >= 2) {
                    return failRun(runtime);
                }
            }
        }
        runtime.previousSearchMetrics = metrics;
        runtime.hasPreviousSearchMetrics = true;
        return requestSearchMove(runtime);
    case AutoFocusRunState::OvershootReturnSearch:
        if (metrics.hfr <= runtime.callbackTargetHfr * (1.0 + m_config.callbackHfrTolerance)) {
            return finish(AutoFocusCompletionKind::ReturnedToBest);
        }
        if (!runtime.callbackSearchHasSample) {
            runtime.callbackSearchHasSample = true;
            runtime.callbackSearchBestHfr = metrics.hfr;
            runtime.previousSearchMetrics = metrics;
            runtime.hasPreviousSearchMetrics = true;
            return requestSearchMove(runtime);
        }
        if (metrics.hfr < runtime.callbackSearchBestHfr) {
            runtime.callbackSearchBestHfr = metrics.hfr;
        } else if (metrics.hfr > runtime.callbackSearchBestHfr *
                                       (1.0 + m_config.hfrImprovementThreshold)) {
            return failRun(runtime);
        }
        runtime.previousSearchMetrics = metrics;
        runtime.hasPreviousSearchMetrics = true;
        return requestSearchMove(runtime);
    case AutoFocusRunState::FinalValidation:
        return failRun(runtime);
    default:
        return {AutoFocusActionType::Failed, 0};
    }
}

AutoFocusAction AutoFocusController::submitSample(int cameraIndex,
                                                   const AutoFocusSample& sample,
                                                   std::int64_t nowMs)
{
    if (!readyForFrames(cameraIndex, nowMs)) {
        return {AutoFocusActionType::None, 0};
    }

    CameraRuntime& runtime = m_runtime[cameraIndex];
    runtime.samples.push_back(sample);
    const std::size_t validCount = static_cast<std::size_t>(std::count_if(
        runtime.samples.begin(), runtime.samples.end(), [](const AutoFocusSample& candidate) {
            return candidate.valid && isPositive(candidate.hfr) && isPositive(candidate.rms);
        }));
    if (validCount < static_cast<std::size_t>(m_config.framesPerState)) {
        return {AutoFocusActionType::None, 0};
    }

    const AutoFocusMetrics metrics =
        aggregate(runtime.samples, m_config.statisticsMode, m_config.trimRatio);
    runtime.samples.clear();
    AutoFocusAction action = submitMetrics(cameraIndex, metrics);
    action.metricsReady = true;
    return action;
}

void AutoFocusController::armStageTimeout(int cameraIndex, std::int64_t nowMs)
{
    if (cameraIndex < 0 || cameraIndex >= static_cast<int>(m_runtime.size()) ||
        !isActive(cameraIndex)) {
        return;
    }
    m_runtime[cameraIndex].stageDeadlineMs = nowMs + m_config.focuserStageTimeoutMs;
}

void AutoFocusController::markFocuserMoveAcknowledged(int cameraIndex)
{
    if (cameraIndex < 0 || cameraIndex >= static_cast<int>(m_runtime.size()) ||
        !isActive(cameraIndex)) {
        return;
    }
    CameraRuntime& runtime = m_runtime[cameraIndex];
    if (runtime.state != AutoFocusRunState::ReferenceCalibration && runtime.awaitingFocuserStop) {
        runtime.focuserMotionObserved = true;
    }
}

AutoFocusAction AutoFocusController::checkTimeout(int cameraIndex, std::int64_t nowMs)
{
    if (cameraIndex < 0 || cameraIndex >= static_cast<int>(m_runtime.size())) {
        return {AutoFocusActionType::None, 0};
    }
    CameraRuntime& runtime = m_runtime[cameraIndex];
    if (!isActive(cameraIndex) || runtime.stageDeadlineMs == 0 ||
        nowMs < runtime.stageDeadlineMs) {
        return {AutoFocusActionType::None, 0};
    }
    runtime.state = AutoFocusRunState::Failed;
    runtime.stageDeadlineMs = 0;
    runtime.awaitingFocuserStop = false;
    runtime.focuserMotionObserved = false;
    runtime.lastCheckedTemperature = runtime.startTemperature;
    runtime.temperatureBaselineValid = true;
    return {AutoFocusActionType::Failed, 0};
}
void AutoFocusController::updateFocuserState(int cameraIndex,
                                              bool deviceOpened,
                                              bool moving,
                                              std::int64_t nowMs)
{
    if (cameraIndex < 0 || cameraIndex >= static_cast<int>(m_runtime.size())) {
        return;
    }
    CameraRuntime& runtime = m_runtime[cameraIndex];
    if (!deviceOpened) {
        const bool wasActive = runtime.state != AutoFocusRunState::Idle &&
                               runtime.state != AutoFocusRunState::Complete &&
                               runtime.state != AutoFocusRunState::Failed;
        const double startTemperature = runtime.startTemperature;
        runtime = {};
        runtime.state = AutoFocusRunState::Failed;
        if (wasActive && std::isfinite(startTemperature)) {
            runtime.lastCheckedTemperature = startTemperature;
            runtime.temperatureBaselineValid = true;
        }
        return;
    }
    if (moving) {
        if (isActive(cameraIndex) && runtime.state != AutoFocusRunState::ReferenceCalibration) {
            runtime.awaitingFocuserStop = true;
            runtime.focuserMotionObserved = true;
        }
        return;
    }
    if (runtime.awaitingFocuserStop) {
        if (!runtime.focuserMotionObserved) {
            return;
        }
        runtime.awaitingFocuserStop = false;
        runtime.focuserMotionObserved = false;
        runtime.stageDeadlineMs = 0;
        runtime.readyAfterMs = nowMs + m_config.settleTimeMs;
        if (runtime.state == AutoFocusRunState::ReturningBest) {
            runtime.state = AutoFocusRunState::OvershootReturnSearch;
            runtime.callbackSearchHasSample = false;
        }
    }
}

bool AutoFocusController::readyForFrames(int cameraIndex, std::int64_t nowMs) const
{
    if (cameraIndex < 0 || cameraIndex >= static_cast<int>(m_runtime.size())) {
        return false;
    }
    const CameraRuntime& runtime = m_runtime[cameraIndex];
    return runtime.state != AutoFocusRunState::Idle &&
           runtime.state != AutoFocusRunState::Complete &&
           runtime.state != AutoFocusRunState::Failed &&
           !runtime.awaitingFocuserStop && nowMs >= runtime.readyAfterMs;
}

AutoFocusAction AutoFocusController::requestSearchMove(CameraRuntime& runtime)
{
    const int nextOffset = runtime.currentOffset + runtime.searchDirection * m_config.focusStep;
    const int searchRadius = runtime.runSearchRadius > 0 ? runtime.runSearchRadius
                                                          : m_config.maximumSearchRange;
    if (runtime.searchIterations >= m_config.maximumIterations ||
        std::abs(nextOffset) > searchRadius) {
        if (runtime.trigger == AutoFocusTrigger::AutoAcquisitionStartup || runtime.callbackUsed ||
            !runtime.hasBestMetrics) {
            return failRun(runtime);
        }
        runtime.searchDirection = -signOf(runtime.searchDirection);
        return returnToBest(runtime, false);
    }
    ++runtime.searchIterations;
    return requestMove(runtime, runtime.searchDirection * m_config.focusStep);
}

AutoFocusAction AutoFocusController::returnToBest(CameraRuntime& runtime, bool finalValidation)
{
    (void)finalValidation;
    // This move is only a fast approach. Completion is decided in
    // OvershootReturnSearch from a fresh HFR measurement.
    if (!runtime.hasBestMetrics) {
        return failRun(runtime);
    }
    runtime.callbackUsed = true;
    if (runtime.callbackTargetHfr <= 0.0) {
        runtime.callbackTargetHfr = runtime.bestMetrics.hfr;
        runtime.callbackTargetOffset = runtime.bestOffset;
    }
    runtime.unreachedReference = true;
    runtime.completionKind = AutoFocusCompletionKind::ReturnedToBest;
    const int relativeStep = runtime.bestOffset - runtime.currentOffset;
    if (relativeStep == 0) {
        runtime.state = AutoFocusRunState::OvershootReturnSearch;
        runtime.callbackSearchHasSample = false;
        return requestSearchMove(runtime);
    }
    runtime.state = AutoFocusRunState::ReturningBest;
    return requestMove(runtime, relativeStep);
}

AutoFocusAction AutoFocusController::failRun(CameraRuntime& runtime)
{
    runtime.state = AutoFocusRunState::Failed;
    runtime.stageDeadlineMs = 0;
    runtime.unreachedReference = true;
    runtime.lastCheckedTemperature = runtime.startTemperature;
    runtime.temperatureBaselineValid = true;
    return {AutoFocusActionType::Failed, 0};
}

AutoFocusAction AutoFocusController::requestMove(CameraRuntime& runtime, int relativeStep)
{
    const int searchRadius = runtime.runSearchRadius > 0 ? runtime.runSearchRadius
                                                          : m_config.maximumSearchRange;
    if (relativeStep == 0 || std::abs(runtime.currentOffset + relativeStep) > searchRadius) {
        return failRun(runtime);
    }
    runtime.currentOffset += relativeStep;
    runtime.pendingMoveDirection = signOf(relativeStep);
    runtime.awaitingFocuserStop = true;
    runtime.focuserMotionObserved = false;
    runtime.readyAfterMs = 0;
    runtime.stageDeadlineMs = 0;
    runtime.samples.clear();
    return {AutoFocusActionType::MoveRelative, relativeStep};
}

int AutoFocusController::signOf(int value)
{
    return (value > 0) - (value < 0);
}

void AutoFocusController::cancel(int cameraIndex)
{
    if (cameraIndex >= 0 && cameraIndex < static_cast<int>(m_runtime.size())) {
        m_runtime[cameraIndex] = {};
    }
}

bool AutoFocusController::isActive(int cameraIndex) const
{
    if (cameraIndex < 0 || cameraIndex >= static_cast<int>(m_runtime.size())) {
        return false;
    }
    const AutoFocusRunState runState = m_runtime[cameraIndex].state;
    return runState != AutoFocusRunState::Idle && runState != AutoFocusRunState::Complete &&
           runState != AutoFocusRunState::Failed;
}

AutoFocusRunState AutoFocusController::state(int cameraIndex) const
{
    return cameraIndex >= 0 && cameraIndex < static_cast<int>(m_runtime.size())
               ? m_runtime[cameraIndex].state
               : AutoFocusRunState::Failed;
}

double AutoFocusController::lastCheckedTemperature(int cameraIndex) const
{
    return cameraIndex >= 0 && cameraIndex < static_cast<int>(m_runtime.size())
               ? m_runtime[cameraIndex].lastCheckedTemperature
               : std::numeric_limits<double>::quiet_NaN();
}

AutoFocusReferenceMetrics AutoFocusController::referenceMetrics(int cameraIndex) const
{
    return cameraIndex >= 0 && cameraIndex < static_cast<int>(m_config.reference.size())
               ? m_config.reference[cameraIndex]
               : AutoFocusReferenceMetrics{};
}

AutoFocusRunSnapshot AutoFocusController::snapshot(int cameraIndex) const
{
    AutoFocusRunSnapshot snapshot;
    if (cameraIndex < 0 || cameraIndex >= static_cast<int>(m_runtime.size())) {
        snapshot.state = AutoFocusRunState::Failed;
        return snapshot;
    }
    const CameraRuntime& runtime = m_runtime[cameraIndex];
    snapshot.state = runtime.state;
    snapshot.currentMetrics = runtime.currentMetrics;
    snapshot.hasCurrentMetrics = runtime.hasCurrentMetrics;
    snapshot.currentOffset = runtime.currentOffset;
    snapshot.bestOffset = runtime.bestOffset;
    snapshot.accumulatedBacklashTravel = 0;
    snapshot.bestHfr = runtime.hasBestMetrics ? runtime.bestMetrics.hfr : 0.0;
    snapshot.searchDirection = runtime.searchDirection;
    snapshot.unreachedReference = runtime.unreachedReference;
    snapshot.completionKind = runtime.completionKind;
    return snapshot;
}

bool AutoFocusController::reachesReference(int cameraIndex,
                                            const AutoFocusMetrics& metrics) const
{
    const AutoFocusReferenceMetrics& reference = m_config.reference[cameraIndex];
    return metrics.hfr <= reference.hfr * (1.0 + m_config.hfrFinalTolerance) &&
           metrics.rms <= reference.rms * (1.0 + m_config.rmsAuxiliaryThreshold) &&
           metrics.hfrStandardDeviation <= metrics.hfr * m_config.focusStabilityThreshold;
}

AutoFocusTrigger AutoFocusController::trigger(int cameraIndex) const
{
    if (cameraIndex < 0 || cameraIndex >= static_cast<int>(m_runtime.size())) {
        return AutoFocusTrigger::Manual;
    }
    return m_runtime[cameraIndex].trigger;
}
