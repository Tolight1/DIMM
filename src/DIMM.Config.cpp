#include "DIMM.h"

#include "AcquisitionImagePolicy.h"
#include "AppConfigPersistence.h"
#include "AutoAcquisitionScheduler.h"
#include "CameraManager.h"
#include "CommManager.h"
#include "ConfigApplicationController.h"
#include "DimmRuntimeHelpers.h"
#include "ExposureFrameRateRules.h"
#include "FrameRateChangePolicy.h"
#include "ExposureFrequencySwitchController.h"
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
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QStringList>
#include <QVector>

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
        if (!std::isfinite(exposure) || exposure <= 0.0 ||
            !std::isfinite(gain) ||
            !std::isfinite(continuousFrameRateHz) || continuousFrameRateHz <= 0.0) {
            setStatusMessage(QStringLiteral("相机参数无效，未应用任何设置"), UiStatusLevel::Warning);
            return;
        }

        const double oldExposureUs[2] = {
            m_cameraExposureUs[0],
            m_cameraExposureUs[1],
        };
        const double oldGainDb = m_configGainDb;
        const double oldConfigExposureUs = m_configExposureUs;
        const double oldConfiguredFrameRateHz = m_configContinuousFrameRateHz;
        const bool tracking =
            m_captureState == CaptureState::Live &&
            m_liveStartupPhase == LiveStartupPhase::Tracking;

        const auto restoreDirectCameraParameters = [&]() {
            for (int i = 0; i < 2; ++i) {
                if (!m_cameraManager->isOpen(i)) {
                    continue;
                }
                m_cameraManager->setExposure(i, oldExposureUs[i]);
                m_cameraManager->setGain(i, oldGainDb);
            }
        };

        QString reason;
        if (tracking) {
            const double targetExposureUs[2] = {exposure, exposure};
            if (!applyTrackingExposureAndFrameRate(targetExposureUs, &reason)) {
                setStatusMessage(reason.isEmpty()
                                     ? QStringLiteral("Tracking 相机曝光/帧率应用失败")
                                     : reason,
                                 UiStatusLevel::Warning);
                return;
            }

            for (int i = 0; i < 2; ++i) {
                if (!m_cameraManager->isOpen(i) ||
                    m_cameraManager->setGain(i, gain)) {
                    continue;
                }

                QString rollbackReason;
                const bool exposureRolledBack =
                    applyTrackingExposureAndFrameRate(oldExposureUs, &rollbackReason);
                restoreDirectCameraParameters();
                m_configExposureUs = oldConfigExposureUs;
                m_configContinuousFrameRateHz = oldConfiguredFrameRateHz;
                setStatusMessage(
                    exposureRolledBack
                        ? QStringLiteral("相机%1增益设置失败，已回滚 Tracking 曝光").arg(i + 1)
                        : QStringLiteral("相机%1增益设置失败，Tracking 曝光回滚失败: %2")
                              .arg(i + 1)
                              .arg(rollbackReason),
                    UiStatusLevel::Error);
                return;
            }
        } else {
            bool applied = true;
            for (int i = 0; i < 2; ++i) {
                if (!m_cameraManager->isOpen(i)) {
                    continue;
                }
                if (!m_cameraManager->setExposure(i, exposure) ||
                    !m_cameraManager->setGain(i, gain)) {
                    applied = false;
                    reason = QStringLiteral("相机%1曝光或增益设置失败").arg(i + 1);
                    break;
                }
            }
            if (!applied) {
                restoreDirectCameraParameters();
                setStatusMessage(reason, UiStatusLevel::Warning);
                return;
            }

            const bool hasUnconfiguredOpenCamera = [&]() {
                for (int i = 0; i < 2; ++i) {
                    if (m_cameraManager->isOpen(i) &&
                        (!std::isfinite(m_lastContinuousFrameRateReadback[i]) ||
                         m_lastContinuousFrameRateReadback[i] <= 0.0)) {
                        return true;
                    }
                }
                return false;
            }();
            const bool rateChangeRequired =
                m_configTriggerMode == 0 &&
                (hasUnconfiguredOpenCamera ||
                 needsFrameRateChange(m_activeContinuousFrameRateHz,
                                      continuousFrameRateHz));
            if (rateChangeRequired &&
                !applyContinuousCameraFrameRate(continuousFrameRateHz, &reason)) {
                restoreDirectCameraParameters();
                setStatusMessage(reason.isEmpty()
                                     ? QStringLiteral("连续采集帧率应用失败，已恢复相机参数")
                                     : reason,
                                 UiStatusLevel::Warning);
                return;
            }
        }

        m_configExposureUs = exposure;
        m_cameraExposureUs[0] = exposure;
        m_cameraExposureUs[1] = exposure;
        m_configGainDb = gain;
        m_configContinuousFrameRateHz = continuousFrameRateHz;
        setStatusMessage(QStringLiteral("相机参数已应用"), UiStatusLevel::Success);
    };
}

