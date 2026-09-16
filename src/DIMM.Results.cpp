#include "DIMM.h"

#include "CameraManager.h"
#include "CommManager.h"
#include "CommProtocol.h"
#include "AcquisitionImagePolicy.h"
#include "AppConfigSnapshot.h"
#include "DimmRuntimeHelpers.h"
#include "ImageProcessor.h"
#include "ImageUtils.h"
#include "PathUtils.h"
#include "PulseGeneratorManager.h"
#include "SettingsDialog.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {

float finiteFloatOrNaN(double value, bool valid)
{
    return valid && std::isfinite(value)
               ? static_cast<float>(value)
               : std::numeric_limits<float>::quiet_NaN();
}

}  // namespace

std::uint32_t DIMM::monitoringDeviceStatus(const CaptureRuntimeContext& runtime,
                                            qint64 nowMs,
                                            double frameRateHz,
                                            bool frameRateValid)
{
    std::uint32_t status = CommProtocol::DEVICE_STATUS_NORMAL;

    const double timeoutRateHz = frameRateValid
                                     ? frameRateHz
                                     : (m_configTriggerMode == 0
                                            ? m_activeContinuousFrameRateHz
                                            : m_activeTriggerFrequencyHz);
    const double safeTimeoutRateHz =
        std::max(0.1, std::isfinite(timeoutRateHz) ? timeoutRateHz : 0.1);
    const qint64 expectedFrameIntervalMs =
        std::max<qint64>(1, static_cast<qint64>(std::ceil(1000.0 / safeTimeoutRateHz)));
    const qint64 frameTimeoutMs = std::max<qint64>(1000, expectedFrameIntervalMs * 3);

    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        const bool cameraOpen = m_cameraManager && m_cameraManager->isOpen(cameraIndex);
        if (!cameraOpen) {
            status |= cameraIndex == 0 ? CommProtocol::DEVICE_STATUS_CAMERA_A_CONNECTION
                                       : CommProtocol::DEVICE_STATUS_CAMERA_B_CONNECTION;
        }

        const bool frameTimedOut =
            !m_cameraManager ||
            !m_cameraManager->isStreaming(cameraIndex) ||
            m_lastAcceptedLiveFrameMs[cameraIndex] < 0 ||
            nowMs - m_lastAcceptedLiveFrameMs[cameraIndex] > frameTimeoutMs;
        if (frameTimedOut) {
            status |= cameraIndex == 0 ? CommProtocol::DEVICE_STATUS_CAMERA_A_CAPTURE
                                       : CommProtocol::DEVICE_STATUS_CAMERA_B_CAPTURE;
        }

        const bool noStar = !runtime.hasValidCentroid[cameraIndex];
        if (noStar) {
            status |= cameraIndex == 0 ? CommProtocol::DEVICE_STATUS_CAMERA_A_NO_STAR
                                       : CommProtocol::DEVICE_STATUS_CAMERA_B_NO_STAR;
        } else if (std::isfinite(runtime.peakBrightness[cameraIndex]) &&
                   std::isfinite(m_autoExposureConfig.targetPeakLowDn) &&
                   runtime.peakBrightness[cameraIndex] < m_autoExposureConfig.targetPeakLowDn) {
            status |= cameraIndex == 0
                          ? CommProtocol::DEVICE_STATUS_CAMERA_A_LOW_BRIGHTNESS
                          : CommProtocol::DEVICE_STATUS_CAMERA_B_LOW_BRIGHTNESS;
        }

        const double requestedExposureUs = m_cameraExposureUs[cameraIndex];
        bool exposureInvalid = !std::isfinite(requestedExposureUs) || requestedExposureUs <= 0.0;
        if (cameraOpen && m_cameraManager) {
            const double actualExposureUs = m_cameraManager->getExposure(cameraIndex);
            const double exposureToleranceUs =
                std::max(1.0, std::abs(requestedExposureUs) * 0.10);
            exposureInvalid = exposureInvalid ||
                              !std::isfinite(actualExposureUs) ||
                              actualExposureUs <= 0.0 ||
                              std::abs(actualExposureUs - requestedExposureUs) >
                                  exposureToleranceUs;
        }
        if (exposureInvalid) {
            status |= CommProtocol::DEVICE_STATUS_EXPOSURE;
        }
    }

    if (m_configTriggerMode != 0 &&
        (m_pulseBoardResponseTimedOut ||
         (m_pulseGeneratorEnabled &&
          (!m_pulseGenerator || !m_pulseGenerator->isRunning())))) {
        status |= CommProtocol::DEVICE_STATUS_TRIGGER;
    }

    if (m_environmentSensorConfig.enabled && !m_latestEnvironment.valid) {
        status |= CommProtocol::DEVICE_STATUS_ENVIRONMENT_SENSOR;
    }

    if (!frameRateValid) {
        status |= CommProtocol::DEVICE_STATUS_FRAME_RATE;
    }

    const qint64 atmosphereAgeMs = runtime.latestAtmosphereTimestampMs > 0
                                       ? nowMs - static_cast<qint64>(
                                                     runtime.latestAtmosphereTimestampMs)
                                       : std::numeric_limits<qint64>::max();
    if (!runtime.hasValidAtmosphere || atmosphereAgeMs < 0 || atmosphereAgeMs > 5000) {
        status |= CommProtocol::DEVICE_STATUS_MEASUREMENT;
    }

    if (!m_resultWriter.isOpen()) {
        status |= CommProtocol::DEVICE_STATUS_DATA_SAVE;
    }

    return status;
}

