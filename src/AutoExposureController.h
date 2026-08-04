#pragma once

#include "AppConfig.h"
#include "AutoExposureLogic.h"

#include <QQueue>
#include <QString>
#include <QtGlobal>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <limits>

enum class AutoExposureState {
    Normal,
    BrightWarning,
    BrightAdjusting,
    DarkWarning,
    DarkAdjusting,
    Cooldown,
    StarLost,
    TrendConflict
};

struct AutoExposureFrameSample {
    int cameraIndex = -1;
    double peakDn = 0.0;
    double supportedPeakDn = 0.0;
    double backgroundDn = 0.0;
    double noiseSigmaDn = 0.0;
    double thresholdDn = 0.0;
    quint64 signalPixelCount = 0;
    quint64 saturatedPixelCount = 0;
    AutoExposurePeakQuality peakQuality = AutoExposurePeakQuality::WeakOrNoSignal;
    quint64 peakSupportPixelCount = 0;
    double rejectedPeakDn = 0.0;
    int rejectedCandidateCount = 0;
    bool spotHardSaturated = false;
    bool centroidValid = false;
    bool measurementUsable = false;
    quint64 frameId = 0;
    qint64 timestampMs = 0;
};

struct AutoExposureCameraWindowStats {
    bool hasSamples = false;
    double peakP50Dn = 0.0;
    double peakP90Dn = 0.0;
    double peakP95Dn = 0.0;
    double latestPeakDn = 0.0;
    double medianSnr = 0.0;
    double validCentroidRatio = 0.0;
    double measurementUsableRatio = 0.0;
    double saturationFrameRatio = 0.0;
    double darkFrameRatio = 0.0;
    double brightFrameRatio = 0.0;
    double stableFrameRatio = 0.0;
    double hardSaturationFrameRatio = 0.0;
    double invalidPeakRatio = 0.0;
    int decisionSampleCount = 0;
    int validPeakSampleCount = 0;
    int rejectedIsolatedPeakCount = 0;
    int weakOrNoSignalCount = 0;
};

struct AutoExposureTrendSnapshot {
    AutoExposureCameraWindowStats camera[2];
    bool commonTrendValid = false;
    bool trendConflict = false;
    double sharedPeakP50Dn = 0.0;
    double sharedPeakP90Dn = 0.0;
    double sharedPeakP95Dn = 0.0;
    double sharedLatestPeakDn = 0.0;
    double sharedSnr = 0.0;
    double sharedValidRatio = 0.0;
    double sharedUsableRatio = 0.0;
    double sharedSaturationRatio = 0.0;
    double sharedDarkRatio = 0.0;
    double sharedBrightRatio = 0.0;
    double sharedStableRatio = 0.0;
    double sharedInvalidPeakRatio = 0.0;
};

struct AutoExposureCameraDecision {
    int cameraIndex = -1;
    AutoExposureState state = AutoExposureState::Normal;
    bool shouldAdjustExposure = false;
    int targetExposureUs = 0;
    QString reason;
};

struct AutoExposureDecision {
    AutoExposureState state = AutoExposureState::Normal;
    bool shouldAdjustExposure = false;
    int targetExposureUs = 0;
    QString reason;
    AutoExposureTrendSnapshot snapshot;
    AutoExposureCameraDecision camera[2];
    bool hasCameraDecision[2] = {false, false};
    bool synchronous = false;
};

class AutoExposureController {
public:
    void configure(const AutoExposureConfig& config)
    {
        m_config = config;
        reset();
    }

    void reset()
    {
        for (int i = 0; i < 2; ++i) {
            m_state[i] = AutoExposureState::Normal;
            m_samples[i].clear();
            m_ignoreSamplesBeforeMs[i] = -1;
        }
        m_latestSnapshot = AutoExposureTrendSnapshot();
        m_latestReason.clear();
        m_conflictSinceMs = -1;
    }

