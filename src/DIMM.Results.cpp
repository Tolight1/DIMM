#include "DIMM.h"

#include "CameraManager.h"
#include "CommManager.h"
#include "CommProtocol.h"
#include "DimmRuntimeHelpers.h"
#include "ImageProcessor.h"
#include "PathUtils.h"
#include "PulseGeneratorManager.h"
#include "SettingsDialog.h"

#include <QDateTime>
#include <QDir>
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
                                            ? m_configContinuousFrameRateHz
                                            : m_pulseGeneratorFrequencyHz);
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

void DIMM::initResultFile()
{
    if (m_resultWriter.isOpen()) {
        return;
    }

    QDir rootDir(m_dataPath);
    if (!rootDir.exists()) {
        rootDir.mkpath(QStringLiteral("."));
    }

    const QString modeDirPath = rootDir.filePath(resultSubdirectoryName());
    QDir modeDir(modeDirPath);
    if (!modeDir.exists()) {
        rootDir.mkpath(resultSubdirectoryName());
    }

    const QString timestampText = QDateTime::currentDateTime().toString(
        QStringLiteral("yyyy-MM-dd_HHmmss"));
    const QString filename = QStringLiteral("%1/DIMM_%2_measurements_%3.txt")
                                 .arg(modeDirPath,
                                      captureModeName(),
                                      timestampText);
    m_detailResultFilePath = QStringLiteral("%1/DIMM_%2_paired_centroids_%3.txt")
                                 .arg(modeDirPath,
                                      captureModeName(),
                                      timestampText);
    m_syncDiagnosticFilePath = QStringLiteral("%1/DIMM_%2_sync_diagnostics_%3.txt")
                                   .arg(modeDirPath,
                                        captureModeName(),
                                        timestampText);
    resetSyncDiagnostics();
    m_resultFileState = m_captureState;
    ResultFileConfig config;
    config.filePath = filename;
    const double apertureMm = m_imageProcessor ? m_imageProcessor->apertureDiameterMm() : 0.0;
    const double baselineMm = m_imageProcessor ? m_imageProcessor->baselineSeparationMm() : 0.0;
    const double baselineAngleDeg = m_imageProcessor ? m_imageProcessor->baselineAngleDeg() : 0.0;
    const double focalCm = m_imageProcessor ? m_imageProcessor->focalLengthCm() : 0.0;
    const double zenithDeg = m_imageProcessor ? m_imageProcessor->zenithAngleDeg() : 0.0;
    const double wavelengthNm = m_imageProcessor ? m_imageProcessor->wavelengthNm() : 0.0;
    const double pixelUm = m_imageProcessor ? m_imageProcessor->pixelSizeUm() : 0.0;
    config.headerLine =
        QStringLiteral("# capture_mode=%1, capture_label=%2\n"
                       "# aperture_mm=%3,baseline_mm=%4,baseline_angle_deg=%5,"
                       "focal_cm=%6,zenith_deg=%7,wavelength_nm=%8,pixel_um=%9\n"
                       "timestamp,mode,frame,paired_samples,dropped_unpaired_samples,"
                       "roi_acquisition_generation,roi_update_count,roi_update_reason,"
                       "roi1_x,roi1_y,roi1_w,roi1_h,roi2_x,roi2_y,roi2_w,roi2_h,ms_since_last_roi_update,"
                       "continuous_frame_rate_target_hz,camera1_frame_rate_readback_hz,camera2_frame_rate_readback_hz,"
                       "camera1_peak_dn,camera2_peak_dn,camera1_snr,camera2_snr,"
                       "camera1_valid_ratio,camera2_valid_ratio,"
                       "camera1_exposure_us,camera2_exposure_us,"
                       "camera1_hot_pixel_template_exposure_us,camera2_hot_pixel_template_exposure_us,"
                       "ae_enabled,ae_state,ae_camera1_state,ae_camera2_state,"
                       "ae_reason,ae_camera1_reason,ae_camera2_reason,"
                       "ae_sequence_id,ae_target_exposure_us,"
                       "ae_camera1_target_exposure_us,ae_camera2_target_exposure_us,"
                       "ae_frames_since_adjust,ae_ui_state,ae_adjust_direction,"
                       "ae_cooldown_remaining_ms,ae_adjustment_session_active,ae_step_us,"
                       "r0_cm,seeing_arcsec,theta0_arcsec,tau0_ms,tau0_state,tau0_resolution_ms,"
                       "var_longitudinal_px2,var_transverse_px2,"
                       "sigma_longitudinal_rad2,sigma_transverse_rad2,"
                       "r0_longitudinal_cm,r0_transverse_cm,variance_sample_count,"
                       "r0_partial_window,r0_risk_flag,r0_target_sample_count,r0_risk_reason,"
                       "sync_residual_us,sync_jitter_us,sync_jitter_avg_us,sync_jitter_max_us,"
                       "env_temperature_c,env_humidity_rh,env_pressure_hpa,env_sensor_valid,"
                       "comm_connected,reporting_enabled")
            .arg(captureModeName(), captureModeLabel())
            .arg(apertureMm, 0, 'f', 3)
            .arg(baselineMm, 0, 'f', 3)
            .arg(baselineAngleDeg, 0, 'f', 3)
            .arg(focalCm, 0, 'f', 3)
            .arg(zenithDeg, 0, 'f', 3)
            .arg(wavelengthNm, 0, 'f', 3)
            .arg(pixelUm, 0, 'f', 3);
    QString error;
    if (m_resultWriter.open(config, &error)) {
        m_resultFilePath = filename;
        if (m_parameterValidationEnabled) {
            initDetailResultFile();
        }
        if (m_syncDiagnosticLoggingEnabled) {
            initSyncDiagnosticFile();
        }
    } else {
        setStatusMessage(QStringLiteral("结果文件创建失败"), UiStatusLevel::Error);
    }
}

