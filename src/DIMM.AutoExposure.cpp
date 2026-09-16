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
        m_rateSwitchInProgress ||
        sample.cameraIndex < 0 ||
        sample.cameraIndex >= 2) {
        return;
    }

    const int currentExposure[2] = {
        static_cast<int>(std::lround(std::max(1.0, m_cameraExposureUs[0]))),
        static_cast<int>(std::lround(std::max(1.0, m_cameraExposureUs[1])))
    };
    const bool previousAdjustmentSessionActive = m_autoExposureAdjustmentSessionActive;
    const AutoExposureState previousState = m_autoExposureState;
    AutoExposureDecision decision =
        m_autoExposureController.addSampleAndEvaluate(sample, currentExposure, sample.timestampMs);
    const bool enteredAutoExposureCooldown =
        previousAdjustmentSessionActive && !decision.adjustmentSessionActive;
    const bool enteredAutoExposureSession =
        !previousAdjustmentSessionActive && decision.adjustmentSessionActive;

    if (enteredAutoExposureSession && !m_autoExposureFocusAdjustmentActive) {
        m_autoExposureResultRecordActive = m_resultSessionActive && m_resultWriter.isOpen();
        if (m_autoExposureResultRecordActive) {
            writeResultSessionEvent(
                QStringLiteral("AutoExposureStart"),
                QStringLiteral("periodic_session_start; cooldown_elapsed"),
                QStringLiteral("Tracking"),
                sample.timestampMs);
        }
    }

    const auto finishResultRecord = [this, &currentExposure, &sample]() {
        if (m_autoExposureFocusAdjustmentActive) {
            completeAutoExposureAdjustmentForAutoFocus(sample.timestampMs);
            return;
        }
        if (!m_autoExposureResultRecordActive) {
            return;
        }
        writeResultSessionEvent(
            QStringLiteral("AutoExposureEnd"),
            QStringLiteral("periodic_session_complete; exposureA=%1; exposureB=%2")
                .arg(currentExposure[0])
                .arg(currentExposure[1]),
            QStringLiteral("Tracking"),
            sample.timestampMs);
        m_autoExposureResultRecordActive = false;
    };

    m_autoExposureState = decision.state;
    m_autoExposureReason = decision.reason;
    m_autoExposureAdjustmentSessionActive = decision.adjustmentSessionActive;
    m_autoExposureCooldownRemainingMs = decision.cooldownRemainingMs;
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
    if (!decision.hasCameraDecision[0] && !decision.hasCameraDecision[1]) {
        for (int i = 0; i < 2; ++i) {
            m_cameraAutoExposureState[i] = decision.state;
        }
    }
    if (m_autoExposureFocusAdjustmentActive) {
        for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
            if (!shouldReleaseAutoFocusPreExposure(
                    m_autoFocusAwaitingPreExposure[cameraIndex],
                    m_cameraAutoExposureState[cameraIndex],
                    m_hasLatestAutoFocusSample[cameraIndex],
                    m_autoExposureFocusAdjustmentStartedMs,
                    sample.timestampMs,
                    m_autoExposureConfig.sampleWindowSec)) {
                continue;
            }
            completeAutoExposureAdjustmentForAutoFocus(sample.timestampMs);
            break;
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
        if (enteredAutoExposureCooldown) {
            finishResultRecord();
            armTrackingImageSaveAfterAutoExposureCooldown(
                sample.timestampMs + std::max<qint64>(1, decision.cooldownRemainingMs));
        }
        return;
    }

    double targetExposureUs[2] = {
        static_cast<double>(currentExposure[0]),
        static_cast<double>(currentExposure[1]),
    };
    bool hasExposureAdjustment = false;
    for (int i = 0; i < 2; ++i) {
        if (!decision.hasCameraDecision[i] ||
            !decision.camera[i].shouldAdjustExposure ||
            decision.camera[i].targetExposureUs <= 0) {
            continue;
        }
        targetExposureUs[i] = decision.camera[i].targetExposureUs;
        hasExposureAdjustment = true;
    }
    if (!hasExposureAdjustment) {
        if (enteredAutoExposureCooldown) {
            finishResultRecord();
            armTrackingImageSaveAfterAutoExposureCooldown(
                sample.timestampMs + std::max<qint64>(1, decision.cooldownRemainingMs));
        }
        return;
    }

    QString reason;
    if (!applyTrackingExposureAndFrameRate(targetExposureUs, &reason)) {
        m_autoExposureReason = reason;
        for (int i = 0; i < 2; ++i) {
            if (decision.hasCameraDecision[i] && decision.camera[i].shouldAdjustExposure) {
                m_cameraAutoExposureReason[i] = reason;
            }
        }
        setStatusMessage(reason.isEmpty()
                             ? QStringLiteral("自动曝光: 曝光/热像素模板/帧率切换失败")
                             : reason,
                         UiStatusLevel::Error);
        return;
    }

    ++m_autoExposureSequenceId;
    m_lastAutoExposureAdjustMs = sample.timestampMs;
    m_autoExposureFramesSinceAdjust = 0;
    for (int i = 0; i < 2; ++i) {
        if (!decision.hasCameraDecision[i] ||
            !decision.camera[i].shouldAdjustExposure ||
            decision.camera[i].targetExposureUs <= 0) {
            continue;
        }
        m_autoExposureController.markExposureApplied(i, sample.timestampMs);
        m_autoExposureTargetExposureUs = decision.camera[i].targetExposureUs;
        m_cameraAutoExposureTargetExposureUs[i] = decision.camera[i].targetExposureUs;
        setStatusMessage(QStringLiteral("自动曝光: 相机%1 %2 -> %3 μs，当前有效帧率 %4 Hz，状态:%5")
                             .arg(i + 1)
                             .arg(currentExposure[i])
                             .arg(decision.camera[i].targetExposureUs)
                             .arg(currentTrackingFrameRateHz(), 0, 'f', 1)
                             .arg(autoExposureStateName(decision.camera[i].state)),
                             UiStatusLevel::Warning);
    }
    if (enteredAutoExposureCooldown) {
        finishResultRecord();
        armTrackingImageSaveAfterAutoExposureCooldown(
            sample.timestampMs + std::max<qint64>(1, decision.cooldownRemainingMs));
    }
}