    AutoExposureDecision addSampleAndEvaluate(const AutoExposureFrameSample& sample,
                                              const int currentExposureUs[2],
                                              qint64 nowMs)
    {
        AutoExposureDecision decision;
        decision.snapshot = m_latestSnapshot;
        if (sample.cameraIndex < 0 || sample.cameraIndex >= 2) {
            decision.state = aggregateState();
            decision.reason = QStringLiteral("自动曝光: 无效相机索引");
            return decision;
        }

        if (sample.timestampMs >= m_ignoreSamplesBeforeMs[sample.cameraIndex]) {
            m_samples[sample.cameraIndex].enqueue(sample);
        }
        prune(nowMs);
        m_latestSnapshot = buildSnapshot();
        decision.snapshot = m_latestSnapshot;

        if (m_config.trendConflictEnabled) {
            evaluateSynchronous(currentExposureUs, nowMs, &decision);
        } else {
            evaluateIndependent(sample.cameraIndex, currentExposureUs[sample.cameraIndex], &decision);
        }

        decision.state = aggregateState();
        decision.reason = aggregateReason(decision);
        decision.snapshot = m_latestSnapshot;
        return decision;
    }

    void markExposureApplied(int cameraIndex, qint64 nowMs)
    {
        if (cameraIndex < 0 || cameraIndex >= 2) {
            return;
        }
        m_samples[cameraIndex].clear();
        m_ignoreSamplesBeforeMs[cameraIndex] = nowMs + qint64(std::max(0, m_config.exposureSettleMs));
    }

    AutoExposureState state() const { return aggregateState(); }
    AutoExposureState stateForCamera(int cameraIndex) const
    {
        return cameraIndex >= 0 && cameraIndex < 2 ? m_state[cameraIndex] : aggregateState();
    }
    AutoExposureTrendSnapshot latestSnapshot() const { return m_latestSnapshot; }

private:
    AutoExposureConfig m_config;
    AutoExposureState m_state[2] = {AutoExposureState::Normal, AutoExposureState::Normal};
    QQueue<AutoExposureFrameSample> m_samples[2];
    qint64 m_ignoreSamplesBeforeMs[2] = {-1, -1};
    AutoExposureTrendSnapshot m_latestSnapshot;
    QString m_latestReason;
    qint64 m_conflictSinceMs = -1;

    AutoExposureDecisionConfig decisionConfig() const
    {
        AutoExposureDecisionConfig config;
        config.targetPeakLowDn = m_config.targetPeakLowDn;
        config.targetPeakHighDn = m_config.targetPeakHighDn;
        config.exposureHysteresisDn = m_config.exposureHysteresisDn;
        config.hardSaturationDn = m_config.hardSaturationDn;
        config.darkFrameRatioThreshold = m_config.darkFrameRatioThreshold;
        config.brightFrameRatioThreshold = m_config.brightFrameRatioThreshold;
        config.stableFrameRatioThreshold = m_config.stableFrameRatioThreshold;
        config.hardSaturationFrameRatioThreshold = m_config.hardSaturationFrameRatioThreshold;
        config.minDecisionSampleCount = m_config.minDecisionSampleCount;
        config.minExposureUs = m_config.minExposureUs;
        config.maxExposureUs = m_config.maxExposureUs;
        config.maxExposureChangeRatioUp = m_config.maxExposureChangeRatioUp;
        config.maxExposureChangeRatioDown = m_config.maxExposureChangeRatioDown;
        config.minExposureDeltaUs = m_config.minExposureDeltaUs;
        config.minExposureChangeRatio = m_config.minExposureChangeRatio;
        return config;
    }

    static AutoExposureAdjustDirection activeDirectionForState(AutoExposureState state)
    {
        if (state == AutoExposureState::BrightAdjusting) {
            return AutoExposureAdjustDirection::Decrease;
        }
        if (state == AutoExposureState::DarkAdjusting) {
            return AutoExposureAdjustDirection::Increase;
        }
        return AutoExposureAdjustDirection::Hold;
    }