QDateTime DIMM::nextResultRecordTimestamp(const QDateTime& candidate)
{
    QDateTime timestamp = candidate.isValid() ? candidate : QDateTime::currentDateTime();
    qint64 timestampMs = timestamp.toMSecsSinceEpoch();
    if (m_lastResultRecordTimestampMs >= 0 &&
        timestampMs < m_lastResultRecordTimestampMs) {
        timestamp = timestamp.addMSecs(m_lastResultRecordTimestampMs - timestampMs);
        timestampMs = m_lastResultRecordTimestampMs;
    }
    m_lastResultRecordTimestampMs = timestampMs;
    return timestamp;
}

std::uint32_t DIMM::currentDeviceStatusForResultLog(qint64 nowMs)
{
    const double frameRate = currentTrackingFrameRateHz();
    return monitoringDeviceStatus(activeRuntime(),
                                  nowMs,
                                  frameRate,
                                  std::isfinite(frameRate) && frameRate > 0.0);
}

bool DIMM::beginResultSession(AcquisitionCsvSessionType resultSessionType)
{
    if (m_resultSessionActive) {
        return m_resultWriter.isOpen();
    }
    if (m_resultWriter.isOpen()) {
        setStatusMessage(QStringLiteral("结果会话状态异常：已有打开的结果文件"),
                         UiStatusLevel::Error);
        return false;
    }

    m_resultSessionType = resultSessionType;
    m_resultSessionStartedAt = QDateTime::currentDateTime();
    m_resultSessionId.clear();
    m_autoFocusLogWriter.close();
    m_autoFocusLogWriter.setEnabled(m_autoFocusConfig.dataLoggingEnabled);
    m_autoFocusRunSequence[0] = 0;
    m_autoFocusRunSequence[1] = 0;
    m_autoFocusRunActive[0] = false;
    m_autoFocusRunActive[1] = false;
    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        m_autoFocusStartupPending[cameraIndex] =
            resultSessionType == AcquisitionCsvSessionType::Auto &&
            m_autoFocusConfig.masterEnabled && m_autoFocusConfig.cameraEnabled[cameraIndex];
        m_autoFocusStartupTriggered[cameraIndex] = false;
    }
    m_resultSettingsSnapshotSequence = 0;
    m_lastResultRecordTimestampMs = -1;
    m_captureStopRequested = false;
    m_starTrackingState = StarTrackingState::Unknown;
    m_searchEventGate.reset();
    m_searchAttempt = 1;
    m_searchAttemptStartedMs = m_resultSessionStartedAt.toMSecsSinceEpoch();
    clearTrackingImageSaveAfterAutoExposureCooldown();
    m_trackingImageIntervalMs = AcquisitionImagePolicy::trackingIntervalMsForExposureUs(
        m_cameraExposureUs[0],
        m_cameraExposureUs[1],
        currentTrackingFrameRateHz(),
        m_autoExposureConfig.autoExposureSampleIntervalMs);
    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        lastSearchImageSavedMs[cameraIndex] = -1;
        lastTrackingImageSavedMs[cameraIndex] = -1;
        m_lastPeriodicTrackingRoiImageSavedMs[cameraIndex] = -1;
        lastSearchImageSavedFrameId[cameraIndex] = 0;
        lastTrackingImageSavedFrameId[cameraIndex] = 0;
    }
    initResultFile();
    if (!m_resultWriter.isOpen()) {
        return false;
    }
    if (!writeResultSettingsSnapshot(currentAppConfig(),
                                     QStringLiteral("SettingsSnapshot"),
                                     QStringLiteral("initial"),
                                     AppConfigSnapshot::initialFileName())) {
        QString closeError;
        m_resultWriter.close(&closeError);
        m_resultFilePath.clear();
        m_resultSessionId.clear();
        return false;
    }
    m_resultSessionActive = true;
    writeResultSessionEvent(
        QStringLiteral("SessionStarted"),
        resultSessionType == AcquisitionCsvSessionType::Auto
            ? QStringLiteral("auto_start")
            : QStringLiteral("manual_start"),
        QStringLiteral("Searching"),
        -1);
    return true;
}

bool DIMM::ensureResultFileOpen()
{
    if (m_captureStopRequested || m_captureState != CaptureState::Live) {
        return false;
    }
    return m_resultSessionActive && m_resultWriter.isOpen();
}