void DIMM::beginAutoExposureAdjustmentForAutoFocus(const QString& phase, int cameraMask)
{
    if (cameraMask <= 0 || (cameraMask & ~0x3) != 0 ||
        m_captureState != CaptureState::Live || m_liveStartupPhase != LiveStartupPhase::Tracking) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    QString cameras;
    for (int camera = 0; camera < 2; ++camera) {
        if ((cameraMask & (1 << camera)) == 0) {
            continue;
        }
        if (!cameras.isEmpty()) {
            cameras += QLatin1Char(',');
        }
        cameras += QString::number(camera + 1);
    }
    if (!m_autoExposureConfig.enabled) {
        const QString reason = QStringLiteral("cameras=%1; phase=%2; disabled")
                                   .arg(cameras)
                                   .arg(phase);
        writeResultSessionEvent(QStringLiteral("AutoExposureStart"), reason,
                                QStringLiteral("Tracking"), nowMs);
        writeResultSessionEvent(QStringLiteral("AutoExposureEnd"), reason,
                                QStringLiteral("Tracking"), nowMs);
        return;
    }
    if (m_autoExposureFocusAdjustmentActive) {
        writeResultSessionEvent(QStringLiteral("AutoExposureEnd"),
                                QStringLiteral("focus_adjustment_restarted"),
                                QStringLiteral("Tracking"), nowMs);
    }
    m_autoExposureFocusAdjustmentActive = true;
    resetAutoExposureState(false);
    m_autoExposureFocusAdjustmentStartedMs = nowMs;
    writeResultSessionEvent(QStringLiteral("AutoExposureStart"),
                            QStringLiteral("cameras=%1; phase=%2")
                                .arg(cameras)
                                .arg(phase),
                            QStringLiteral("Tracking"), nowMs);
}