    AutoExposureAdjustDirection sharedActiveDirection() const
    {
        bool hasDarkAdjustment = false;
        for (AutoExposureState state : m_state) {
            if (state == AutoExposureState::BrightAdjusting) {
                return AutoExposureAdjustDirection::Decrease;
            }
            if (state == AutoExposureState::DarkAdjusting) {
                hasDarkAdjustment = true;
            }
        }
        return hasDarkAdjustment ? AutoExposureAdjustDirection::Increase
                                 : AutoExposureAdjustDirection::Hold;
    }

    static AutoExposureState stateForAction(const AutoExposureControlAction& action)
    {
        if (action.direction == AutoExposureAdjustDirection::Decrease) {
            return AutoExposureState::BrightAdjusting;
        }
        if (action.direction == AutoExposureAdjustDirection::Increase) {
            return AutoExposureState::DarkAdjusting;
        }
        if (action.reason == "MAX_EXPOSURE_DARK_HOLD") {
            return AutoExposureState::StarLost;
        }
        return AutoExposureState::Normal;
    }

    static QString reasonForAction(const AutoExposureControlAction& action)
    {
        return QString::fromStdString(action.reason);
    }

    static AutoExposureSpotResult spotResultFromSample(const AutoExposureFrameSample& sample)
    {
        AutoExposureSpotResult result;
        result.quality = sample.peakQuality;
        result.decisionSample = true;
        result.validSpotPeak =
            sample.peakQuality == AutoExposurePeakQuality::ValidSpotPeak ||
            sample.peakQuality == AutoExposurePeakQuality::SpotSaturated;
        result.spotHardSaturated = sample.spotHardSaturated;
        result.peakDn = sample.peakDn;
        result.supportedPeakDn = sample.supportedPeakDn > 0.0 ? sample.supportedPeakDn : sample.peakDn;
        result.rejectedPeakDn = sample.rejectedPeakDn;
        result.rejectedCandidateCount = sample.rejectedCandidateCount;
        result.supportPixelCount = int(std::min<quint64>(
            sample.peakSupportPixelCount, quint64(std::numeric_limits<int>::max())));
        result.spotSaturatedPixelCount =
            sample.spotHardSaturated ? std::max(1, int(sample.saturatedPixelCount)) : 0;
        return result;
    }

    void prune(qint64 nowMs)
    {
        const qint64 windowMs = qint64(std::max(1, m_config.sampleWindowSec)) * 1000;
        for (auto& queue : m_samples) {
            while (!queue.isEmpty() && nowMs - queue.head().timestampMs > windowMs) {
                queue.dequeue();
            }
        }
    }

