#pragma once

#include "AppConfig.h"

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
    double fitPeakDn = 0.0;
    double backgroundDn = 0.0;
    double noiseSigmaDn = 0.0;
    double thresholdDn = 0.0;
    quint64 signalPixelCount = 0;
    quint64 saturatedPixelCount = 0;
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
    double medianSnr = 0.0;
    double validCentroidRatio = 0.0;
    double measurementUsableRatio = 0.0;
    double saturationFrameRatio = 0.0;
    double darkFrameRatio = 0.0;
};

struct AutoExposureTrendSnapshot {
    AutoExposureCameraWindowStats camera[2];
    bool commonTrendValid = false;
    bool trendConflict = false;
    double sharedPeakP50Dn = 0.0;
    double sharedPeakP90Dn = 0.0;
    double sharedPeakP95Dn = 0.0;
    double sharedSnr = 0.0;
    double sharedValidRatio = 0.0;
    double sharedUsableRatio = 0.0;
    double sharedSaturationRatio = 0.0;
    double sharedDarkRatio = 0.0;
};

struct AutoExposureDecision {
    AutoExposureState state = AutoExposureState::Normal;
    bool shouldAdjustExposure = false;
    int targetExposureUs = 0;
    QString reason;
    AutoExposureTrendSnapshot snapshot;
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
        m_state = AutoExposureState::Normal;
        m_samples[0].clear();
        m_samples[1].clear();
        m_latestSnapshot = AutoExposureTrendSnapshot();
        m_latestReason.clear();
        m_brightSinceMs = -1;
        m_darkSinceMs = -1;
        m_conflictSinceMs = -1;
        m_starLostSinceMs = -1;
        m_safeSinceMs = -1;
        m_cooldownUntilMs = -1;
        m_waitingForTargetAfterAdjustment = false;
    }

    AutoExposureDecision addSampleAndEvaluate(const AutoExposureFrameSample& sample,
                                              int currentExposureUs,
                                              const QVector<int>& templateExposures,
                                              qint64 nowMs)
    {
        AutoExposureDecision decision;
        decision.targetExposureUs = currentExposureUs;
        if (sample.cameraIndex < 0 || sample.cameraIndex >= 2) {
            decision.state = m_state;
            decision.reason = QStringLiteral("自动曝光: 无效相机索引");
            decision.snapshot = m_latestSnapshot;
            return decision;
        }

        m_samples[sample.cameraIndex].enqueue(sample);
        prune(nowMs);
        m_latestSnapshot = buildSnapshot();
        decision.snapshot = m_latestSnapshot;

        if (!m_latestSnapshot.commonTrendValid) {
            m_latestReason = QStringLiteral("自动曝光: 等待双相机窗口样本");
            decision.state = m_state;
            decision.reason = m_latestReason;
            return decision;
        }

        const qint64 conflictPersistenceMs = qint64(m_config.trendConflictPersistenceSec) * 1000;
        if (m_latestSnapshot.trendConflict) {
            if (m_conflictSinceMs < 0) {
                m_conflictSinceMs = nowMs;
            }
            if (nowMs - m_conflictSinceMs >= conflictPersistenceMs) {
                m_state = AutoExposureState::TrendConflict;
                m_latestReason = QStringLiteral("TREND_CONFLICT");
                decision.state = m_state;
                decision.reason = m_latestReason;
                return decision;
            }
        } else {
            m_conflictSinceMs = -1;
        }

        if (m_cooldownUntilMs > nowMs) {
            m_state = AutoExposureState::Cooldown;
            m_latestReason = QStringLiteral("COOLDOWN");
            decision.state = m_state;
            decision.reason = m_latestReason;
            return decision;
        }

        const bool overbrightUnusable =
            m_latestSnapshot.sharedPeakP90Dn >= m_config.targetPeakHighDn &&
            m_latestSnapshot.sharedUsableRatio < m_config.minValidCentroidRatio;
        const bool bright =
            m_latestSnapshot.sharedPeakP90Dn >= m_config.nearSaturationDn ||
            m_latestSnapshot.sharedPeakP95Dn >= m_config.hardSaturationDn ||
            m_latestSnapshot.sharedSaturationRatio >= m_config.brightFrameRatioThreshold ||
            overbrightUnusable;
        const bool dark =
            m_latestSnapshot.sharedSnr <= m_config.darkSnrWarning ||
            m_latestSnapshot.sharedValidRatio < m_config.minValidCentroidRatio ||
            m_latestSnapshot.sharedDarkRatio >= m_config.darkFrameRatioThreshold;
        const bool starLost =
            currentExposureUs >= int(std::lround(m_config.maxExposureUs)) &&
            m_latestSnapshot.sharedValidRatio <= m_config.starLostValidRatio;

        const qint64 brightPersistenceMs = qint64(m_config.brightPersistenceSec) * 1000;
        const qint64 darkPersistenceMs = qint64(m_config.darkPersistenceSec) * 1000;
        const qint64 starLostPersistenceMs = qint64(m_config.starLostPersistenceSec) * 1000;
        const qint64 safePersistenceMs = qint64(m_config.safePersistenceSec) * 1000;

        if (starLost) {
            if (m_starLostSinceMs < 0) {
                m_starLostSinceMs = nowMs;
            }
            if (nowMs - m_starLostSinceMs >= starLostPersistenceMs) {
                m_state = AutoExposureState::StarLost;
                m_latestReason = QStringLiteral("WEATHER_TOO_DARK / STAR_LOST");
                decision.state = m_state;
                decision.reason = m_latestReason;
                return decision;
            }
        } else {
            m_starLostSinceMs = -1;
        }

        if (bright) {
            m_darkSinceMs = -1;
            m_safeSinceMs = -1;
            if (m_brightSinceMs < 0) {
                m_brightSinceMs = nowMs;
            }
            if (nowMs - m_brightSinceMs >= brightPersistenceMs) {
                const int target = chooseTargetExposure(currentExposureUs, templateExposures, m_latestSnapshot, false);
                decision.shouldAdjustExposure = target > 0 && target != currentExposureUs;
                decision.targetExposureUs = target;
                if (decision.shouldAdjustExposure) {
                    m_state = AutoExposureState::BrightAdjusting;
                    m_latestReason = QStringLiteral("BRIGHT_ADJUSTING");
                    m_waitingForTargetAfterAdjustment = true;
                    m_brightSinceMs = -1;
                } else {
                    m_state = AutoExposureState::BrightWarning;
                    m_latestReason = QStringLiteral("LOWER_EXPOSURE_UNAVAILABLE");
                }
                decision.state = m_state;
                decision.reason = m_latestReason;
                return decision;
            }
            m_state = AutoExposureState::BrightWarning;
            m_latestReason = QStringLiteral("BRIGHT_WARNING");
            decision.state = m_state;
            decision.reason = m_latestReason;
            return decision;
        }

        if (dark) {
            m_brightSinceMs = -1;
            m_safeSinceMs = -1;
            if (m_darkSinceMs < 0) {
                m_darkSinceMs = nowMs;
            }
            if (nowMs - m_darkSinceMs >= darkPersistenceMs &&
                currentExposureUs < int(std::lround(m_config.maxExposureUs))) {
                const int target = chooseTargetExposure(currentExposureUs, templateExposures, m_latestSnapshot, true);
                m_state = AutoExposureState::DarkAdjusting;
                m_latestReason = QStringLiteral("DARK_ADJUSTING");
                decision.shouldAdjustExposure = target > 0 && target != currentExposureUs;
                decision.targetExposureUs = target;
                if (decision.shouldAdjustExposure) {
                    m_waitingForTargetAfterAdjustment = true;
                    m_darkSinceMs = -1;
                }
                decision.state = m_state;
                decision.reason = m_latestReason;
                return decision;
            }
            m_state = AutoExposureState::DarkWarning;
            m_latestReason = QStringLiteral("DARK_WARNING");
            decision.state = m_state;
            decision.reason = m_latestReason;
            return decision;
        }

        m_brightSinceMs = -1;
        m_darkSinceMs = -1;
        if (m_safeSinceMs < 0) {
            m_safeSinceMs = nowMs;
        }
        // cooldown only starts after the target range is reached. Exposure adjustments can
        // therefore continue step-by-step while the scene is still too bright or too dark.
        if (m_waitingForTargetAfterAdjustment &&
            nowMs - m_safeSinceMs >= safePersistenceMs) {
            m_waitingForTargetAfterAdjustment = false;
            m_cooldownUntilMs = nowMs + qint64(m_config.cooldownSec) * 1000;
            m_state = AutoExposureState::Cooldown;
            m_latestReason = QStringLiteral("TARGET_REACHED_COOLDOWN");
            decision.state = m_state;
            decision.reason = m_latestReason;
            return decision;
        }
        if (nowMs - m_safeSinceMs >= safePersistenceMs || m_state == AutoExposureState::Normal) {
            m_state = AutoExposureState::Normal;
        }
        m_latestReason = QStringLiteral("NORMAL");
        decision.state = m_state;
        decision.reason = m_latestReason;
        return decision;
    }

    AutoExposureState state() const { return m_state; }
    AutoExposureTrendSnapshot latestSnapshot() const { return m_latestSnapshot; }
    QString latestReason() const { return m_latestReason; }