void DIMM::writeResultSessionEvent(const QString& recordType,
                                   const QString& reason,
                                   const QString& acquisitionState,
                                   qint64 sourceTimestampMs,
                                   std::uint32_t deviceStatus)
{
    if (!m_resultSessionActive || !m_resultWriter.isOpen()) {
        return;
    }

    const QDateTime savedAt = QDateTime::currentDateTime();
    const QDateTime timestamp = nextResultRecordTimestamp(savedAt);
    const QDateTime sourceTimestamp = sourceTimestampMs > 0
                                          ? QDateTime::fromMSecsSinceEpoch(sourceTimestampMs)
                                          : savedAt;
    m_resultWriter.enqueueLine(AcquisitionCsv::formatEventRecord(
        recordType,
        m_resultSessionId,
        timestamp,
        savedAt,
        sourceTimestamp,
        m_liveAcquisitionGeneration,
        deviceStatus,
        acquisitionState,
        m_searchAttempt,
        reason));
}

bool DIMM::writeResultSettingsSnapshot(const AppConfig& config,
                                       const QString& recordType,
                                       const QString& settingsReason,
                                       const QString& relativeFileName)
{
    if (!m_resultWriter.isOpen() || m_resultFilePath.isEmpty()) {
        return false;
    }

    const QFileInfo resultFileInfo(m_resultFilePath);
    QString error;
    if (!AppConfigSnapshot::write(resultFileInfo.absolutePath(),
                                  relativeFileName,
                                  config,
                                  &error)) {
        const QDateTime savedAt = QDateTime::currentDateTime();
        const QString acquisitionState = m_captureState == CaptureState::Paused
                                             ? QStringLiteral("Paused")
                                             : (m_starTrackingState == StarTrackingState::Tracked
                                                    ? QStringLiteral("Tracking")
                                                    : QStringLiteral("Searching"));
        const bool initial = recordType == QStringLiteral("SettingsSnapshot");
        m_resultWriter.enqueueLine(AcquisitionCsv::formatSettingsRecord(
            initial ? QStringLiteral("SettingsSnapshotFailed")
                    : QStringLiteral("SettingsChangedFailed"),
            m_resultSessionId,
            nextResultRecordTimestamp(savedAt),
            savedAt,
            m_liveAcquisitionGeneration,
            acquisitionState,
            m_searchAttempt,
            QString(),
            QStringLiteral("%1; %2")
                .arg(initial ? QStringLiteral("initial_failed")
                             : QStringLiteral("changed_failed"),
                     error)));
        setStatusMessage(QStringLiteral("设置快照保存失败: %1").arg(error), UiStatusLevel::Error);
        return false;
    }

    const QDateTime savedAt = QDateTime::currentDateTime();
    const QString acquisitionState = m_captureState == CaptureState::Paused
                                         ? QStringLiteral("Paused")
                                         : (m_starTrackingState == StarTrackingState::Tracked
                                                ? QStringLiteral("Tracking")
                                                : QStringLiteral("Searching"));
    m_resultWriter.enqueueLine(AcquisitionCsv::formatSettingsRecord(
        recordType,
        m_resultSessionId,
        nextResultRecordTimestamp(savedAt),
        savedAt,
        m_liveAcquisitionGeneration,
        acquisitionState,
        m_searchAttempt,
        relativeFileName,
        settingsReason));
    return true;
}

bool DIMM::writeChangedResultSettingsSnapshot(const AppConfig& config,
                                              const ConfigChangeSet& changes)
{
    if (!changes.any() || !m_resultSessionActive || !m_resultWriter.isOpen()) {
        return true;
    }

    const int sequence = m_resultSettingsSnapshotSequence + 1;
    if (!writeResultSettingsSnapshot(config,
                                     QStringLiteral("SettingsChanged"),
                                     AppConfigSnapshot::changeState(changes),
                                     AppConfigSnapshot::changedFileName(sequence))) {
        return false;
    }
    m_resultSettingsSnapshotSequence = sequence;
    return true;
}

void DIMM::writeSearchStartedEvent(const QString& reason, qint64 sourceTimestampMs)
{
    clearTrackingImageSaveAfterAutoExposureCooldown();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    writeResultSessionEvent(QStringLiteral("SearchStart"),
                             reason,
                             QStringLiteral("Searching"),
                             sourceTimestampMs,
                             currentDeviceStatusForResultLog(nowMs));
}

void DIMM::writeSearchEndedEvent(const QString& reason, qint64 sourceTimestampMs)
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    writeResultSessionEvent(QStringLiteral("SearchEnd"),
                             reason,
                             QStringLiteral("Tracking"),
                             sourceTimestampMs,
                             currentDeviceStatusForResultLog(nowMs));
}

void DIMM::writeSearchLifecycleEventsOnce(const QString& searchReason,
                                          const QString& pauseReason,
                                          qint64 sourceTimestampMs)
{
    if (!m_resultSessionActive || !m_resultWriter.isOpen() ||
        !m_searchEventGate.tryBeginSearch()) {
        return;
    }

    writeSearchStartedEvent(searchReason, sourceTimestampMs);
    Q_UNUSED(pauseReason);
}

