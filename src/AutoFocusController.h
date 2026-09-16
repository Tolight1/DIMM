#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum class AutoFocusStatisticsMode {
    Mean,
    Median,
    TrimmedMean
};

struct AutoFocusReferenceMetrics {
    double hfr = 0.0;
    double rms = 0.0;
};

enum class AutoFocusTrigger {
    Manual,
    Temperature,
    AutoAcquisitionStartup
};

struct AutoFocusConfig {
    bool masterEnabled = false;
    std::array<bool, 2> cameraEnabled = {false, false};
    std::array<AutoFocusReferenceMetrics, 2> reference;
    std::array<double, 2> reasonableHfrMinimum = {0.0, 0.0};
    std::array<bool, 2> autoCalibrateHfr = {false, false};
    std::array<bool, 2> autoCalibrateRms = {false, false};
    int framesPerState = 0;
    int settleTimeMs = 0;
    int focuserStageTimeoutMs = 30000;
    double temperatureTriggerThreshold = 0.0;
    AutoFocusStatisticsMode statisticsMode = AutoFocusStatisticsMode::Mean;
    double trimRatio = 0.0;
    int focusStep = 0;
    int maximumSearchRange = 0;
    int maximumIterations = 0;
    double directionImprovementThreshold = 0.0;
    double directionWorseningThreshold = 0.0;
    double hfrImprovementThreshold = 0.0;
    double initialMetricTolerance = 0.10;
    double callbackHfrTolerance = 0.05;
    int startupSearchRadius = 0;
    double hfrFinalTolerance = 0.0;
    double focusStabilityThreshold = 0.0;
    double rmsAuxiliaryThreshold = 0.0;
    int callbackStep = 0;
    bool dataLoggingEnabled = false;
};

struct AutoFocusSample {
    double hfr = 0.0;
    double rms = 0.0;
    bool valid = false;
};

struct AutoFocusMetrics {
    double hfr = 0.0;
    double rms = 0.0;
    double hfrStandardDeviation = 0.0;
    double rmsStandardDeviation = 0.0;
    std::size_t rawFrameCount = 0;
    std::size_t validFrameCount = 0;
    bool valid = false;
};

enum class AutoFocusRunState {
    Idle,
    ReferenceCalibration,
    AwaitingInitialMetrics,
    DirectionSearch,
    Adjusting,
    Callback,
    ReturningBest,
    OvershootReturnSearch,
    FinalValidation,
    Complete,
    Failed
};

enum class AutoFocusActionType {
    None,
    AwaitingInitialMetrics,
    ReferenceCalibrated,
    MoveRelative,
    Finished,
    Failed
};

enum class AutoFocusCompletionKind {
    None,
    AlreadyWithinTolerance,
    ReachedTargetBoundary,
    ReturnedToBest
};

struct AutoFocusAction {
    AutoFocusActionType type = AutoFocusActionType::None;
    int relativeStep = 0;
    bool metricsReady = false;
};

struct AutoFocusRunSnapshot {
    AutoFocusRunState state = AutoFocusRunState::Idle;
    AutoFocusMetrics currentMetrics;
    bool hasCurrentMetrics = false;
    int currentOffset = 0;
    int bestOffset = 0;
    int accumulatedBacklashTravel = 0;
    double bestHfr = 0.0;
    int searchDirection = 0;
    bool unreachedReference = false;
    AutoFocusCompletionKind completionKind = AutoFocusCompletionKind::None;
};

class AutoFocusController {
public:
    explicit AutoFocusController(AutoFocusConfig config);

    static std::string validateConfig(const AutoFocusConfig& config);
    static AutoFocusMetrics aggregate(const std::vector<AutoFocusSample>& samples,
                                      AutoFocusStatisticsMode mode,
                                      double trimRatio = 0.0);