void DIMM::initDetailResultFile()
{
    if (!m_parameterValidationEnabled) {
        return;
    }
    if (m_detailResultWriter.isOpen() || m_detailResultFilePath.isEmpty()) {
        return;
    }

    ResultFileConfig config;
    config.filePath = m_detailResultFilePath;
    const double apertureMm = m_imageProcessor ? m_imageProcessor->apertureDiameterMm() : 0.0;
    const double baselineMm = m_imageProcessor ? m_imageProcessor->baselineSeparationMm() : 0.0;
    const double baselineAngleDeg = m_imageProcessor ? m_imageProcessor->baselineAngleDeg() : 0.0;
    const double focalCm = m_imageProcessor ? m_imageProcessor->focalLengthCm() : 0.0;
    const double zenithDeg = m_imageProcessor ? m_imageProcessor->zenithAngleDeg() : 0.0;
    const double wavelengthNm = m_imageProcessor ? m_imageProcessor->wavelengthNm() : 0.0;
    const double pixelUm = m_imageProcessor ? m_imageProcessor->pixelSizeUm() : 0.0;
    config.headerLine =
        QStringLiteral("# capture_mode=%1, capture_label=%2\n"
                       "# aperture_mm=%3,baseline_mm=%4,baseline_angle_deg=%5,"
                       "focal_cm=%6,zenith_deg=%7,wavelength_nm=%8,pixel_um=%9\n"
                       "timestamp,mode,frame,pair_index,cam1_frame_id,cam2_frame_id,pair_serial,"
                       "cam1_timestamp,cam2_timestamp,"
                       "cam1_centroid_x,cam1_centroid_y,cam2_centroid_x,cam2_centroid_y,"
                       "longitudinal_px,transverse_px,sync_residual_us")
            .arg(captureModeName(), captureModeLabel())
            .arg(apertureMm, 0, 'f', 3)
            .arg(baselineMm, 0, 'f', 3)
            .arg(baselineAngleDeg, 0, 'f', 3)
            .arg(focalCm, 0, 'f', 3)
            .arg(zenithDeg, 0, 'f', 3)
            .arg(wavelengthNm, 0, 'f', 3)
            .arg(pixelUm, 0, 'f', 3);
    QString error;
    if (!m_detailResultWriter.open(config, &error)) {
        setStatusMessage(QStringLiteral("质心明细文件创建失败"), UiStatusLevel::Error);
    }
}