private:
    AutoExposureConfig m_config;
    AutoExposureState m_state = AutoExposureState::Normal;
    QQueue<AutoExposureFrameSample> m_samples[2];
    AutoExposureTrendSnapshot m_latestSnapshot;
    QString m_latestReason;
    qint64 m_brightSinceMs = -1;
    qint64 m_darkSinceMs = -1;
    qint64 m_conflictSinceMs = -1;
    qint64 m_starLostSinceMs = -1;
    qint64 m_safeSinceMs = -1;
    qint64 m_cooldownUntilMs = -1;
    bool m_waitingForTargetAfterAdjustment = false;

    static double percentile(QVector<double> values, double p)
    {
        if (values.isEmpty()) {
            return 0.0;
        }
        std::sort(values.begin(), values.end());
        const double position = std::clamp(p, 0.0, 1.0) * double(values.size() - 1);
        const int lower = int(std::floor(position));
        const int upper = int(std::ceil(position));
        if (lower == upper) {
            return values[lower];
        }
        const double fraction = position - double(lower);
        return values[lower] * (1.0 - fraction) + values[upper] * fraction;
    }

    void prune(qint64 nowMs)
    {
        const qint64 windowMs = qint64(std::max(10, m_config.sampleWindowSec)) * 1000;
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

        QVector<double> peaks;
        QVector<double> snrs;
        int validCount = 0;
        int usableCount = 0;
        int saturatedFrameCount = 0;
        int darkFrameCount = 0;
        peaks.reserve(m_samples[cameraIndex].size());
        snrs.reserve(m_samples[cameraIndex].size());
        for (const auto& sample : m_samples[cameraIndex]) {
            const double controlPeak =
                m_config.useFittedPeak && std::isfinite(sample.fitPeakDn) && sample.fitPeakDn > 0.0
                    ? sample.fitPeakDn
                    : sample.peakDn;
            const double sigma = std::max(sample.noiseSigmaDn, 1.0);
            const double snr = (controlPeak - sample.backgroundDn) / sigma;
            peaks.push_back(controlPeak);
            snrs.push_back(snr);
            validCount += sample.centroidValid ? 1 : 0;
            usableCount += sample.measurementUsable ? 1 : 0;
            saturatedFrameCount += sample.saturatedPixelCount >= quint64(m_config.saturatedPixelCount) ? 1 : 0;
            darkFrameCount += (!sample.centroidValid || snr <= m_config.darkSnrCritical) ? 1 : 0;
        }

        const double n = double(m_samples[cameraIndex].size());
        stats.hasSamples = true;
        stats.peakP50Dn = percentile(peaks, 0.50);
        stats.peakP90Dn = percentile(peaks, 0.90);
        stats.peakP95Dn = percentile(peaks, 0.95);
        stats.medianSnr = percentile(snrs, 0.50);
        stats.validCentroidRatio = double(validCount) / n;
        stats.measurementUsableRatio = double(usableCount) / n;
        stats.saturationFrameRatio = double(saturatedFrameCount) / n;
        stats.darkFrameRatio = double(darkFrameCount) / n;
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
        snapshot.sharedSnr = (snapshot.camera[0].medianSnr + snapshot.camera[1].medianSnr) * 0.5;
        snapshot.sharedValidRatio = std::min(snapshot.camera[0].validCentroidRatio,
                                             snapshot.camera[1].validCentroidRatio);
        snapshot.sharedUsableRatio = std::min(snapshot.camera[0].measurementUsableRatio,
                                              snapshot.camera[1].measurementUsableRatio);
        snapshot.sharedSaturationRatio = std::max(snapshot.camera[0].saturationFrameRatio,
                                                  snapshot.camera[1].saturationFrameRatio);
        snapshot.sharedDarkRatio = std::max(snapshot.camera[0].darkFrameRatio,
                                            snapshot.camera[1].darkFrameRatio);

        const double meanPeak = std::max(snapshot.sharedPeakP50Dn, 1.0);
        const double relativeDifference =
            std::abs(snapshot.camera[0].peakP50Dn - snapshot.camera[1].peakP50Dn) / meanPeak;
        snapshot.trendConflict = relativeDifference > m_config.cameraAgreementRatio;
        return snapshot;
    }

    int chooseTargetExposure(int currentExposureUs,
                             const QVector<int>& templateExposures,
                             const AutoExposureTrendSnapshot& snapshot,
                             bool brighten) const
    {
        const double ratio = brighten
                                 ? std::clamp(m_config.targetPeakLowDn / std::max(snapshot.sharedPeakP50Dn, 1.0),
                                              1.0,
                                              m_config.maxExposureChangeRatioUp)
                                 : std::clamp(m_config.targetPeakHighDn / std::max(snapshot.sharedPeakP90Dn, 1.0),
                                              m_config.maxExposureChangeRatioDown,
                                              1.0);
        const int rawTarget = int(std::lround(std::clamp(double(currentExposureUs) * ratio,
                                                         m_config.minExposureUs,
                                                         m_config.maxExposureUs)));
        // A darken target must not increase exposure. This matters when the user manually
        // sets exposure below the configured minimum, for example 50 us with minExposureUs=500.
        if (!brighten && rawTarget >= currentExposureUs) {
            return currentExposureUs;
        }
        QVector<int> exposures = templateExposures;
        std::sort(exposures.begin(), exposures.end());
        exposures.erase(std::unique(exposures.begin(), exposures.end()), exposures.end());
        if (exposures.isEmpty()) {
            return currentExposureUs;
        }

        auto nearestIt = std::lower_bound(exposures.begin(), exposures.end(), rawTarget);
        int targetIndex = 0;
        if (brighten) {
            targetIndex = nearestIt == exposures.end()
                              ? exposures.size() - 1
                              : int(nearestIt - exposures.begin());
        } else if (nearestIt == exposures.end()) {
            targetIndex = exposures.size() - 1;
        } else if (*nearestIt == rawTarget) {
            targetIndex = int(nearestIt - exposures.begin());
        } else if (nearestIt == exposures.begin()) {
            targetIndex = 0;
        } else {
            // A darken adjustment uses the next lower template, not the numerically
            // nearest template. Otherwise 4000 us -> raw 3516 us would stick at 4000 us.
            targetIndex = int(nearestIt - exposures.begin() - 1);
        }

        int currentIndex = targetIndex;
        auto currentIt = std::lower_bound(exposures.begin(), exposures.end(), currentExposureUs);
        if (currentIt == exposures.end()) {
            currentIndex = exposures.size() - 1;
        } else if (currentIt == exposures.begin()) {
            currentIndex = 0;
        } else {
            const int upperIndex = int(currentIt - exposures.begin());
            const int lowerIndex = upperIndex - 1;
            currentIndex = std::abs(exposures[upperIndex] - currentExposureUs) <
                                   std::abs(exposures[lowerIndex] - currentExposureUs)
                               ? upperIndex
                               : lowerIndex;
        }

        const int maxStep = std::max(1, m_config.maxTemplateStepPerAdjust);
        targetIndex = std::clamp(targetIndex, currentIndex - maxStep, currentIndex + maxStep);
        const int templateTarget = exposures[targetIndex];
        if (!brighten && templateTarget >= currentExposureUs) {
            return currentExposureUs;
        }
        return templateTarget;
    }
};