void DIMM::writeHardwareErrorEvent(const QString& source,
                                   int deviceIndex,
                                   int errorCode,
                                   const QString& operation,
                                   const QString& detail,
                                   bool recoverable,
                                   qint64 sourceTimestampMs)
{
    const QString reason =
        QStringLiteral("source=%1;device=%2;code=%3;operation=%4;recoverable=%5;detail=%6")
            .arg(source)
            .arg(deviceIndex + 1)
            .arg(errorCode)
            .arg(operation)
            .arg(recoverable ? QStringLiteral("true") : QStringLiteral("false"))
            .arg(detail);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    writeResultSessionEvent(QStringLiteral("HardwareError"),
                             reason,
                             QStringLiteral("Paused"),
                             sourceTimestampMs,
                             currentDeviceStatusForResultLog(nowMs));
}

void DIMM::writeAcquisitionPauseEvent(const QString& reason,
                                      qint64 sourceTimestampMs,
                                      std::uint32_t deviceStatus)
{
    writeResultSessionEvent(QStringLiteral("AcquisitionPaused"),
                             reason,
                             QStringLiteral("Paused"),
                             sourceTimestampMs,
                             deviceStatus);
}

void DIMM::writeAcquisitionResumeEvent(const QString& reason,
                                       qint64 sourceTimestampMs,
                                       std::uint32_t deviceStatus)
{
    writeResultSessionEvent(QStringLiteral("AcquisitionResumed"),
                             reason,
                             QStringLiteral("Tracking"),
                             sourceTimestampMs,
                             deviceStatus);
}

void DIMM::noteStarTrackingState(bool tracked,
                                 qint64 sourceTimestampMs,
                                 const QString& reason)
{
    if (m_captureStopRequested || !m_resultSessionActive || !m_resultWriter.isOpen()) {
        return;
    }

    Q_UNUSED(sourceTimestampMs);
    Q_UNUSED(reason);
    m_starTrackingState = tracked ? StarTrackingState::Tracked
                                  : StarTrackingState::Lost;
}

void DIMM::armTrackingImageSaveAfterAutoExposureCooldown(qint64 cooldownUntilMs)
{
    if (m_captureStopRequested || !m_resultSessionActive || !m_resultWriter.isOpen() ||
        m_captureState != CaptureState::Live || m_liveStartupPhase != LiveStartupPhase::Tracking) {
        return;
    }
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        m_trackingImageSaveOnNextRoiFrame[cameraIndex] = true;
        m_trackingImageSaveNextRetryMs[cameraIndex] = nowMs;
    }
    m_trackingImageSaveCooldownUntilMs =
        std::max(cooldownUntilMs, nowMs + 1);
}

void DIMM::clearTrackingImageSaveAfterAutoExposureCooldown()
{
    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        m_trackingImageSaveOnNextRoiFrame[cameraIndex] = false;
        m_trackingImageSaveNextRetryMs[cameraIndex] = -1;
    }
    m_trackingImageSaveCooldownUntilMs = -1;
}