void DIMM::setupAutoExposureSettingsCallbacks()
{
    m_settingsDialog->onApplyAutoExposure =
        [this](const AutoExposureConfig& config) {
            m_autoExposureConfig = config;
            resetAutoExposureState(false);
            m_trackingImageIntervalMs = AcquisitionImagePolicy::trackingIntervalMsForExposureUs(
                m_cameraExposureUs[0],
                m_cameraExposureUs[1],
                currentTrackingFrameRateHz(),
                m_autoExposureConfig.autoExposureSampleIntervalMs);
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
            setStatusMessage(config.enabled ? QStringLiteral("自动曝光已启用，状态机保护模式")
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
        m_autoFocusSensorHadValidData = false;
        m_autoFocusSensorOutageNotified = false;
        m_autoFocusSensorAlertNotBeforeMs = QDateTime::currentMSecsSinceEpoch() +
                                             kAutoFocusSensorStartupGraceMs;
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
            m_imageProcessor->setTargetFrameRateHz(currentTrackingFrameRateHz());
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

        m_activeTriggerFrequencyHz = frequencyHz;

        setStatusMessage(enabled
                             ? QStringLiteral("触发设置已下发到脉冲板 %1 @ %2 Hz")
                                   .arg(portName)
                                   .arg(frequencyHz, 0, 'f', 1)
                             : QStringLiteral("脉冲板输出已关闭并同步"),
                         enabled ? UiStatusLevel::Success : UiStatusLevel::Warning);
        return true;
        };
    m_settingsDialog->onSetPulseControlSource =
        [this](QString portName,
               int baudRate,
               int terminalId,
               bool remoteControl,
               QString* errorMessage) -> bool {
        m_pulseGeneratorPort = portName;
        m_pulseGeneratorBaudRate = baudRate;
        m_pulseGeneratorTerminalId = terminalId;

        if (!m_pulseGenerator) {
            m_pulseGeneratorRemoteControl = remoteControl;
            return true;
        }

        if (m_configTriggerMode == 0) {
            m_pulseGeneratorRemoteControl = remoteControl;
            const QString savedMessage = QStringLiteral("当前为连续采集模式，触发器控制源已保存，切换到硬件触发时再下发");
            setStatusMessage(savedMessage, UiStatusLevel::Info);
            if (errorMessage) {
                *errorMessage = savedMessage;
            }
            return true;
        }

        if (isLiveCaptureActive()) {
            const QString pendingMessage = QStringLiteral("实时采集中，触发器控制源已保存，将在停止采集后再下发");
            setStatusMessage(pendingMessage, UiStatusLevel::Warning);
            if (errorMessage) {
                *errorMessage = pendingMessage;
            }
            m_pulseGeneratorRemoteControl = remoteControl;
            return true;
        }

        PulseGeneratorManager::Config pulseConfig;
        pulseConfig.enabled = m_pulseGeneratorEnabled;
        pulseConfig.portName = portName;
        pulseConfig.baudRate = baudRate;
        pulseConfig.terminalId = terminalId;
        pulseConfig.frequencyHz = m_pulseGeneratorFrequencyHz;
        pulseConfig.pulseCount = m_pulseGeneratorPulseCount;
        pulseConfig.dutyPercent = m_pulseGeneratorDutyPercent;
        pulseConfig.remoteControl = m_pulseGeneratorRemoteControl;
        if (!m_pulseGenerator->setControlSource(pulseConfig, remoteControl, errorMessage)) {
            setStatusMessage(errorMessage && !errorMessage->isEmpty()
                                 ? *errorMessage
                                 : QStringLiteral("触发器控制源切换失败"),
                             UiStatusLevel::Error);
            return false;
        }

        m_pulseGeneratorRemoteControl = remoteControl;
        setStatusMessage(remoteControl
                             ? QStringLiteral("触发器控制源已切换为远程")
                             : QStringLiteral("触发器控制源已切换为本地"),
                         UiStatusLevel::Success);
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
            m_imageProcessor->setTargetFrameRateHz(currentTrackingFrameRateHz());
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

        if (m_captureState != CaptureState::Live) {
            m_activeTriggerFrequencyHz = frequencyHz;
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
            m_autoAcquisitionRecovery.reset();
            m_lastAutoAcquisitionAttemptMs = -1;
            m_lastAutoAcquisitionStatusKey.clear();
            m_lastAutoAcquisitionStatusMs = -1;

            if (!m_autoAcquisitionStartedCurrentRun) {
                m_autoAcquisitionActiveWindowId.clear();
            }

            resetLiveStartupRecoveryState(true);
        }
        if (!config.enabled) {
            m_autoAcquisitionRecovery.reset();
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
    m_settingsDialog->onApplyProcessing = [this](int backgroundThresholdClipIterations,
                                                 double backgroundThresholdClipSigma,
                                                 double backgroundThresholdSigmaMultiplier,
                                                 int centroidMode,
                                                 int peakKernelRadiusPx,
                                                 double strongHotPixelExcessDn,
                                                 int r0HistoryWindowFrames) {
        m_imageProcessor->setBackgroundNoiseThresholdConfig(backgroundThresholdClipIterations,
                                                             backgroundThresholdClipSigma,
                                                             backgroundThresholdSigmaMultiplier);
        m_imageProcessor->setCentroidMode(centroidMode);
        m_imageProcessor->setPeakKernelCentroidConfig(peakKernelRadiusPx,
                                                      strongHotPixelExcessDn);
        m_imageProcessor->setAtmosphereHistoryWindowFrames(r0HistoryWindowFrames);
        setStatusMessage(QStringLiteral("图像处理参数已更新"), UiStatusLevel::Success);
    };
    m_settingsDialog->onApplyPsdAnalysis = [this](const CdimPsdAnalysisConfig& config) {
        m_imageProcessor->setPsdAnalysisConfig(config);
        setStatusMessage(QStringLiteral("PSD 方差去噪参数已更新"), UiStatusLevel::Success);
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
        [this](double sigmaThreshold,
               double peakFraction,
               int minArea,
               int maxArea,
               int connectivity) {
            InitialStarDetectionConfig config;
            config.sigmaThreshold = sigmaThreshold;
            config.peakFraction = peakFraction;
            config.minArea = minArea;
            config.maxArea = maxArea;
            config.connectivity = connectivity;
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
               double pixelSizeUm,
               double outerScaleM) {
            m_imageProcessor->setOpticalParams(apertureDiameterMm,
                                               baselineSeparationMm,
                                               baselineAngleDeg,
                                               focalLengthCm,
                                               zenithAngleDeg,
                                               lambdaNm,
                                               pixelSizeUm,
                                               outerScaleM);
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
        setStatusMessage(QStringLiteral("正在连接上位机 %1:%2").arg(ip).arg(port), UiStatusLevel::Warning);
        refreshStatusUi();
    };
    m_settingsDialog->onAfterApply = [this](const AppConfig& config, const ConfigChangeSet& changes) {
        if (!m_settingsDialog || !m_settingsDialog->applyStatusLabel) {
            return;
        }

        if (!isSettingsApplyAllowed()) {
            m_settingsDialog->applyStatusLabel->setText(QStringLiteral("部分设置待连接流程结束后再处理"));
            m_settingsDialog->applyStatusLabel->setStyleSheet(statusLabelStyle(UiStatusLevel::Warning));
            return;
        }

        savePersistentSettings(config, changes);

        const bool snapshotSaved =
            !changes.any() || !m_resultSessionActive || !m_resultWriter.isOpen() ||
            writeChangedResultSettingsSnapshot(config, changes);

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
        if (!snapshotSaved) {
            message += QStringLiteral("；设置快照保存失败，详情已写入采集 CSV");
            level = UiStatusLevel::Warning;
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
        config.processing.backgroundThresholdClipIterations =
            m_imageProcessor->backgroundThresholdClipIterations();
        config.processing.backgroundThresholdClipSigma = m_imageProcessor->backgroundThresholdClipSigma();
        config.processing.backgroundThresholdSigmaMultiplier =
            m_imageProcessor->backgroundThresholdSigmaMultiplier();
        config.processing.centroidMode = m_imageProcessor->centroidMethod();
        config.processing.peakKernelRadiusPx = m_imageProcessor->peakKernelRadiusPx();
        config.processing.strongHotPixelExcessDn = m_imageProcessor->strongHotPixelExcessDn();
        config.processing.r0HistoryWindowFrames =
            m_imageProcessor->atmosphereHistoryWindowFrames();
        config.processing.psdAnalysis = m_imageProcessor->psdAnalysisConfig();
        config.optical.apertureDiameterMm = m_imageProcessor->apertureDiameterMm();
        config.optical.baselineSeparationMm = m_imageProcessor->baselineSeparationMm();
        config.optical.baselineAngleDeg = m_imageProcessor->baselineAngleDeg();
        config.optical.focalLengthCm = m_imageProcessor->focalLengthCm();
        config.optical.zenithAngleDeg = m_imageProcessor->zenithAngleDeg();
        config.optical.wavelengthNm = m_imageProcessor->wavelengthNm();
        config.optical.pixelSizeUm = m_imageProcessor->pixelSizeUm();
        config.optical.outerScaleM = m_imageProcessor->outerScaleMeters();
    }
    config.roiRecentering.thresholdPx = m_roiRecenteringThresholdPx;
    config.roiRecentering.requiredFrames = m_roiRecenteringRequiredFrames;
    config.roiRecentering.cooldownMs = m_roiRecenteringCooldownMs;
    config.roiRecentering.minimumShiftPx = m_roiRecenteringMinimumShiftPx;
    const InitialStarDetectionConfig starConfig = currentInitialStarDetectionConfig();
    config.starDetection.sigmaThreshold = starConfig.sigmaThreshold;
    config.starDetection.peakFraction = starConfig.peakFraction;
    config.starDetection.minArea = starConfig.minArea;
    config.starDetection.maxArea = starConfig.maxArea;
    config.starDetection.connectivity = starConfig.connectivity;
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
    m_activeContinuousFrameRateHz = m_configContinuousFrameRateHz;
    m_configTriggerMode = config.trigger.mode;
    m_autoAcquisitionConfig = config.autoAcquisition;

    m_autoExposureConfig = config.autoExposure;
    resetAutoExposureState(false);

    m_roiRecenteringThresholdPx = config.roiRecentering.thresholdPx;
    m_roiRecenteringRequiredFrames = config.roiRecentering.requiredFrames;
    m_roiRecenteringCooldownMs = config.roiRecentering.cooldownMs;
    m_roiRecenteringMinimumShiftPx = config.roiRecentering.minimumShiftPx;

    InitialStarDetectionConfig starConfig;
    starConfig.sigmaThreshold = config.starDetection.sigmaThreshold;
    starConfig.peakFraction = config.starDetection.peakFraction;
    starConfig.minArea = config.starDetection.minArea;
    starConfig.maxArea = config.starDetection.maxArea;
    starConfig.connectivity = config.starDetection.connectivity;
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
    m_activeTriggerFrequencyHz = m_pulseGeneratorFrequencyHz;
    m_pulseGeneratorPulseCount = config.pulseGenerator.pulseCount;
    m_pulseGeneratorDutyPercent = config.pulseGenerator.dutyPercent;
    m_pulseGeneratorRemoteControl = config.pulseGenerator.remoteControl;

    if (m_configTriggerMode != 0 && m_pulseGeneratorEnabled) {
        QString pulseReason;
        if (!startHardwarePulseStage(m_pulseGeneratorFrequencyHz,
                                     QStringLiteral("启动默认"),
                                     &pulseReason)) {
            setStatusMessage(
                pulseReason.isEmpty()
                    ? QStringLiteral("启动默认触发输出失败")
                    : QStringLiteral("启动默认触发输出失败: %1").arg(pulseReason),
                UiStatusLevel::Error);
        }
    }

    if (m_imageProcessor) {
        m_imageProcessor->setBackgroundNoiseThresholdConfig(
            config.processing.backgroundThresholdClipIterations,
            config.processing.backgroundThresholdClipSigma,
            config.processing.backgroundThresholdSigmaMultiplier);
        m_imageProcessor->setCentroidMethod(config.processing.centroidMode);
        m_imageProcessor->setPeakKernelCentroidConfig(config.processing.peakKernelRadiusPx,
                                                      config.processing.strongHotPixelExcessDn);
        m_imageProcessor->setAtmosphereHistoryWindowFrames(
            config.processing.r0HistoryWindowFrames);
        m_imageProcessor->setPsdAnalysisConfig(config.processing.psdAnalysis);
        m_imageProcessor->setOpticalParams(config.optical.apertureDiameterMm,
                                           config.optical.baselineSeparationMm,
                                           config.optical.baselineAngleDeg,
                                           config.optical.focalLengthCm,
                                           config.optical.zenithAngleDeg,
                                           config.optical.wavelengthNm,
                                           config.optical.pixelSizeUm,
                                           config.optical.outerScaleM);
        m_imageProcessor->setTargetFrameRateHz(currentTrackingFrameRateHz());
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
        m_autoFocusSensorHadValidData = false;
        m_autoFocusSensorOutageNotified = false;
        m_autoFocusSensorAlertNotBeforeMs = QDateTime::currentMSecsSinceEpoch() +
                                             kAutoFocusSensorStartupGraceMs;
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
        if (m_settingsDialog->autoAcquisitionRecoveryScanIntervalEdit) {
            m_settingsDialog->autoAcquisitionRecoveryScanIntervalEdit->setText(
                QString::number(m_autoAcquisitionConfig.recoveryScanIntervalMinutes));
        }
        if (m_settingsDialog->autoAcquisitionAttemptDurationEdit) {
            m_settingsDialog->autoAcquisitionAttemptDurationEdit->setText(
                QString::number(m_autoAcquisitionConfig.starFindingAttemptDurationSec));
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
        m_settingsDialog->setCommittedConfig(currentAppConfig());
    }
}

void DIMM::savePersistentSettings(const AppConfig& config, const ConfigChangeSet& changes)
{
    AppConfigPersistence::saveChanged(config, changes);
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

bool DIMM::applyStarFindingExposure(QString* reason)
{
    bool exposureNeedsUpdate = false;
    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        if (std::abs(m_cameraExposureUs[cameraIndex] -
                     static_cast<double>(kStarFindingExposureUs)) > 0.5) {
            exposureNeedsUpdate = true;
            break;
        }
    }
    if (!exposureNeedsUpdate) {
        return true;
    }
    return applyExposureAndHotPixelTemplate(kStarFindingExposureUs, reason);
}

bool DIMM::applyTrackingExposureAndFrameRate(const double targetExposureUs[2],
                                             QString* reason,
                                             bool forceFrequencySwitch)
{
    if (m_frequencyVerificationPending && !m_frequencyVerificationRollbackInProgress) {
        if (reason) {
            *reason = QStringLiteral("自动曝光: 正在验证上一档触发频率");
        }
        return false;
    }
    if (!targetExposureUs || !m_cameraManager ||
        m_captureState != CaptureState::Live ||
        (m_liveStartupPhase != LiveStartupPhase::Tracking && !m_liveHardwareRoiActive)) {
        if (reason) {
            *reason = QStringLiteral("自动曝光: 当前不在可切换的 Tracking 阶段");
        }
        return false;
    }

    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        if (!std::isfinite(targetExposureUs[cameraIndex]) ||
            targetExposureUs[cameraIndex] <= 0.0) {
            if (reason) {
                *reason = QStringLiteral("自动曝光: 目标曝光无效");
            }
            return false;
        }
    }

    const double oldExposureUs[2] = {
        m_cameraExposureUs[0],
        m_cameraExposureUs[1],
    };
    const double oldRateHz = currentTrackingFrameRateHz();
    const bool frequencySwitchEnabled =
        forceFrequencySwitch || m_autoExposureConfig.exposureFrequencySwitchEnabled;
    QVector<ExposureFrameRateWindow> exposureFrameRateWindows;
    QString windowReason;
    if (frequencySwitchEnabled &&
        !parseExposureFrameRateWindows(m_autoExposureConfig.exposureFrameRateWindows,
                                       &exposureFrameRateWindows,
                                       &windowReason)) {
        if (reason) {
            *reason = QStringLiteral("自动曝光: ROI 曝光区间-频率无效：%1").arg(windowReason);
        }
        return false;
    }
    const ExposureFrequencySwitchPlan switchPlan = ExposureFrequencySwitchController::plan(
        oldExposureUs[0], oldExposureUs[1], targetExposureUs[0], targetExposureUs[1],
        oldRateHz, exposureFrameRateWindows, frequencySwitchEnabled);
    const double targetRateHz = switchPlan.targetFrequencyHz;
    const bool trackingRateChange =
        switchPlan.action == ExposureFrequencySwitchAction::FrequencyAndExposure;
    if (switchPlan.action == ExposureFrequencySwitchAction::NoChange) {
        m_trackingImageIntervalMs = AcquisitionImagePolicy::trackingIntervalMsForExposureUs(
            targetExposureUs[0],
            targetExposureUs[1],
            targetRateHz,
            m_autoExposureConfig.autoExposureSampleIntervalMs);
        return true;
    }

    qint64 switchStartedMs = -1;
    QElapsedTimer switchTimer;
    if (trackingRateChange) {
        switchStartedMs = QDateTime::currentMSecsSinceEpoch();
        switchTimer.start();
        m_rateSwitchInProgress = true;
        m_rateSwitchStartedMs = switchStartedMs;
        m_rateSwitchOldRateHz = oldRateHz;
        m_rateSwitchNewRateHz = targetRateHz;
        m_rateSwitchPauseMs = 0;
        m_rateSwitchHardwareApplyMs = 0;
        m_rateSwitchTimingPending = true;
        if (m_resultSessionActive && m_resultWriter.isOpen()) {
            writeAcquisitionPauseEvent(QStringLiteral("exposure_and_rate_change"),
                                       switchStartedMs,
                                       currentDeviceStatusForResultLog(switchStartedMs));
        }
    }

    QString operationReason;
    bool success = true;
    QVector<int> appliedExposureCameraIndexes;
    const auto applyTargetExposures = [&]() {
        for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
            const int targetExposure =
                static_cast<int>(std::lround(targetExposureUs[cameraIndex]));
            if (!applyExposureAndHotPixelTemplate(cameraIndex, targetExposure, &operationReason)) {
                return false;
            }
            appliedExposureCameraIndexes.append(cameraIndex);
        }
        return true;
    };

    bool rollbackAlreadyAttempted = false;
    bool rollbackSuccess = true;
    switch (switchPlan.action) {
    case ExposureFrequencySwitchAction::ExposureOnly:
        success = applyTargetExposures();
        break;
    case ExposureFrequencySwitchAction::FrequencyAndExposure:
        if (m_configTriggerMode == 0) {
            success = applyContinuousCameraFrameRate(targetRateHz, &operationReason);
            if (success) {
                success = applyTargetExposures();
            }
        } else {
            ExposureFrequencySwitchCallbacks callbacks;
            callbacks.stopTriggerOutput = [&](QString* callbackReason) {
                if (!m_pulseGeneratorEnabled || !m_pulseGenerator) {
                    if (callbackReason) {
                        *callbackReason = QStringLiteral("自动曝光: 硬件触发器未就绪，无法停止触发输出");
                    }
                    return false;
                }
                if (!m_pulseGenerator->stop(callbackReason)) {
                    return false;
                }
                m_rateSwitchPauseMs = switchTimer.elapsed();
                return true;
            };
            callbacks.applyFrequency = [&](double frequencyHz, QString* callbackReason) {
                if (!m_pulseGeneratorEnabled || !m_pulseGenerator) {
                    if (callbackReason) {
                        *callbackReason = QStringLiteral("自动曝光: 硬件触发器未就绪，无法修改触发频率");
                    }
                    return false;
                }
                PulseGeneratorManager::Config pulseConfig = m_pulseGenerator->config();
                pulseConfig.enabled = true;
                pulseConfig.frequencyHz = frequencyHz;
                if (!m_pulseGenerator->applyConfig(pulseConfig, callbackReason)) {
                    return false;
                }
                if (std::abs(m_pulseGenerator->config().frequencyHz - frequencyHz) > 0.05) {
                    if (callbackReason) {
                        *callbackReason = QStringLiteral("自动曝光: 触发频率配置读回失败");
                    }
                    return false;
                }
                return true;
            };
            callbacks.applyExposures = [&](QString* callbackReason) {
                const bool applied = applyTargetExposures();
                if (!applied && callbackReason) {
                    *callbackReason = operationReason;
                }
                return applied;
            };
            callbacks.rollback = [&](QString*) {
                bool restored = true;
                for (int rollbackIndex = appliedExposureCameraIndexes.size() - 1;
                     rollbackIndex >= 0;
                     --rollbackIndex) {
                    QString rollbackReason;
                    const int cameraIndex = appliedExposureCameraIndexes[rollbackIndex];
                    restored = applyExposureAndHotPixelTemplate(
                                   cameraIndex,
                                   static_cast<int>(std::lround(oldExposureUs[cameraIndex])),
                                   &rollbackReason) &&
                               restored;
                }
                PulseGeneratorManager::Config rollbackPulseConfig = m_pulseGenerator->config();
                rollbackPulseConfig.enabled = true;
                rollbackPulseConfig.frequencyHz = oldRateHz;
                QString rollbackReason;
                restored = m_pulseGenerator->configureAndStart(rollbackPulseConfig,
                                                                &rollbackReason) &&
                           restored;
                return restored;
            };
            callbacks.startTriggerOutput = [&](double frequencyHz, QString* callbackReason) {
                PulseGeneratorManager::Config pulseConfig = m_pulseGenerator->config();
                if (std::abs(pulseConfig.frequencyHz - frequencyHz) > 0.05) {
                    if (callbackReason) {
                        *callbackReason = QStringLiteral("自动曝光: 启动前触发频率异常");
                    }
                    return false;
                }
                if (!m_pulseGenerator->configureAndStart(pulseConfig, callbackReason)) {
                    return false;
                }
                if (!m_pulseGenerator->isRunningAtFrequency(frequencyHz)) {
                    if (callbackReason) {
                        *callbackReason = QStringLiteral("自动曝光: 启动后触发频率验证失败");
                    }
                    return false;
                }
                m_rateSwitchHardwareApplyMs = switchTimer.elapsed();
                return true;
            };
            callbacks.flushQueues = [&]() { m_cameraManager->flushPairQueues(); };
            const ExposureFrequencySwitchResult result =
                ExposureFrequencySwitchController::switchForExposure(
                    switchPlan, callbacks, &operationReason);
            success = result == ExposureFrequencySwitchResult::Applied;
            rollbackAlreadyAttempted = result != ExposureFrequencySwitchResult::Applied;
            rollbackSuccess = result == ExposureFrequencySwitchResult::RolledBack;
        }
        break;
    case ExposureFrequencySwitchAction::NoChange:
        break;
    }

    if (!success) {
        const QString primaryReason = operationReason;
        if (!rollbackAlreadyAttempted) {
            for (int rollbackIndex = appliedExposureCameraIndexes.size() - 1;
                 rollbackIndex >= 0;
                 --rollbackIndex) {
                const int cameraIndex = appliedExposureCameraIndexes[rollbackIndex];
                QString rollbackReason;
                rollbackSuccess = applyExposureAndHotPixelTemplate(
                                      cameraIndex,
                                      static_cast<int>(std::lround(oldExposureUs[cameraIndex])),
                                      &rollbackReason) &&
                                  rollbackSuccess;
            }
            if (trackingRateChange && m_configTriggerMode == 0) {
                QString rollbackReason;
                rollbackSuccess = applyContinuousCameraFrameRate(oldRateHz, &rollbackReason) &&
                                  rollbackSuccess;
            }
        }
        if (trackingRateChange) {
            m_rateSwitchInProgress = false;
        }
        if (m_resultSessionActive && m_resultWriter.isOpen()) {
            const qint64 failedAtMs = QDateTime::currentMSecsSinceEpoch();
            writeHardwareErrorEvent(QStringLiteral("acquisition"),
                                    -1,
                                    0,
                                    QStringLiteral("exposure_and_rate_change"),
                                    primaryReason,
                                    rollbackSuccess,
                                    failedAtMs);
            if (rollbackSuccess) {
                writeAcquisitionResumeEvent(QStringLiteral("exposure_and_rate_rollback"),
                                            failedAtMs,
                                            currentDeviceStatusForResultLog(failedAtMs));
            }
        }
        if (reason) {
            *reason = primaryReason.isEmpty()
                          ? QStringLiteral("自动曝光: 曝光/帧率切换失败，已尝试回滚")
                          : primaryReason + QStringLiteral("；已尝试回滚");
        }
        if (trackingRateChange) {
            RateSwitchTiming timing;
            timing.pauseMs = 0;
            timing.hardwareApplyMs = switchTimer.elapsed();
            timing.firstValidPairMs = 0;
            timing.success = false;
            logRateSwitchTiming(timing, oldRateHz, targetRateHz,
                                m_configTriggerMode == 0
                                    ? QStringLiteral("continuous")
                                    : QStringLiteral("hardware_trigger"));
            m_rateSwitchTimingPending = false;
        }
        if (!rollbackSuccess) {
            closeResultSessionForHardwareError();
            updateCaptureState(CaptureState::Idle);
            if (m_liveStartupOrigin == LiveStartupOrigin::AutoAcquisition) {
                closeAutoAcquisitionCameras();
            }
        }
        return false;
    }

    if (trackingRateChange && m_configTriggerMode != 0) {
        m_activeTriggerFrequencyHz = targetRateHz;
        if (m_imageProcessor) {
            m_imageProcessor->setTargetFrameRateHz(targetRateHz);
        }
    }

    m_trackingImageIntervalMs = AcquisitionImagePolicy::trackingIntervalMsForExposureUs(
        targetExposureUs[0],
        targetExposureUs[1],
        targetRateHz,
        m_autoExposureConfig.autoExposureSampleIntervalMs);
    if (trackingRateChange && m_configTriggerMode != 0 &&
        !m_frequencyVerificationRollbackInProgress) {
        m_frequencyVerificationPending = true;
        m_frequencyVerificationNotBeforeMs = QDateTime::currentMSecsSinceEpoch() + 1000;
        m_frequencyVerificationTargetHz = targetRateHz;
        m_frequencyVerificationPreviousExposureUs[0] = oldExposureUs[0];
        m_frequencyVerificationPreviousExposureUs[1] = oldExposureUs[1];
        return true;
    }
    if (trackingRateChange) {
        m_rateSwitchHardwareApplyMs = switchTimer.elapsed();
        m_rateSwitchInProgress = false;
        if (m_rateSwitchTimingPending) {
            RateSwitchTiming timing;
            timing.pauseMs = m_rateSwitchPauseMs;
            timing.hardwareApplyMs = m_rateSwitchHardwareApplyMs;
            timing.firstValidPairMs = 0;
            timing.success = true;
            logRateSwitchTiming(timing, oldRateHz, targetRateHz,
                                m_configTriggerMode == 0
                                    ? QStringLiteral("continuous")
                                    : QStringLiteral("hardware_trigger"));
            m_rateSwitchTimingPending = false;
        }
        if (m_resultSessionActive && m_resultWriter.isOpen()) {
            const qint64 resumedAtMs = QDateTime::currentMSecsSinceEpoch();
            writeAcquisitionResumeEvent(QStringLiteral("exposure_and_rate_change_complete"),
                                        resumedAtMs,
                                        currentDeviceStatusForResultLog(resumedAtMs));
        }
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
            *reason = QStringLiteral("自动曝光: 无效相机或曝光参数");
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