    AutoExposureCameraWindowStats statsForCamera(int cameraIndex) const
    {
        AutoExposureCameraWindowStats stats;
        if (cameraIndex < 0 || cameraIndex >= 2 || m_samples[cameraIndex].isEmpty()) {
            return stats;
        }

        std::vector<AutoExposureSpotResult> spotSamples;
        QVector<double> snrs;
        int centroidValidCount = 0;
        int measurementUsableCount = 0;
        spotSamples.reserve(static_cast<std::size_t>(m_samples[cameraIndex].size()));
        snrs.reserve(m_samples[cameraIndex].size());
        for (const auto& sample : m_samples[cameraIndex]) {
            spotSamples.push_back(spotResultFromSample(sample));
            if (sample.peakQuality == AutoExposurePeakQuality::ValidSpotPeak ||
                sample.peakQuality == AutoExposurePeakQuality::SpotSaturated) {
                const double peak = sample.supportedPeakDn > 0.0 ? sample.supportedPeakDn : sample.peakDn;
                const double sigma = std::max(sample.noiseSigmaDn, 1.0);
                snrs.push_back((peak - sample.backgroundDn) / sigma);
            }
            centroidValidCount += sample.centroidValid ? 1 : 0;
            measurementUsableCount += sample.measurementUsable ? 1 : 0;
        }

        const AutoExposureWindowStats logicStats =
            summarizeAutoExposureWindow(spotSamples, decisionConfig());
        const double n = double(m_samples[cameraIndex].size());
        stats.hasSamples = true;
        stats.peakP50Dn = logicStats.peakP50Dn;
        stats.peakP90Dn = logicStats.peakP90Dn;
        stats.peakP95Dn = logicStats.peakP95Dn;
        stats.latestPeakDn = logicStats.latestPeakDn;
        stats.medianSnr = percentile(snrs, 0.50);
        stats.validCentroidRatio = n > 0.0 ? double(centroidValidCount) / n : 0.0;
        stats.measurementUsableRatio = n > 0.0 ? double(measurementUsableCount) / n : 0.0;
        stats.saturationFrameRatio = logicStats.hardSaturationRatio;
        stats.darkFrameRatio = logicStats.darkRatio;
        stats.brightFrameRatio = logicStats.brightRatio;
        stats.stableFrameRatio = logicStats.stableRatio;
        stats.hardSaturationFrameRatio = logicStats.hardSaturationRatio;
        stats.invalidPeakRatio = logicStats.invalidPeakRatio;
        stats.decisionSampleCount = logicStats.decisionSampleCount;
        stats.validPeakSampleCount = logicStats.validPeakSampleCount;
        stats.rejectedIsolatedPeakCount = logicStats.rejectedIsolatedPeakCount;
        stats.weakOrNoSignalCount = logicStats.weakOrNoSignalCount;
        return stats;
    }

    AutoExposureTrendSnapshot buildSnapshot() const
    {
        AutoExposureTrendSnapshot snapshot;
        snapshot.camera[0] = statsForCamera(0);
        snapshot.camera[1] = statsForCamera(1);
        snapshot.commonTrendValid = snapshot.camera[0].hasSamples && snapshot.camera[1].hasSamples;
        if (!snapshot.commonTrendValid) {
            return snapshot;
        }

        snapshot.sharedPeakP50Dn = (snapshot.camera[0].peakP50Dn + snapshot.camera[1].peakP50Dn) * 0.5;
        snapshot.sharedPeakP90Dn = (snapshot.camera[0].peakP90Dn + snapshot.camera[1].peakP90Dn) * 0.5;
        snapshot.sharedPeakP95Dn = (snapshot.camera[0].peakP95Dn + snapshot.camera[1].peakP95Dn) * 0.5;
        snapshot.sharedLatestPeakDn =
            (snapshot.camera[0].latestPeakDn + snapshot.camera[1].latestPeakDn) * 0.5;
        snapshot.sharedSnr = (snapshot.camera[0].medianSnr + snapshot.camera[1].medianSnr) * 0.5;
        snapshot.sharedValidRatio = std::min(snapshot.camera[0].validCentroidRatio,
                                             snapshot.camera[1].validCentroidRatio);
        snapshot.sharedUsableRatio = std::min(snapshot.camera[0].measurementUsableRatio,
                                              snapshot.camera[1].measurementUsableRatio);
        snapshot.sharedSaturationRatio = std::max(snapshot.camera[0].hardSaturationFrameRatio,
                                                  snapshot.camera[1].hardSaturationFrameRatio);
        snapshot.sharedDarkRatio = std::max(snapshot.camera[0].darkFrameRatio,
                                            snapshot.camera[1].darkFrameRatio);
        snapshot.sharedBrightRatio = std::max(snapshot.camera[0].brightFrameRatio,
                                              snapshot.camera[1].brightFrameRatio);
        snapshot.sharedStableRatio = std::min(snapshot.camera[0].stableFrameRatio,
                                              snapshot.camera[1].stableFrameRatio);
        snapshot.sharedInvalidPeakRatio = std::max(snapshot.camera[0].invalidPeakRatio,
                                                   snapshot.camera[1].invalidPeakRatio);

        if (snapshot.camera[0].validPeakSampleCount >= m_config.minDecisionSampleCount &&
            snapshot.camera[1].validPeakSampleCount >= m_config.minDecisionSampleCount) {
            const double meanPeak = std::max(snapshot.sharedPeakP50Dn, 1.0);
            const double relativeDifference =
                std::abs(snapshot.camera[0].peakP50Dn - snapshot.camera[1].peakP50Dn) / meanPeak;
            snapshot.trendConflict = relativeDifference > m_config.cameraAgreementRatio;
        }
        return snapshot;
    }