bool DIMM::saveLiveFullFrameImage(int cameraIndex,
                                  const CameraFrame& packet,
                                  bool forceTrackingCooldownSave)
{
    if (m_captureStopRequested ||
        !m_resultSessionActive || !m_resultWriter.isOpen() ||
        m_captureState != CaptureState::Live || cameraIndex < 0 || cameraIndex >= 2 ||
        packet.image.empty()) {
        return false;
    }

    const bool searching = m_liveStartupPhase == LiveStartupPhase::LocatePair;
    const bool tracking = m_liveStartupPhase == LiveStartupPhase::Tracking;
    if ((!searching && !tracking) || (tracking && !forceTrackingCooldownSave)) {
        return false;
    }
    const AcquisitionImagePolicy::Phase imagePhase =
        searching ? AcquisitionImagePolicy::Phase::Searching
                  : AcquisitionImagePolicy::Phase::Tracking;
    if (!AcquisitionImagePolicy::isEligibleFrameForPhase(imagePhase,
                                                         packet.image.cols,
                                                         packet.image.rows,
                                                         kFixedRoiSize)) {
        return false;
    }
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (tracking && m_trackingImageSaveCooldownUntilMs >= 0 &&
        nowMs >= m_trackingImageSaveCooldownUntilMs) {
        // A failed request may be retried on every valid ROI frame, but never beyond
        // the cooldown that armed it. The next cooldown edge will arm a new request.
        clearTrackingImageSaveAfterAutoExposureCooldown();
        return false;
    }
    qint64& lastSavedMs = searching ? lastSearchImageSavedMs[cameraIndex]
                                    : lastTrackingImageSavedMs[cameraIndex];
    quint64& lastSavedFrameId = searching ? lastSearchImageSavedFrameId[cameraIndex]
                                          : lastTrackingImageSavedFrameId[cameraIndex];
    if (searching) {
        const qint64 periodMs = AcquisitionImagePolicy::intervalMs(
            imagePhase,
            m_autoAcquisitionConfig.recoveryScanIntervalMinutes,
            0);
        if (!AcquisitionImagePolicy::isDue(lastSavedMs, nowMs, periodMs)) {
            return false;
        }
    } else if (packet.frameId > 0 && packet.frameId == lastSavedFrameId) {
        return false;
    }

    const cv::Mat grayscale = ImageUtils::grayscaleDetectionFrame(packet.image);
    const cv::Mat mono8 = ImageUtils::fullFrameMono8Preview(
        grayscale,
        packet.bitDepth,
        packet.maxPixelValue);
    double minValue = 0.0;
    double maxValue = 0.0;
    if (!grayscale.empty()) {
        cv::minMaxLoc(grayscale, &minValue, &maxValue);
    }
    qInfo() << "live image source"
            << "camera" << cameraIndex
            << "dimensions" << packet.image.cols << "x" << packet.image.rows
            << "type" << packet.image.type()
            << "bitDepth" << packet.bitDepth
            << "min" << minValue
            << "max" << maxValue;
    if (mono8.empty()) {
        setStatusMessage(QStringLiteral("图像保存失败：不支持的像素格式或位深元数据"),
                         UiStatusLevel::Warning);
        return false;
    }

    const QFileInfo sessionInfo(m_resultFilePath);
    QDir sessionDir(sessionInfo.absolutePath());
    if (!sessionDir.mkpath(QStringLiteral("images"))) {
        setStatusMessage(QStringLiteral("图像目录创建失败"), UiStatusLevel::Warning);
        return false;
    }

    const QDateTime savedAt = QDateTime::currentDateTime();
    const QString imageKind = searching ? QStringLiteral("search_full_frame")
                                        : QStringLiteral("tracking_roi");
    const QString stateLabel = searching ? QStringLiteral("Searching")
                                         : QStringLiteral("Tracking");
    const QString stamp = savedAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd_HHmmss_zzz"));
    const QString baseName = QStringLiteral("camera%1_%2_%3_state-%4_frame-%5.bmp")
                                 .arg(cameraIndex + 1)
                                 .arg(imageKind)
                                 .arg(stamp)
                                 .arg(stateLabel)
                                 .arg(packet.frameId);
    QString imagePath = sessionDir.filePath(QStringLiteral("images/%1").arg(baseName));
    int suffix = 1;
    while (QFileInfo::exists(imagePath)) {
        imagePath = sessionDir.filePath(QStringLiteral("images/%1_%2.bmp")
                                            .arg(baseName.chopped(4))
                                            .arg(suffix++));
    }

    try {
        if (!cv::imwrite(imagePath.toStdString(), mono8)) {
            setStatusMessage(QStringLiteral("BMP 保存失败"), UiStatusLevel::Warning);
            return false;
        }
    } catch (const cv::Exception& error) {
        qWarning() << "live BMP save failed:" << error.what();
        setStatusMessage(QStringLiteral("BMP 保存失败: %1").arg(error.what()),
                         UiStatusLevel::Warning);
        return false;
    }

    lastSavedMs = nowMs;
    lastSavedFrameId = packet.frameId;
    return true;
}

void DIMM::initResultFile()
{
    if (m_resultWriter.isOpen()) {
        return;
    }

    if (!m_resultSessionStartedAt.isValid()) {
        m_resultSessionStartedAt = QDateTime::currentDateTime();
    }
    const QString filename = AcquisitionCsv::makeUniqueSessionFilePath(
        m_dataPath,
        m_resultSessionType,
        m_resultSessionStartedAt);
    const QFileInfo fileInfo(filename);
    QDir sessionDir(fileInfo.absolutePath());
    if (!sessionDir.mkpath(QStringLiteral(".")) ||
        !sessionDir.mkpath(QStringLiteral("images"))) {
        setStatusMessage(QStringLiteral("结果会话目录创建失败"), UiStatusLevel::Error);
        return;
    }

    m_resultSessionId = sessionDir.dirName();
    m_detailResultFilePath.clear();
    m_syncDiagnosticFilePath.clear();
    resetSyncDiagnostics();
    m_resultFileState = m_captureState;
    ResultFileConfig config;
    config.filePath = filename;
    config.headerLine = AcquisitionCsv::headerLine();
    QString error;
    if (m_resultWriter.open(config, &error)) {
        m_resultFilePath = filename;
    } else {
        setStatusMessage(QStringLiteral("结果文件创建失败: %1").arg(error), UiStatusLevel::Error);
    }
}

void DIMM::initDetailResultFile()
{
    // Paired details remain available in CaptureRuntimeContext for validation, but
    // the session contract has exactly one CSV file.
}

void DIMM::initSyncDiagnosticFile()
{
    // Synchronous diagnostics use the application log; they are not independent
    // result files and therefore cannot create a second session artifact.
}