void DIMM::initSyncDiagnosticFile()
{
    if (!m_syncDiagnosticLoggingEnabled) {
        return;
    }
    if (m_syncDiagnosticWriter.isOpen() || m_syncDiagnosticFilePath.isEmpty()) {
        return;
    }

    ResultFileConfig config;
    config.filePath = m_syncDiagnosticFilePath;
    config.headerLine =
        QStringLiteral("# capture_mode=%1, capture_label=%2\n"
                       "timestamp_ms,event,camera,frame_id,camera_timestamp,received_ms,"
                       "live_generation,boundary_count,boundary_gap_count,expected_next_frame_id,"
                       "peer_frame_id,frame_id_offset,aligned_frame_id,peer_aligned_frame_id,"
                       "dropped_unpaired_samples,note")
            .arg(captureModeName(), captureModeLabel());

    QString error;
    if (!m_syncDiagnosticWriter.open(config, &error)) {
        setStatusMessage(QStringLiteral("同步诊断日志创建失败"), UiStatusLevel::Error);
    }
}

void DIMM::closeResultFile()
{
    m_resultWriter.close();
    m_detailResultWriter.close();
    m_syncDiagnosticWriter.close();
    m_detailResultFilePath.clear();
    m_syncDiagnosticFilePath.clear();
    m_resultFileState = CaptureState::Idle;
}

