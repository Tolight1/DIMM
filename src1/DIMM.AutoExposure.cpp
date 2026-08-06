#include "DIMM.h"

#include "DimmRuntimeHelpers.h"
#include "ImageProcessor.h"
#include "SettingsDialog.h"

#include <algorithm>
#include <cmath>
#include <limits>

void DIMM::handleAutoExposureSample(const AutoExposureFrameSample& sample)
{
    if (!m_autoExposureConfig.enabled ||
        m_captureState != CaptureState::Live ||
        m_liveStartupPhase != LiveStartupPhase::Tracking ||
        sample.cameraIndex < 0 ||
        sample.cameraIndex >= 2) {
        return;
    }

    const int currentExposure[2] = {
        static_cast<int>(std::lround(std::max(1.0, m_cameraExposureUs[0]))),
        static_cast<int>(std::lround(std::max(1.0, m_cameraExposureUs[1])))
    };
    const AutoExposureState previousState = m_autoExposureState;
    AutoExposureDecision decision =
        m_autoExposureController.addSampleAndEvaluate(sample, currentExposure, sample.timestampMs);

    m_autoExposureState = decision.state;
    m_autoExposureReason = decision.reason;
    m_latestAutoExposureTrend = decision.snapshot;
    for (int i = 0; i < 2; ++i) {
        m_latestAutoExposurePeakDn[i] = decision.snapshot.camera[i].latestPeakDn;
        m_latestAutoExposureSnr[i] = decision.snapshot.camera[i].medianSnr;
        m_latestAutoExposureValidRatio[i] = decision.snapshot.camera[i].validCentroidRatio;
        m_latestAutoExposureUsableRatio[i] = decision.snapshot.camera[i].measurementUsableRatio;
        if (decision.hasCameraDecision[i]) {
            m_cameraAutoExposureState[i] = decision.camera[i].state;
            m_cameraAutoExposureReason[i] = decision.camera[i].reason;
            m_cameraAutoExposureTargetExposureUs[i] = decision.camera[i].targetExposureUs;
        }
    }
    if (m_autoExposureFramesSinceAdjust < std::numeric_limits<quint64>::max()) {
        ++m_autoExposureFramesSinceAdjust;
    }

    if (m_autoExposureState != previousState) {
        if (m_autoExposureState == AutoExposureState::StarLost) {
            setStatusMessage(QStringLiteral("自动曝光: WEATHER_TOO_DARK / STAR_LOST，最大曝光下仍无法稳定观测星点"),
                             UiStatusLevel::Error);
        } else if (m_autoExposureState == AutoExposureState::TrendConflict) {
            setStatusMessage(QStringLiteral("自动曝光: 两台相机亮度趋势冲突，保持当前曝光"),
                             UiStatusLevel::Warning);
        }
    }

    if (!decision.shouldAdjustExposure) {
        return;
    }

    for (int i = 0; i < 2; ++i) {
        if (!decision.hasCameraDecision[i] ||
            !decision.camera[i].shouldAdjustExposure ||
            decision.camera[i].targetExposureUs <= 0) {
            continue;
        }
        const int oldExposure = currentExposure[i];
        QString reason;
        if (!applyExposureAndHotPixelTemplate(i, decision.camera[i].targetExposureUs, &reason)) {
            m_cameraAutoExposureReason[i] = reason;
            m_autoExposureReason = reason;
            setStatusMessage(reason.isEmpty()
                                 ? QStringLiteral("自动曝光: 曝光/热像素模板切换失败")
                                 : reason,
                             UiStatusLevel::Error);
            continue;
        }

        m_autoExposureController.markExposureApplied(i, sample.timestampMs);
        ++m_autoExposureSequenceId;
        m_autoExposureTargetExposureUs = decision.camera[i].targetExposureUs;
        m_cameraAutoExposureTargetExposureUs[i] = decision.camera[i].targetExposureUs;
        m_lastAutoExposureAdjustMs = sample.timestampMs;
        m_autoExposureFramesSinceAdjust = 0;
        setStatusMessage(QStringLiteral("自动曝光: 相机%1 %2 -> %3 μs，状态:%4")
                             .arg(i + 1)
                             .arg(oldExposure)
                             .arg(decision.camera[i].targetExposureUs)
                             .arg(autoExposureStateName(decision.camera[i].state)),
                         UiStatusLevel::Warning);
    }
}