void DIMM::closeResultFile(ResultSessionEndReason reason)
{
    if (m_rateSwitchTimingPending) {
        RateSwitchTiming timing;
        timing.pauseMs = m_rateSwitchPauseMs;
        timing.hardwareApplyMs = m_rateSwitchHardwareApplyMs;
        timing.firstValidPairMs = 0;
        timing.success = false;
        logRateSwitchTiming(timing,
                            m_rateSwitchOldRateHz,
                            m_rateSwitchNewRateHz,
                            m_configTriggerMode == 0
                                ? QStringLiteral("continuous")
                                : QStringLiteral("hardware_trigger"));
    }
    m_rateSwitchInProgress = false;
    m_rateSwitchTimingPending = false;
    if (m_resultSessionActive && m_resultWriter.isOpen()) {
        QString reasonText;
        switch (reason) {
        case ResultSessionEndReason::AutoWindowEnd:
            reasonText = QStringLiteral("auto_end");
            break;
        case ResultSessionEndReason::UnrecoverableError:
            reasonText = QStringLiteral("hardware_error");
            break;
        case ResultSessionEndReason::Destruction:
            reasonText = QStringLiteral("destruction");
            break;
        case ResultSessionEndReason::ManualStop:
        default:
            reasonText = QStringLiteral("manual_end");
            break;
        }
        writeResultSessionEvent(
            QStringLiteral("SessionEnded"),
            reasonText,
            QStringLiteral("Ended"),
            -1);
    }
    QString resultError;
    if (!m_resultWriter.close(&resultError)) {
        setStatusMessage(QStringLiteral("结果 CSV 写入失败: %1").arg(resultError),
                         UiStatusLevel::Error);
    }
    m_detailResultWriter.close();
    m_syncDiagnosticWriter.close();
    m_autoFocusLogWriter.close();
    m_autoFocusRunActive[0] = false;
    m_autoFocusRunActive[1] = false;
    m_autoFocusRealtimeMetricsSampler.reset();
    m_detailResultFilePath.clear();
    m_syncDiagnosticFilePath.clear();
    m_resultFileState = CaptureState::Idle;
    m_starTrackingState = StarTrackingState::Unknown;
    m_searchEventGate.reset();
    m_resultSessionActive = false;
    m_lastResultRecordTimestampMs = -1;
}

void DIMM::closeResultSessionForHardwareError()
{
    closeResultFile(ResultSessionEndReason::UnrecoverableError);
}

void DIMM::logRateSwitchTiming(const RateSwitchTiming& timing,
                               double oldRateHz,
                               double newRateHz,
                               const QString& mode)
{
    qInfo() << "acquisition rate switch"
            << "mode" << mode
            << "oldRateHz" << oldRateHz
            << "newRateHz" << newRateHz
            << "exposureAUs" << m_cameraExposureUs[0]
            << "exposureBUs" << m_cameraExposureUs[1]
            << "pauseMs" << timing.pauseMs
            << "hardwareApplyMs" << timing.hardwareApplyMs
            << "firstValidPairMs" << timing.firstValidPairMs
            << "outcome" << (timing.success ? QStringLiteral("success")
                                             : QStringLiteral("rollback_or_failure"));
}

void DIMM::saveResultRow(int frame)
{
    ++m_resultRowsSeen;
    const int interval = qMax(1, m_saveInterval);
    if ((m_resultRowsSeen - 1) % interval != 0) {
        return;
    }

    auto& runtime = activeRuntime();
    if (m_captureStopRequested) {
        return;
    }
    if (m_captureState != CaptureState::Live ||
        m_liveStartupPhase != LiveStartupPhase::Tracking ||
        !m_resultSessionActive || !AcquisitionCsv::allowsData(m_starTrackingState) ||
        !runtime.hasValidCentroid[0] || !runtime.hasValidCentroid[1] ||
        !runtime.hasValidAtmosphere || !ensureResultFileOpen()) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const QDateTime savedAt = QDateTime::currentDateTime();
    const AtmosphericParams& atmosphere = runtime.latestAtmosphere;
    const double frameRate = currentTrackingFrameRateHz();
    const std::uint32_t deviceStatus = monitoringDeviceStatus(
        runtime, nowMs, frameRate, std::isfinite(frameRate) && frameRate > 0.0);
    AcquisitionCsvDataRecord record;
    record.timestamp = nextResultRecordTimestamp(savedAt);
    record.sessionId = m_resultSessionId;
    record.sourceTimestamp = QDateTime::fromMSecsSinceEpoch(
        runtime.latestAtmosphereTimestampMs > 0
            ? static_cast<qint64>(runtime.latestAtmosphereTimestampMs)
            : nowMs);
    record.savedAt = savedAt;
    record.sourceGeneration = m_liveAcquisitionGeneration;
    record.temperature = m_latestEnvironment.temperatureC;
    record.humidity = m_latestEnvironment.humidityRh;
    record.pressure = m_latestEnvironment.pressureHpa;
    record.r0 = atmosphere.r0;
    record.seeing = atmosphere.seeing;
    record.theta0 = atmosphere.theta0;
    record.tau0 = atmosphere.tau0Valid ? atmosphere.tau0 : std::numeric_limits<double>::quiet_NaN();
    record.peakBrightnessA = runtime.peakBrightness[0];
    record.peakBrightnessB = runtime.peakBrightness[1];
    record.exposureTimeA = m_cameraExposureUs[0];
    record.exposureTimeB = m_cameraExposureUs[1];
    record.frameRate = frameRate;
    record.deviceStatus = QString::number(deviceStatus);
    record.acquisitionState = QStringLiteral("Tracking");
    record.searchAttempt = m_searchAttempt;
    // Keep the primary CSV row cadence and the two camera columns intact,
    // while consuming independent live metric groups for each camera.
    const auto realtimeMetricsA =
        m_autoFocusRealtimeMetricsSampler.takeCompletedMetrics(0, nowMs);
    if (realtimeMetricsA.has_value()) {
        record.autoFocusHfrA = realtimeMetricsA->hfr;
        record.autoFocusRmsA = realtimeMetricsA->rms;
        record.hasAutoFocusMetricsA = true;
    }
    const auto realtimeMetricsB =
        m_autoFocusRealtimeMetricsSampler.takeCompletedMetrics(1, nowMs);
    if (realtimeMetricsB.has_value()) {
        record.autoFocusHfrB = realtimeMetricsB->hfr;
        record.autoFocusRmsB = realtimeMetricsB->rms;
        record.hasAutoFocusMetricsB = true;
    }
    m_resultWriter.enqueueLine(AcquisitionCsv::formatDataRecord(record));
    saveDetailResultRows(frame, runtime.pendingPairedCentroidDetails);
    runtime.pendingPairedCentroidDetails.clear();
}