void DIMM::saveResultRow(int frame)
{
    ++m_resultRowsSeen;
    const int interval = qMax(1, m_saveInterval);
    if ((m_resultRowsSeen - 1) % interval != 0) {
        return;
    }

    if (!m_resultWriter.isOpen()) {
        initResultFile();
    }
    if (!m_resultWriter.isOpen()) {
        return;
    }
    if (m_resultFileState != m_captureState) {
        closeResultFile();
        initResultFile();
        if (!m_resultWriter.isOpen()) {
            return;
        }
    }

    auto& runtime = activeRuntime();
    RoiRect currentRois[2];
    if (m_imageProcessor) {
        currentRois[0] = m_imageProcessor->getCurrentRoi(0);
        currentRois[1] = m_imageProcessor->getCurrentRoi(1);
    }
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 msSinceLastRoiUpdate = m_lastRoiUpdateMs >= 0 ? nowMs - m_lastRoiUpdateMs : -1;
    const QString roiUpdateReason = csvSafeField(m_lastRoiUpdateReason);

    const AtmosphericParams& atmosphere = runtime.latestAtmosphere;

    const QString tau0ValueText =
        atmosphere.tau0Valid
            ? QString::number(atmosphere.tau0, 'f', 3)
            : QString();

    const QString tau0StateText =
        !atmosphere.tau0Valid
            ? QStringLiteral("invalid")
            : (atmosphere.tau0UnderResolved
                   ? QStringLiteral("under_resolved")
                   : QStringLiteral("resolved"));

    const QString tau0ResolutionText =
        atmosphere.tau0Valid
            ? QString::number(atmosphere.tau0ResolutionMs, 'f', 3)
            : QString();

    const QStringList fields = {
        QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
        captureModeName(),
        QString::number(frame),
        QString::number(runtime.pairedSampleCount),
        QString::number(runtime.droppedUnpairedSampleCount),
        QString::number(m_liveAcquisitionGeneration),
        QString::number(m_roiUpdateCount),
        roiUpdateReason,
        QString::number(currentRois[0].x),
        QString::number(currentRois[0].y),
        QString::number(currentRois[0].w),
        QString::number(currentRois[0].h),
        QString::number(currentRois[1].x),
        QString::number(currentRois[1].y),
        QString::number(currentRois[1].w),
        QString::number(currentRois[1].h),
        QString::number(msSinceLastRoiUpdate),
        QString::number(m_configContinuousFrameRateHz, 'f', 3),
        QString::number(m_lastContinuousFrameRateReadback[0], 'f', 3),
        QString::number(m_lastContinuousFrameRateReadback[1], 'f', 3),
        QString::number(m_latestAutoExposurePeakDn[0], 'f', 1),
        QString::number(m_latestAutoExposurePeakDn[1], 'f', 1),
        QString::number(m_latestAutoExposureSnr[0], 'f', 2),
        QString::number(m_latestAutoExposureSnr[1], 'f', 2),
        QString::number(m_latestAutoExposureValidRatio[0], 'f', 3),
        QString::number(m_latestAutoExposureValidRatio[1], 'f', 3),
        QString::number(m_cameraExposureUs[0], 'f', 0),
        QString::number(m_cameraExposureUs[1], 'f', 0),
        QString::number(m_hotPixelTemplateExposureUs[0]),
        QString::number(m_hotPixelTemplateExposureUs[1]),
        m_autoExposureConfig.enabled ? QStringLiteral("1") : QStringLiteral("0"),
        autoExposureStateName(m_autoExposureState),
        autoExposureStateName(m_cameraAutoExposureState[0]),
        autoExposureStateName(m_cameraAutoExposureState[1]),
        csvSafeField(m_autoExposureReason),
        csvSafeField(m_cameraAutoExposureReason[0]),
        csvSafeField(m_cameraAutoExposureReason[1]),
        QString::number(m_autoExposureSequenceId),
        QString::number(m_autoExposureTargetExposureUs),
        QString::number(m_cameraAutoExposureTargetExposureUs[0]),
        QString::number(m_cameraAutoExposureTargetExposureUs[1]),
        QString::number(m_autoExposureFramesSinceAdjust),
        csvSafeField(autoExposureUiStatusText()),
        csvSafeField(autoExposureAdjustDirectionText()),
        QString::number(m_autoExposureCooldownRemainingMs),
        m_autoExposureAdjustmentSessionActive ? QStringLiteral("1") : QStringLiteral("0"),
        QString::number(m_autoExposureConfig.autoExposureStepUs, 'f', 0),
        QString::number(runtime.latestAtmosphere.r0, 'f', 3),
        QString::number(runtime.latestAtmosphere.seeing, 'f', 3),
        QString::number(runtime.latestAtmosphere.theta0, 'f', 3),
        tau0ValueText,
        tau0StateText,
        tau0ResolutionText,
        QString::number(runtime.latestAtmosphere.longitudinalVariancePx2, 'g', 12),
        QString::number(runtime.latestAtmosphere.transverseVariancePx2, 'g', 12),
        QString::number(runtime.latestAtmosphere.longitudinalVarianceRad2, 'g', 12),
        QString::number(runtime.latestAtmosphere.transverseVarianceRad2, 'g', 12),
        QString::number(runtime.latestAtmosphere.r0LongitudinalCm, 'f', 3),
        QString::number(runtime.latestAtmosphere.r0TransverseCm, 'f', 3),
        QString::number(runtime.latestAtmosphere.sampleCount),
        runtime.latestAtmosphere.partialWindow ? QStringLiteral("1") : QStringLiteral("0"),
        runtime.latestAtmosphere.riskFlag ? QStringLiteral("1") : QStringLiteral("0"),
        QString::number(runtime.latestAtmosphere.targetSampleCount),
        csvSafeField(runtime.latestAtmosphere.riskReason),
        QString::number(runtime.latestSyncResidualUs, 'f', 3),
        QString::number(runtime.latestSyncJitterUs, 'f', 3),
        QString::number(runtime.averageSyncJitterUs, 'f', 3),
        QString::number(runtime.maxSyncJitterUs, 'f', 3),
        QString::number(m_latestEnvironment.temperatureC, 'f', 1),
        QString::number(m_latestEnvironment.humidityRh, 'f', 1),
        QString::number(m_latestEnvironment.pressureHpa, 'f', 1),
        m_latestEnvironment.valid ? QStringLiteral("1") : QStringLiteral("0"),
        m_commConnected ? QStringLiteral("1") : QStringLiteral("0"),
        m_reporting ? QStringLiteral("1") : QStringLiteral("0")
    };

    m_resultWriter.enqueue(MeasurementRecord{fields});
    saveDetailResultRows(frame, runtime.pendingPairedCentroidDetails);
    runtime.pendingPairedCentroidDetails.clear();
}