    static double percentile(QVector<double> values, double fraction)
    {
        if (values.isEmpty()) {
            return 0.0;
        }
        std::sort(values.begin(), values.end());
        const double clamped = std::clamp(fraction, 0.0, 1.0);
        const qsizetype maxIndex = values.size() - 1;
        const qsizetype index = static_cast<qsizetype>(std::floor(clamped * double(maxIndex)));
        return values[std::clamp(index, qsizetype(0), maxIndex)];
    }

    AutoExposureWindowStats logicStatsForCamera(int cameraIndex) const
    {
        std::vector<AutoExposureSpotResult> spotSamples;
        if (cameraIndex < 0 || cameraIndex >= 2) {
            return AutoExposureWindowStats();
        }
        spotSamples.reserve(static_cast<std::size_t>(m_samples[cameraIndex].size()));
        for (const auto& sample : m_samples[cameraIndex]) {
            spotSamples.push_back(spotResultFromSample(sample));
        }
        return summarizeAutoExposureWindow(spotSamples, decisionConfig());
    }

    AutoExposureWindowStats sharedLogicStats() const
    {
        AutoExposureWindowStats stats;
        const AutoExposureCameraWindowStats& cam0 = m_latestSnapshot.camera[0];
        const AutoExposureCameraWindowStats& cam1 = m_latestSnapshot.camera[1];
        stats.decisionSampleCount = std::min(cam0.decisionSampleCount, cam1.decisionSampleCount);
        stats.validPeakSampleCount = std::min(cam0.validPeakSampleCount, cam1.validPeakSampleCount);
        stats.peakP50Dn = m_latestSnapshot.sharedPeakP50Dn;
        stats.peakP90Dn = m_latestSnapshot.sharedPeakP90Dn;
        stats.peakP95Dn = m_latestSnapshot.sharedPeakP95Dn;
        stats.latestPeakDn = m_latestSnapshot.sharedLatestPeakDn;
        stats.darkRatio = m_latestSnapshot.sharedDarkRatio;
        stats.brightRatio = m_latestSnapshot.sharedBrightRatio;
        stats.stableRatio = m_latestSnapshot.sharedStableRatio;
        stats.hardSaturationRatio = m_latestSnapshot.sharedSaturationRatio;
        stats.invalidPeakRatio = m_latestSnapshot.sharedInvalidPeakRatio;
        return stats;
    }

    void fillCameraDecision(int cameraIndex,
                            int currentExposureUs,
                            const AutoExposureControlAction& action,
                            AutoExposureDecision* decision)
    {
        if (!decision || cameraIndex < 0 || cameraIndex >= 2) {
            return;
        }
        AutoExposureCameraDecision cameraDecision;
        cameraDecision.cameraIndex = cameraIndex;
        cameraDecision.state = stateForAction(action);
        if (action.direction == AutoExposureAdjustDirection::Hold &&
            action.reason == "WAIT_SAMPLES" &&
            activeDirectionForState(m_state[cameraIndex]) != AutoExposureAdjustDirection::Hold) {
            cameraDecision.state = m_state[cameraIndex];
        }
        cameraDecision.shouldAdjustExposure =
            action.direction != AutoExposureAdjustDirection::Hold &&
            action.targetExposureUs > 0 &&
            action.targetExposureUs != currentExposureUs;
        cameraDecision.targetExposureUs = action.targetExposureUs;
        cameraDecision.reason = reasonForAction(action);
        m_state[cameraIndex] = cameraDecision.state;
        decision->camera[cameraIndex] = cameraDecision;
        decision->hasCameraDecision[cameraIndex] = true;
        if (cameraDecision.shouldAdjustExposure && !decision->shouldAdjustExposure) {
            decision->shouldAdjustExposure = true;
            decision->targetExposureUs = cameraDecision.targetExposureUs;
            decision->state = cameraDecision.state;
            decision->reason = cameraDecision.reason;
        }
    }