void DIMM::saveDetailResultRows(int frame, const QVector<PairedCentroidDetail>& details)
{
    Q_UNUSED(frame);
    Q_UNUSED(details);
}

void DIMM::flushPendingWrites()
{
    QString resultError;
    if (!m_resultWriter.flush(&resultError)) {
        setStatusMessage(QStringLiteral("结果 CSV 写入失败: %1").arg(resultError),
                         UiStatusLevel::Error);
    }
    m_detailResultWriter.flush();
    m_syncDiagnosticWriter.flush();
}

void DIMM::resetSyncDiagnostics()
{
    for (int i = 0; i < 2; ++i) {
        m_diagnosticLastCapturedFrameId[i] = 0;
        m_diagnosticCapturedPacketCount[i] = 0;
        m_diagnosticCapturedPacketGapCount[i] = 0;
        m_diagnosticLastLiveFrameId[i] = 0;
        m_diagnosticLivePacketCount[i] = 0;
        m_diagnosticLivePacketGapCount[i] = 0;
    }
}

void DIMM::recordSyncDiagnosticEvent(const QString& event,
                                     int cameraIndex,
                                     const CameraFrame& packet,
                                     const QString& note)
{
    if (!hasActiveCapture() ||
        !m_syncDiagnosticLoggingEnabled ||
        cameraIndex < 0 ||
        cameraIndex >= 2) {
        return;
    }
    const bool captureEvent = event == QStringLiteral("capture");
    quint64& lastFrameId = captureEvent
                               ? m_diagnosticLastCapturedFrameId[cameraIndex]
                               : m_diagnosticLastLiveFrameId[cameraIndex];
    quint64& packetCount = captureEvent
                               ? m_diagnosticCapturedPacketCount[cameraIndex]
                               : m_diagnosticLivePacketCount[cameraIndex];
    quint64& gapCount = captureEvent
                            ? m_diagnosticCapturedPacketGapCount[cameraIndex]
                            : m_diagnosticLivePacketGapCount[cameraIndex];

    const quint64 expectedNextFrameId = lastFrameId > 0 ? lastFrameId + 1 : 0;
    if (packet.frameId > 0 && expectedNextFrameId > 0 && packet.frameId != expectedNextFrameId) {
        ++gapCount;
    }
    if (packet.frameId > 0) {
        lastFrameId = packet.frameId;
    }
    ++packetCount;

    qInfo() << "sync diagnostic"
            << event
            << "sync_residual_us sync_jitter_us sync_jitter_avg_us sync_jitter_max_us"
            << "camera" << cameraIndex + 1
            << "frame" << packet.frameId
            << "cameraTimestamp" << packet.cameraTimestamp
            << "receivedMs" << packet.receivedMs
            << "generation" << m_liveAcquisitionGeneration
            << "packetCount" << packetCount
            << "gapCount" << gapCount
            << "expectedNextFrameId" << expectedNextFrameId
            << note;
}