void DIMM::saveDetailResultRows(int frame, const QVector<PairedCentroidDetail>& details)
{
    if (!m_parameterValidationEnabled) {
        return;
    }
    if (details.isEmpty()) {
        return;
    }
    if (!m_detailResultWriter.isOpen()) {
        initDetailResultFile();
    }
    if (!m_detailResultWriter.isOpen()) {
        return;
    }

    for (int i = 0; i < details.size(); ++i) {
        const PairedCentroidDetail& detail = details[i];
        const QStringList fields = {
            QDateTime::fromMSecsSinceEpoch(detail.timestampMs).toString(Qt::ISODateWithMs),
            captureModeName(),
            QString::number(frame),
            QString::number(i + 1),
            QString::number(detail.frameId1),
            QString::number(detail.frameId2),
            QString::number(detail.pairedSampleCount),
            QString::number(detail.cameraTimestamp1),
            QString::number(detail.cameraTimestamp2),
            QString::number(detail.centroid1X, 'f', 6),
            QString::number(detail.centroid1Y, 'f', 6),
            QString::number(detail.centroid2X, 'f', 6),
            QString::number(detail.centroid2Y, 'f', 6),
            QString::number(detail.longitudinal, 'f', 6),
            QString::number(detail.transverse, 'f', 6),
            QString::number(detail.syncResidualUs, 'f', 3)
        };
        m_detailResultWriter.enqueue(MeasurementRecord{fields});
    }
}

void DIMM::flushPendingWrites()
{
    m_resultWriter.flush();
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
    if (!m_syncDiagnosticWriter.isOpen()) {
        if (m_syncDiagnosticFilePath.isEmpty()) {
            initResultFile();
        } else {
            initSyncDiagnosticFile();
        }
    }
    if (!m_syncDiagnosticWriter.isOpen()) {
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

    const QStringList fields{
        QString::number(QDateTime::currentMSecsSinceEpoch()),
        csvSafeField(event),
        QString::number(cameraIndex + 1),
        QString::number(packet.frameId),
        QString::number(packet.cameraTimestamp),
        QString::number(packet.receivedMs),
        QString::number(m_liveAcquisitionGeneration),
        QString::number(packetCount),
        QString::number(gapCount),
        QString::number(expectedNextFrameId),
        QString(),
        QString(),
        QString(),
        QString(),
        QString(),
        csvSafeField(note)
    };
    m_syncDiagnosticWriter.enqueue(MeasurementRecord{fields});
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
    if (!m_syncDiagnosticWriter.isOpen()) {
        if (m_syncDiagnosticFilePath.isEmpty()) {
            initResultFile();
        } else {
            initSyncDiagnosticFile();
        }
    }
    if (!m_syncDiagnosticWriter.isOpen()) {
        return;
    }

    const bool droppedCam0 = droppedCameraIndex == 0;
    const quint64 frameId = droppedCam0 ? cam0FrameId : cam1FrameId;
    const quint64 peerFrameId = droppedCam0 ? cam1FrameId : cam0FrameId;
    const quint64 cameraTimestamp = droppedCam0 ? cam0Timestamp : cam1Timestamp;
    const qint64 alignedFrameId = droppedCam0 ? alignedFrameId0 : alignedFrameId1;
    const qint64 peerAlignedFrameId = droppedCam0 ? alignedFrameId1 : alignedFrameId0;

    const QStringList fields{
        QString::number(QDateTime::currentMSecsSinceEpoch()),
        QStringLiteral("unpaired_drop"),
        QString::number(droppedCameraIndex + 1),
        QString::number(frameId),
        QString::number(cameraTimestamp),
        QString(),
        QString::number(m_liveAcquisitionGeneration),
        QString(),
        QString(),
        QString(),
        QString::number(peerFrameId),
        QString::number(frameIdOffset),
        QString::number(alignedFrameId),
        QString::number(peerAlignedFrameId),
        QString::number(droppedUnpairedSamples),
        QStringLiteral("older_unpaired")
    };
    m_syncDiagnosticWriter.enqueue(MeasurementRecord{fields});
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
            std::max(0.05, std::abs(m_configContinuousFrameRateHz) * 0.05);
        frameRateValid = std::isfinite(cameraAFrameRate) && cameraAFrameRate > 0.0 &&
                         std::isfinite(cameraBFrameRate) && cameraBFrameRate > 0.0 &&
                         std::abs(cameraAFrameRate - cameraBFrameRate) <= frameRateTolerance;
        if (frameRateValid) {
            frameRateHz = (cameraAFrameRate + cameraBFrameRate) * 0.5;
        }
    } else {
        frameRateHz = m_pulseGeneratorFrequencyHz;
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
