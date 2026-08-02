#include "DIMM.h"

#include "CameraManager.h"
#include "CommManager.h"
#include "DimmRuntimeHelpers.h"
#include "ImageProcessor.h"
#include "PathUtils.h"
#include "SettingsDialog.h"

#include <QDateTime>
#include <QDir>
#include <QStringList>
#include <QVector>

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
                       "ae_frames_since_adjust,"
                       "r0_cm,seeing_arcsec,theta0_arcsec,tau0_ms,"
                       "var_longitudinal_px2,var_transverse_px2,"
                       "sigma_longitudinal_rad2,sigma_transverse_rad2,"
                       "r0_longitudinal_cm,r0_transverse_cm,variance_sample_count,"
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
        setStatusMessage(QStringLiteral("璐ㄥ績鏄庣粏鏂囦欢鍒涘缓澶辫。"), UiStatusLevel::Error);
    }
}

void DIMM::closeResultFile()
{
    m_resultWriter.close();
    m_detailResultWriter.close();
    m_detailResultFilePath.clear();
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
        QString::number(runtime.latestAtmosphere.r0, 'f', 3),
        QString::number(runtime.latestAtmosphere.seeing, 'f', 3),
        QString::number(runtime.latestAtmosphere.theta0, 'f', 3),
        QString::number(runtime.latestAtmosphere.tau0, 'f', 3),
        QString::number(runtime.latestAtmosphere.longitudinalVariancePx2, 'g', 12),
        QString::number(runtime.latestAtmosphere.transverseVariancePx2, 'g', 12),
        QString::number(runtime.latestAtmosphere.longitudinalVarianceRad2, 'g', 12),
        QString::number(runtime.latestAtmosphere.transverseVarianceRad2, 'g', 12),
        QString::number(runtime.latestAtmosphere.r0LongitudinalCm, 'f', 3),
        QString::number(runtime.latestAtmosphere.r0TransverseCm, 'f', 3),
        QString::number(runtime.latestAtmosphere.sampleCount),
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
    const RoiRect roi0 = m_imageProcessor ? m_imageProcessor->getCurrentRoi(0) : RoiRect();
    const RoiRect roi1 = m_imageProcessor ? m_imageProcessor->getCurrentRoi(1) : RoiRect();
    m_commManager->sendMeasurement(runtime.latestAtmosphere.r0,
                                   runtime.latestAtmosphere.seeing,
                                   runtime.latestAtmosphere.theta0,
                                   runtime.latestAtmosphere.tau0,
                                   runtime.centroidX[0],
                                   runtime.centroidY[0],
                                   runtime.centroidX[1],
                                   runtime.centroidY[1],
                                   runtime.peakBrightness[0],
                                   runtime.peakBrightness[1],
                                   roi0.x,
                                   roi0.y,
                                   roi0.w,
                                   roi0.h,
                                   roi1.x,
                                   roi1.y,
                                   roi1.w,
                                   roi1.h,
                                   static_cast<uint32_t>(runtime.frameCount));
}

void DIMM::reportDeviceStatus()
{
    if (!m_commConnected || !isLiveCaptureActive()) {
        return;
    }

    float temp = 0.0f;
    float fps = 0.0f;
    const bool cam0Connected = m_cameraManager->isOpen(0);
    const bool cam1Connected = m_cameraManager->isOpen(1);
    if (m_latestEnvironment.valid) {
        temp = static_cast<float>(m_latestEnvironment.temperatureC);
    }
    if (cam0Connected) {
        fps = static_cast<float>(m_cameraManager->getFrameRate(0));
    }

    const uint32_t uptimeMs = static_cast<uint32_t>(QDateTime::currentMSecsSinceEpoch()) - m_startTimeMs;
    m_commManager->sendDeviceStatus(temp,
                                    fps,
                                    cam0Connected,
                                    cam1Connected,
                                    hasActiveCapture(),
                                    uptimeMs);
}