void DIMM::recordSyncUnpairedDropDiagnostic(int droppedCameraIndex,
                                            quint64 cam0FrameId,
                                            quint64 cam1FrameId,
                                            qint64 frameIdOffset,
                                            qint64 alignedFrameId0,
                                            qint64 alignedFrameId1,
                                            quint64 cam0Timestamp,
                                            quint64 cam1Timestamp,
                                            quint64 droppedUnpairedSamples)
{
    if (!hasActiveCapture() ||
        !m_syncDiagnosticLoggingEnabled ||
        droppedCameraIndex < 0 ||
        droppedCameraIndex >= 2) {
        return;
    }
    const bool droppedCam0 = droppedCameraIndex == 0;
    const quint64 frameId = droppedCam0 ? cam0FrameId : cam1FrameId;
    const quint64 peerFrameId = droppedCam0 ? cam1FrameId : cam0FrameId;
    const quint64 cameraTimestamp = droppedCam0 ? cam0Timestamp : cam1Timestamp;
    const qint64 alignedFrameId = droppedCam0 ? alignedFrameId0 : alignedFrameId1;
    const qint64 peerAlignedFrameId = droppedCam0 ? alignedFrameId1 : alignedFrameId0;

    qInfo() << "sync unpaired drop"
            << "camera" << droppedCameraIndex + 1
            << "frame" << frameId
            << "peerFrame" << peerFrameId
            << "frameIdOffset" << frameIdOffset
            << "alignedFrame" << alignedFrameId
            << "peerAlignedFrame" << peerAlignedFrameId
            << "droppedSamples" << droppedUnpairedSamples
            << "cameraTimestamp" << cameraTimestamp;
}

QString DIMM::csvSafeField(QString value) const
{
    value.replace(QLatin1Char(','), QLatin1Char(';'));
    value.replace(QLatin1Char('\n'), QLatin1Char(' '));
    value.replace(QLatin1Char('\r'), QLatin1Char(' '));
    return value;
}

void DIMM::reportMeasurement()
{
    if (!canReportMeasurements()) {
        return;
    }

    const auto& runtime = activeRuntime();
    const float temperature = finiteFloatOrNaN(m_latestEnvironment.temperatureC,
                                               m_latestEnvironment.valid);
    const float humidity = finiteFloatOrNaN(m_latestEnvironment.humidityRh,
                                            m_latestEnvironment.valid);
    const float pressure = finiteFloatOrNaN(m_latestEnvironment.pressureHpa,
                                            m_latestEnvironment.valid);
    const bool atmosphereValid = runtime.hasValidAtmosphere;
    const float r0 = finiteFloatOrNaN(runtime.latestAtmosphere.r0, atmosphereValid);
    const float seeing = finiteFloatOrNaN(runtime.latestAtmosphere.seeing, atmosphereValid);
    const float theta0 = finiteFloatOrNaN(runtime.latestAtmosphere.theta0, atmosphereValid);
    const float tau0 = finiteFloatOrNaN(
        runtime.latestAtmosphere.tau0,
        atmosphereValid && runtime.latestAtmosphere.tau0Valid &&
            !runtime.latestAtmosphere.tau0UnderResolved);
    const float peakBrightnessCameraA = finiteFloatOrNaN(
        runtime.peakBrightness[0],
        runtime.hasValidCentroid[0]);
    const float peakBrightnessCameraB = finiteFloatOrNaN(
        runtime.peakBrightness[1],
        runtime.hasValidCentroid[1]);
    double frameRateHz = std::numeric_limits<double>::quiet_NaN();
    bool frameRateValid = false;
    if (m_configTriggerMode == 0) {
        const double cameraAFrameRate = m_lastContinuousFrameRateReadback[0];
        const double cameraBFrameRate = m_lastContinuousFrameRateReadback[1];
        const double frameRateTolerance =
            std::max(0.05, std::abs(m_activeContinuousFrameRateHz) * 0.05);
        frameRateValid = std::isfinite(cameraAFrameRate) && cameraAFrameRate > 0.0 &&
                         std::isfinite(cameraBFrameRate) && cameraBFrameRate > 0.0 &&
                         std::abs(cameraAFrameRate - cameraBFrameRate) <= frameRateTolerance;
        if (frameRateValid) {
            frameRateHz = (cameraAFrameRate + cameraBFrameRate) * 0.5;
        }
    } else {
        frameRateHz = m_activeTriggerFrequencyHz;
        frameRateValid = std::isfinite(frameRateHz) && frameRateHz > 0.0;
    }
    const float exposureTimeCameraAUs = finiteFloatOrNaN(
        m_cameraExposureUs[0],
        std::isfinite(m_cameraExposureUs[0]) && m_cameraExposureUs[0] > 0.0);
    const float exposureTimeCameraBUs = finiteFloatOrNaN(
        m_cameraExposureUs[1],
        std::isfinite(m_cameraExposureUs[1]) && m_cameraExposureUs[1] > 0.0);
    const float reportedFrameRateHz = finiteFloatOrNaN(frameRateHz, frameRateValid);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const std::uint32_t deviceStatus =
        monitoringDeviceStatus(runtime, nowMs, frameRateHz, frameRateValid);
    const quint64 timestampMs = runtime.latestAtmosphereTimestampMs != 0
                                    ? runtime.latestAtmosphereTimestampMs
                                    : static_cast<quint64>(nowMs);
    m_commManager->sendMonitoringFrame(temperature,
                                       humidity,
                                       pressure,
                                       r0,
                                       seeing,
                                       theta0,
                                       tau0,
                                       peakBrightnessCameraA,
                                       peakBrightnessCameraB,
                                       exposureTimeCameraAUs,
                                       exposureTimeCameraBUs,
                                       reportedFrameRateHz,
                                       deviceStatus,
                                       timestampMs);
}