    AutoFocusAction start(int cameraIndex,
                          double temperature,
                          AutoFocusTrigger trigger);
    // Compatibility overload for existing callers: true means manual, false means temperature.
    AutoFocusAction start(int cameraIndex, double temperature, bool manual);
    void setConfig(AutoFocusConfig config);
    AutoFocusAction startReferenceCalibration(int cameraIndex, std::int64_t nowMs);
    void primeTemperatureBaseline(int cameraIndex, double temperature, bool valid);
    AutoFocusAction updateTemperature(int cameraIndex, double temperature, bool valid);
    AutoFocusAction submitMetrics(int cameraIndex, const AutoFocusMetrics& metrics);
    AutoFocusAction submitSample(int cameraIndex,
                                 const AutoFocusSample& sample,
                                 std::int64_t nowMs);
    void armStageTimeout(int cameraIndex, std::int64_t nowMs);
    void markFocuserMoveAcknowledged(int cameraIndex);
    AutoFocusAction checkTimeout(int cameraIndex, std::int64_t nowMs);
    void updateFocuserState(int cameraIndex,
                            bool deviceOpened,
                            bool moving,
                            std::int64_t nowMs);
    bool readyForFrames(int cameraIndex, std::int64_t nowMs) const;
    void cancel(int cameraIndex);
    bool isActive(int cameraIndex) const;
    AutoFocusRunState state(int cameraIndex) const;
    AutoFocusTrigger trigger(int cameraIndex) const;
    double lastCheckedTemperature(int cameraIndex) const;
    AutoFocusReferenceMetrics referenceMetrics(int cameraIndex) const;
    AutoFocusRunSnapshot snapshot(int cameraIndex) const;

private:
    struct CameraRuntime {
        AutoFocusRunState state = AutoFocusRunState::Idle;
        double startTemperature = 0.0;
        double lastCheckedTemperature = 0.0;
        bool temperatureBaselineValid = false;
        AutoFocusMetrics currentMetrics;
        bool hasCurrentMetrics = false;
        AutoFocusMetrics bestMetrics;
        AutoFocusMetrics provisionalMetrics;
        AutoFocusMetrics previousSearchMetrics;
        bool hasBestMetrics = false;
        bool hasProvisionalMetrics = false;
        bool hasPreviousSearchMetrics = false;
        int improvingThresholdHits = 0;
        int worseningThresholdHits = 0;
        int unqualifiedOvershootHits = 0;
        bool callbackMeasurementPending = false;
        bool directionCorrectionUsed = false;
        bool callbackUsed = false;
        bool directionConfirmed = false;
        AutoFocusTrigger trigger = AutoFocusTrigger::Manual;
        int runSearchRadius = 0;
        double callbackTargetHfr = 0.0;
        int callbackTargetOffset = 0;
        double callbackSearchBestHfr = 0.0;
        bool callbackSearchHasSample = false;
        int pendingMoveDirection = 0;
        int searchDirection = 0;
        int currentOffset = 0;
        int bestOffset = 0;
        int searchIterations = 0;
        bool unreachedReference = false;
        AutoFocusCompletionKind completionKind = AutoFocusCompletionKind::None;
        bool awaitingFocuserStop = false;
        bool focuserMotionObserved = false;
        std::int64_t readyAfterMs = 0;
        std::int64_t stageDeadlineMs = 0;
        std::vector<AutoFocusSample> samples;
    };

    AutoFocusAction requestSearchMove(CameraRuntime& runtime);
    AutoFocusAction returnToBest(CameraRuntime& runtime, bool finalValidation = false);
    AutoFocusAction failRun(CameraRuntime& runtime);
    AutoFocusAction requestMove(CameraRuntime& runtime, int relativeStep);
    static int signOf(int value);
    bool reachesReference(int cameraIndex, const AutoFocusMetrics& metrics) const;

    AutoFocusConfig m_config;
    std::array<CameraRuntime, 2> m_runtime;
    std::array<int, 2> m_lastCompletedMoveDirection = {0, 0};
    std::array<double, 2> m_lastAutofocusTemperature = {0.0, 0.0};
    std::array<bool, 2> m_hasLastAutofocusTemperature = {false, false};
};
