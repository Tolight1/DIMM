#include "DIMM.h"

#include "AppConfigPersistence.h"
#include "AutoAcquisitionScheduler.h"
#include "CameraManager.h"
#include "CommManager.h"
#include "ConfigApplicationController.h"
#include "DimmRuntimeHelpers.h"
#include "HotPixelTemplateSettings.h"
#include "ImageProcessor.h"
#include "InitialStarDetectionConfig.h"
#include "PathUtils.h"
#include "PulseGeneratorManager.h"
#include "SettingsDialog.h"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QStringList>

void DIMM::setupSettingsCallbacks()
{
    setupCameraSettingsCallbacks();
    setupAutoExposureSettingsCallbacks();
    setupTriggerSettingsCallbacks();
    setupEnvironmentSettingsCallbacks();
    setupPulseGeneratorSettingsCallbacks();
    setupAutoAcquisitionSettingsCallbacks();
    setupProcessingSettingsCallbacks();
    setupOpticsSettingsCallbacks();
    setupAlignmentSettingsCallbacks();
    setupStorageSettingsCallbacks();
    setupNetworkSettingsCallbacks();
}

void DIMM::setupCameraSettingsCallbacks()
{
    m_settingsDialog->onApplyCamera = [this](double exposure, double gain, double continuousFrameRateHz) {
        m_configExposureUs = exposure;
        m_cameraExposureUs[0] = exposure;
        m_cameraExposureUs[1] = exposure;
        m_configGainDb = gain;
        m_configContinuousFrameRateHz = continuousFrameRateHz;
        for (int i = 0; i < 2; ++i) {
            if (m_cameraManager->isOpen(i)) {
                m_cameraManager->setExposure(i, exposure);
                m_cameraManager->setGain(i, gain);
            }
        }
        QString reason;
        if (!applyContinuousCameraFrameRate(&reason)) {
            setStatusMessage(reason.isEmpty()
                                 ? QStringLiteral("连续采集帧率应用失败")
                                 : reason,
                             UiStatusLevel::Warning);
            return;
        }
        setStatusMessage(QStringLiteral("相机参数已应用"), UiStatusLevel::Success);
    };
}

void DIMM::setupAutoExposureSettingsCallbacks()
{
    m_settingsDialog->onApplyAutoExposure =
        [this](const AutoExposureConfig& config) {
            m_autoExposureConfig = config;
            resetAutoExposureState();
            if (m_imageProcessor) {
                m_imageProcessor->setAutoExposureMetricConfig(config.enabled,
                                                              config.hardSaturationDn,
                                                              config.autoExposureSampleIntervalMs,
                                                              config.peakSupportRadiusPx,
                                                              config.peakSupportFraction,
                                                              config.minPeakSupportPixelCount,
                                                              config.minNeighborPeakRatio,
                                                              config.maxPeakCandidateCount,
                                                              config.supportedPeakPercentile,
                                                              config.saturatedPixelCount);
            }
            setStatusMessage(config.enabled ? QStringLiteral("自动曝光已启用 状态机保护模式")
                                            : QStringLiteral("自动曝光已关闭"),
                             config.enabled ? UiStatusLevel::Success : UiStatusLevel::Warning);
        };
}

void DIMM::setupTriggerSettingsCallbacks()
{
    m_settingsDialog->onApplyTriggerMode = [this](int mode) {
        m_configTriggerMode = mode;
        if (isLiveCaptureActive()) {
            setStatusMessage(QStringLiteral("实时采集中，触发模式变更已保存，将在停止采集后生效"),
                             UiStatusLevel::Warning);
            return;
        }
        for (int i = 0; i < 2; ++i) {
            if (m_cameraManager->isOpen(i)) {
                if (mode == 0) {
                    m_cameraManager->setTriggerMode(i, TriggerMode::Continuous);
                } else {
                    m_cameraManager->configureExternalTrigger(i);
                }
            }
        }
        setStatusMessage(mode == 0 ? QStringLiteral("触发模式已切换为连续采集")
                                   : QStringLiteral("触发模式已切换为硬件触发"),
                        UiStatusLevel::Success);
    };
}

void DIMM::setupEnvironmentSettingsCallbacks()
{
    m_settingsDialog->onApplyEnvironmentSensor = [this](const EnvironmentSensorConfig& config) {
        m_environmentSensorConfig = config;
        m_latestEnvironment = EnvironmentSensorData();
        if (!m_environmentSensor) {
            return;
        }

        if (!m_environmentSensorConfig.enabled) {
            m_environmentSensor->stop();
            updateCameraInfo();
            setStatusMessage(QStringLiteral("温湿压传感器已关闭"), UiStatusLevel::Warning);
            return;
        }

        m_environmentSensor->start(m_environmentSensorConfig);
        updateCameraInfo();
        setStatusMessage(QStringLiteral("温湿压传感器串口已切换到 %1").arg(m_environmentSensorConfig.portName),
                         UiStatusLevel::Success);
    };
}