void DIMM::completeAutoExposureAdjustmentForAutoFocus(qint64 sourceTimestampMs)
{
    if (!m_autoExposureFocusAdjustmentActive) {
        return;
    }

    writeResultSessionEvent(QStringLiteral("AutoExposureEnd"),
                            QStringLiteral("focus_adjustment_complete"),
                            QStringLiteral("Tracking"), sourceTimestampMs);
    m_autoExposureFocusAdjustmentActive = false;
    m_autoExposureFocusAdjustmentStartedMs = -1;
    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        if (!m_autoFocusAwaitingPreExposure[cameraIndex]) {
            continue;
        }
        const AutoFocusAction action = m_deferredAutoFocusAction[cameraIndex];
        const QString context = m_deferredAutoFocusContext[cameraIndex];
        m_autoFocusAwaitingPreExposure[cameraIndex] = false;
        m_autoFocusPreExposureComplete[cameraIndex] = true;
        m_deferredAutoFocusAction[cameraIndex] = {};
        m_deferredAutoFocusContext[cameraIndex].clear();
        handleAutoFocusAction(cameraIndex, action, context);
    }
}

void DIMM::resetAutoExposureState(bool applyInitialExposure)
{
    clearTrackingImageSaveAfterAutoExposureCooldown();
    m_autoExposureController.configure(m_autoExposureConfig);
    m_latestAutoExposureTrend = AutoExposureTrendSnapshot();
    m_autoExposureState = AutoExposureState::Normal;
    m_autoExposureReason.clear();
    m_autoExposureTargetExposureUs = 0;
    m_lastAutoExposureAdjustMs = -1;
    m_autoExposureFramesSinceAdjust = 0;
    m_autoExposureAdjustmentSessionActive = m_autoExposureController.adjustmentSessionActive();
    m_autoExposureCooldownRemainingMs = 0;
    for (int i = 0; i < 2; ++i) {
        m_cameraAutoExposureState[i] = AutoExposureState::Normal;
        m_cameraAutoExposureReason[i].clear();
        m_cameraAutoExposureTargetExposureUs[i] = 0;
        m_latestAutoExposurePeakDn[i] = 0.0;
        m_latestAutoExposureSnr[i] = 0.0;
        m_latestAutoExposureValidRatio[i] = 0.0;
        m_latestAutoExposureUsableRatio[i] = 0.0;
    }
    if (!applyInitialExposure) {
        return;
    }

    QString reason;
    const int initialExposureUs =
        static_cast<int>(std::lround(m_autoExposureConfig.initialExposureUs));
    if (!applyExposureAndHotPixelTemplate(initialExposureUs, &reason)) {
        m_autoExposureReason = reason;
        setStatusMessage(reason.isEmpty()
                             ? QStringLiteral("自动采集: 启动默认曝光应用失败")
                             : reason,
                         UiStatusLevel::Warning);
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
    case AutoExposureState::Fluctuating:
        return QStringLiteral("FLUCTUATING");
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
        return QStringLiteral("调整: 采样中");
    case AutoExposureState::BrightAdjusting:
        return QStringLiteral("调整: 减曝光");
    case AutoExposureState::DarkWarning:
        return QStringLiteral("调整: 采样中");
    case AutoExposureState::DarkAdjusting:
        return QStringLiteral("调整: 增曝光");
    case AutoExposureState::Fluctuating:
        return QStringLiteral("波动");
    case AutoExposureState::Cooldown:
        return QStringLiteral("冷却");
    case AutoExposureState::StarLost:
        return QStringLiteral("波动");
    case AutoExposureState::TrendConflict:
        return QStringLiteral("波动");
    case AutoExposureState::Normal:
    default:
        return QStringLiteral("调整: 采样中");
    }
}

QString DIMM::autoExposureUiStatusText() const
{
    if (!m_autoExposureConfig.enabled) {
        return QStringLiteral("关闭");
    }
    return autoExposureStateShortText(m_autoExposureState);
}

QString DIMM::autoExposureAdjustDirectionText() const
{
    if (!m_autoExposureConfig.enabled) {
        return QStringLiteral("OFF");
    }
    switch (m_autoExposureState) {
    case AutoExposureState::BrightAdjusting:
        return QStringLiteral("DECREASE");
    case AutoExposureState::DarkAdjusting:
        return QStringLiteral("INCREASE");
    case AutoExposureState::Normal:
    case AutoExposureState::BrightWarning:
    case AutoExposureState::DarkWarning:
        return m_autoExposureAdjustmentSessionActive ? QStringLiteral("SAMPLING")
                                                     : QStringLiteral("HOLD");
    default:
        return QStringLiteral("HOLD");
    }
}