    void evaluateIndependent(int cameraIndex, int currentExposureUs, AutoExposureDecision* decision)
    {
        const AutoExposureWindowStats stats = logicStatsForCamera(cameraIndex);
        const AutoExposureControlAction action =
            chooseAutoExposureAction(stats,
                                     currentExposureUs,
                                     decisionConfig(),
                                     activeDirectionForState(m_state[cameraIndex]));
        fillCameraDecision(cameraIndex, currentExposureUs, action, decision);
    }

    void evaluateSynchronous(const int currentExposureUs[2], qint64 nowMs, AutoExposureDecision* decision)
    {
        decision->synchronous = true;
        if (!m_latestSnapshot.commonTrendValid) {
            m_latestReason = QStringLiteral("自动曝光: 等待双相机窗口样本");
            return;
        }

        const qint64 conflictPersistenceMs = qint64(std::max(1, m_config.trendConflictPersistenceSec)) * 1000;
        if (m_latestSnapshot.trendConflict) {
            if (m_conflictSinceMs < 0) {
                m_conflictSinceMs = nowMs;
            }
            if (nowMs - m_conflictSinceMs >= conflictPersistenceMs) {
                for (int i = 0; i < 2; ++i) {
                    AutoExposureCameraDecision cameraDecision;
                    cameraDecision.cameraIndex = i;
                    cameraDecision.state = AutoExposureState::TrendConflict;
                    cameraDecision.targetExposureUs = currentExposureUs[i];
                    cameraDecision.reason = QStringLiteral("TREND_CONFLICT");
                    m_state[i] = AutoExposureState::TrendConflict;
                    decision->camera[i] = cameraDecision;
                    decision->hasCameraDecision[i] = true;
                }
                decision->state = AutoExposureState::TrendConflict;
                decision->reason = QStringLiteral("TREND_CONFLICT");
                return;
            }
        } else {
            m_conflictSinceMs = -1;
        }

        const int sharedCurrentExposure =
            int(std::lround((double(currentExposureUs[0]) + double(currentExposureUs[1])) * 0.5));
        const AutoExposureControlAction action =
            chooseAutoExposureAction(sharedLogicStats(),
                                     sharedCurrentExposure,
                                     decisionConfig(),
                                     sharedActiveDirection());
        for (int i = 0; i < 2; ++i) {
            fillCameraDecision(i, currentExposureUs[i], action, decision);
        }
    }

    AutoExposureState aggregateState() const
    {
        for (AutoExposureState state : m_state) {
            if (state == AutoExposureState::TrendConflict ||
                state == AutoExposureState::StarLost ||
                state == AutoExposureState::BrightAdjusting ||
                state == AutoExposureState::DarkAdjusting) {
                return state;
            }
        }
        return AutoExposureState::Normal;
    }

    QString aggregateReason(const AutoExposureDecision& decision) const
    {
        if (!decision.reason.isEmpty()) {
            return decision.reason;
        }
        for (int i = 0; i < 2; ++i) {
            if (decision.hasCameraDecision[i] && !decision.camera[i].reason.isEmpty()) {
                return decision.camera[i].reason;
            }
        }
        return m_latestReason;
    }
};