void DIMM::setupPulseGeneratorSettingsCallbacks()
{
    m_settingsDialog->onApplyPulseGenerator =
        [this](bool enabled,
               QString portName,
               int baudRate,
               int terminalId,
               double frequencyHz,
               quint32 pulseCount,
               double dutyPercent,
               bool remoteControl,
               QString* errorMessage) -> bool {
        m_pulseGeneratorEnabled = enabled;
        m_pulseGeneratorPort = portName;
        m_pulseGeneratorBaudRate = baudRate;
        m_pulseGeneratorTerminalId = terminalId;
        m_pulseGeneratorFrequencyHz = frequencyHz;
        m_pulseGeneratorPulseCount = pulseCount;
        m_pulseGeneratorDutyPercent = dutyPercent;
        m_pulseGeneratorRemoteControl = remoteControl;
        if (m_imageProcessor) {
            m_imageProcessor->setTargetFrameRateHz(m_pulseGeneratorFrequencyHz);
        }
        if (!m_pulseGenerator) {
            return true;
        }

        if (m_configTriggerMode == 0) {
            const QString savedMessage = enabled
                                             ? QStringLiteral("当前为连续采集模式，触发参数已保存，切换到硬件触发并开始采集时再下发")
                                             : QStringLiteral("当前为连续采集模式，触发输出已关闭");
            setStatusMessage(savedMessage, enabled ? UiStatusLevel::Info : UiStatusLevel::Warning);
            if (errorMessage) {
                *errorMessage = savedMessage;
            }
            return true;
        }

        if (isLiveCaptureActive()) {
            const QString pendingMessage = QStringLiteral("实时采集中，触发设置已保存，将在停止采集后再下发");
            setStatusMessage(pendingMessage, UiStatusLevel::Warning);
            if (errorMessage) {
                *errorMessage = pendingMessage;
            }
            return true;
        }

        PulseGeneratorManager::Config pulseConfig;
        pulseConfig.enabled = enabled;
        pulseConfig.portName = portName;
        pulseConfig.baudRate = baudRate;
        pulseConfig.terminalId = terminalId;
        pulseConfig.frequencyHz = frequencyHz;
        pulseConfig.pulseCount = pulseCount;
        pulseConfig.dutyPercent = dutyPercent;
        pulseConfig.remoteControl = remoteControl;
        if (!m_pulseGenerator->applyConfig(pulseConfig, errorMessage)) {
            setStatusMessage(errorMessage && !errorMessage->isEmpty()
                                 ? *errorMessage
                                 : QStringLiteral("触发设置下发失败"),
                             UiStatusLevel::Error);
            return false;
        }

        setStatusMessage(enabled
                             ? QStringLiteral("触发设置已下发到脉冲板 %1 @ %2 Hz")
                                   .arg(portName)
                                   .arg(frequencyHz, 0, 'f', 1)
                             : QStringLiteral("脉冲板输出已关闭并同步"),
                         enabled ? UiStatusLevel::Success : UiStatusLevel::Warning);
        return true;
        };
    m_settingsDialog->onStartPulseOutput =
        [this](QString portName,
               int baudRate,
               int terminalId,
               double frequencyHz,
               quint32 pulseCount,
               double dutyPercent,
               bool remoteControl,
               QString* errorMessage) -> bool {
        m_pulseGeneratorEnabled = true;
        m_pulseGeneratorPort = portName;
        m_pulseGeneratorBaudRate = baudRate;
        m_pulseGeneratorTerminalId = terminalId;
        m_pulseGeneratorFrequencyHz = frequencyHz;
        m_pulseGeneratorPulseCount = pulseCount;
        m_pulseGeneratorDutyPercent = dutyPercent;
        m_pulseGeneratorRemoteControl = remoteControl;
        if (m_imageProcessor) {
            m_imageProcessor->setTargetFrameRateHz(m_pulseGeneratorFrequencyHz);
        }
        if (!m_pulseGenerator) {
            return true;
        }

        PulseGeneratorManager::Config pulseConfig;
        pulseConfig.enabled = true;
        pulseConfig.portName = portName;
        pulseConfig.baudRate = baudRate;
        pulseConfig.terminalId = terminalId;
        pulseConfig.frequencyHz = frequencyHz;
        pulseConfig.pulseCount = pulseCount;
        pulseConfig.dutyPercent = dutyPercent;
        pulseConfig.remoteControl = remoteControl;
        if (!m_pulseGenerator->configureAndStart(pulseConfig, errorMessage)) {
            setStatusMessage(errorMessage && !errorMessage->isEmpty()
                                 ? *errorMessage
                                 : QStringLiteral("脉冲输出启动失败"),
                             UiStatusLevel::Error);
            return false;
        }

        if (m_captureState == CaptureState::Live && m_configTriggerMode != 0) {
            setStatusMessage(QStringLiteral("状态: 脉冲板已开始输出 %1 @ %2 Hz，等待相机接收触发帧")
                                 .arg(portName)
                                 .arg(frequencyHz, 0, 'f', 1),
                             UiStatusLevel::Success);
            scheduleHardwareTriggerStartupCheck();
        } else {
            setStatusMessage(QStringLiteral("脉冲板已开始输出 %1 @ %2 Hz")
                                 .arg(portName)
                                 .arg(frequencyHz, 0, 'f', 1),
                             UiStatusLevel::Success);
        }
        return true;
        };
    m_settingsDialog->onStopPulseOutput = [this](QString* errorMessage) -> bool {
        if (!m_pulseGenerator) {
            return true;
        }
        if (!m_pulseGenerator->stop(errorMessage)) {
            setStatusMessage(errorMessage && !errorMessage->isEmpty()
                                 ? *errorMessage
                                 : QStringLiteral("关闭脉冲失败"),
                             UiStatusLevel::Error);
            return false;
        }

        setStatusMessage(QStringLiteral("脉冲板输出已关闭"), UiStatusLevel::Warning);
        return true;
    };
}