void DIMM::resetAutoExposureState()
{
    m_autoExposureController.configure(m_autoExposureConfig);
    m_latestAutoExposureTrend = AutoExposureTrendSnapshot();
    m_autoExposureState = AutoExposureState::Normal;
    m_autoExposureReason.clear();
    m_autoExposureTargetExposureUs = 0;
    m_lastAutoExposureAdjustMs = -1;
    m_autoExposureFramesSinceAdjust = 0;
    for (int i = 0; i < 2; ++i) {
        m_cameraAutoExposureState[i] = AutoExposureState::Normal;
        m_cameraAutoExposureReason[i].clear();
        m_cameraAutoExposureTargetExposureUs[i] = 0;
        m_latestAutoExposurePeakDn[i] = 0.0;
        m_latestAutoExposureSnr[i] = 0.0;
        m_latestAutoExposureValidRatio[i] = 0.0;
        m_latestAutoExposureUsableRatio[i] = 0.0;
    }
}

bool DIMM::isAutoExposureRoiRelocalizationGraceActive(qint64 nowMs) const
{
    if (!m_autoExposureConfig.enabled ||
        m_lastAutoExposureAdjustMs < 0 ||
        nowMs < m_lastAutoExposureAdjustMs) {
        return false;
    }

    return (nowMs - m_lastAutoExposureAdjustMs) <
           kAutoExposureRoiRelocalizationGraceMs;
}

QString DIMM::autoExposureStateName(AutoExposureState state) const
{
    switch (state) {
    case AutoExposureState::BrightWarning:
        return QStringLiteral("BRIGHT_WARNING");
    case AutoExposureState::BrightAdjusting:
        return QStringLiteral("BRIGHT_ADJUSTING");
    case AutoExposureState::DarkWarning:
        return QStringLiteral("DARK_WARNING");
    case AutoExposureState::DarkAdjusting:
        return QStringLiteral("DARK_ADJUSTING");
    case AutoExposureState::Cooldown:
        return QStringLiteral("COOLDOWN");
    case AutoExposureState::StarLost:
        return QStringLiteral("STAR_LOST");
    case AutoExposureState::TrendConflict:
        return QStringLiteral("TREND_CONFLICT");
    case AutoExposureState::Normal:
    default:
        return QStringLiteral("NORMAL");
    }
}

QString DIMM::autoExposureStateShortText(AutoExposureState state) const
{
    switch (state) {
    case AutoExposureState::BrightWarning:
        return QStringLiteral("亮警");
    case AutoExposureState::BrightAdjusting:
        return QStringLiteral("降曝");
    case AutoExposureState::DarkWarning:
        return QStringLiteral("暗警");
    case AutoExposureState::DarkAdjusting:
        return QStringLiteral("增曝");
    case AutoExposureState::Cooldown:
        return QStringLiteral("冷却");
    case AutoExposureState::StarLost:
        return QStringLiteral("丢星");
    case AutoExposureState::TrendConflict:
        return QStringLiteral("冲突");
    case AutoExposureState::Normal:
    default:
        return QStringLiteral("正常");
    }
}

QString DIMM::autoExposureUiStatusText() const
{
    if (!m_autoExposureConfig.enabled) {
        return QStringLiteral("关闭");
    }
    const QString cam0State = autoExposureStateShortText(m_cameraAutoExposureState[0]);
    const QString cam1State = autoExposureStateShortText(m_cameraAutoExposureState[1]);
    if (cam0State == cam1State) {
        return cam0State;
    }
    return QStringLiteral("C1%1/C2%2").arg(cam0State, cam1State);
}