void DIMM::setupAutoAcquisitionSettingsCallbacks()
{
    m_settingsDialog->onApplyAutoAcquisition = [this](const AutoAcquisitionConfig& config) {
        const QDateTime now = QDateTime::currentDateTime();

        const bool wasEnabled = m_autoAcquisitionConfig.enabled;

        const AutoAcquisitionWindow oldWindow =
            AutoAcquisitionScheduler::resolveWindow(
                m_autoAcquisitionConfig,
                now);

        m_autoAcquisitionConfig = config;

        const AutoAcquisitionWindow newWindow =
            AutoAcquisitionScheduler::resolveWindow(
                m_autoAcquisitionConfig,
                now);

        const bool scheduleChanged =
            oldWindow.valid != newWindow.valid ||
            (oldWindow.valid &&
             newWindow.valid &&
             oldWindow.windowId != newWindow.windowId);

        if ((!wasEnabled && config.enabled) || scheduleChanged) {
            m_autoAcquisitionSuppressedWindowId.clear();
            m_lastAutoAcquisitionAttemptMs = -1;
            m_lastAutoAcquisitionStatusKey.clear();
            m_lastAutoAcquisitionStatusMs = -1;

            if (!m_autoAcquisitionStartedCurrentRun) {
                m_autoAcquisitionActiveWindowId.clear();
            }

            resetLiveStartupRecoveryState(true);
        }

        const AutoAcquisitionWindow window = newWindow;
        if (m_settingsDialog->autoAcquisitionNextStartLabel) {
            m_settingsDialog->autoAcquisitionNextStartLabel->setText(
                window.valid
                    ? QStringLiteral("下次开始: %1").arg(window.start.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
                    : QStringLiteral("下次开始: %1").arg(window.errorMessage));
        }
        if (m_settingsDialog->autoAcquisitionNextStopLabel) {
            m_settingsDialog->autoAcquisitionNextStopLabel->setText(
                window.valid
                    ? QStringLiteral("下次停止: %1").arg(window.stop.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
                    : QStringLiteral("下次停止: %1").arg(window.errorMessage));
        }
        setStatusMessage(config.enabled ? QStringLiteral("自动采集已启用")
                                        : QStringLiteral("自动采集已关闭"),
                         config.enabled ? UiStatusLevel::Success : UiStatusLevel::Warning);
    };
}

void DIMM::setupProcessingSettingsCallbacks()
{
    m_settingsDialog->onApplyProcessing = [this](int backgroundKernelSize,
                                                 double backgroundSigmaMultiplier,
                                                 int centroidMode,
                                                 int peakKernelMethod,
                                                 int peakKernelRadiusPx,
                                                 double strongHotPixelExcessDn) {
        m_imageProcessor->setBackgroundDenoiseKernelSize(backgroundKernelSize);
        m_imageProcessor->setBackgroundDenoiseSigmaMultiplier(backgroundSigmaMultiplier);
        m_imageProcessor->setCentroidMode(centroidMode);
        m_imageProcessor->setPeakKernelCentroidConfig(peakKernelMethod,
                                                      peakKernelRadiusPx,
                                                      strongHotPixelExcessDn);
        setStatusMessage(QStringLiteral("图像处理参数已更新"), UiStatusLevel::Success);
    };
    m_settingsDialog->onApplyRoiRecentering =
        [this](double thresholdPx, int requiredFrames, qint64 cooldownMs, double minimumShiftPx) {
            m_roiRecenteringThresholdPx = thresholdPx;
            m_roiRecenteringRequiredFrames = requiredFrames;
            m_roiRecenteringCooldownMs = cooldownMs;
            m_roiRecenteringMinimumShiftPx = minimumShiftPx;
            activeRuntime().roiRecenteringCandidateFrameCount = 0;
            setStatusMessage(QStringLiteral("ROI 重居中参数已更新"), UiStatusLevel::Success);
        };
    m_settingsDialog->onApplyFullFrameStarDetection =
        [this](double thresholdAbsolute,
               double sigmaThreshold,
               double peakFraction,
               double minimumIntensity,
               int minArea,
               int maxArea) {
            InitialStarDetectionConfig config;
            config.thresholdAbsolute = thresholdAbsolute;
            config.sigmaThreshold = sigmaThreshold;
            config.peakFraction = peakFraction;
            config.minimumIntensity = minimumIntensity;
            config.minArea = minArea;
            config.maxArea = maxArea;
            setCurrentInitialStarDetectionConfig(config);
            setStatusMessage(QStringLiteral("全画幅找星参数已更新"), UiStatusLevel::Success);
        };
    m_settingsDialog->onApplyHotPixelTemplates =
        [this](bool enabled,
               QString camera0MaskPath,
               QString camera0ExcessPath,
               QString camera1MaskPath,
               QString camera1ExcessPath,
               int templateWidth,
               int templateHeight) {
            m_hotPixelTemplatesEnabled = enabled;
            m_hotPixelCamera0MaskPath = enabled ? PathUtils::relativizePathToAppDir(camera0MaskPath) : QString();
            m_hotPixelCamera0ExcessPath = enabled ? PathUtils::relativizePathToAppDir(camera0ExcessPath) : QString();
            m_hotPixelCamera1MaskPath = enabled ? PathUtils::relativizePathToAppDir(camera1MaskPath) : QString();
            m_hotPixelCamera1ExcessPath = enabled ? PathUtils::relativizePathToAppDir(camera1ExcessPath) : QString();
            m_hotPixelTemplateWidth = enabled ? templateWidth : 0;
            m_hotPixelTemplateHeight = enabled ? templateHeight : 0;
            m_hotPixelTemplateExposureUs[0] =
                enabled ? PathUtils::exposureUsFromTemplatePath(m_hotPixelCamera0MaskPath) : 0;
            m_hotPixelTemplateExposureUs[1] =
                enabled ? PathUtils::exposureUsFromTemplatePath(m_hotPixelCamera1MaskPath) : 0;
            m_cachedHotPixelTemplateExposures.clear();
            m_cachedHotPixelTemplateScanMs = -1;

            bool matchedRequestedExposureTemplate = false;
            bool missingRequestedExposureTemplate = false;
            if (enabled) {
                for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
                    const int selectedTemplateExposureUs =
                        selectHotPixelTemplateExposureForCameraExposure(cameraIndex,
                                                                        m_cameraExposureUs[cameraIndex]);
                    QString maskPath;
                    QString excessPath;
                    if (selectedTemplateExposureUs > 0 &&
                        selectedTemplateExposureUs != m_hotPixelTemplateExposureUs[cameraIndex] &&
                        resolveHotPixelTemplatePathsForCameraExposure(cameraIndex,
                                                                      selectedTemplateExposureUs,
                                                                      &maskPath,
                                                                      &excessPath)) {
                        if (cameraIndex == 0) {
                            m_hotPixelCamera0MaskPath = maskPath;
                            m_hotPixelCamera0ExcessPath = excessPath;
                        } else {
                            m_hotPixelCamera1MaskPath = maskPath;
                            m_hotPixelCamera1ExcessPath = excessPath;
                        }
                        m_hotPixelTemplateExposureUs[cameraIndex] = selectedTemplateExposureUs;
                        matchedRequestedExposureTemplate = true;
                    } else if (selectedTemplateExposureUs > 0 &&
                               selectedTemplateExposureUs != m_hotPixelTemplateExposureUs[cameraIndex]) {
                        missingRequestedExposureTemplate = true;
                    }
                }
            }

            refreshHotPixelTemplates();
            if (!enabled) {
                setStatusMessage(QStringLiteral("热像素模板修正已关闭"), UiStatusLevel::Warning);
            } else if (matchedRequestedExposureTemplate) {
                setStatusMessage(QStringLiteral("热像素模板已按相机曝光自动匹配并启用"),
                                 UiStatusLevel::Success);
            } else if (missingRequestedExposureTemplate) {
                setStatusMessage(QStringLiteral("部分相机当前曝光未找到对应热像素模板，保持原模板"),
                                 UiStatusLevel::Warning);
            } else {
                setStatusMessage(QStringLiteral("热像素模板修正已启用"), UiStatusLevel::Success);
            }
        };
}

void DIMM::setupOpticsSettingsCallbacks()
{
    m_settingsDialog->onApplyOptics =
        [this](double apertureDiameterMm,
               double baselineSeparationMm,
               double baselineAngleDeg,
               double focalLengthCm,
               double zenithAngleDeg,
               double lambdaNm,
               double pixelSizeUm) {
            m_imageProcessor->setOpticalParams(apertureDiameterMm,
                                               baselineSeparationMm,
                                               baselineAngleDeg,
                                               focalLengthCm,
                                               zenithAngleDeg,
                                               lambdaNm,
                                               pixelSizeUm);
        setStatusMessage(QStringLiteral("光学参数已更新"), UiStatusLevel::Success);
        };
}

void DIMM::setupAlignmentSettingsCallbacks()
{
    m_settingsDialog->onApplyAlignment =
        [this](bool autoRadius,
               double focalLengthMm,
               double pixelSizeUm,
               double polarDistanceArcmin,
               double radiusAdjustPx,
               double previewRateHz) {
            m_alignmentAutoRadius = autoRadius;
            m_alignmentFocalLengthMm = focalLengthMm;
            m_alignmentPixelSizeUm = pixelSizeUm;
            m_alignmentPolarisPolarDistanceArcmin = polarDistanceArcmin;
            m_alignmentRadiusAdjustPx = radiusAdjustPx;
            m_alignmentPreviewRateHz = previewRateHz;
            if (m_captureState == CaptureState::Alignment) {
                if (m_fullFrameCanvas1) {
                    m_fullFrameCanvas1->update();
                }
                if (m_fullFrameCanvas2) {
                    m_fullFrameCanvas2->update();
                }
                setStatusMessage(QStringLiteral("对准参数已更新，轨道半径 %1 px")
                                     .arg(alignmentOrbitRadiusPx(), 0, 'f', 1),
                                 UiStatusLevel::Info);
            }
        };
    m_settingsDialog->onApplyPolarisSolver =
        [this](bool enabled,
               bool showMatchedCatalogStars,
               int maxDetectedStars,
               int minMatchedStars,
               double maxRmsPx,
               int retryIntervalMs,
               double minMatchedSpatialSpreadPx,
               double minPolarisSnr,
               bool allowSaturatedPolarisConfirmation) {
            m_alignmentAutoSolveEnabled = enabled;
            m_alignmentShowMatchedCatalogStars = showMatchedCatalogStars;
            m_alignmentMaxDetectedStars = maxDetectedStars;
            m_alignmentMinMatchedStars = minMatchedStars;
            m_alignmentMaxRmsPx = maxRmsPx;
            m_alignmentRetryIntervalMs = retryIntervalMs;
            m_alignmentMinMatchedSpatialSpreadPx = minMatchedSpatialSpreadPx;
            m_alignmentMinPolarisSnr = minPolarisSnr;
            m_alignmentAllowSaturatedPolarisConfirmation = allowSaturatedPolarisConfirmation;
            if (m_captureState == CaptureState::Alignment) {
                setStatusMessage(enabled
                                     ? QStringLiteral("北极星自动识别参数已更新")
                                     : QStringLiteral("北极星自动识别已关闭"),
                                 enabled ? UiStatusLevel::Info : UiStatusLevel::Warning);
            }
        };
}

void DIMM::setupStorageSettingsCallbacks()
{
    m_settingsDialog->onApplyStorage = [this](QString path,
                                              int interval,
                                              bool parameterValidationEnabled,
                                              bool syncDiagnosticLoggingEnabled) {
        m_dataPath = path;
        m_saveInterval = qMax(1, interval);
        m_parameterValidationEnabled = parameterValidationEnabled;
        m_syncDiagnosticLoggingEnabled = syncDiagnosticLoggingEnabled;
        if (!m_parameterValidationEnabled) {
            m_detailResultWriter.close();
            m_liveRuntime.pendingPairedCentroidDetails.clear();
            m_simulationRuntime.pendingPairedCentroidDetails.clear();
        } else if (m_resultWriter.isOpen()) {
            initDetailResultFile();
        }
        if (!m_syncDiagnosticLoggingEnabled) {
            m_syncDiagnosticWriter.close();
            m_syncDiagnosticFilePath.clear();
        } else if (m_resultWriter.isOpen()) {
            initSyncDiagnosticFile();
        }
        setStatusMessage(QStringLiteral("存储参数已更新"), UiStatusLevel::Success);
    };
}

void DIMM::setupNetworkSettingsCallbacks()
{
    m_settingsDialog->onApplyNetwork = [this](QString ip, quint16 port) {
        if (!isSettingsApplyAllowed()) {
            if (m_settingsDialog && m_settingsDialog->applyStatusLabel) {
                m_settingsDialog->applyStatusLabel->setText(QStringLiteral("相机连接中，暂不允许修改网络设置"));
                m_settingsDialog->applyStatusLabel->setStyleSheet(statusLabelStyle(UiStatusLevel::Warning));
            }
            return;
        }
        m_commManager->setRemoteAddress(ip, port);
        setStatusMessage(QStringLiteral("网络参数已保存 %1:%2").arg(ip).arg(port), UiStatusLevel::Success);
        refreshStatusUi();
    };
    m_settingsDialog->onConnectNetwork = [this](QString ip, quint16 port) {
        if (!isSettingsApplyAllowed()) {
            if (m_settingsDialog && m_settingsDialog->applyStatusLabel) {
                m_settingsDialog->applyStatusLabel->setText(QStringLiteral("相机连接中，暂不允许连接上位机"));
                m_settingsDialog->applyStatusLabel->setStyleSheet(statusLabelStyle(UiStatusLevel::Warning));
            }
            return;
        }
        m_commManager->setRemoteAddress(ip, port);
        m_reporting = false;
        m_commConnecting = true;
        if (m_reportTimer) {
            m_reportTimer->stop();
        }
        m_commManager->disconnectFromHost();
        m_commManager->connectToHost(ip, port);
        setStatusMessage(QStringLiteral("正在连接上位机%1:%2").arg(ip).arg(port), UiStatusLevel::Warning);
        refreshStatusUi();
    };
    m_settingsDialog->onAfterApply = [this]() {
        if (!m_settingsDialog || !m_settingsDialog->applyStatusLabel) {
            return;
        }

        if (!isSettingsApplyAllowed()) {
            m_settingsDialog->applyStatusLabel->setText(QStringLiteral("部分设置待连接流程结束后再处理"));
            m_settingsDialog->applyStatusLabel->setStyleSheet(statusLabelStyle(UiStatusLevel::Warning));
            return;
        }

        savePersistentSettings();

        const int connectedCameras = openCameraCount();
        QString message;
        UiStatusLevel level = UiStatusLevel::Success;
        if (connectedCameras <= 0) {
            message = QStringLiteral("配置已保存，待相机连接后生效");
            level = UiStatusLevel::Warning;
        } else {
            message = QStringLiteral("配置已下发到 %1 台在线相机").arg(connectedCameras);
            level = UiStatusLevel::Success;
        }

        m_settingsDialog->applyStatusLabel->setText(message);
        m_settingsDialog->applyStatusLabel->setStyleSheet(statusLabelStyle(level));
    };

}

AppConfig DIMM::currentAppConfig() const
{
    AppConfig config;
    config.camera.exposureUs = m_configExposureUs;
    config.camera.gainDb = m_configGainDb;
    config.camera.continuousFrameRateHz = m_configContinuousFrameRateHz;
    config.autoExposure = m_autoExposureConfig;
    if (m_imageProcessor) {
        config.processing.backgroundKernelSize = m_imageProcessor->backgroundDenoiseKernelSize();
        config.processing.backgroundSigmaMultiplier = m_imageProcessor->backgroundDenoiseSigmaMultiplier();
        config.processing.centroidMode = m_imageProcessor->centroidMethod();
        config.processing.peakKernelMethod = m_imageProcessor->peakKernelMethod();
        config.processing.peakKernelRadiusPx = m_imageProcessor->peakKernelRadiusPx();
        config.processing.strongHotPixelExcessDn = m_imageProcessor->strongHotPixelExcessDn();
        config.optical.apertureDiameterMm = m_imageProcessor->apertureDiameterMm();
        config.optical.baselineSeparationMm = m_imageProcessor->baselineSeparationMm();
        config.optical.baselineAngleDeg = m_imageProcessor->baselineAngleDeg();
        config.optical.focalLengthCm = m_imageProcessor->focalLengthCm();
        config.optical.zenithAngleDeg = m_imageProcessor->zenithAngleDeg();
        config.optical.wavelengthNm = m_imageProcessor->wavelengthNm();
        config.optical.pixelSizeUm = m_imageProcessor->pixelSizeUm();
    }
    config.roiRecentering.thresholdPx = m_roiRecenteringThresholdPx;
    config.roiRecentering.requiredFrames = m_roiRecenteringRequiredFrames;
    config.roiRecentering.cooldownMs = m_roiRecenteringCooldownMs;
    config.roiRecentering.minimumShiftPx = m_roiRecenteringMinimumShiftPx;
    const InitialStarDetectionConfig starConfig = currentInitialStarDetectionConfig();
    config.starDetection.thresholdAbsolute = starConfig.thresholdAbsolute;
    config.starDetection.sigmaThreshold = starConfig.sigmaThreshold;
    config.starDetection.peakFraction = starConfig.peakFraction;
    config.starDetection.minimumIntensity = starConfig.minimumIntensity;
    config.starDetection.minArea = starConfig.minArea;
    config.starDetection.maxArea = starConfig.maxArea;
    config.hotPixel.enabled = m_hotPixelTemplatesEnabled;
    config.hotPixel.camera0MaskPath = m_hotPixelCamera0MaskPath;
    config.hotPixel.camera0ExcessPath = m_hotPixelCamera0ExcessPath;
    config.hotPixel.camera1MaskPath = m_hotPixelCamera1MaskPath;
    config.hotPixel.camera1ExcessPath = m_hotPixelCamera1ExcessPath;
    config.hotPixel.templateWidth = m_hotPixelTemplateWidth;
    config.hotPixel.templateHeight = m_hotPixelTemplateHeight;
    config.alignment.autoRadius = m_alignmentAutoRadius;
    config.alignment.focalLengthMm = m_alignmentFocalLengthMm;
    config.alignment.pixelSizeUm = m_alignmentPixelSizeUm;
    config.alignment.polarDistanceArcmin = m_alignmentPolarisPolarDistanceArcmin;
    config.alignment.radiusAdjustPx = m_alignmentRadiusAdjustPx;
    config.alignment.previewRateHz = m_alignmentPreviewRateHz;
    config.polarisSolver.enabled = m_alignmentAutoSolveEnabled;
    config.polarisSolver.showMatchedCatalogStars = m_alignmentShowMatchedCatalogStars;
    config.polarisSolver.maxDetectedStars = m_alignmentMaxDetectedStars;
    config.polarisSolver.minMatchedStars = m_alignmentMinMatchedStars;
    config.polarisSolver.maxRmsPx = m_alignmentMaxRmsPx;
    config.polarisSolver.retryIntervalMs = m_alignmentRetryIntervalMs;
    config.polarisSolver.minMatchedSpatialSpreadPx = m_alignmentMinMatchedSpatialSpreadPx;
    config.polarisSolver.minPolarisSnr = m_alignmentMinPolarisSnr;
    config.polarisSolver.allowSaturatedPolarisConfirmation =
        m_alignmentAllowSaturatedPolarisConfirmation;
    config.storage.path = m_dataPath;
    config.storage.interval = m_saveInterval;
    config.storage.parameterValidationEnabled = m_parameterValidationEnabled;
    config.storage.syncDiagnosticLoggingEnabled = m_syncDiagnosticLoggingEnabled;
    config.trigger.mode = m_configTriggerMode;
    config.environmentSensor = m_environmentSensorConfig;
    config.pulseGenerator.enabled = m_pulseGeneratorEnabled;
    config.pulseGenerator.portName = m_pulseGeneratorPort;
    config.pulseGenerator.baudRate = m_pulseGeneratorBaudRate;
    config.pulseGenerator.terminalId = m_pulseGeneratorTerminalId;
    config.pulseGenerator.frequencyHz = m_pulseGeneratorFrequencyHz;
    config.pulseGenerator.pulseCount = m_pulseGeneratorPulseCount;
    config.pulseGenerator.dutyPercent = m_pulseGeneratorDutyPercent;
    config.pulseGenerator.remoteControl = m_pulseGeneratorRemoteControl;
    config.autoAcquisition = m_autoAcquisitionConfig;
    if (m_commManager) {
        config.network.ip = m_commManager->remoteAddress();
        config.network.port = m_commManager->remotePort();
    }
    return config;
}

void DIMM::applyStartupConfig(const AppConfig& config)
{
    m_configExposureUs = config.camera.exposureUs;
    m_cameraExposureUs[0] = config.camera.exposureUs;
    m_cameraExposureUs[1] = config.camera.exposureUs;
    m_configGainDb = config.camera.gainDb;
    m_configContinuousFrameRateHz = config.camera.continuousFrameRateHz;
    m_configTriggerMode = config.trigger.mode;
    m_autoAcquisitionConfig = config.autoAcquisition;

    m_autoExposureConfig = config.autoExposure;
    resetAutoExposureState();

    m_roiRecenteringThresholdPx = config.roiRecentering.thresholdPx;
    m_roiRecenteringRequiredFrames = config.roiRecentering.requiredFrames;
    m_roiRecenteringCooldownMs = config.roiRecentering.cooldownMs;
    m_roiRecenteringMinimumShiftPx = config.roiRecentering.minimumShiftPx;

    InitialStarDetectionConfig starConfig;
    starConfig.thresholdAbsolute = config.starDetection.thresholdAbsolute;
    starConfig.sigmaThreshold = config.starDetection.sigmaThreshold;
    starConfig.peakFraction = config.starDetection.peakFraction;
    starConfig.minimumIntensity = config.starDetection.minimumIntensity;
    starConfig.minArea = config.starDetection.minArea;
    starConfig.maxArea = config.starDetection.maxArea;
    setCurrentInitialStarDetectionConfig(starConfig);

    m_hotPixelTemplatesEnabled = config.hotPixel.enabled;
    m_hotPixelCamera0MaskPath = PathUtils::relativizePathToAppDir(config.hotPixel.camera0MaskPath);
    m_hotPixelCamera0ExcessPath = PathUtils::relativizePathToAppDir(config.hotPixel.camera0ExcessPath);
    m_hotPixelCamera1MaskPath = PathUtils::relativizePathToAppDir(config.hotPixel.camera1MaskPath);
    m_hotPixelCamera1ExcessPath = PathUtils::relativizePathToAppDir(config.hotPixel.camera1ExcessPath);
    m_hotPixelTemplateWidth = m_hotPixelTemplatesEnabled ? config.hotPixel.templateWidth : 0;
    m_hotPixelTemplateHeight = m_hotPixelTemplatesEnabled ? config.hotPixel.templateHeight : 0;
    m_hotPixelTemplateExposureUs[0] =
        m_hotPixelTemplatesEnabled ? PathUtils::exposureUsFromTemplatePath(m_hotPixelCamera0MaskPath) : 0;
    m_hotPixelTemplateExposureUs[1] =
        m_hotPixelTemplatesEnabled ? PathUtils::exposureUsFromTemplatePath(m_hotPixelCamera1MaskPath) : 0;
    m_cachedHotPixelTemplateExposures.clear();
    m_cachedHotPixelTemplateScanMs = -1;

    m_alignmentAutoRadius = config.alignment.autoRadius;
    m_alignmentFocalLengthMm = config.alignment.focalLengthMm;
    m_alignmentPixelSizeUm = config.alignment.pixelSizeUm;
    m_alignmentPolarisPolarDistanceArcmin = config.alignment.polarDistanceArcmin;
    m_alignmentRadiusAdjustPx = config.alignment.radiusAdjustPx;
    m_alignmentPreviewRateHz = config.alignment.previewRateHz;
    m_alignmentAutoSolveEnabled = config.polarisSolver.enabled;
    m_alignmentShowMatchedCatalogStars = config.polarisSolver.showMatchedCatalogStars;
    m_alignmentMaxDetectedStars = config.polarisSolver.maxDetectedStars;
    m_alignmentMinMatchedStars = config.polarisSolver.minMatchedStars;
    m_alignmentMaxRmsPx = config.polarisSolver.maxRmsPx;
    m_alignmentRetryIntervalMs = config.polarisSolver.retryIntervalMs;
    m_alignmentMinMatchedSpatialSpreadPx = config.polarisSolver.minMatchedSpatialSpreadPx;
    m_alignmentMinPolarisSnr = config.polarisSolver.minPolarisSnr;
    m_alignmentAllowSaturatedPolarisConfirmation =
        config.polarisSolver.allowSaturatedPolarisConfirmation;

    m_dataPath = config.storage.path;
    m_saveInterval = qMax(1, config.storage.interval);
    m_parameterValidationEnabled = config.storage.parameterValidationEnabled;
    m_syncDiagnosticLoggingEnabled = config.storage.syncDiagnosticLoggingEnabled;

    m_environmentSensorConfig = config.environmentSensor;
    m_pulseGeneratorEnabled = config.pulseGenerator.enabled;
    m_pulseGeneratorPort = config.pulseGenerator.portName;
    m_pulseGeneratorBaudRate = config.pulseGenerator.baudRate;
    m_pulseGeneratorTerminalId = config.pulseGenerator.terminalId;
    m_pulseGeneratorFrequencyHz = config.pulseGenerator.frequencyHz;
    m_pulseGeneratorPulseCount = config.pulseGenerator.pulseCount;
    m_pulseGeneratorDutyPercent = config.pulseGenerator.dutyPercent;
    m_pulseGeneratorRemoteControl = config.pulseGenerator.remoteControl;

    if (m_imageProcessor) {
        m_imageProcessor->setBackgroundDenoiseKernelSize(config.processing.backgroundKernelSize);
        m_imageProcessor->setBackgroundDenoiseSigmaMultiplier(
            config.processing.backgroundSigmaMultiplier);
        m_imageProcessor->setCentroidMethod(config.processing.centroidMode);
        m_imageProcessor->setPeakKernelCentroidConfig(config.processing.peakKernelMethod,
                                                      config.processing.peakKernelRadiusPx,
                                                      config.processing.strongHotPixelExcessDn);
        m_imageProcessor->setOpticalParams(config.optical.apertureDiameterMm,
                                           config.optical.baselineSeparationMm,
                                           config.optical.baselineAngleDeg,
                                           config.optical.focalLengthCm,
                                           config.optical.zenithAngleDeg,
                                           config.optical.wavelengthNm,
                                           config.optical.pixelSizeUm);
        m_imageProcessor->setTargetFrameRateHz(m_pulseGeneratorFrequencyHz);
        m_imageProcessor->setAutoExposureMetricConfig(m_autoExposureConfig.enabled,
                                                      m_autoExposureConfig.hardSaturationDn,
                                                      m_autoExposureConfig.autoExposureSampleIntervalMs,
                                                      m_autoExposureConfig.peakSupportRadiusPx,
                                                      m_autoExposureConfig.peakSupportFraction,
                                                      m_autoExposureConfig.minPeakSupportPixelCount,
                                                      m_autoExposureConfig.minNeighborPeakRatio,
                                                      m_autoExposureConfig.maxPeakCandidateCount,
                                                      m_autoExposureConfig.supportedPeakPercentile,
                                                      m_autoExposureConfig.saturatedPixelCount);
        refreshHotPixelTemplates();
    }

    if (m_environmentSensor) {
        m_latestEnvironment = EnvironmentSensorData();
        if (m_environmentSensorConfig.enabled) {
            m_environmentSensor->start(m_environmentSensorConfig);
        } else {
            m_environmentSensor->stop();
        }
    }
    if (m_commManager) {
        m_commManager->setRemoteAddress(config.network.ip, config.network.port);
    }
    if (m_settingsDialog) {
        if (m_settingsDialog->autoAcquisitionEnableCheck) {
            m_settingsDialog->autoAcquisitionEnableCheck->setChecked(m_autoAcquisitionConfig.enabled);
        }
        if (m_settingsDialog->autoAcquisitionLatitudeEdit) {
            m_settingsDialog->autoAcquisitionLatitudeEdit->setText(
                QString::number(m_autoAcquisitionConfig.latitudeDeg, 'f', 6));
        }
        if (m_settingsDialog->autoAcquisitionLongitudeEdit) {
            m_settingsDialog->autoAcquisitionLongitudeEdit->setText(
                QString::number(m_autoAcquisitionConfig.longitudeDeg, 'f', 6));
        }
        if (m_settingsDialog->autoAcquisitionStartOffsetEdit) {
            m_settingsDialog->autoAcquisitionStartOffsetEdit->setText(
                QString::number(m_autoAcquisitionConfig.startOffsetMinutesAfterSunset));
        }
        if (m_settingsDialog->autoAcquisitionStopOffsetEdit) {
            m_settingsDialog->autoAcquisitionStopOffsetEdit->setText(
                QString::number(m_autoAcquisitionConfig.stopOffsetMinutesBeforeSunrise));
        }
        if (m_settingsDialog->autoAcquisitionTestOverrideCheck) {
            m_settingsDialog->autoAcquisitionTestOverrideCheck->setChecked(
                m_autoAcquisitionConfig.testTimeOverrideEnabled);
        }
        if (m_settingsDialog->autoAcquisitionTestStartEdit) {
            m_settingsDialog->autoAcquisitionTestStartEdit->setText(
                m_autoAcquisitionConfig.testStartTime.toString(QStringLiteral("HH:mm")));
        }
        if (m_settingsDialog->autoAcquisitionTestStopEdit) {
            m_settingsDialog->autoAcquisitionTestStopEdit->setText(
                m_autoAcquisitionConfig.testStopTime.toString(QStringLiteral("HH:mm")));
        }
        const AutoAcquisitionWindow window =
            AutoAcquisitionScheduler::resolveWindow(m_autoAcquisitionConfig, QDateTime::currentDateTime());
        if (m_settingsDialog->autoAcquisitionNextStartLabel) {
            m_settingsDialog->autoAcquisitionNextStartLabel->setText(
                window.valid
                    ? QStringLiteral("下次开始: %1").arg(window.start.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
                    : QStringLiteral("下次开始: %1").arg(window.errorMessage));
        }
        if (m_settingsDialog->autoAcquisitionNextStopLabel) {
            m_settingsDialog->autoAcquisitionNextStopLabel->setText(
                window.valid
                    ? QStringLiteral("下次停止: %1").arg(window.stop.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
                    : QStringLiteral("下次停止: %1").arg(window.errorMessage));
        }
    }
}

void DIMM::savePersistentSettings()
{
    AppConfigPersistence::save(currentAppConfig());
}

QVector<int> DIMM::scanHotPixelExposureTemplates() const
{
    QVector<int> cam0 = scanHotPixelExposureTemplatesForCamera(0);
    QVector<int> common;
    for (int exposureUs : cam0) {
        QString maskPath;
        QString excessPath;
        if (resolveHotPixelTemplatePathsForCameraExposure(1, exposureUs, &maskPath, &excessPath)) {
            common.push_back(exposureUs);
        }
    }
    return common;
}

QVector<int> DIMM::scanHotPixelExposureTemplatesForCamera(int cameraIndex) const
{
    QVector<int> exposures;
    if (cameraIndex < 0 || cameraIndex >= 2 || !m_hotPixelTemplatesEnabled) {
        return exposures;
    }

    const QString maskPath = cameraIndex == 0 ? m_hotPixelCamera0MaskPath : m_hotPixelCamera1MaskPath;
    if (maskPath.isEmpty()) {
        return exposures;
    }

    QDir exposureDir = QFileInfo(PathUtils::resolvePathFromAppDir(maskPath)).absoluteDir();
    if (!exposureDir.cdUp()) {
        return exposures;
    }

    const QFileInfoList entries =
        exposureDir.entryInfoList(QStringList() << QStringLiteral("exposure_*us"),
                                  QDir::Dirs | QDir::NoDotAndDotDot,
                                  QDir::Name);
    for (const QFileInfo& entry : entries) {
        const int exposureUs = PathUtils::exposureUsFromTemplateDirName(entry.fileName());
        if (exposureUs <= 0 || exposures.contains(exposureUs)) {
            continue;
        }
        QString resolvedMask;
        QString resolvedExcess;
        if (resolveHotPixelTemplatePathsForCameraExposure(cameraIndex,
                                                          exposureUs,
                                                          &resolvedMask,
                                                          &resolvedExcess)) {
            exposures.push_back(exposureUs);
        }
    }
    std::sort(exposures.begin(), exposures.end());
    return exposures;
}

int DIMM::selectHotPixelTemplateExposureForCurrentExposure(double currentExposure) const
{
    return selectHotPixelTemplateExposureForCameraExposure(0, currentExposure);
}

int DIMM::selectHotPixelTemplateExposureForCameraExposure(int cameraIndex, double currentExposure) const
{
    QVector<int> exposures = scanHotPixelExposureTemplatesForCamera(cameraIndex);
    if (exposures.isEmpty()) {
        return 0;
    }

    const int currentUs = static_cast<int>(std::lround(std::max(1.0, currentExposure)));
    auto upper = std::lower_bound(exposures.begin(), exposures.end(), currentUs);
    if (upper == exposures.begin()) {
        return *upper;
    }
    if (upper == exposures.end()) {
        return exposures.back();
    }
    const int upperValue = *upper;
    const int lowerValue = *(upper - 1);
    return std::abs(upperValue - currentUs) < std::abs(currentUs - lowerValue)
               ? upperValue
               : lowerValue;
}

bool DIMM::resolveHotPixelTemplatePathsForExposure(int exposureUs,
                                                   QString* camera0Mask,
                                                   QString* camera0Excess,
                                                   QString* camera1Mask,
                                                   QString* camera1Excess) const
{
    if (exposureUs <= 0) {
        return false;
    }

    const QString cam0Mask = PathUtils::replaceTemplateExposurePath(m_hotPixelCamera0MaskPath, exposureUs);
    const QString cam0Excess = PathUtils::replaceTemplateExposurePath(m_hotPixelCamera0ExcessPath, exposureUs);
    const QString cam1Mask = PathUtils::replaceTemplateExposurePath(m_hotPixelCamera1MaskPath, exposureUs);
    const QString cam1Excess = PathUtils::replaceTemplateExposurePath(m_hotPixelCamera1ExcessPath, exposureUs);
    if (cam0Mask.isEmpty() || cam0Excess.isEmpty() || cam1Mask.isEmpty() || cam1Excess.isEmpty()) {
        return false;
    }
    if (!QFileInfo::exists(PathUtils::resolvePathFromAppDir(cam0Mask)) ||
        !QFileInfo::exists(PathUtils::resolvePathFromAppDir(cam0Excess)) ||
        !QFileInfo::exists(PathUtils::resolvePathFromAppDir(cam1Mask)) ||
        !QFileInfo::exists(PathUtils::resolvePathFromAppDir(cam1Excess))) {
        return false;
    }

    if (camera0Mask) {
        *camera0Mask = cam0Mask;
    }
    if (camera0Excess) {
        *camera0Excess = cam0Excess;
    }
    if (camera1Mask) {
        *camera1Mask = cam1Mask;
    }
    if (camera1Excess) {
        *camera1Excess = cam1Excess;
    }
    return true;
}

bool DIMM::resolveHotPixelTemplatePathsForCameraExposure(int cameraIndex,
                                                         int exposureUs,
                                                         QString* maskPath,
                                                         QString* excessPath) const
{
    if (cameraIndex < 0 || cameraIndex >= 2 || exposureUs <= 0) {
        return false;
    }
    const QString baseMask = cameraIndex == 0 ? m_hotPixelCamera0MaskPath : m_hotPixelCamera1MaskPath;
    const QString baseExcess = cameraIndex == 0 ? m_hotPixelCamera0ExcessPath : m_hotPixelCamera1ExcessPath;
    const QString resolvedMask = PathUtils::replaceTemplateExposurePath(baseMask, exposureUs);
    const QString resolvedExcess = PathUtils::replaceTemplateExposurePath(baseExcess, exposureUs);
    if (resolvedMask.isEmpty() || resolvedExcess.isEmpty()) {
        return false;
    }
    if (!QFileInfo::exists(PathUtils::resolvePathFromAppDir(resolvedMask)) ||
        !QFileInfo::exists(PathUtils::resolvePathFromAppDir(resolvedExcess))) {
        return false;
    }
    if (maskPath) {
        *maskPath = resolvedMask;
    }
    if (excessPath) {
        *excessPath = resolvedExcess;
    }
    return true;
}

bool DIMM::applyExposureAndHotPixelTemplate(int exposureUs, QString* reason)
{
    for (int i = 0; i < 2; ++i) {
        if (!applyExposureAndHotPixelTemplate(i, exposureUs, reason)) {
            return false;
        }
    }
    m_configExposureUs = exposureUs;
    return true;
}

bool DIMM::applyExposureAndHotPixelTemplate(int cameraIndex, int exposureUs, QString* reason)
{
    if (cameraIndex < 0 || cameraIndex >= 2 || exposureUs <= 0) {
        if (reason) {
            *reason = QStringLiteral("自动曝光: 无效相机或曝光参数。");
        }
        return false;
    }

    const int templateExposureUs =
        selectHotPixelTemplateExposureForCameraExposure(cameraIndex, exposureUs);
    QString maskPath;
    QString excessPath;
    if (m_hotPixelTemplatesEnabled &&
        (templateExposureUs <= 0 ||
         !resolveHotPixelTemplatePathsForCameraExposure(cameraIndex,
                                                        templateExposureUs,
                                                        &maskPath,
                                                        &excessPath))) {
        if (reason) {
            *reason = QStringLiteral("自动曝光: 相机%1缺少接近 %2 μs 的热像素模板，保持当前曝光。")
                          .arg(cameraIndex + 1)
                          .arg(exposureUs);
        }
        return false;
    }

    if (m_cameraManager->isOpen(cameraIndex) &&
        !m_cameraManager->setExposure(cameraIndex, exposureUs)) {
        if (reason) {
            *reason = QStringLiteral("自动曝光: 相机%1设置 %2 μs 曝光失败。")
                          .arg(cameraIndex + 1)
                          .arg(exposureUs);
        }
        return false;
    }

    m_cameraExposureUs[cameraIndex] = exposureUs;
    m_configExposureUs = (m_cameraExposureUs[0] + m_cameraExposureUs[1]) * 0.5;
    if (m_hotPixelTemplatesEnabled) {
        if (cameraIndex == 0) {
            m_hotPixelCamera0MaskPath = PathUtils::relativizePathToAppDir(maskPath);
            m_hotPixelCamera0ExcessPath = PathUtils::relativizePathToAppDir(excessPath);
        } else {
            m_hotPixelCamera1MaskPath = PathUtils::relativizePathToAppDir(maskPath);
            m_hotPixelCamera1ExcessPath = PathUtils::relativizePathToAppDir(excessPath);
        }
        m_hotPixelTemplateExposureUs[cameraIndex] = templateExposureUs;
    }
    refreshHotPixelTemplates();
    if (m_settingsDialog) {
        m_settingsDialog->exposureEdit->setText(QString::number(m_configExposureUs, 'f', 0));
    }
    return true;
}

void DIMM::refreshHotPixelTemplates()
{
    if (m_imageProcessor) {
        m_imageProcessor->configureHotPixelTemplates(PathUtils::resolvePathFromAppDir(m_hotPixelCamera0MaskPath),
                                                     PathUtils::resolvePathFromAppDir(m_hotPixelCamera0ExcessPath),
                                                     PathUtils::resolvePathFromAppDir(m_hotPixelCamera1MaskPath),
                                                     PathUtils::resolvePathFromAppDir(m_hotPixelCamera1ExcessPath),
                                                     m_hotPixelTemplateWidth,
                                                     m_hotPixelTemplateHeight);
    }
    if (m_settingsDialog) {
        m_settingsDialog->hotPixelCam0MaskEdit->setText(m_hotPixelCamera0MaskPath);
        m_settingsDialog->hotPixelCam0ExcessEdit->setText(m_hotPixelCamera0ExcessPath);
        m_settingsDialog->hotPixelCam1MaskEdit->setText(m_hotPixelCamera1MaskPath);
        m_settingsDialog->hotPixelCam1ExcessEdit->setText(m_hotPixelCamera1ExcessPath);
    }
}
