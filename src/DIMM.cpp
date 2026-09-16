#include "DIMM.h"
#include "DimmRuntimeHelpers.h"
#include "ExposureFrequencySwitchController.h"
#include "FrameRateChangePolicy.h"

#include "AlignmentCameraCoordinator.h"
#include "AlignmentCoarseController.h"
#include "AlignmentController.h"
#include "AlignmentFrameCoordinator.h"
#include "AlignmentLocalTracker.h"
#include "AlignmentTaskManager.h"
#include "AlignmentUiPresenter.h"
#include "AcquisitionImagePolicy.h"
#include "CameraManager.h"
#include "CanvasWidgets.h"
#include "CommManager.h"
#include "AppConfigPersistence.h"
#include "AutoAcquisitionCameraLifecycle.h"
#include "AutoFocusConnectionPolicy.h"
#include "AutoFocusMetricCalculator.h"
#include "AutoFocusSettings.h"
#include "EafFocuserManager.h"
#include "FocuserControlWidget.h"
#include "FullFrameStarDetector.h"
#include "HotPixelTemplateSettings.h"
#include "ImageUtils.h"
#include "ImageProcessor.h"
#include "InitialStarDetectionConfig.h"
#include "PathUtils.h"
#include "PolarisDetectionPipeline.h"
#include "PolarisSolver.h"
#include "SettingsDialog.h"
#include "PolarisTracker.h"
#include "PulseGeneratorManager.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QAction>
#include <QCloseEvent>
#include <QApplication>
#include <QDate>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLatin1Char>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointF>
#include <QPushButton>
#include <QRadioButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QFrame>
#include <QSignalBlocker>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QSettings>
#include <QShowEvent>
#include <QShortcut>
#include <QStringList>
#include <QThread>
#include <QTabWidget>
#include <QTime>
#include <QVBoxLayout>

namespace {
using PolarisDetectionPipeline::InitialStarCandidate;
using PolarisDetectionPipeline::InitialStarSelection;
}

DIMM::DIMM(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui_DIMM)
{
    registerMetaTypes();
    ui->setupUi(this);

    setupServiceManagers();
    initializeCaptureServices();
    setupStatusBarUi();
    setupMainWindowUi();
    setupRuntimeActions();
    setupConnections();
    setupPreviewCanvases();
    setupSettingsCallbacks();
    setupCameraConnections();
    setupImageProcessorConnections();
    setupAutoFocusConnections();
    setupRuntimeTimers();
    setupCommConnections();
    setupCanvasMouseStatusConnections();

    applyStartupConfig(AppConfigPersistence::load(currentAppConfig()));
    setupReportTimer();
    hideLegacyRoiScheduleUi();
    updateMinuteRoi(true);
    refreshUi();
    updateCaptureState(m_captureState);
}

void DIMM::registerMetaTypes()
{
    qRegisterMetaType<PolarisSolveResult>("PolarisSolveResult");
    qRegisterMetaType<TelescopeSlot>("TelescopeSlot");
    qRegisterMetaType<EafDeviceDescriptor>("EafDeviceDescriptor");
    qRegisterMetaType<EafDeviceState>("EafDeviceState");
    qRegisterMetaType<QVector<EafDeviceDescriptor>>("QVector<EafDeviceDescriptor>");
    qRegisterMetaType<EnvironmentSensorData>("EnvironmentSensorData");
    qRegisterMetaType<CoarseAlignmentEstimate>("CoarseAlignmentEstimate");

}

void DIMM::setupServiceManagers()
{
    m_autoExposureController.configure(m_autoExposureConfig);
    m_settingsDialog = new SettingsDialog(this);
    m_focuserManager = new EafFocuserManager(this);
    m_focuserControlWidget = new FocuserControlWidget(m_settingsDialog);
    m_focuserControlWidget->setManager(m_focuserManager);
    m_focuserManager->initialize();
    m_settingsDialog->addSettingsPage(m_focuserControlWidget, QStringLiteral("自动调焦"));
    {
        QSettings settings;
        m_autoFocusConfig = AutoFocusSettings::load(settings);
    }
    m_autoFocusLogWriter.setEnabled(m_autoFocusConfig.dataLoggingEnabled);
    m_autoFocusRealtimeMetricsSampler.configure(m_autoFocusConfig.framesPerState,
                                                m_autoFocusConfig.statisticsMode,
                                                m_autoFocusConfig.trimRatio);
    m_autoFocusController = std::make_unique<AutoFocusController>(m_autoFocusConfig);
    m_environmentSensor = new EnvironmentSensorManager(this);
    connect(m_environmentSensor, &EnvironmentSensorManager::dataUpdated, this, [this](EnvironmentSensorData data) {
        m_latestEnvironment = data;
        if (m_autoFocusController) {
            if (data.valid) {
                m_autoFocusSensorHadValidData = true;
                m_autoFocusSensorOutageNotified = false;
            } else if (isTrackingForAutoFocus() &&
                       m_autoFocusConfig.masterEnabled &&
                       m_autoFocusSensorHadValidData &&
                       QDateTime::currentMSecsSinceEpoch() >= m_autoFocusSensorAlertNotBeforeMs &&
                       !m_autoFocusSensorOutageNotified) {
                m_autoFocusSensorOutageNotified = true;
                QMessageBox::warning(this,
                                     QStringLiteral("自动调焦环境传感器"),
                                     QStringLiteral("环境传感器数据无效：正在进行的自动调焦会继续完成，新的温度触发已暂停。"));
            }
            if (isTrackingForAutoFocus()) {
                for (int camera = 0; camera < 2; ++camera) {
                    handleAutoFocusAction(
                        camera,
                        m_autoFocusController->updateTemperature(
                            camera, data.temperatureC, data.valid),
                        QStringLiteral("温度触发"));
                }
            }
        }
        updateCameraInfo();
    });
    connect(m_environmentSensor, &EnvironmentSensorManager::errorOccurred, this, [this](const QString& error) {
        qDebug() << "[EnvironmentSensor]" << error;
    });
}

void DIMM::setupRuntimeActions()
{
    m_actionAlignmentMode = new QAction(QStringLiteral("对准模式"), this);
    m_actionAlignmentMode->setObjectName(QStringLiteral("btnAlignmentMode"));
    m_actionAlignmentMode->setCheckable(true);
    if (ui->toolbar) {
        ui->toolbar->insertAction(ui->btnSettings, m_actionAlignmentMode);
    }
    if (ui->menuTools) {
        ui->menuTools->insertAction(ui->actionROISchedule, m_actionAlignmentMode);
    }

    m_actionToggleCoarseAlignment = new QAction(QStringLiteral("开始粗对准"), this);
    m_actionToggleCoarseAlignment->setObjectName(QStringLiteral("btnToggleCoarseAlignment"));
    m_actionToggleCoarseAlignment->setCheckable(true);
    if (ui->toolbar) {
        ui->toolbar->insertAction(ui->btnSettings, m_actionToggleCoarseAlignment);
    }
    if (ui->menuTools) {
        ui->menuTools->insertAction(ui->actionROISchedule, m_actionToggleCoarseAlignment);
    }

    m_actionConfirmCamera1Polaris = new QAction(QStringLiteral("确认相机1的北极星"), this);
    m_actionConfirmCamera1Polaris->setObjectName(QStringLiteral("btnConfirmCamera1Polaris"));
    m_actionConfirmCamera2Polaris = new QAction(QStringLiteral("确认相机2的北极星"), this);
    m_actionConfirmCamera2Polaris->setObjectName(QStringLiteral("btnConfirmCamera2Polaris"));
    m_actionConfirmAndStartCapture = new QAction(QStringLiteral("确认并开始采集"), this);
    m_actionConfirmAndStartCapture->setObjectName(QStringLiteral("btnConfirmAndStartCapture"));
    m_actionRetryCamera1PolarisSolve = new QAction(QStringLiteral("重新自动识别相机1"), this);
    m_actionRetryCamera1PolarisSolve->setObjectName(QStringLiteral("btnRetryCamera1PolarisSolve"));
    m_actionRetryCamera2PolarisSolve = new QAction(QStringLiteral("重新自动识别相机2"), this);
    m_actionRetryCamera2PolarisSolve->setObjectName(QStringLiteral("btnRetryCamera2PolarisSolve"));
    m_actionRetryBothPolarisSolve = new QAction(QStringLiteral("重新自动识别双相机"), this);
    m_actionRetryBothPolarisSolve->setObjectName(QStringLiteral("btnRetryBothPolarisSolve"));
    if (ui->menuTools) {
        ui->menuTools->insertAction(ui->actionROISchedule, m_actionConfirmAndStartCapture);
        ui->menuTools->insertAction(ui->actionROISchedule, m_actionConfirmCamera2Polaris);
        ui->menuTools->insertAction(m_actionConfirmCamera2Polaris, m_actionConfirmCamera1Polaris);
        ui->menuTools->insertAction(m_actionConfirmCamera1Polaris, m_actionRetryBothPolarisSolve);
        ui->menuTools->insertAction(m_actionRetryBothPolarisSolve, m_actionRetryCamera2PolarisSolve);
        ui->menuTools->insertAction(m_actionRetryCamera2PolarisSolve, m_actionRetryCamera1PolarisSolve);
    }

    hideLegacyRoiScheduleUi();

}

void DIMM::initializeCaptureServices()
{
    m_cameraManager = &CameraManager::instance();
    m_cameraManager->init();
    m_imageProcessor = new ImageProcessor(this);
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
    m_polarisSolverController = new PolarisSolverController(this);
    connect(m_polarisSolverController,
            &PolarisSolverController::solveFinished,
            this,
            &DIMM::onPolarisSolveFinished);
    connect(m_polarisSolverController,
            &PolarisSolverController::solveStatusChanged,
            this,
            &DIMM::onPolarisSolveStatusChanged);
    m_alignmentCoarseController = new AlignmentCoarseController(this);
    connect(m_alignmentCoarseController,
            &AlignmentCoarseController::estimateReady,
            this,
            &DIMM::onCoarseAlignmentEstimateReady,
            Qt::QueuedConnection);
    {
        const QString appThresholdPath =
            QDir(QApplication::applicationDirPath()).filePath(QStringLiteral("threshold.txt"));
        const QString cwdThresholdPath =
            QDir::current().filePath(QStringLiteral("threshold.txt"));
        QString configMessage;
        QString loadedThresholdPath;
        if (QFileInfo::exists(appThresholdPath)) {
            m_imageProcessor->loadProcessingConfig(appThresholdPath, &configMessage);
            loadedThresholdPath = appThresholdPath;
        } else if (QFileInfo::exists(cwdThresholdPath)) {
            m_imageProcessor->loadProcessingConfig(cwdThresholdPath, &configMessage);
            loadedThresholdPath = cwdThresholdPath;
        }

        if (!loadedThresholdPath.isEmpty()) {
            HotPixelTemplateSettings hotSettings;
            if (loadHotPixelTemplateSettings(loadedThresholdPath, &hotSettings)) {
                m_hotPixelTemplatesEnabled = true;
                m_hotPixelCamera0MaskPath = PathUtils::relativizePathToAppDir(hotSettings.camera0Mask);
                m_hotPixelCamera0ExcessPath = PathUtils::relativizePathToAppDir(hotSettings.camera0Excess);
                m_hotPixelCamera1MaskPath = PathUtils::relativizePathToAppDir(hotSettings.camera1Mask);
                m_hotPixelCamera1ExcessPath = PathUtils::relativizePathToAppDir(hotSettings.camera1Excess);
                m_hotPixelTemplateWidth = hotSettings.width;
                m_hotPixelTemplateHeight = hotSettings.height;
                m_hotPixelTemplateExposureUs[0] =
                    PathUtils::exposureUsFromTemplatePath(m_hotPixelCamera0MaskPath);
                m_hotPixelTemplateExposureUs[1] =
                    PathUtils::exposureUsFromTemplatePath(m_hotPixelCamera1MaskPath);
                m_cachedHotPixelTemplateExposures.clear();
                m_cachedHotPixelTemplateScanMs = -1;
                refreshHotPixelTemplates();
            }
        }
    }
    m_pulseGenerator = new PulseGeneratorManager();
}

void DIMM::setupCameraConnections()
{
    connect(m_cameraManager, &CameraManager::frameReady, this, &DIMM::onFrameReady, Qt::QueuedConnection);
    connect(m_cameraManager, &CameraManager::frameCaptured, this, &DIMM::onCapturedFramePacket, Qt::QueuedConnection);
    connect(m_cameraManager, &CameraManager::cameraConnected, this, &DIMM::onCameraConnected, Qt::QueuedConnection);
    connect(m_cameraManager, &CameraManager::cameraDisconnected, this, &DIMM::onCameraDisconnected, Qt::QueuedConnection);
    connect(m_cameraManager, &CameraManager::cameraError, this, &DIMM::onCameraError, Qt::QueuedConnection);

}

void DIMM::setupImageProcessorConnections()
{
    setupCentroidProcessorConnection();
    setupAutoExposureProcessorConnection();
    setupDifferentialSampleProcessorConnections();
    setupFrameProcessedProcessorConnection();
    setupSyncSampleProcessorConnection();
    setupRoiImageProcessorConnection();
    setupAtmosphereProcessorConnection();
    connect(m_imageProcessor,
            &ImageProcessor::roiBackgroundThresholdReady,
            this,
            [this](int cameraIndex, double background, double noiseSigma, double threshold) {
                setRoiBackgroundThresholdDisplay(cameraIndex, background, noiseSigma, threshold);
            });
    connect(m_imageProcessor,
            &ImageProcessor::calculationImageReady,
            this,
            [this](int cameraIndex, quint64 frameId, cv::Mat calculationImage) {
                const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                const bool saveForAutoExposureCooldown =
                    cameraIndex >= 0 && cameraIndex < 2 &&
                    m_trackingImageSaveOnNextRoiFrame[cameraIndex];
                const bool saveForPeriodicTracking =
                    cameraIndex >= 0 && cameraIndex < 2 &&
                    !saveForAutoExposureCooldown &&
                    AcquisitionImagePolicy::shouldSavePeriodicTrackingRoiImage(
                        m_autoExposureConfig.enabled,
                        m_lastPeriodicTrackingRoiImageSavedMs[cameraIndex],
                        nowMs);
                if (cameraIndex < 0 || cameraIndex >= 2 || calculationImage.empty() ||
                    (!saveForAutoExposureCooldown && !saveForPeriodicTracking) ||
                    !m_resultSessionActive || !m_resultWriter.isOpen() ||
                    m_liveStartupPhase != LiveStartupPhase::Tracking) {
                    return;
                }
                const QFileInfo sessionInfo(m_resultFilePath);
                QDir sessionDir(sessionInfo.absolutePath());
                if (!sessionDir.mkpath(QStringLiteral("images"))) {
                    return;
                }
                const QString stamp = QDateTime::currentDateTime().toLocalTime().toString(
                    QStringLiteral("yyyy-MM-dd_HHmmss_zzz"));
                const QString imageKind = saveForAutoExposureCooldown
                                              ? QStringLiteral("calculation")
                                              : QStringLiteral("periodic");
                const QString path = sessionDir.filePath(
                    QStringLiteral("images/camera%1_tracking_roi_%2_%3_frame-%4.bmp")
                        .arg(cameraIndex + 1)
                        .arg(imageKind)
                        .arg(stamp)
                        .arg(frameId));
                const cv::Mat mono8 = ImageUtils::fullFrameMono8Preview(calculationImage, 12, 4095.0);
                if (mono8.empty() || !cv::imwrite(path.toStdString(), mono8)) {
                    return;
                }
                if (saveForAutoExposureCooldown) {
                    m_trackingImageSaveOnNextRoiFrame[cameraIndex] = false;
                    m_trackingImageSaveNextRetryMs[cameraIndex] = -1;
                } else {
                    m_lastPeriodicTrackingRoiImageSavedMs[cameraIndex] = nowMs;
                }
            });
    connect(m_imageProcessor,
            &ImageProcessor::acquisitionStopRequested,
            this,
            [this](const QString& reason) {
                setStatusMessage(QStringLiteral("Status: %1").arg(reason), UiStatusLevel::Error);
                onStopCapture();
            });
}

void DIMM::setupAutoFocusConnections()
{
    if (!m_autoFocusController || !m_focuserControlWidget || !m_focuserManager ||
        !m_imageProcessor) {
        return;
    }

    connect(m_focuserControlWidget,
            &FocuserControlWidget::autoFocusConfigApplied,
            this,
            [this](AutoFocusConfig requestedConfig) {
                AutoFocusConfig immediatelyApplied = m_autoFocusConfig;
                if (!requestedConfig.masterEnabled) {
                    immediatelyApplied.masterEnabled = false;
                    for (int camera = 0; camera < 2; ++camera) {
                        cancelAutoFocus(camera,
                                        QStringLiteral("自动调焦总开关已关闭"),
                                        true);
                    }
                }
                for (int camera = 0; camera < 2; ++camera) {
                    if (!requestedConfig.cameraEnabled[camera]) {
                        immediatelyApplied.cameraEnabled[camera] = false;
                        cancelAutoFocus(camera,
                                        QStringLiteral("相机自动调焦已关闭"),
                                        true);
                    }
                }
                m_autoFocusConfig = immediatelyApplied;
                m_autoFocusController->setConfig(m_autoFocusConfig);
                if (m_autoFocusController->isActive(0) || m_autoFocusController->isActive(1)) {
                    m_pendingAutoFocusConfig = requestedConfig;
                    setStatusMessage(QStringLiteral("自动调焦参数将在当前轮次结束后生效"),
                                     UiStatusLevel::Warning);
                    return;
                }
                applyAutoFocusConfig(requestedConfig);
            });
    connect(m_focuserControlWidget,
            &FocuserControlWidget::manualAutoFocusRequested,
            this,
            &DIMM::startManualAutoFocus);
    connect(m_focuserControlWidget,
            &FocuserControlWidget::autoFocusDisabled,
            this,
            [this](int cameraIndex) {
                if (cameraIndex < 0) {
                    m_autoFocusConfig.masterEnabled = false;
                    for (int camera = 0; camera < 2; ++camera) {
                        cancelAutoFocus(camera,
                                        QStringLiteral("自动调焦总开关已关闭"),
                                        true);
                    }
                } else if (cameraIndex < 2) {
                    m_autoFocusConfig.cameraEnabled[cameraIndex] = false;
                    cancelAutoFocus(cameraIndex,
                                    QStringLiteral("相机自动调焦已关闭"),
                                    true);
                }
                m_pendingAutoFocusConfig.reset();
                m_autoFocusController->setConfig(m_autoFocusConfig);
            });

    connect(m_focuserManager,
            &EafFocuserManager::stateChanged,
            this,
            [this](TelescopeSlot slot, EafDeviceState state) {
                handleAutoAcquisitionPreFocusFocuserState(static_cast<int>(slot), state);
                handleAutoFocusFocuserState(static_cast<int>(slot),
                                             state.opened,
                                             state.moving,
                                             state.currentPosition);
            });
    connect(m_focuserManager,
            &EafFocuserManager::commandFinished,
            this,
            [this](TelescopeSlot slot, const QString& command) {
                const int cameraIndex = static_cast<int>(slot);
                if (cameraIndex < 0 || cameraIndex >= 2) {
                    return;
                }
                if (command == QStringLiteral("move") &&
                    m_autoAcquisitionPreFocus.awaitingMoveAcknowledgement(cameraIndex)) {
                    handleAutoAcquisitionPreFocusCommandFinished(cameraIndex, command);
                    return;
                }
                if (command == QStringLiteral("move") && m_autoFocusController &&
                    !m_autoFocusAwaitingTimeoutStop[cameraIndex]) {
                    m_autoFocusController->markFocuserMoveAcknowledged(cameraIndex);
                    return;
                }
                if (command != QStringLiteral("stop") ||
                    !m_autoFocusAwaitingTimeoutStop[cameraIndex]) {
                    return;
                }
                m_autoFocusTimeoutStopCommandFinished[cameraIndex] = true;
                m_focuserManager->requestStateRefresh(slot);
            });
    connect(m_focuserManager,
            &EafFocuserManager::commandFailed,
            this,
            [this](TelescopeSlot slot, const QString& command, const QString& error) {
                handleAutoAcquisitionPreFocusCommandFailed(static_cast<int>(slot), command, error);
            });
    connect(m_focuserManager,
            &EafFocuserManager::deviceRemoved,
            this,
            [this](TelescopeSlot slot, const QString&) {
                handleAutoAcquisitionPreFocusFocuserState(
                    static_cast<int>(slot),
                    {false, false, false, false, 0, 0, 0});
                handleAutoFocusFocuserState(static_cast<int>(slot), false, false, 0);
            });
    connect(m_imageProcessor,
            &ImageProcessor::autoFocusRoiMeasurementReady,
            this,
            [this](int cameraIndex,
                   quint64,
                   cv::Mat calculationImage,
                   bool centroidValid,
                   double centroidX,
                   double centroidY) {
                handleAutoFocusMeasurement(cameraIndex,
                                            calculationImage,
                                            centroidValid,
                                            centroidX,
                                            centroidY);
            });
}

bool DIMM::isTrackingForAutoFocus() const
{
    return m_captureState == CaptureState::Live &&
           m_liveStartupPhase == LiveStartupPhase::Tracking &&
           !m_autoAcquisitionPreFocus.blocksAutoFocus();
}

QString DIMM::autoFocusStateText(AutoFocusRunState state) const
{
    switch (state) {
    case AutoFocusRunState::ReferenceCalibration:
        return QStringLiteral("参考标定");
    case AutoFocusRunState::AwaitingInitialMetrics:
        return QStringLiteral("等待初始指标");
    case AutoFocusRunState::DirectionSearch:
        return QStringLiteral("调焦方向搜索");
    case AutoFocusRunState::Adjusting:
        return QStringLiteral("调焦调整搜索");
    case AutoFocusRunState::Callback:
        return QStringLiteral("回调补偿");
    case AutoFocusRunState::ReturningBest:
        return QStringLiteral("回到最佳位置");
    case AutoFocusRunState::OvershootReturnSearch:
        return QStringLiteral("越焦回调后搜索");
    case AutoFocusRunState::FinalValidation:
        return QStringLiteral("最终验证");
    case AutoFocusRunState::Complete:
        return QStringLiteral("完成");
    case AutoFocusRunState::Failed:
        return QStringLiteral("失败");
    case AutoFocusRunState::Idle:
        return QStringLiteral("空闲");
    }
    return QStringLiteral("未知");
}

QString DIMM::autoFocusStatisticsText() const
{
    switch (m_autoFocusConfig.statisticsMode) {
    case AutoFocusStatisticsMode::Mean:
        return QStringLiteral("Mean");
    case AutoFocusStatisticsMode::Median:
        return QStringLiteral("Median");
    case AutoFocusStatisticsMode::TrimmedMean:
        return QStringLiteral("TrimmedMean");
    }
    return QStringLiteral("Unknown");
}

void DIMM::logAutoFocusEvent(int cameraIndex,
                             const AutoFocusAction& action,
                             const QString& finalResult)
{
    if (!m_autoFocusConfig.dataLoggingEnabled || !m_resultSessionActive ||
        !m_resultWriter.isOpen() || !m_autoFocusController || cameraIndex < 0 ||
        cameraIndex >= 2) {
        return;
    }

    const AutoFocusRunState state = m_autoFocusController->state(cameraIndex);
    if (!m_autoFocusRunActive[cameraIndex] &&
        action.type == AutoFocusActionType::AwaitingInitialMetrics) {
        ++m_autoFocusRunSequence[cameraIndex];
        m_autoFocusPendingDirectionStep[cameraIndex] = 0;
        AutoFocusLogRecord initial;
        initial.timestamp = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
        initial.sequence = m_autoFocusRunSequence[cameraIndex];
        initial.stage = QStringLiteral("InitialPosition");
        initial.motorPosition = m_autoFocusFocuserPosition[cameraIndex];
        initial.statisticsMode = autoFocusStatisticsText();
        QString initialError;
        if (!m_autoFocusLogWriter.append(QFileInfo(m_resultFilePath).absolutePath(),
                                         cameraIndex,
                                         initial,
                                         &initialError)) {
            setStatusMessage(QStringLiteral("相机 %1 自动调焦日志写入失败：%2")
                                 .arg(cameraIndex + 1)
                                 .arg(initialError),
                             UiStatusLevel::Error);
            return;
        }
        m_autoFocusRunActive[cameraIndex] = true;
    }
    if (!m_autoFocusRunActive[cameraIndex]) {
        return;
    }

    if (action.type == AutoFocusActionType::MoveRelative) {
        m_autoFocusPendingDirectionStep[cameraIndex] = action.relativeStep;
        return;
    }

    const AutoFocusRunSnapshot snapshot = m_autoFocusController->snapshot(cameraIndex);
    AutoFocusLogRecord record;
    record.timestamp = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    record.sequence = m_autoFocusRunSequence[cameraIndex];
    record.stage = finalResult.isEmpty() ? QStringLiteral("MotorMove")
                                         : autoFocusStateText(snapshot.state);
    record.motorPosition = m_autoFocusFocuserPosition[cameraIndex];
    record.directionStep = m_autoFocusPendingDirectionStep[cameraIndex];
    if (snapshot.hasCurrentMetrics) {
        record.metrics = snapshot.currentMetrics;
    }
    record.statisticsMode = autoFocusStatisticsText();
    record.searchDeadZoneActive = false;
    record.reverseBacklashActive = false;
    record.accumulatedBacklashTravel = snapshot.accumulatedBacklashTravel;
    record.bestHfr = snapshot.bestHfr;
    record.reference = m_autoFocusController->referenceMetrics(cameraIndex);
    record.finalResult = finalResult;
    QString error;
    const QString sessionDirectory = QFileInfo(m_resultFilePath).absolutePath();
    if (!m_autoFocusLogWriter.append(sessionDirectory, cameraIndex, record, &error)) {
        setStatusMessage(QStringLiteral("相机 %1 自动调焦日志写入失败：%2")
                             .arg(cameraIndex + 1)
                             .arg(error),
                         UiStatusLevel::Error);
    }
    if (!finalResult.isEmpty()) {
        m_autoFocusRunActive[cameraIndex] = false;
    }
    if (action.type != AutoFocusActionType::MoveRelative) {
        m_autoFocusPendingDirectionStep[cameraIndex] = 0;
    }
}

void DIMM::setAutoFocusManualLock(int cameraIndex, bool locked)
{
    if (cameraIndex < 0 || cameraIndex >= 2 ||
        m_autoFocusManualLocked[cameraIndex] == locked) {
        return;
    }
    m_autoFocusManualLocked[cameraIndex] = locked;
    const TelescopeSlot slot = static_cast<TelescopeSlot>(cameraIndex);
    const QString reason = locked
                               ? QStringLiteral("自动调焦运行中，已锁定该路人工焦点器控制")
                               : QString();
    if (m_focuserManager) {
        m_focuserManager->setManualMotionAllowed(slot, !locked, reason);
    }
    if (m_focuserControlWidget) {
        m_focuserControlWidget->setAutoFocusMotionLocked(slot, locked);
    }
}

void DIMM::applyAutoFocusConfig(const AutoFocusConfig& config)
{
    const AutoFocusConfig previousConfig = m_autoFocusConfig;
    m_autoFocusConfig = config;
    m_autoFocusLogWriter.setEnabled(config.dataLoggingEnabled);
    m_autoFocusRealtimeMetricsSampler.configure(config.framesPerState,
                                                config.statisticsMode,
                                                config.trimRatio);
    m_pendingAutoFocusConfig.reset();
    if (!m_autoFocusController) {
        m_autoFocusController = std::make_unique<AutoFocusController>(config);
    } else {
        m_autoFocusController->setConfig(config);
    }
    if (!m_focuserManager) {
        return;
    }
    for (int camera = 0; camera < 2; ++camera) {
        if (shouldOpenFocuserAfterAutoFocusEnable(previousConfig,
                                                  config,
                                                  camera,
                                                  m_autoFocusFocuserOpened[camera])) {
            m_focuserManager->openAssignedDevice(static_cast<TelescopeSlot>(camera));
        }
    }
}

void DIMM::beginAutoAcquisitionPreFocus()
{
    if (m_liveStartupOrigin != LiveStartupOrigin::AutoAcquisition ||
        !m_latestEnvironment.valid || !m_focuserManager ||
        !m_autoAcquisitionPreFocus.begin(m_latestEnvironment.temperatureC)) {
        if (m_liveStartupOrigin == LiveStartupOrigin::AutoAcquisition &&
            !m_latestEnvironment.valid) {
            setStatusMessage(QStringLiteral("自动采集温度预调焦已跳过：环境温度无效"),
                             UiStatusLevel::Warning);
        }
        return;
    }

    for (int camera = 0; camera < 2; ++camera) {
        m_autoAcquisitionPreFocusMoveIssued[camera] = false;
        setAutoFocusManualLock(camera, true);
        const TelescopeSlot slot = static_cast<TelescopeSlot>(camera);
        if (!m_autoFocusFocuserOpened[camera] &&
            !m_autoAcquisitionPreFocusOpenRequestedByConfig[camera]) {
            m_focuserManager->openAssignedDevice(slot);
        }
        m_focuserManager->requestStateRefresh(slot);
    }
    setStatusMessage(QStringLiteral("自动采集已按当前温度启动调焦器预定位，正在并行找星"),
                     UiStatusLevel::Info);
}

void DIMM::handleAutoAcquisitionPreFocusFocuserState(int cameraIndex,
                                                      const EafDeviceState& state)
{
    const AutoAcquisitionPreFocusAction action = m_autoAcquisitionPreFocus.update(
        cameraIndex,
        {state.opened,
         state.positionValid,
         state.motionValid,
         state.moving,
         state.currentPosition,
         state.maxStep});
    if (action.type == AutoAcquisitionPreFocusActionType::MoveAbsolute && m_focuserManager) {
        m_autoAcquisitionPreFocusMoveIssued[cameraIndex] = true;
        m_focuserManager->moveAbsoluteForAutoFocus(static_cast<TelescopeSlot>(cameraIndex), action.target);
    }
    resumeTrackingAutoFocusAfterPreFocus();
}

void DIMM::handleAutoAcquisitionPreFocusCommandFinished(int cameraIndex,
                                                         const QString& command)
{
    if (command != QStringLiteral("move") ||
        !m_autoAcquisitionPreFocus.awaitingMoveAcknowledgement(cameraIndex)) {
        return;
    }
    m_autoAcquisitionPreFocus.markMoveAccepted(cameraIndex);
    if (m_focuserManager) {
        m_focuserManager->requestStateRefresh(static_cast<TelescopeSlot>(cameraIndex));
    }
}

void DIMM::handleAutoAcquisitionPreFocusCommandFailed(int cameraIndex,
                                                       const QString& command,
                                                       const QString& error)
{
    if (cameraIndex < 0 || cameraIndex >= 2) {
        return;
    }
    if (command == QStringLiteral("open")) {
        m_autoAcquisitionPreFocus.markOpenFailed(cameraIndex);
        setStatusMessage(QStringLiteral("相机 %1 温度预调焦已跳过：%2")
                             .arg(cameraIndex + 1)
                             .arg(error),
                         UiStatusLevel::Warning);
        resumeTrackingAutoFocusAfterPreFocus();
        return;
    }
    if ((command != QStringLiteral("move") && command != QStringLiteral("autoFocusMove")) ||
        !m_autoAcquisitionPreFocus.awaitingMoveAcknowledgement(cameraIndex)) {
        return;
    }
    m_autoAcquisitionPreFocus.markMoveFailed(cameraIndex);
    if (m_focuserManager) {
        m_focuserManager->requestStateRefresh(static_cast<TelescopeSlot>(cameraIndex));
    }
    setStatusMessage(QStringLiteral("相机 %1 温度预调焦移动失败：%2；等待电机停止")
                         .arg(cameraIndex + 1)
                         .arg(error),
                     UiStatusLevel::Warning);
}

void DIMM::cancelAutoAcquisitionPreFocus(const QString& reason)
{
    if (!m_autoAcquisitionPreFocus.active()) {
        return;
    }
    for (int camera = 0; camera < 2; ++camera) {
        if (m_autoAcquisitionPreFocusMoveIssued[camera] && m_focuserManager) {
            m_focuserManager->stopMotion(static_cast<TelescopeSlot>(camera));
        }
        m_autoAcquisitionPreFocusMoveIssued[camera] = false;
        setAutoFocusManualLock(camera, false);
    }
    m_autoAcquisitionPreFocus.cancel();
    setStatusMessage(QStringLiteral("自动采集温度预调焦已取消：%1").arg(reason),
                     UiStatusLevel::Warning);
}

void DIMM::resumeTrackingAutoFocusAfterPreFocus()
{
    if (!m_autoAcquisitionPreFocus.takeReady()) {
        return;
    }
    for (int camera = 0; camera < 2; ++camera) {
        m_autoAcquisitionPreFocusMoveIssued[camera] = false;
        setAutoFocusManualLock(camera, false);
    }
    if (m_captureState != CaptureState::Live ||
        m_liveStartupPhase != LiveStartupPhase::Tracking) {
        return;
    }
    if (m_focuserControlWidget) {
        m_focuserControlWidget->setAutoFocusTrackingAvailable(true);
    }
    if (m_autoFocusController && m_latestEnvironment.valid) {
        for (int camera = 0; camera < 2; ++camera) {
            m_autoFocusController->primeTemperatureBaseline(
                camera, m_latestEnvironment.temperatureC, true);
        }
    }
    if (m_focuserManager) {
        m_focuserManager->requestStateRefresh(TelescopeSlot::Telescope1);
        m_focuserManager->requestStateRefresh(TelescopeSlot::Telescope2);
    }
    startAutoFocusReferenceCalibration();
    startAutoFocusForAutoAcquisition();
}

void DIMM::applyPendingAutoFocusConfigIfIdle()
{
    if (!m_pendingAutoFocusConfig.has_value() || !m_autoFocusController ||
        m_autoFocusController->isActive(0) || m_autoFocusController->isActive(1)) {
        return;
    }
    applyAutoFocusConfig(*m_pendingAutoFocusConfig);
}

void DIMM::cancelAutoFocus(int cameraIndex, const QString& reason, bool stopFocuser)
{
    if (cameraIndex < 0 || cameraIndex >= 2 || !m_autoFocusController) {
        return;
    }
    const bool wasActive = m_autoFocusController->isActive(cameraIndex);
    const bool wasMainRun = m_autoFocusMainRecordActive[cameraIndex];
    if (wasActive) {
        logAutoFocusEvent(cameraIndex,
                          AutoFocusAction{},
                          QStringLiteral("cancelled: %1").arg(reason));
        if (m_autoFocusMainRecordActive[cameraIndex]) {
            writeResultSessionEvent(QStringLiteral("AutoFocusEnd"),
                                    QStringLiteral("camera=%1; reason=cancelled")
                                        .arg(cameraIndex + 1),
                                    QStringLiteral("Tracking"),
                                    QDateTime::currentMSecsSinceEpoch());
            m_autoFocusMainRecordActive[cameraIndex] = false;
        }
        m_autoFocusAwaitingPreExposure[cameraIndex] = false;
        m_autoFocusPreExposureComplete[cameraIndex] = false;
        m_deferredAutoFocusAction[cameraIndex] = {};
        m_deferredAutoFocusContext[cameraIndex].clear();
    }
    m_autoFocusController->cancel(cameraIndex);
    m_autoFocusAwaitingTimeoutStop[cameraIndex] = false;
    m_autoFocusTimeoutStopCommandFinished[cameraIndex] = false;
    if (wasMainRun) {
        m_autoFocusCompletionBarrier.reset();
    }
    if (stopFocuser && wasActive && m_focuserManager) {
        m_focuserManager->stopMotion(static_cast<TelescopeSlot>(cameraIndex));
    }
    setAutoFocusManualLock(cameraIndex, false);
    if (wasActive) {
        setStatusMessage(QStringLiteral("相机 %1 自动调焦已停止：%2")
                             .arg(cameraIndex + 1)
                             .arg(reason),
                         UiStatusLevel::Warning);
    }
}

void DIMM::stopAutoFocusForTrackingExit(const QString& reason)
{
    if (m_focuserControlWidget) {
        m_focuserControlWidget->setAutoFocusTrackingAvailable(false);
    }
    for (int camera = 0; camera < 2; ++camera) {
        cancelAutoFocus(camera, reason, true);
        m_autoFocusReconnectAfterMs[camera] = -1;
        m_autoFocusReferenceCalibrationStarted[camera] = false;
    }
    m_autoFocusCompletionBarrier.reset();
    if (m_autoAcquisitionPreFocus.blocksAutoFocus()) {
        for (int camera = 0; camera < 2; ++camera) {
            setAutoFocusManualLock(camera, true);
        }
    }
}

void DIMM::startAutoFocusReferenceCalibration()
{
    if (!isTrackingForAutoFocus() || !m_autoFocusController) {
        return;
    }
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    for (int camera = 0; camera < 2; ++camera) {
        if (!m_autoFocusConfig.masterEnabled || !m_autoFocusConfig.cameraEnabled[camera] ||
            m_autoFocusReferenceCalibrationStarted[camera] ||
            m_autoFocusController->isActive(camera) ||
            (!m_autoFocusConfig.autoCalibrateHfr[camera] &&
             !m_autoFocusConfig.autoCalibrateRms[camera])) {
            continue;
        }
        if (!m_autoFocusFocuserOpened[camera] || m_autoFocusFocuserMoving[camera]) {
            setStatusMessage(QStringLiteral("相机 %1 自动参考标定未启动：焦点器不可用或仍在移动")
                                 .arg(camera + 1),
                             UiStatusLevel::Warning);
            continue;
        }
        const AutoFocusAction action =
            m_autoFocusController->startReferenceCalibration(camera, nowMs);
        if (action.type == AutoFocusActionType::Failed) {
            setStatusMessage(QStringLiteral("相机 %1 自动参考标定未启动：设置无效")
                                 .arg(camera + 1),
                             UiStatusLevel::Warning);
            continue;
        }
        m_autoFocusReferenceCalibrationStarted[camera] = true;
        setAutoFocusManualLock(camera, true);
        setStatusMessage(QStringLiteral("相机 %1 自动调焦：等待稳定后标定参考值")
                             .arg(camera + 1),
                         UiStatusLevel::Warning);
    }
}

void DIMM::startManualAutoFocus()
{
    if (!isTrackingForAutoFocus() || !m_autoFocusController) {
        return;
    }
    if (!m_latestEnvironment.valid) {
        setStatusMessage(QStringLiteral("环境传感器无效，无法建立本轮自动调焦温度基准"),
                         UiStatusLevel::Warning);
        return;
    }
    for (int camera = 0; camera < 2; ++camera) {
        if (!m_autoFocusConfig.masterEnabled || !m_autoFocusConfig.cameraEnabled[camera] ||
            m_autoFocusController->isActive(camera)) {
            continue;
        }
        if (!m_autoFocusFocuserOpened[camera] || m_autoFocusFocuserMoving[camera]) {
            setStatusMessage(QStringLiteral("相机 %1 自动调焦未启动：焦点器不可用或仍在移动")
                                 .arg(camera + 1),
                             UiStatusLevel::Warning);
            continue;
        }
        handleAutoFocusAction(camera,
                              m_autoFocusController->start(
                                  camera,
                                  m_latestEnvironment.temperatureC,
                                  AutoFocusTrigger::Manual),
                              QStringLiteral("手动立即调焦"));
    }
}

void DIMM::startAutoFocusForAutoAcquisition()
{
    if (!isTrackingForAutoFocus() || !m_autoFocusController ||
        m_liveStartupOrigin != LiveStartupOrigin::AutoAcquisition) {
        return;
    }
    for (int camera = 0; camera < 2; ++camera) {
        if (!m_autoFocusStartupPending[camera] || m_autoFocusStartupTriggered[camera] ||
            !m_autoFocusConfig.masterEnabled || !m_autoFocusConfig.cameraEnabled[camera] ||
            m_autoFocusController->isActive(camera)) {
            continue;
        }
        if (m_autoFocusReferenceCalibrationStarted[camera] ||
            m_autoFocusController->state(camera) == AutoFocusRunState::ReferenceCalibration) {
            continue;
        }
        if (!m_autoFocusFocuserOpened[camera] || m_autoFocusFocuserMoving[camera]) {
            continue;
        }
        if (!m_latestEnvironment.valid) {
            continue;
        }
        const AutoFocusAction action = m_autoFocusController->start(
            camera,
            m_latestEnvironment.temperatureC,
            AutoFocusTrigger::AutoAcquisitionStartup);
        if (action.type == AutoFocusActionType::AwaitingInitialMetrics) {
            m_autoFocusStartupPending[camera] = false;
            m_autoFocusStartupTriggered[camera] = true;
            handleAutoFocusAction(camera, action, QStringLiteral("自动采集启动调焦"));
        }
    }
}

void DIMM::handleAutoFocusMeasurement(int cameraIndex,
                                      const cv::Mat& calculationImage,
                                      bool centroidValid,
                                      double centroidX,
                                      double centroidY)
{
    if (m_captureState != CaptureState::Live ||
        m_liveStartupPhase != LiveStartupPhase::Tracking ||
        cameraIndex < 0 || cameraIndex >= 2) {
        return;
    }

    AutoFocusRoiMeasurement measurement;
    measurement.calculationImage = calculationImage;
    measurement.centroidValid = centroidValid;
    measurement.centroidX = centroidX;
    measurement.centroidY = centroidY;
    const AutoFocusSample sample = AutoFocusMetricCalculator::calculate(measurement);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    m_autoFocusRealtimeMetricsSampler.submitSample(cameraIndex, sample, nowMs);
    m_latestAutoFocusSample[cameraIndex] = sample;
    m_hasLatestAutoFocusSample[cameraIndex] = sample.valid;

    if (!isTrackingForAutoFocus() || !m_autoFocusController ||
        m_autoFocusAwaitingPreExposure[cameraIndex]) {
        return;
    }

    if (!m_autoFocusController->isActive(cameraIndex)) {
        return;
    }
    const AutoFocusAction action =
        m_autoFocusController->submitSample(cameraIndex,
                                             sample,
                                             nowMs);
    if (action.metricsReady && action.type != AutoFocusActionType::Finished &&
        action.type != AutoFocusActionType::Failed) {
        logAutoFocusEvent(cameraIndex, AutoFocusAction{});
    }
    handleAutoFocusAction(cameraIndex, action, QStringLiteral("ROI 测量"));
}

void DIMM::handleAutoFocusFocuserState(int cameraIndex,
                                       bool opened,
                                       bool moving,
                                       int position)
{
    if (cameraIndex < 0 || cameraIndex >= 2 || !m_autoFocusController) {
        return;
    }
    const bool wasActive = m_autoFocusController->isActive(cameraIndex);
    m_autoFocusFocuserOpened[cameraIndex] = opened;
    m_autoFocusFocuserMoving[cameraIndex] = moving;
    m_autoFocusFocuserPosition[cameraIndex] = position;
    const bool timeoutStopConfirmed = m_autoFocusAwaitingTimeoutStop[cameraIndex] &&
                                      m_autoFocusTimeoutStopCommandFinished[cameraIndex] &&
                                      !moving;
    if (wasActive && !opened) {
        logAutoFocusEvent(cameraIndex,
                          AutoFocusAction{},
                          QStringLiteral("failed: focuser_disconnected"));
    }
    m_autoFocusController->updateFocuserState(
        cameraIndex, opened, moving, QDateTime::currentMSecsSinceEpoch());

    if (!opened) {
        const bool timeoutStopPending = m_autoFocusAwaitingTimeoutStop[cameraIndex];
        m_autoFocusAwaitingTimeoutStop[cameraIndex] = false;
        m_autoFocusTimeoutStopCommandFinished[cameraIndex] = false;
        m_autoFocusAwaitingPreExposure[cameraIndex] = false;
        m_autoFocusPreExposureComplete[cameraIndex] = false;
        m_deferredAutoFocusAction[cameraIndex] = {};
        m_deferredAutoFocusContext[cameraIndex].clear();
        if (wasActive) {
            if (m_autoFocusMainRecordActive[cameraIndex]) {
                writeResultSessionEvent(QStringLiteral("AutoFocusEnd"),
                                        QStringLiteral("camera=%1").arg(cameraIndex + 1),
                                        QStringLiteral("Tracking"),
                                        QDateTime::currentMSecsSinceEpoch());
                m_autoFocusMainRecordActive[cameraIndex] = false;
            }
            completeAutoFocusCycle(cameraIndex);
            setAutoFocusManualLock(cameraIndex, false);
            setStatusMessage(QStringLiteral("相机 %1 焦点器已断开，自动调焦已结束；Tracking 采集继续")
                                 .arg(cameraIndex + 1),
                             UiStatusLevel::Warning);
        } else if (timeoutStopPending) {
            handleAutoFocusAction(cameraIndex,
                                  {AutoFocusActionType::Failed, 0},
                                  QStringLiteral("调焦阶段超时后焦点器断开"));
        }
        if (isTrackingForAutoFocus() && m_autoFocusConfig.masterEnabled &&
            m_autoFocusConfig.cameraEnabled[cameraIndex]) {
            m_autoFocusReconnectAfterMs[cameraIndex] =
                QDateTime::currentMSecsSinceEpoch() + 10000;
        }
        return;
    }

    m_autoFocusReconnectAfterMs[cameraIndex] = -1;
    if (timeoutStopConfirmed) {
        m_autoFocusAwaitingTimeoutStop[cameraIndex] = false;
        m_autoFocusTimeoutStopCommandFinished[cameraIndex] = false;
        handleAutoFocusAction(cameraIndex,
                              {AutoFocusActionType::Failed, 0},
                              QStringLiteral("调焦阶段超时"));
        return;
    }
    if (isTrackingForAutoFocus()) {
        startAutoFocusReferenceCalibration();
        startAutoFocusForAutoAcquisition();
    }
    if (!m_autoFocusController->isActive(cameraIndex) && wasActive) {
        const AutoFocusRunSnapshot snapshot = m_autoFocusController->snapshot(cameraIndex);
        logAutoFocusEvent(cameraIndex,
                          AutoFocusAction{},
                          snapshot.unreachedReference ? QStringLiteral("unreached")
                                                       : QStringLiteral("failed"));
        if (m_autoFocusMainRecordActive[cameraIndex]) {
            writeResultSessionEvent(QStringLiteral("AutoFocusEnd"),
                                    QStringLiteral("camera=%1").arg(cameraIndex + 1),
                                    QStringLiteral("Tracking"),
                                    QDateTime::currentMSecsSinceEpoch());
            m_autoFocusMainRecordActive[cameraIndex] = false;
        }
        m_autoFocusPreExposureComplete[cameraIndex] = false;
        completeAutoFocusCycle(cameraIndex);
        setAutoFocusManualLock(cameraIndex, false);
        applyPendingAutoFocusConfigIfIdle();
    }
}

void DIMM::completeAutoFocusCycle(int cameraIndex)
{
    m_autoFocusCompletionBarrier.complete(cameraIndex);
    const std::uint8_t cameraMask = m_autoFocusCompletionBarrier.takeReadyMask();
    if (cameraMask != 0) {
        beginAutoExposureAdjustmentForAutoFocus(QStringLiteral("after_autofocus"),
                                                cameraMask);
    }
}

void DIMM::handleAutoFocusAction(int cameraIndex,
                                 const AutoFocusAction& action,
                                 const QString& context)
{
    if (cameraIndex < 0 || cameraIndex >= 2 || !m_autoFocusController) {
        return;
    }
    switch (action.type) {
    case AutoFocusActionType::None:
        return;
    case AutoFocusActionType::AwaitingInitialMetrics:
        if (!m_autoFocusMainRecordActive[cameraIndex]) {
            m_autoFocusMainRecordActive[cameraIndex] = true;
            m_autoFocusCompletionBarrier.begin(cameraIndex);
            QString trigger;
            switch (m_autoFocusController->trigger(cameraIndex)) {
            case AutoFocusTrigger::Manual:
                trigger = QStringLiteral("manual");
                break;
            case AutoFocusTrigger::Temperature:
                trigger = QStringLiteral("temperature");
                break;
            case AutoFocusTrigger::AutoAcquisitionStartup:
                trigger = QStringLiteral("auto_acquisition_startup");
                break;
            }
            QString reason = QStringLiteral("camera=%1; trigger=%2")
                                 .arg(cameraIndex + 1)
                                 .arg(trigger);
            if (!m_autoFocusConfig.dataLoggingEnabled) {
                reason += QStringLiteral("; process_log=disabled");
            }
            writeResultSessionEvent(QStringLiteral("AutoFocusStart"),
                                    reason,
                                    QStringLiteral("Tracking"),
                                    QDateTime::currentMSecsSinceEpoch());
            logAutoFocusEvent(cameraIndex, action);
        }
        if (m_autoExposureConfig.enabled &&
            !m_autoFocusPreExposureComplete[cameraIndex]) {
            m_autoFocusAwaitingPreExposure[cameraIndex] = true;
            m_deferredAutoFocusAction[cameraIndex] = action;
            m_deferredAutoFocusContext[cameraIndex] = context;
            beginAutoExposureAdjustmentForAutoFocus(QStringLiteral("before_autofocus"),
                                                    1 << cameraIndex);
            setAutoFocusManualLock(cameraIndex, true);
            return;
        }
        if (!m_autoFocusPreExposureComplete[cameraIndex]) {
            beginAutoExposureAdjustmentForAutoFocus(QStringLiteral("before_autofocus"),
                                                    1 << cameraIndex);
            m_autoFocusPreExposureComplete[cameraIndex] = true;
        }
        setAutoFocusManualLock(cameraIndex, true);
        setStatusMessage(QStringLiteral("相机 %1 自动调焦：曝光稳定后判定初始指标")
                             .arg(cameraIndex + 1),
                         UiStatusLevel::Warning);
        return;
    case AutoFocusActionType::MoveRelative:
        if (!m_autoFocusFocuserOpened[cameraIndex]) {
            cancelAutoFocus(cameraIndex,
                            QStringLiteral("焦点器不可用"),
                            false);
            return;
        }
        setAutoFocusManualLock(cameraIndex, true);
        logAutoFocusEvent(cameraIndex, action);
        if (!m_focuserManager) {
            cancelAutoFocus(cameraIndex, QStringLiteral("焦点器管理器不可用"), false);
            return;
        }
        m_autoFocusController->armStageTimeout(cameraIndex,
                                               QDateTime::currentMSecsSinceEpoch());
        m_focuserManager->moveRelativeForAutoFocus(static_cast<TelescopeSlot>(cameraIndex),
                                                    action.relativeStep);
        setStatusMessage(QStringLiteral("相机 %1 自动调焦：%2，移动 %3 步")
                             .arg(cameraIndex + 1)
                             .arg(autoFocusStateText(m_autoFocusController->state(cameraIndex)))
                             .arg(action.relativeStep),
                         UiStatusLevel::Warning);
        return;
    case AutoFocusActionType::ReferenceCalibrated: {
        m_autoFocusReferenceCalibrationStarted[cameraIndex] = false;
        m_autoFocusConfig.reference[cameraIndex] =
            m_autoFocusController->referenceMetrics(cameraIndex);
        {
            QSettings settings;
            AutoFocusSettings::save(settings, m_autoFocusConfig);
        }
        if (m_focuserControlWidget) {
            m_focuserControlWidget->setAutoFocusConfigUi(m_autoFocusConfig);
        }
        setAutoFocusManualLock(cameraIndex, false);
        setStatusMessage(QStringLiteral("相机 %1 自动调焦参考值已标定")
                             .arg(cameraIndex + 1),
                         UiStatusLevel::Success);
        applyPendingAutoFocusConfigIfIdle();
        startAutoFocusForAutoAcquisition();
        return;
    }
    case AutoFocusActionType::Finished: {
        m_autoFocusAwaitingTimeoutStop[cameraIndex] = false;
        m_autoFocusTimeoutStopCommandFinished[cameraIndex] = false;
        const AutoFocusCompletionKind completion =
            m_autoFocusController->snapshot(cameraIndex).completionKind;
        if (completion == AutoFocusCompletionKind::AlreadyWithinTolerance) {
            logAutoFocusEvent(cameraIndex, action, QStringLiteral("AlreadyWithinTolerance"));
            m_autoFocusPreExposureComplete[cameraIndex] = false;
            setAutoFocusManualLock(cameraIndex, false);
            if (m_autoFocusMainRecordActive[cameraIndex]) {
                writeResultSessionEvent(QStringLiteral("AutoFocusEnd"),
                                        QStringLiteral("camera=%1").arg(cameraIndex + 1),
                                        QStringLiteral("Tracking"),
                                        QDateTime::currentMSecsSinceEpoch());
                m_autoFocusMainRecordActive[cameraIndex] = false;
            }
            completeAutoFocusCycle(cameraIndex);
            setStatusMessage(QStringLiteral("相机 %1 初始指标在容差范围内，无需调焦（%2）")
                                 .arg(cameraIndex + 1)
                                 .arg(context),
                             UiStatusLevel::Success);
            applyPendingAutoFocusConfigIfIdle();
            return;
        }
        logAutoFocusEvent(cameraIndex,
                          action,
                          completion == AutoFocusCompletionKind::ReturnedToBest
                              ? QStringLiteral("ReturnedToBest")
                              : QStringLiteral("ReachedTargetBoundary"));
        setAutoFocusManualLock(cameraIndex, false);
        if (m_autoFocusMainRecordActive[cameraIndex]) {
            writeResultSessionEvent(QStringLiteral("AutoFocusEnd"),
                                    QStringLiteral("camera=%1").arg(cameraIndex + 1),
                                    QStringLiteral("Tracking"),
                                    QDateTime::currentMSecsSinceEpoch());
            m_autoFocusMainRecordActive[cameraIndex] = false;
        }
        m_autoFocusPreExposureComplete[cameraIndex] = false;
        completeAutoFocusCycle(cameraIndex);
        setStatusMessage(QStringLiteral("相机 %1 自动调焦完成（%2）")
                             .arg(cameraIndex + 1)
                             .arg(context),
                         UiStatusLevel::Success);
        applyPendingAutoFocusConfigIfIdle();
        return;
    }
    case AutoFocusActionType::Failed:
        m_autoFocusAwaitingTimeoutStop[cameraIndex] = false;
        m_autoFocusTimeoutStopCommandFinished[cameraIndex] = false;
        logAutoFocusEvent(cameraIndex,
                          action,
                          m_autoFocusController->snapshot(cameraIndex).unreachedReference
                              ? QStringLiteral("unreached")
                              : QStringLiteral("failed"));
        setAutoFocusManualLock(cameraIndex, false);
        if (m_autoFocusMainRecordActive[cameraIndex]) {
            writeResultSessionEvent(QStringLiteral("AutoFocusEnd"),
                                    QStringLiteral("camera=%1").arg(cameraIndex + 1),
                                    QStringLiteral("Tracking"),
                                    QDateTime::currentMSecsSinceEpoch());
            m_autoFocusMainRecordActive[cameraIndex] = false;
        }
        m_autoFocusPreExposureComplete[cameraIndex] = false;
        completeAutoFocusCycle(cameraIndex);
        setStatusMessage(QStringLiteral("相机 %1 自动调焦未达目标或失败（%2）")
                             .arg(cameraIndex + 1)
                             .arg(context),
                         UiStatusLevel::Warning);
        applyPendingAutoFocusConfigIfIdle();
        return;
    }
}

void DIMM::setupCentroidProcessorConnection()
{
    connect(m_imageProcessor,
            &ImageProcessor::centroidReady,
            this,
            [this](int camIdx,
                   double x,
                   double y,
                   double peakValue,
                   double totalFlux,
                   double background,
                   double noiseSigma,
                   double threshold,
                   quint64 signalPixelCount) {
        if (!hasActiveCapture()) {
            return;
        }
        if (camIdx < 0 || camIdx >= 2) {
            return;
        }
        if (m_captureState == CaptureState::Live &&
            m_liveStartupPhase != LiveStartupPhase::Tracking) {
            return;
        }
        auto& runtime = activeRuntime();
        const bool usable = isUsableCentroidSample(camIdx,
                                                   x,
                                                   y,
                                                   peakValue,
                                                   totalFlux,
                                                   background,
                                                   noiseSigma,
                                                   threshold,
                                                   signalPixelCount,
                                                   false);
        auto* label = camIdx == 0 ? ui->lblCam1ROICoord : ui->lblCam2ROICoord;
        label->setText(QStringLiteral("(%1, %2)").arg(x, 0, 'f', 1).arg(y, 0, 'f', 1));
        if (!usable) {
            runtime.hasValidCentroid[camIdx] = false;
            handleLiveRoiCentroidLoss(camIdx);
            return;
        }

        runtime.centroidX[camIdx] = x;
        runtime.centroidY[camIdx] = y;
        runtime.peakBrightness[camIdx] = peakValue;
        runtime.hasValidCentroid[camIdx] = true;
        runtime.lostCentroidFrameCount[camIdx] = 0;
        runtime.lostCentroidSinceMs[camIdx] = -1;
        if (m_captureState == CaptureState::Live && m_liveStartupPhase == LiveStartupPhase::Tracking) {
            runtime.lastTargetPosition[camIdx] = QPointF(x, y);
            runtime.hasLastTargetPosition[camIdx] = true;
        }
        if (m_captureState == CaptureState::Live &&
            m_liveStartupPhase == LiveStartupPhase::Tracking) {
            if (shouldUpdateRoiForRecentering()) {
                updateMinuteRoi(true);
            }
        }
    });

}

void DIMM::setupAutoExposureProcessorConnection()
{
    connect(m_imageProcessor,
            &ImageProcessor::autoExposureSampleReady,
            this,
            [this](int cameraIndex,
                   double peakValue,
                   double background,
                   double noiseSigma,
                   double threshold,
                   quint64 signalPixelCount,
                   quint64 saturatedPixelCount,
                   int peakQuality,
                   double supportedPeakValue,
                   quint64 peakSupportPixelCount,
                   double rejectedPeakValue,
                   int rejectedCandidateCount,
                   bool spotHardSaturated,
                   bool centroidValid,
                   bool measurementUsable,
                   bool decisionSample,
                   quint64 frameId,
                   qint64 timestampMs) {
        AutoExposureFrameSample sample;
        sample.cameraIndex = cameraIndex;
        sample.peakDn = peakValue;
        sample.supportedPeakDn = supportedPeakValue;
        sample.backgroundDn = background;
        sample.noiseSigmaDn = noiseSigma;
        sample.thresholdDn = threshold;
        sample.signalPixelCount = signalPixelCount;
        sample.saturatedPixelCount = saturatedPixelCount;
        sample.peakQuality = static_cast<AutoExposurePeakQuality>(peakQuality);
        sample.peakSupportPixelCount = peakSupportPixelCount;
        sample.rejectedPeakDn = rejectedPeakValue;
        sample.rejectedCandidateCount = rejectedCandidateCount;
        sample.spotHardSaturated = spotHardSaturated;
        sample.centroidValid = centroidValid;
        sample.measurementUsable = measurementUsable;
        sample.decisionSample = decisionSample;
        sample.frameId = frameId;
        sample.timestampMs = timestampMs;
        handleAutoExposureSample(sample);
    });

}

void DIMM::setupDifferentialSampleProcessorConnections()
{
    connect(m_imageProcessor,
            &ImageProcessor::differentialSampleReady,
            this,
            [this](quint64 pairedSampleCount, quint64 droppedUnpairedCount) {
        if (!hasActiveCapture()) {
            return;
        }
        auto& runtime = activeRuntime();
        runtime.pairedSampleCount = pairedSampleCount;
        runtime.droppedUnpairedSampleCount = droppedUnpairedCount;
        refreshMeasurementUi();
    });

    connect(m_imageProcessor,
            &ImageProcessor::differentialSampleDetailReady,
            this,
            [this](quint64 pairedSampleCount,
                   quint64 frameId1,
                   quint64 frameId2,
                   quint64 cameraTimestamp1,
                   quint64 cameraTimestamp2,
                   double centroid1X,
                   double centroid1Y,
                   double centroid2X,
                   double centroid2Y,
                   double longitudinal,
                   double transverse,
                   double syncResidualUs,
                   qint64 timestampMs) {
        if (!hasActiveCapture()) {
            return;
        }
        if (!m_parameterValidationEnabled) {
            return;
        }
        auto& runtime = activeRuntime();
        PairedCentroidDetail detail;
        detail.pairedSampleCount = pairedSampleCount;
        detail.frameId1 = frameId1;
        detail.frameId2 = frameId2;
        detail.cameraTimestamp1 = cameraTimestamp1;
        detail.cameraTimestamp2 = cameraTimestamp2;
        detail.centroid1X = centroid1X;
        detail.centroid1Y = centroid1Y;
        detail.centroid2X = centroid2X;
        detail.centroid2Y = centroid2Y;
        detail.longitudinal = longitudinal;
        detail.transverse = transverse;
        detail.syncResidualUs = syncResidualUs;
        detail.timestampMs = timestampMs;
        runtime.pendingPairedCentroidDetails.append(detail);
    });

    connect(m_imageProcessor,
            &ImageProcessor::unpairedSampleDropped,
            this,
            [this](int droppedCameraIndex,
                   quint64 cam0FrameId,
                   quint64 cam1FrameId,
                   qint64 frameIdOffset,
                   qint64 alignedFrameId0,
                   qint64 alignedFrameId1,
                   quint64 cam0Timestamp,
                   quint64 cam1Timestamp,
                   quint64 droppedUnpairedSamples) {
        if (!hasActiveCapture()) {
            return;
        }
        recordSyncUnpairedDropDiagnostic(droppedCameraIndex,
                                         cam0FrameId,
                                         cam1FrameId,
                                         frameIdOffset,
                                         alignedFrameId0,
                                         alignedFrameId1,
                                         cam0Timestamp,
                                         cam1Timestamp,
                                         droppedUnpairedSamples);
    });

}

void DIMM::setupFrameProcessedProcessorConnection()
{
    connect(m_imageProcessor, &ImageProcessor::frameProcessed, this, [this](int camIdx, bool centroidValid, double elapsedMs) {
        if (!hasActiveCapture()) {
            return;
        }
        if (camIdx < 0 || camIdx >= 2) {
            return;
        }
        if (m_captureState == CaptureState::Live &&
            m_liveStartupPhase != LiveStartupPhase::Tracking) {
            return;
        }
        auto& runtime = activeRuntime();
        ++runtime.processedFrameCount;
        ++runtime.processedFrameCountPerCamera[camIdx];
        if (centroidValid && runtime.hasValidCentroid[camIdx]) {
            ++runtime.validCentroidCount;
            ++runtime.validCentroidCountPerCamera[camIdx];
            runtime.lostCentroidFrameCount[camIdx] = 0;
            runtime.lostCentroidSinceMs[camIdx] = -1;
        } else if (!centroidValid) {
            runtime.hasValidCentroid[camIdx] = false;
            handleLiveRoiCentroidLoss(camIdx);
        }
        runtime.latestProcessingLatencyMs = elapsedMs;
        if (runtime.processedFrameCount == 1) {
            runtime.averageProcessingLatencyMs = elapsedMs;
        } else {
            runtime.averageProcessingLatencyMs +=
                (elapsedMs - runtime.averageProcessingLatencyMs) /
                static_cast<double>(runtime.processedFrameCount);
        }
    });

}

void DIMM::setupSyncSampleProcessorConnection()
{
    connect(m_imageProcessor, &ImageProcessor::syncSampleReady, this, [this](double syncResidualUs) {
        if (!hasActiveCapture()) {
            return;
        }
        auto& runtime = activeRuntime();
        ++runtime.syncSampleCount;

        runtime.latestSyncResidualUs = syncResidualUs;
        const double syncJitterUs = std::abs(syncResidualUs);
        ++runtime.syncJitterSampleCount;
        runtime.latestSyncJitterUs = syncJitterUs;
        runtime.maxSyncJitterUs = std::max(runtime.maxSyncJitterUs, syncJitterUs);
        if (runtime.syncJitterSampleCount == 1) {
            runtime.averageSyncJitterUs = syncJitterUs;
        } else {
            runtime.averageSyncJitterUs +=
                (syncJitterUs - runtime.averageSyncJitterUs) /
                static_cast<double>(runtime.syncJitterSampleCount);
        }
    });

}

void DIMM::setupRoiImageProcessorConnection()
{
    connect(m_imageProcessor, &ImageProcessor::roiImageReady, this, [this](int camIdx, cv::Mat roiImage) {
        if (!hasActiveCapture()) {
            return;
        }
        if (camIdx < 0 || camIdx >= 2) {
            return;
        }
        auto& runtime = activeRuntime();
        if (camIdx == 0) {
            m_cam1RoiCanvas->setRoiImage(roiImage);
            const RoiRect roi = m_imageProcessor ? m_imageProcessor->getCurrentRoi(0) : RoiRect();
            m_cam1RoiCanvas->setCentroid(runtime.centroidX[0] - roi.x,
                                         runtime.centroidY[0] - roi.y);
        } else if (camIdx == 1) {
            m_cam2RoiCanvas->setRoiImage(roiImage);
            const RoiRect roi = m_imageProcessor ? m_imageProcessor->getCurrentRoi(1) : RoiRect();
            m_cam2RoiCanvas->setCentroid(runtime.centroidX[1] - roi.x,
                                         runtime.centroidY[1] - roi.y);
        }
    });

}

void DIMM::setupAtmosphereProcessorConnection()
{
    connect(m_imageProcessor,
            &ImageProcessor::atmosphereReady,
            this,
            [this](double r0,
                   double seeing,
                   double theta0,
                   double tau0,
                   bool tau0Valid,
                   bool tau0UnderResolved,
                   double tau0ResolutionMs,
                   double longitudinalVariancePx2,
                   double transverseVariancePx2,
                   double longitudinalVarianceRad2,
                   double transverseVarianceRad2,
                   double r0LongitudinalCm,
                   double r0TransverseCm) {
        if (!hasActiveCapture()) {
            return;
        }
        auto& runtime = activeRuntime();
        runtime.hasValidAtmosphere = true;
        runtime.latestAtmosphereTimestampMs =
            static_cast<quint64>(QDateTime::currentMSecsSinceEpoch());
        runtime.latestAtmosphere.r0 = r0;
        runtime.latestAtmosphere.seeing = seeing;
        runtime.latestAtmosphere.theta0 = theta0;
        runtime.latestAtmosphere.tau0 = tau0;
        runtime.latestAtmosphere.tau0Valid = tau0Valid;
        runtime.latestAtmosphere.tau0UnderResolved = tau0UnderResolved;
        runtime.latestAtmosphere.tau0ResolutionMs = tau0ResolutionMs;
        runtime.latestAtmosphere.longitudinalVariancePx2 = longitudinalVariancePx2;
        runtime.latestAtmosphere.transverseVariancePx2 = transverseVariancePx2;
        runtime.latestAtmosphere.longitudinalVarianceRad2 = longitudinalVarianceRad2;
        runtime.latestAtmosphere.transverseVarianceRad2 = transverseVarianceRad2;
        runtime.latestAtmosphere.r0LongitudinalCm = r0LongitudinalCm;
        runtime.latestAtmosphere.r0TransverseCm = r0TransverseCm;
        refreshMeasurementUi();

        saveResultRow(runtime.frameCount);
    });

    connect(m_imageProcessor,
            &ImageProcessor::psdAnalysisReady,
            this,
            [this](const CdimPsdAnalysisResult& result) {
        if (!hasActiveCapture()) {
            return;
        }
        auto& runtime = activeRuntime();
        runtime.latestPsdAnalysis = result;
        runtime.hasPsdAnalysis = result.sampleCount > 0;
        runtime.latestAtmosphere.psdAnalysis = result;
        refreshPsdAnalysisUi(result);
    });

}

void DIMM::refreshPsdAnalysisUi(const CdimPsdAnalysisResult& result)
{
    if (!result.enabled) {
        if (m_longitudinalPsdChart) {
            m_longitudinalPsdChart->clear();
        }
        if (m_transversePsdChart) {
            m_transversePsdChart->clear();
        }
    } else {
        if (m_longitudinalPsdChart) {
            m_longitudinalPsdChart->setResult(result.longitudinal, result.fsActualHz);
        }
        if (m_transversePsdChart) {
            m_transversePsdChart->setResult(result.transverse, result.fsActualHz);
        }
    }
    if (!m_lblPsdSummary) {
        return;
    }
    const QString fsSource = result.fsSource == CdimPsdFsSource::Timestamp
                                 ? QStringLiteral("时间戳")
                                 : QStringLiteral("配置帧率回退");
    const QString correction = !result.enabled
                                   ? QStringLiteral("PSD 已关闭，r0 使用原始时域方差")
                                   : result.valid
                                         ? QStringLiteral("修正已启用")
                                         : QStringLiteral("修正无效，r0 使用原始时域方差");
    m_lblPsdSummary->setText(
        QStringLiteral("fs=%1 Hz（%2） Nyquist=%3 Hz | Δf=%4 Hz | 方差输入 px²，r0 输入 rad² | %5")
            .arg(result.fsActualHz, 0, 'f', 2)
            .arg(fsSource)
            .arg(result.nyquistHz, 0, 'f', 2)
            .arg(result.frequencyResolutionHz, 0, 'f', 3)
            .arg(correction));
    m_lblPsdSummary->setToolTip(result.warnings.join(QStringLiteral("\n")));
}

void DIMM::setupRuntimeTimers()
{
    m_1hzTimer = new QTimer(this);
    connect(m_1hzTimer, &QTimer::timeout, this, &DIMM::on1hzTick);
    m_1hzTimer->start(1000);

    m_hardwareTriggerStartupTimer = new QTimer(this);
    m_hardwareTriggerStartupTimer->setSingleShot(true);
    connect(m_hardwareTriggerStartupTimer, &QTimer::timeout, this, [this]() {
        checkHardwareTriggerStartup();
    });

    m_liveStartupRetryTimer = new QTimer(this);
    m_liveStartupRetryTimer->setSingleShot(true);

    connect(
        m_liveStartupRetryTimer,
        &QTimer::timeout,
        this,
        [this]() {
            retryFailedLiveStartup();
        });

    m_fileFlushTimer = new QTimer(this);
    connect(m_fileFlushTimer, &QTimer::timeout, this, &DIMM::flushPendingWrites);
    m_fileFlushTimer->start(2000);

}

void DIMM::setupCommConnections()
{
    m_commManager = new CommManager(this);
    connect(m_commManager, &CommManager::connected, this, [this]() {
        m_commConnecting = false;
        updateCommState(true);
        m_reporting = isLiveCaptureActive();
        if (m_reporting && m_reportTimer) {
            m_reportTimer->start();
        }
        setStatusMessage(QStringLiteral("上位机已连接"), UiStatusLevel::Success);
    });
    connect(m_commManager, &CommManager::disconnected, this, [this]() {
        m_commConnecting = false;
        updateCommState(false);
        m_reporting = false;
        if (m_reportTimer) {
            m_reportTimer->stop();
        }
        setStatusMessage(QStringLiteral("上位机已断开"), UiStatusLevel::Warning);
    });
    connect(m_commManager, &CommManager::connectionError, this, [this](const QString& msg) {
        m_commConnecting = false;
        updateCommState(false);
        m_reporting = false;
        if (m_reportTimer) {
            m_reportTimer->stop();
        }
        setStatusMessage(QStringLiteral("通信错误: %1").arg(msg), UiStatusLevel::Error);
    });
}

void DIMM::setupReportTimer()
{
    m_reportTimer = new QTimer(this);
    m_reportTimer->setInterval(1000);
    connect(m_reportTimer, &QTimer::timeout, this, &DIMM::reportMeasurement);

    m_startTimeMs = static_cast<uint32_t>(QDateTime::currentMSecsSinceEpoch());

}

DIMM::~DIMM()
{
    shutdownForExit();
}

void DIMM::closeEvent(QCloseEvent* event)
{
    shutdownForExit();
    event->accept();
}

void DIMM::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    if (!m_mainSplitterStartupLayoutApplied) {
        m_mainSplitterSizes.clear();
        m_mainSplitterLayoutPending = true;
    }
    QTimer::singleShot(0, this, [this]() {
        refreshPanelUi();
    });
}

void DIMM::shutdownForExit()
{
    if (m_shutdownCompleted) {
        return;
    }
    m_shutdownCompleted = true;
    if (m_focuserManager) {
        m_focuserManager->shutdown();
    }
    if (m_environmentSensor) {
        m_environmentSensor->stop();
    }
    if (m_reportTimer) {
        m_reportTimer->stop();
    }
    if (m_fileFlushTimer) {
        m_fileFlushTimer->stop();
    }
    if (m_1hzTimer) {
        m_1hzTimer->stop();
    }
    if (m_hardwareTriggerStartupTimer) {
        m_hardwareTriggerStartupTimer->stop();
    }

    if (m_liveStartupRetryTimer) {
        m_liveStartupRetryTimer->stop();
    }
    if (m_commManager) {
        m_commManager->disconnectFromHost();
    }
    if (m_polarisSolverController) {
        disconnect(m_polarisSolverController, nullptr, this, nullptr);
    }
    if (m_cameraManager && m_configTriggerMode != 0) {
        QString ignoredReason;
        setLiveHardwareTriggerLine(QString::fromLatin1(kPausedTriggerLine), &ignoredReason);
    }
    if (m_pulseGenerator) {
        m_pulseGenerator->disconnect();
    }
    if (m_cameraManager) {
        disconnect(m_cameraManager, nullptr, this, nullptr);
        m_cameraManager->stopAll();
        m_cameraManager->closeAll();
    }
    if (m_pulseGenerator) {
        delete m_pulseGenerator;
        m_pulseGenerator = nullptr;
    }
    closeResultFile(ResultSessionEndReason::Destruction);
    delete ui;
}

void DIMM::setupConnections()
{
    connect(ui->btnStart, &QAction::triggered, this, &DIMM::onStartCapture);
    connect(ui->btnStop, &QAction::triggered, this, &DIMM::onStopCapture);
    connect(ui->btnFullFrame, &QAction::triggered, this, &DIMM::onShowMainPage);
    connect(ui->btnSettings, &QAction::triggered, this, &DIMM::onShowSettings);
    connect(m_actionAlignmentMode, &QAction::triggered, this, &DIMM::onToggleAlignmentMode);
    connect(m_actionToggleCoarseAlignment,
            &QAction::triggered,
            this,
            &DIMM::onToggleCoarseAlignment);
    connect(m_actionConfirmCamera1Polaris,
            &QAction::triggered,
            this,
            &DIMM::onConfirmCamera1PolarisCandidate);
    connect(m_actionConfirmCamera2Polaris,
            &QAction::triggered,
            this,
            &DIMM::onConfirmCamera2PolarisCandidate);
    connect(m_actionConfirmAndStartCapture,
            &QAction::triggered,
            this,
            &DIMM::onConfirmAndStartCapture);
    connect(m_actionRetryCamera1PolarisSolve, &QAction::triggered, this, [this]() {
        requestAutomaticPolarisSolve(0, true);
    });
    connect(m_actionRetryCamera2PolarisSolve, &QAction::triggered, this, [this]() {
        requestAutomaticPolarisSolve(1, true);
    });
    connect(m_actionRetryBothPolarisSolve,
            &QAction::triggered,
            this,
            &DIMM::requestAutomaticPolarisSolveBoth);
    connect(ui->btnToggleROI, &QPushButton::clicked, this, &DIMM::onToggleRoiImages);
    connect(ui->btnToggleCharts, &QPushButton::clicked, this, &DIMM::onToggleCharts);

    connect(ui->actionSaveConfig, &QAction::triggered, this, &DIMM::onSaveConfig);
    connect(ui->actionLoadConfig, &QAction::triggered, this, &DIMM::onLoadConfig);
    connect(ui->actionExportData, &QAction::triggered, this, &DIMM::onExportData);
    connect(ui->actionExportReport, &QAction::triggered, this, &DIMM::onExportReport);
    connect(ui->actionExit, &QAction::triggered, this, &QMainWindow::close);
    connect(ui->actionConnectAll, &QAction::triggered, this, &DIMM::onConnectAll);
    connect(ui->actionDisconnectAll, &QAction::triggered, this, &DIMM::onDisconnectAll);
    connect(ui->actionCameraSettings, &QAction::triggered, this, &DIMM::onShowSettings);
    connect(ui->actionViewMain, &QAction::triggered, this, &DIMM::onShowMainPage);
    connect(ui->actionViewSettings, &QAction::triggered, this, &DIMM::onShowSettings);
    connect(ui->actionToggleROIImages, &QAction::triggered, this, &DIMM::onToggleRoiImages);
    connect(ui->actionToggleCharts, &QAction::triggered, this, &DIMM::onToggleCharts);
    connect(ui->actionTrajectoryCalc, &QAction::triggered, this, [this]() {
        QMessageBox::information(this, QStringLiteral("轨迹计算"), QStringLiteral("轨迹导入与预览功能将在后续版本中补充"));
    });
    connect(ui->actionAbout, &QAction::triggered, this, &DIMM::onAbout);

    connect(ui->btnImportTrajectory, &QPushButton::clicked, this, [this]() {
        const QString file = QFileDialog::getOpenFileName(
            this, QStringLiteral("导入轨迹文件"), QString(), QStringLiteral("文本文件 (*.txt *.csv)"));
        if (!file.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("导入轨迹文件"), QStringLiteral("已选择文件: %1").arg(file));
        }
    });

    auto* spaceShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(spaceShortcut, &QShortcut::activated, this, &DIMM::onStartCapture);

    auto* escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escShortcut, &QShortcut::activated, this, &DIMM::onStopCapture);

}

void DIMM::resetMeasurementState()
{
    auto& runtime = activeRuntime();

    // Preserve the target confirmed in alignment mode so the next live
    // full-frame localization can reacquire the same star. In contrast,
    // lastTargetPosition belongs to the previous live tracking session
    // and is intentionally reset below.
    const QPointF preservedConfirmedPolarisPosition[2] = {
        runtime.confirmedPolarisPosition[0],
        runtime.confirmedPolarisPosition[1]
    };
    const bool preservedHasConfirmedPolarisPosition[2] = {
        runtime.hasConfirmedPolarisPosition[0],
        runtime.hasConfirmedPolarisPosition[1]
    };
    runtime = CaptureRuntimeContext();
    runtime.confirmedPolarisPosition[0] = preservedConfirmedPolarisPosition[0];
    runtime.confirmedPolarisPosition[1] = preservedConfirmedPolarisPosition[1];
    runtime.hasConfirmedPolarisPosition[0] = preservedHasConfirmedPolarisPosition[0];
    runtime.hasConfirmedPolarisPosition[1] = preservedHasConfirmedPolarisPosition[1];
    runtime.hasLastTargetPosition[0] = false;
    runtime.hasLastTargetPosition[1] = false;
    runtime.lastTargetPosition[0] = QPointF();
    runtime.lastTargetPosition[1] = QPointF();
    runtime.selectedInitialCandidateIndex[0] = -1;
    runtime.selectedInitialCandidateIndex[1] = -1;
    runtime.pendingInitialCandidateSelectionRequired[0] = false;
    runtime.pendingInitialCandidateSelectionRequired[1] = false;
    clearStableCandidateTrackers();
    clearActualRoiTracks();
    if (m_fullFrameCanvas1) {
        m_fullFrameCanvas1->clearStarCandidateOverlays();
    }
    if (m_fullFrameCanvas2) {
        m_fullFrameCanvas2->clearStarCandidateOverlays();
    }
    m_resultRowsSeen = 0;
    m_autoFocusRealtimeMetricsSampler.reset();
    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex) {
        m_latestAutoFocusSample[cameraIndex] = AutoFocusSample{};
        m_hasLatestAutoFocusSample[cameraIndex] = false;
    }
    m_starTrackingState = StarTrackingState::Unknown;
    m_roiUpdateCount = 0;
    m_lastRoiUpdateMs = -1;
    m_lastRoiUpdateReason.clear();
    resetSyncDiagnostics();
    resetLiveFrameAcceptanceGates();
    resetAutoExposureState(false);
    if (m_r0Chart) {
        m_r0Chart->clear();
    }
    if (m_seeingChart) {
        m_seeingChart->clear();
    }
    if (m_longitudinalPsdChart) {
        m_longitudinalPsdChart->clear();
    }
    if (m_transversePsdChart) {
        m_transversePsdChart->clear();
    }
    if (m_lblPsdSummary) {
        m_lblPsdSummary->setText(QStringLiteral("等待完整 r0 窗口"));
        m_lblPsdSummary->setToolTip(QString());
    }
    advanceLiveAcquisitionGeneration();
    if (m_imageProcessor) {
        m_imageProcessor->resetAcquisitionStatistics();
    }
    ui->lblCam1ROICoord->setText(QStringLiteral("(0.0, 0.0)"));
    ui->lblCam2ROICoord->setText(QStringLiteral("(0.0, 0.0)"));
    refreshMeasurementUi();
}

void DIMM::updateCaptureState(CaptureState state)
{
    m_captureState = state;
    const bool focuserMotionAllowed =
        m_captureState == CaptureState::Idle ||
        m_captureState == CaptureState::Live ||
        m_captureState == CaptureState::Paused;
    const QString reason = focuserMotionAllowed
                               ? QString()
                               : QStringLiteral("对准模式中禁止移动焦点。请先退出对准模式");
    if (m_focuserManager) {
        m_focuserManager->setMotionAllowed(focuserMotionAllowed, reason);
    }
    if (m_focuserControlWidget) {
        m_focuserControlWidget->setMotionAllowed(focuserMotionAllowed, reason);
    }
    refreshUi();
}

void DIMM::updateCommState(bool connected)
{
    m_commConnected = connected;
    refreshStatusUi();
}

bool DIMM::isSettingsApplyAllowed() const
{
    return !m_connectingCameras;
}

bool DIMM::canStartLiveCapture(QString* reason) const
{
    if (m_connectingCameras) {
        if (reason) {
            *reason = QStringLiteral("相机正在连接中，请等待当前连接流程完成");
        }
        return false;
    }
    if (m_commConnecting) {
        if (reason) {
            *reason = QStringLiteral("网络通信正在连接中，请稍后再开始采集");
        }
        return false;
    }
    const int cameraCount = openCameraCount();
    if (cameraCount < 2) {
        if (reason) {
            *reason = cameraCount == 0
                          ? QStringLiteral("当前未连接相机。\n请先连接两台相机后再开始实时采集。")
                          : QStringLiteral("当前只连接了一台相机。\n请先确保两台相机都已连接后再开始实时采集。");
        }
        return false;
    }
    return true;
}

bool DIMM::ensureAutoAcquisitionCamerasReady(QString* reason)
{
    if (!m_cameraManager) {
        if (reason) {
            *reason = QStringLiteral("相机管理器未初始化");
        }
        return false;
    }
    if (m_connectingCameras) {
        if (reason) {
            *reason = QStringLiteral("相机正在连接中，请等待当前连接流程完成");
        }
        return false;
    }

    const AutoAcquisitionCameraLifecycleDecision decision =
        decideAutoAcquisitionCameraLifecycle(openCameraCount(), 2);
    if (!decision.needsOpenAll) {
        m_autoAcquisitionCameraLifecycleActive = true;
        return true;
    }

    m_connectingCameras = true;
    refreshActionStates();
    setAutoAcquisitionStatus(QStringLiteral("自动采集正在连接相机"),
                             UiStatusLevel::Warning,
                             QStringLiteral("auto-camera-connect"));
    const auto devices = m_cameraManager->enumerateDevices();
    const bool opened = !devices.isEmpty() && m_cameraManager->openAll();
    m_connectingCameras = false;
    refreshUi();

    if (!opened || openCameraCount() < 2) {
        if (reason) {
            *reason = devices.isEmpty()
                          ? QStringLiteral("自动采集未发现两台相机")
                          : QStringLiteral("自动采集自动连接相机失败：需要两台相机，当前已连接 %1 台")
                                .arg(openCameraCount());
        }
        m_autoAcquisitionCameraLifecycleActive = false;
        return false;
    }

    m_autoAcquisitionCameraLifecycleActive = true;
    return true;
}

void DIMM::closeAutoAcquisitionCameras()
{
    if (!m_autoAcquisitionCameraLifecycleActive || !m_cameraManager) {
        return;
    }

    m_cameraManager->closeAll();
    m_autoAcquisitionCameraLifecycleActive = false;
    refreshUi();
}

bool DIMM::canConnectOrDisconnectCameras(QString* reason) const
{
    if (hasActiveCapture()) {
        if (reason) {
            *reason = QStringLiteral("请先停止或暂停采集，再执行相机连接操作");
        }
        return false;
    }
    if (m_connectingCameras) {
        if (reason) {
            *reason = QStringLiteral("相机连接流程仍在进行中，请稍候");
        }
        return false;
    }
    return true;
}

DIMM::CaptureRuntimeContext& DIMM::activeRuntime()
{
    return m_liveRuntime;
}

const DIMM::CaptureRuntimeContext& DIMM::activeRuntime() const
{
    return m_liveRuntime;
}

bool DIMM::hasAnyOpenCamera() const
{
    return openCameraCount() > 0;
}

int DIMM::openCameraCount() const
{
    int count = 0;
    for (int i = 0; i < 2; ++i) {
        if (m_cameraManager && m_cameraManager->isOpen(i)) {
            ++count;
        }
    }
    return count;
}

bool DIMM::hasActiveCapture() const
{
    return m_captureState == CaptureState::Live ||
           m_captureState == CaptureState::Alignment;
}

bool DIMM::isLiveCaptureActive() const
{
    return m_captureState == CaptureState::Live;
}

bool DIMM::canReportMeasurements() const
{
    return m_commConnected && m_reporting && isLiveCaptureActive();
}

QString DIMM::captureModeName() const
{
    switch (m_captureState) {
    case CaptureState::Live:
        return QStringLiteral("live");
    case CaptureState::Paused:
        return QStringLiteral("paused");
    case CaptureState::Alignment:
        return QStringLiteral("alignment");
    case CaptureState::Idle:
    default:
        return QStringLiteral("idle");
    }
}

QString DIMM::captureModeLabel() const
{
    switch (m_captureState) {
    case CaptureState::Live:
        return QStringLiteral("实时采集");
    case CaptureState::Paused:
        return QStringLiteral("暂停");
    case CaptureState::Alignment:
        return QStringLiteral("对准模式");
    case CaptureState::Idle:
    default:
        return QStringLiteral("空闲");
    }
}

QString DIMM::resultSubdirectoryName() const
{
    switch (m_captureState) {
    case CaptureState::Live:
        return QStringLiteral("live");
    case CaptureState::Paused:
        return QStringLiteral("paused");
    case CaptureState::Alignment:
        return QStringLiteral("alignment");
    case CaptureState::Idle:
    default:
        return QStringLiteral("idle");
    }
}

bool DIMM::hasValidCentroidsForRoiUpdate() const
{
    auto& runtime = activeRuntime();
    return runtime.hasValidCentroid[0] && runtime.hasValidCentroid[1];
}

void DIMM::showMissingAlignmentFrameForSolve(int cameraIndex)
{
    setAlignmentSolveLabel(cameraIndex,
                           AlignmentUiPresenter::formatMissingFrameSolveLabel(),
                           UiStatusLevel::Warning);
    setStatusMessage(AlignmentUiPresenter::formatMissingFrameStatusMessage(cameraIndex),
                     UiStatusLevel::Warning);
}

void DIMM::showSubmittedAlignmentSolve(int cameraIndex, bool force)
{
    setAlignmentSolveLabel(cameraIndex,
                           AlignmentUiPresenter::formatSubmittedSolveLabel(force),
                           UiStatusLevel::Info);
}

bool DIMM::trackAlignmentPolarisLocally(int cameraIndex,
                                        const cv::Mat& frame,
                                        QPointF* trackedPosition,
                                        double* peakValue)
{
    if (!isValidCameraIndex(cameraIndex) ||
        frame.empty() ||
        !trackedPosition ||
        !m_liveRuntime.hasConfirmedPolarisPosition[cameraIndex]) {
        return false;
    }

    const AlignmentLocalTracker::CentroidDetector centroidDetector =
        [](const cv::Mat& roi, QPointF* centroid, double* peak) {
            return detectInitialStarCentroid(roi, centroid, peak) ||
                   detectInitialStarCentroidFast(roi, centroid, peak);
        };
    return AlignmentLocalTracker::trackFromConfirmedPosition(
        frame,
        m_liveRuntime.confirmedPolarisPosition[cameraIndex],
        &m_alignmentSession.camera(cameraIndex).solveRuntime,
        centroidDetector,
        trackedPosition,
        peakValue);
}

void DIMM::logPolarisSolveResult(const PolarisSolveResult& result) const
{
    qInfo().noquote() << AlignmentUiPresenter::formatPolarisSolveLogLine(result);
}

void DIMM::onPolarisSolveStatusChanged(int cameraIndex,
                                       PolarisSolveStatus status,
                                       QString message,
                                       quint64 generation)
{
    if (generation != m_alignmentSession.solveGeneration() ||
        m_captureState != CaptureState::Alignment ||
        !isValidCameraIndex(cameraIndex)) {
        return;
    }
    if (status == PolarisSolveStatus::ManualConfirmed) {
        setAlignmentSolveLabel(cameraIndex,
                               AlignmentUiPresenter::formatManualConfirmedSolveLabel(message),
                               UiStatusLevel::Success);
        setStatusMessage(AlignmentUiPresenter::formatManualConfirmedStatusMessage(cameraIndex, message),
                         UiStatusLevel::Success);
        return;
    }
    if (m_alignmentSession.camera(cameraIndex).solveRuntime.state == AlignmentSolveState::ManualOnly) {
        return;
    }
    if (status == PolarisSolveStatus::DetectingStars ||
        status == PolarisSolveStatus::MatchingCatalog) {
        setAlignmentSolveLabel(cameraIndex,
                               AlignmentUiPresenter::formatMatchingSolveLabel(message),
                               UiStatusLevel::Info);
        setStatusMessage(AlignmentUiPresenter::formatMatchingStatusMessage(cameraIndex, message),
                         UiStatusLevel::Info);
    }
}

InitialStarSelection DIMM::selectAlignmentInitialCandidate(
    int cameraIndex,
    const QVector<InitialStarCandidate>& candidates,
    bool manualSelectionRequested)
{
    auto& runtime = m_liveRuntime;
    AlignmentCandidateRuntimeAccess access;
    access.confirmedPolarisPosition = &runtime.confirmedPolarisPosition[cameraIndex];
    access.hasConfirmedPolarisPosition = &runtime.hasConfirmedPolarisPosition[cameraIndex];
    access.lastTargetPosition = &runtime.lastTargetPosition[cameraIndex];
    access.hasLastTargetPosition = &runtime.hasLastTargetPosition[cameraIndex];
    access.selectedInitialCandidateIndex = &runtime.selectedInitialCandidateIndex[cameraIndex];
    return AlignmentSession::selectInitialCandidate(access,
                                                    candidates,
                                                    manualSelectionRequested);
}

bool DIMM::handleManualAlignmentCandidatePrompt(
    int cameraIndex,
    FullFrameCanvas* targetCanvas,
    FullFrameCanvas::AlignmentOverlay* overlay,
    const QVector<InitialStarCandidate>& candidates,
    InitialStarSelection* selection)
{
    auto& runtime = m_liveRuntime;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 lastPromptMs = runtime.lastInitialCandidatePromptMs[cameraIndex];
    if (!AlignmentSession::shouldShowCandidatePrompt(lastPromptMs, nowMs)) {
        return true;
    }

    int chosenCandidateIndex = -1;
    m_alignmentSession.camera(cameraIndex).selectionRequested = false;
    if (!promptAlignmentCandidateSelection(cameraIndex, candidates, &chosenCandidateIndex)) {
        AlignmentSession::recordCandidatePromptCancelled(
            &runtime.lastInitialCandidatePromptMs[cameraIndex],
            nowMs);
        setStatusMessage(QStringLiteral("状态: 相机%1对准候选星点选择已取消，保留候选框等待确认")
                             .arg(cameraIndex + 1),
                         UiStatusLevel::Warning);
        if (targetCanvas && overlay) {
            targetCanvas->setAlignmentOverlay(*overlay);
        }
        return false;
    }

    AlignmentSession::recordCandidatePromptAccepted(
        &runtime.selectedInitialCandidateIndex[cameraIndex],
        &runtime.lastInitialCandidatePromptMs[cameraIndex],
        chosenCandidateIndex);
    if (targetCanvas) {
        targetCanvas->setStarCandidateOverlays(
            PolarisDetectionPipeline::buildCandidateOverlays(candidates, chosenCandidateIndex));
    }
    if (selection) {
        *selection = PolarisDetectionPipeline::selectFullFrameStarCandidate(
            candidates,
            runtime.selectedInitialCandidateIndex[cameraIndex],
            true);
        runtime.pendingInitialCandidateSelectionRequired[cameraIndex] =
            selection->requiresUserSelection;
    }
    return true;
}

void DIMM::applyAlignmentSelectedCandidate(
    int cameraIndex,
    FullFrameCanvas* targetCanvas,
    const QVector<InitialStarCandidate>& candidates,
    const InitialStarSelection& selection,
    bool manualSelectionRequested,
    QPointF* selectedStar)
{
    auto& runtime = m_liveRuntime;
    const QPointF star = selection.candidate.center;
    if (selectedStar) {
        *selectedStar = star;
    }
    AlignmentCandidateRuntimeAccess access;
    access.confirmedPolarisPosition = &runtime.confirmedPolarisPosition[cameraIndex];
    access.hasConfirmedPolarisPosition = &runtime.hasConfirmedPolarisPosition[cameraIndex];
    access.lastTargetPosition = &runtime.lastTargetPosition[cameraIndex];
    access.hasLastTargetPosition = &runtime.hasLastTargetPosition[cameraIndex];
    access.pendingInitialCandidateSelectionRequired =
        &runtime.pendingInitialCandidateSelectionRequired[cameraIndex];
    access.selectedInitialCandidateIndex = &runtime.selectedInitialCandidateIndex[cameraIndex];
    access.selectionRequested = &m_alignmentSession.camera(cameraIndex).selectionRequested;
    AlignmentSession::recordSelectedCandidate(access,
                                              star,
                                              selection.candidate.index);
    runtime.hasConfirmedPolarisPhase[cameraIndex] = false;
    runtime.confirmedPolarisPhaseRad[cameraIndex] = 0.0;
    runtime.confirmedPolarisTimeUtc[cameraIndex] = QDateTime();
    if (manualSelectionRequested) {
        applyManualAlignmentConfirmation(cameraIndex, star);
    }
    if (targetCanvas) {
        targetCanvas->setStarCandidateOverlays(
            PolarisDetectionPipeline::buildCandidateOverlays(
                candidates, selection.candidate.index));
    }
    refreshActionStates();
}

void DIMM::applyManualAlignmentConfirmation(int cameraIndex, const QPointF& star)
{
    m_alignmentSession.applyManualConfirmation(cameraIndex, star);
    if (m_polarisSolverController) {
        m_polarisSolverController->cancelCamera(cameraIndex, m_alignmentSession.solveGeneration());
    }
    const QString manualConfirmedMessage =
        AlignmentSession::manualConfirmedMessage(star);
    onPolarisSolveStatusChanged(cameraIndex,
                                PolarisSolveStatus::ManualConfirmed,
                                manualConfirmedMessage,
                                m_alignmentSession.solveGeneration());
    setAlignmentSolveLabel(cameraIndex,
                           AlignmentUiPresenter::formatManualConfirmedSolveLabel(manualConfirmedMessage),
                           UiStatusLevel::Success);
}

void DIMM::updateConfirmedPolarisFromFallbackCentroid(int cameraIndex,
                                                      const cv::Mat& frame,
                                                      bool allowGuiCandidateDetection,
                                                      cv::Mat* mono8,
                                                      QPointF* star,
                                                      double* peakValue)
{
    auto& runtime = m_liveRuntime;
    if (mono8 && mono8->empty() && allowGuiCandidateDetection) {
        cv::Mat grayscale;
        if (frame.channels() == 1) {
            grayscale = frame;
        } else {
            cv::cvtColor(frame, grayscale, cv::COLOR_BGR2GRAY);
        }
        *mono8 = grayscale;
    }
    AlignmentCandidateRuntimeAccess access;
    access.confirmedPolarisPosition = &runtime.confirmedPolarisPosition[cameraIndex];
    access.hasConfirmedPolarisPosition = &runtime.hasConfirmedPolarisPosition[cameraIndex];
    access.lastTargetPosition = &runtime.lastTargetPosition[cameraIndex];
    access.hasLastTargetPosition = &runtime.hasLastTargetPosition[cameraIndex];
    access.selectedInitialCandidateIndex = &runtime.selectedInitialCandidateIndex[cameraIndex];
    const AlignmentSession::CentroidDetector centroidDetector =
        [](const cv::Mat& image, QPointF* centroid, double* peak) {
            return detectInitialStarCentroid(image, centroid, peak) ||
                   detectInitialStarCentroidFast(image, centroid, peak);
        };
    AlignmentSession::updateFromFallbackCentroid(access,
                                                 frame,
                                                 allowGuiCandidateDetection,
                                                 mono8,
                                                 star,
                                                 peakValue,
                                                 centroidDetector);
}

void DIMM::evaluateAutoAcquisitionSchedule()
{
    if (!m_autoAcquisitionConfig.enabled) {
        m_autoAcquisitionRecovery.reset();
        return;
    }

    const QDateTime now = QDateTime::currentDateTime();
    const AutoAcquisitionWindow window =
        AutoAcquisitionScheduler::resolveWindow(m_autoAcquisitionConfig, now);
    if (!window.valid) {
        setAutoAcquisitionStatus(QStringLiteral("自动采集计划不可用: %1").arg(window.errorMessage),
                                 UiStatusLevel::Warning,
                                 QStringLiteral("invalid-window"));
        return;
    }

    if (!m_autoAcquisitionSuppressedWindowId.isEmpty() &&
        m_autoAcquisitionSuppressedWindowId != window.windowId) {
        m_autoAcquisitionSuppressedWindowId.clear();
    }

    const bool insideWindow = AutoAcquisitionScheduler::contains(window, now);
    const qint64 nowMs = now.toMSecsSinceEpoch();
    if (!insideWindow) {
        if (m_autoAcquisitionStartedCurrentRun &&
            (m_captureState == CaptureState::Live ||
             m_captureState == CaptureState::Paused)) {
            m_autoAcquisitionCommandInProgress = true;
            onStopCapture();
            m_autoAcquisitionCommandInProgress = false;
            m_autoAcquisitionStartedCurrentRun = false;
            m_autoAcquisitionActiveWindowId.clear();
            setAutoAcquisitionStatus(QStringLiteral("自动采集已按计划停止"),
                                     UiStatusLevel::Success,
                                     QStringLiteral("auto-stop"));
        }
        if (m_autoAcquisitionActiveWindowId != window.windowId) {
            m_autoAcquisitionActiveWindowId.clear();
        }
        m_autoAcquisitionRecovery.leaveWindow();
        clearStableCandidateTrackers();
        return;
    }

    if (m_autoAcquisitionRecovery.windowId() != window.windowId) {
        clearStableCandidateTrackers();
        clearActualRoiTracks();
    }
    m_autoAcquisitionRecovery.enterWindow(window.windowId, nowMs);

    if (m_captureState == CaptureState::Live) {
        return;
    }

    if (m_autoAcquisitionSuppressedWindowId == window.windowId) {
        setAutoAcquisitionStatus(QStringLiteral("自动采集本窗口已被手动停止，等待下一观测窗口"),
                                 UiStatusLevel::Warning,
                                 QStringLiteral("suppressed-window"));
        return;
    }

    if (m_captureState == CaptureState::Alignment) {
        setAutoAcquisitionStatus(QStringLiteral("自动采集等待当前模式结束"),
                                 UiStatusLevel::Warning,
                                 QStringLiteral("blocked-mode"));
        return;
    }

    if (!m_autoAcquisitionRecovery.shouldAttemptScan(
            window.windowId,
            nowMs,
            m_autoAcquisitionConfig.recoveryScanIntervalMinutes)) {
        return;
    }

    QString reason;
    if (!ensureAutoAcquisitionCamerasReady(&reason)) {
        setAutoAcquisitionStatus(reason.isEmpty()
                                     ? QStringLiteral("自动采集等待相机连接")
                                     : QStringLiteral("自动采集等待: %1").arg(reason),
                                 UiStatusLevel::Warning,
                                 QStringLiteral("waiting-camera-connect"));
        return;
    }
    if (!canStartLiveCapture(&reason)) {
        setAutoAcquisitionStatus(reason.isEmpty()
                                     ? QStringLiteral("自动采集等待相机连接")
                                     : QStringLiteral("自动采集等待: %1").arg(reason),
                                 UiStatusLevel::Warning,
                                 QStringLiteral("waiting-start-readiness"));
        return;
    }
    if (!startFullFrameLocalizationPulse(&reason)) {
        setAutoAcquisitionStatus(reason.isEmpty()
                                     ? QStringLiteral("自动采集等待触发输出")
                                     : QStringLiteral("自动采集等待触发输出: %1").arg(reason),
                                 UiStatusLevel::Warning,
                                 QStringLiteral("waiting-trigger-output"));
        return;
    }

    resetAutoExposureState(true);

    for (int camera = 0; camera < 2; ++camera) {
        m_autoAcquisitionPreFocusOpenRequestedByConfig[camera] = false;
    }
    if (!m_autoFocusConfig.masterEnabled) {
        AutoFocusConfig enabledConfig = m_autoFocusConfig;
        enabledConfig.masterEnabled = true;
        for (int camera = 0; camera < 2; ++camera) {
            m_autoAcquisitionPreFocusOpenRequestedByConfig[camera] =
                enabledConfig.cameraEnabled[camera] && !m_autoFocusFocuserOpened[camera];
        }
        applyAutoFocusConfig(enabledConfig);
        QSettings settings;
        AutoFocusSettings::save(settings, m_autoFocusConfig);
        settings.sync();
        if (m_focuserControlWidget) {
            m_focuserControlWidget->setAutoFocusConfigUi(m_autoFocusConfig);
        }
        setStatusMessage(QStringLiteral("自动采集已启用并保存自动调焦总开关"),
                         UiStatusLevel::Success);
    }

    m_liveStartupOrigin =
        LiveStartupOrigin::AutoAcquisition;

    m_liveStartupWindowId =
        window.windowId;

    m_liveStartupRetryCount = 0;
    m_liveStartupConfirmed = false;
    m_liveStartupRecoveryInProgress = false;
    m_pulseBoardResponseTimedOut = false;

    cancelAutoAcquisitionPreFocus(QStringLiteral("新的自动采集启动"));
    beginAutoAcquisitionPreFocus();

    m_autoAcquisitionRecovery.noteScanStarted(window.windowId, nowMs);
    m_lastAutoAcquisitionAttemptMs = nowMs;

    m_autoAcquisitionCommandInProgress = true;
    onStartCapture();
    m_autoAcquisitionCommandInProgress = false;

    if (m_captureState == CaptureState::Live) {
        m_autoAcquisitionStartedCurrentRun = true;
        m_autoAcquisitionActiveWindowId = window.windowId;
        m_autoAcquisitionRecovery.noteTrackingStarted(window.windowId);

        if (m_configTriggerMode == 0) {
            setAutoAcquisitionStatus(
                QStringLiteral("自动采集已按计划启动"),
                UiStatusLevel::Success,
                QStringLiteral("auto-start"));
        } else {
            setAutoAcquisitionStatus(
                QStringLiteral("自动采集启动流程已发起，等待全画幅和 ROI 高频触发确认"),
                UiStatusLevel::Warning,
                QStringLiteral("auto-start-pending"));
        }
    } else if (m_liveStartupOrigin == LiveStartupOrigin::AutoAcquisition) {
        cancelAutoAcquisitionPreFocus(QStringLiteral("自动采集启动失败"));
        closeAutoAcquisitionCameras();
    }
}

void DIMM::setAutoAcquisitionStatus(const QString& text,
                                    UiStatusLevel level,
                                    const QString& throttleKey)
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (!throttleKey.isEmpty() &&
        m_lastAutoAcquisitionStatusKey == throttleKey &&
        m_lastAutoAcquisitionStatusMs >= 0 &&
        nowMs - m_lastAutoAcquisitionStatusMs < 60000) {
        return;
    }
    m_lastAutoAcquisitionStatusKey = throttleKey;
    m_lastAutoAcquisitionStatusMs = nowMs;
    setStatusMessage(text, level);
}

void DIMM::prepareAutoAcquisitionRelocalization(const QString& reason,
                                                bool manualSelectionRequired)
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool continuousSearch = !manualSelectionRequired;
    if (manualSelectionRequired) {
        m_autoAcquisitionRecovery.noteManualSelectionRequired(nowMs);
    } else {
        m_autoAcquisitionRecovery.noteScanFoundNoStar(nowMs);
    }

    if (continuousSearch && m_captureState == CaptureState::Live) {
        auto& runtime = activeRuntime();
        runtime.liveRelocalizationStartedMs = nowMs;
        ++m_searchAttempt;
        m_searchAttemptStartedMs = nowMs;
        writeSearchLifecycleEventsOnce(reason,
                                      QStringLiteral("search_relocalization"),
                                      nowMs);
        runtime.pendingInitialRoiReady[0] = false;
        runtime.pendingInitialRoiReady[1] = false;
        m_liveStartupOrigin = LiveStartupOrigin::AutoAcquisition;
        stopAutoFocusForTrackingExit(QStringLiteral("自动采集正在重新定位"));
        m_liveStartupPhase = LiveStartupPhase::LocatePair;
        m_liveHardwareRoiActive = false;
        resetLiveFrameAcceptanceGates();
        QString switchReason;
        const bool fullFrameReady = applyLiveFullFrameForRelocalization(&switchReason);
        if (!fullFrameReady && m_resultSessionActive && m_resultWriter.isOpen()) {
            writeHardwareErrorEvent(QStringLiteral("camera"),
                                    -1,
                                    0,
                                    QStringLiteral("search_full_frame_switch"),
                                    switchReason,
                                    true,
                                    nowMs);
        }
        if (fullFrameReady || m_configTriggerMode == 0) {
            setAutoAcquisitionStatus(
                reason + QStringLiteral("；连续搜索保持采集并重新等待全画幅"),
                UiStatusLevel::Info,
                QStringLiteral("auto-continuous-retry"));
        } else {
            handleHardwareTriggerStartupFailure(
                switchReason.isEmpty() ? reason : switchReason);
        }
        return;
    }

    auto& runtime = activeRuntime();
    runtime.liveRelocalizationStartedMs = -1;
    runtime.pendingInitialRoiReady[0] = false;
    runtime.pendingInitialRoiReady[1] = false;
    m_liveStartupOrigin = LiveStartupOrigin::AutoAcquisition;
    m_liveHardwareRoiActive = false;
    stopAutoFocusForTrackingExit(QStringLiteral("自动采集正在重新定位"));
    m_liveStartupPhase = LiveStartupPhase::LocatePair;
    ++m_searchAttempt;
    m_searchAttemptStartedMs = nowMs;
    writeSearchLifecycleEventsOnce(reason,
                                  QStringLiteral("search_relocalization"),
                                  nowMs);
    resetLiveFrameAcceptanceGates();

    QString switchReason;
    const bool fullFrameReady = applyLiveFullFrameForRelocalization(&switchReason);
    if (!fullFrameReady && m_configTriggerMode != 0) {
        writeHardwareErrorEvent(QStringLiteral("camera_or_trigger"),
                                -1,
                                0,
                                QStringLiteral("search_full_frame_switch"),
                                switchReason,
                                true,
                                nowMs);
        setAutoAcquisitionStatus(
            switchReason.isEmpty() ? QStringLiteral("找星切换全画幅失败") : switchReason,
            UiStatusLevel::Error,
            QStringLiteral("auto-recovery-full-frame-failed"));
        return;
    }

    setAutoAcquisitionStatus(reason,
                             manualSelectionRequired ? UiStatusLevel::Warning
                                                     : UiStatusLevel::Info,
                             manualSelectionRequired
                                 ? QStringLiteral("auto-manual-selection-hold")
                             : QStringLiteral("auto-relocalization"));
}

void DIMM::noteManualAutoAcquisitionStopIfNeeded()
{
    if (m_autoAcquisitionCommandInProgress) {
        return;
    }

    QString suppressedWindowId;
    bool shouldSuppress = false;

    if (m_autoAcquisitionStartedCurrentRun &&
        !m_autoAcquisitionActiveWindowId.isEmpty()) {
        suppressedWindowId = m_autoAcquisitionActiveWindowId;
        shouldSuppress = true;
    } else if (m_autoAcquisitionConfig.enabled) {
        const QDateTime now = QDateTime::currentDateTime();
        const AutoAcquisitionWindow window =
            AutoAcquisitionScheduler::resolveWindow(
                m_autoAcquisitionConfig,
                now);
        if (window.valid &&
            AutoAcquisitionScheduler::contains(window, now)) {
            suppressedWindowId = window.windowId;
            shouldSuppress = true;
        }
    }

    if (!shouldSuppress || suppressedWindowId.isEmpty()) {
        return;
    }

    m_autoAcquisitionSuppressedWindowId = suppressedWindowId;
    m_autoAcquisitionRecovery.noteManualStop();
    m_autoAcquisitionStartedCurrentRun = false;
    m_autoAcquisitionActiveWindowId.clear();
    setAutoAcquisitionStatus(QStringLiteral("自动采集已手动停止，本观测窗口不再自动重启"),
                             UiStatusLevel::Warning,
                             QStringLiteral("manual-stop-suppression"));
}

bool DIMM::shouldRetryFailedLiveStartup() const
{
    if (m_liveStartupOrigin ==
        LiveStartupOrigin::Manual) {
        return true;
    }

    if (!m_autoAcquisitionConfig.enabled) {
        return false;
    }

    const QDateTime now =
        QDateTime::currentDateTime();

    const AutoAcquisitionWindow window =
        AutoAcquisitionScheduler::resolveWindow(
            m_autoAcquisitionConfig,
            now);

    if (!window.valid ||
        !AutoAcquisitionScheduler::contains(
            window,
            now)) {
        return false;
    }

    if (window.windowId !=
        m_liveStartupWindowId) {
        return false;
    }

    if (m_autoAcquisitionSuppressedWindowId ==
        window.windowId) {
        return false;
    }

    return true;
}

void DIMM::handleHardwareTriggerStartupFailure(
    const QString& detail)
{
    if (m_liveStartupRecoveryInProgress) {
        return;
    }

    m_hardwareTriggerStartupStage =
        HardwareTriggerStartupStage::None;

    for (int cameraIndex = 0;
         cameraIndex < 2;
         ++cameraIndex) {
        m_hardwareTriggerStageFrameSeen[cameraIndex] = false;
    }

    m_liveStartupRecoveryInProgress = true;
    m_liveStartupConfirmed = false;
    m_pulseBoardResponseTimedOut = false;

    if (m_hardwareTriggerStartupTimer) {
        m_hardwareTriggerStartupTimer->stop();
    }

    const bool previousCommandState =
        m_autoAcquisitionCommandInProgress;

    const bool retryAllowed =
        m_liveStartupRetryCount <
            kLiveStartupMaxImmediateRetries &&
        shouldRetryFailedLiveStartup();
    if (m_resultSessionActive && m_resultWriter.isOpen()) {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        writeHardwareErrorEvent(QStringLiteral("trigger"),
                                 -1,
                                 0,
                                 QStringLiteral("live_startup"),
                                 detail,
                                 retryAllowed,
                                 nowMs);
    }

    /*
     * 内部故障恢复不属于用户手动停止。
     * 临时置为 true，阻止当前观测窗口被标记为人工停止。
     * noteManualAutoAcquisitionStopIfNeeded()
     * 写入 suppressedWindowId。
     */
    m_autoAcquisitionCommandInProgress = true;

    stopLiveCapture();

    m_reporting = false;
    if (m_reportTimer) {
        m_reportTimer->stop();
    }

    if (retryAllowed && m_resultSessionActive && m_resultWriter.isOpen()) {
        writeAcquisitionPauseEvent(QStringLiteral("hardware_trigger_startup_failure"),
                                   QDateTime::currentMSecsSinceEpoch(),
                                   currentDeviceStatusForResultLog(
                                       QDateTime::currentMSecsSinceEpoch()));
        updateCaptureState(CaptureState::Paused);
    } else {
        closeResultSessionForHardwareError();
        updateCaptureState(CaptureState::Idle);
        if (m_liveStartupOrigin == LiveStartupOrigin::AutoAcquisition) {
            closeAutoAcquisitionCameras();
        }
    }
    resetMeasurementState();

    m_autoAcquisitionCommandInProgress =
        previousCommandState;

    if (!retryAllowed) {
        m_liveStartupRecoveryInProgress = false;

        if (m_liveStartupOrigin ==
            LiveStartupOrigin::AutoAcquisition) {
            /*
             * 允许现有 1 分钟调度器稍后再次尝试。
             */
            m_autoAcquisitionStartedCurrentRun = false;
            m_autoAcquisitionActiveWindowId.clear();
            m_lastAutoAcquisitionAttemptMs =
                QDateTime::currentMSecsSinceEpoch();

            setAutoAcquisitionStatus(
                QStringLiteral(
                    "自动采集硬件触发连续启动失败，已安全停止，稍后重新尝试"),
                UiStatusLevel::Error,
                QStringLiteral(
                    "auto-start-retry-exhausted"));
        } else {
            setStatusMessage(
                QStringLiteral(
                    "硬件触发启动失败，已安全停止: %1")
                    .arg(detail),
                UiStatusLevel::Error);
        }

        return;
    }

    ++m_liveStartupRetryCount;

    setStatusMessage(
        QStringLiteral(
            "硬件触发启动失败，已自动停止。\n"
            "%1 秒后进行第 %2/%3 次重试。原因: %4")
            .arg(kLiveStartupRetryDelayMs / 1000)
            .arg(m_liveStartupRetryCount)
            .arg(kLiveStartupMaxImmediateRetries)
            .arg(detail),
        UiStatusLevel::Warning);

    if (m_liveStartupRetryTimer) {
        m_liveStartupRetryTimer->start(
            kLiveStartupRetryDelayMs);
    } else {
        m_liveStartupRecoveryInProgress = false;
    }
}

void DIMM::retryFailedLiveStartup()
{
    if (!m_liveStartupRecoveryInProgress) {
        return;
    }

    if (!shouldRetryFailedLiveStartup()) {
        m_liveStartupRecoveryInProgress = false;

        setStatusMessage(
            QStringLiteral(
                "硬件触发自动重试已取消"),
            UiStatusLevel::Warning);

        return;
    }

    const LiveStartupOrigin startupOrigin =
        m_liveStartupOrigin;

    const QString startupWindowId =
        m_liveStartupWindowId;

    m_liveStartupRecoveryInProgress = false;
    m_liveStartupConfirmed = false;
    m_pulseBoardResponseTimedOut = false;

    /*
     * 防止 onStartCapture() 把自动重试覆盖为手动启动。
     */
    const bool automatic =
        startupOrigin ==
        LiveStartupOrigin::AutoAcquisition;

    const bool previousAutoCommandState =
        m_autoAcquisitionCommandInProgress;

    const bool previousInternalRetryState =
        m_internalLiveStartupRetry;

    m_autoAcquisitionCommandInProgress =
        automatic;

    m_internalLiveStartupRetry = true;

    m_liveStartupOrigin = startupOrigin;
    m_liveStartupWindowId = startupWindowId;

    onStartCapture();

    m_internalLiveStartupRetry =
        previousInternalRetryState;

    m_autoAcquisitionCommandInProgress =
        previousAutoCommandState;

    /*
     * onStartCapture() 可能立即失败并回到 Idle。
     * 此时继续进入统一失败恢复。
     */
    if (m_captureState != CaptureState::Live) {
        handleHardwareTriggerStartupFailure(
            QStringLiteral(
                "重新启动采集流程失败"));
    }
}

bool DIMM::isPulseBoardResponseTimeout(const QString& reason) const
{
    return reason.contains(QStringLiteral("Timed out waiting for pulse-board response."),
                           Qt::CaseInsensitive);
}

void DIMM::setPulseBoardResponseTimeoutStatus(const QString& text,
                                              UiStatusLevel level)
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_lastPulseBoardTimeoutStatusMs >= 0 &&
        nowMs - m_lastPulseBoardTimeoutStatusMs <
            kPulseBoardTimeoutStatusThrottleMs) {
        return;
    }

    m_lastPulseBoardTimeoutStatusMs = nowMs;
    setStatusMessage(text, level);
}

void DIMM::onStartCapture()
{
    if (!m_autoAcquisitionCommandInProgress &&
        !m_internalLiveStartupRetry &&
        !m_liveStartupRecoveryInProgress &&
        m_captureState != CaptureState::Live) {
        m_liveStartupOrigin =
            LiveStartupOrigin::Manual;

        cancelAutoAcquisitionPreFocus(QStringLiteral("手动采集启动"));

        m_liveStartupWindowId.clear();
        m_liveStartupRetryCount = 0;
        m_liveStartupConfirmed = false;
        m_pulseBoardResponseTimedOut = false;
        m_lastPulseBoardTimeoutStatusMs = -1;

        if (m_liveStartupRetryTimer) {
            m_liveStartupRetryTimer->stop();
        }
    }

    if (m_captureState == CaptureState::Alignment) {
        const QString message = QStringLiteral("请先退出对准模式，再开始正式采集。");
        QMessageBox::warning(this, QStringLiteral("开始采集"), message);
        setStatusMessage(QStringLiteral("状态: 请先退出对准模式"), UiStatusLevel::Warning);
        return;
    }

    if (m_captureState == CaptureState::Live) {
        resetLiveStartupRecoveryState(true);
        cancelAutoAcquisitionPreFocus(QStringLiteral("采集暂停"));

        noteManualAutoAcquisitionStopIfNeeded();
        if (m_resultSessionActive && m_resultWriter.isOpen()) {
            const qint64 pausedAtMs = QDateTime::currentMSecsSinceEpoch();
            writeAcquisitionPauseEvent(QStringLiteral("manual_pause"),
                                       pausedAtMs,
                                       currentDeviceStatusForResultLog(pausedAtMs));
        }
        stopLiveCapture();
        updateCaptureState(CaptureState::Paused);
        setStatusMessage(QStringLiteral("状态: 已暂停"), UiStatusLevel::Warning);
        return;
    }

    const bool resumeExistingResultSession =
        m_captureState == CaptureState::Paused && m_resultWriter.isOpen();

    QString reason;
    if (!canStartLiveCapture(&reason)) {
        QMessageBox::warning(this, QStringLiteral("开始采集"), reason);
        setStatusMessage(QStringLiteral("状态: 等待双相机连接"), UiStatusLevel::Warning);
        return;
    }

    m_liveStartupConfirmed = false;
    m_pulseBoardResponseTimedOut = false;
    m_lastPulseBoardTimeoutStatusMs = -1;

    if (m_hardwareTriggerStartupTimer) {
        m_hardwareTriggerStartupTimer->stop();
    }

    if (!resumeExistingResultSession) {
        closeResultFile(ResultSessionEndReason::ManualStop);
        resetMeasurementState();
    } else {
        m_captureStopRequested = false;
        resetMeasurementState();
    }
    m_liveHardwareRoiActive = false;
    m_liveStartupPhase = LiveStartupPhase::None;
    stopAutoFocusForTrackingExit(QStringLiteral("开始新的采集流程"));
    updateMinuteRoi(true);

    if (!configureLiveCameras(&reason)) {
        if (resumeExistingResultSession && m_resultSessionActive && m_resultWriter.isOpen()) {
            const qint64 failedAtMs = QDateTime::currentMSecsSinceEpoch();
            writeHardwareErrorEvent(QStringLiteral("camera"),
                                    -1,
                                    0,
                                    QStringLiteral("configure_live_cameras"),
                                    reason,
                                    true,
                                    failedAtMs);
            writeAcquisitionPauseEvent(QStringLiteral("configure_live_cameras"),
                                       failedAtMs,
                                       currentDeviceStatusForResultLog(failedAtMs));
            updateCaptureState(CaptureState::Paused);
        } else {
            updateCaptureState(CaptureState::Idle);
        }
        setStatusMessage(reason, UiStatusLevel::Error);
        QMessageBox::warning(this, QStringLiteral("开始采集"), reason);
        return;
    }

    const bool liveStarted =
        m_configTriggerMode == 0 ? startDualCameraLocalization(&reason) : m_cameraManager->startAll();

    if (liveStarted) {
        updateCaptureState(CaptureState::Live);
        if (!resumeExistingResultSession) {
            const AcquisitionCsvSessionType resultSessionType =
                m_liveStartupOrigin == LiveStartupOrigin::AutoAcquisition
                    ? AcquisitionCsvSessionType::Auto
                    : AcquisitionCsvSessionType::Manual;
            if (!beginResultSession(resultSessionType)) {
                m_cameraManager->stopAll();
                updateCaptureState(CaptureState::Idle);
                setStatusMessage(QStringLiteral("状态: 结果会话创建失败"), UiStatusLevel::Error);
                return;
            }
            if (m_searchAttempt == 0) {
                m_searchAttempt = 1;
                m_searchAttemptStartedMs = QDateTime::currentMSecsSinceEpoch();
            }
            writeSearchLifecycleEventsOnce(QStringLiteral("initial_search"),
                                           QStringLiteral("initial_search"),
                                           QDateTime::currentMSecsSinceEpoch());
        } else {
            writeSearchLifecycleEventsOnce(
                m_liveStartupOrigin == LiveStartupOrigin::AutoAcquisition
                    ? QStringLiteral("automatic_resume_search")
                    : QStringLiteral("manual_resume_search"),
                QStringLiteral("resume_search"),
                QDateTime::currentMSecsSinceEpoch());
        }
        m_reporting = m_commConnected;
        if (m_reporting && m_reportTimer) {
            m_reportTimer->start();
        }
        if (m_configTriggerMode == 0) {
            setStatusMessage(QStringLiteral("状态: 连续采集已启动，正在双相机全画幅定位"),
                             UiStatusLevel::Warning);
        } else {
            m_liveStartupPhase =
                LiveStartupPhase::LocatePair;

            const bool reuseRunningPulse =
                isFullFrameLocalizationPulseRunning();

            if (reuseRunningPulse) {
                m_activeTriggerFrequencyHz = kFullFrameLocalizationPulseHz;
                if (m_imageProcessor) {
                    m_imageProcessor->setTargetFrameRateHz(kFullFrameLocalizationPulseHz);
                }
                beginHardwareTriggerStartupStage(
                    HardwareTriggerStartupStage::WaitingFullFramePair);

                setStatusMessage(
                    QStringLiteral(
                        "状态: 硬件触发已就绪，复用当前脉冲输出并等待双相机新的全画幅图像"),
                    UiStatusLevel::Success);

                return;
            }

            if (!startFullFrameLocalizationPulse(&reason)) {
                if (isPulseBoardResponseTimeout(reason)) {
                    m_pulseBoardResponseTimedOut = true;

                    beginHardwareTriggerStartupStage(
                        HardwareTriggerStartupStage::WaitingFullFramePair);

                    setPulseBoardResponseTimeoutStatus(
                        QStringLiteral(
                            "状态: 脉冲板全画幅触发应答超时，继续等待双相机新的全画幅图像确认触发是否生效"));

                    return;
                }
                handleHardwareTriggerStartupFailure(
                    reason.isEmpty()
                        ? QStringLiteral("全画幅低频触发启动失败")
                        : reason);
                return;
            }

            m_activeTriggerFrequencyHz = kFullFrameLocalizationPulseHz;
            if (m_imageProcessor) {
                m_imageProcessor->setTargetFrameRateHz(kFullFrameLocalizationPulseHz);
            }

            beginHardwareTriggerStartupStage(
                HardwareTriggerStartupStage::WaitingFullFramePair);

            setStatusMessage(
                m_pulseGeneratorEnabled
                    ? QStringLiteral(
                          "状态: 全画幅低频触发已发起，等待双相机新的全画幅图像")
                    : QStringLiteral(
                          "状态: 请输出低频脉冲，等待双相机新的全画幅图像"),
                m_pulseGeneratorEnabled
                    ? UiStatusLevel::Success
                    : UiStatusLevel::Warning);

            return;
        }
        return;
    }

    if (resumeExistingResultSession && m_resultSessionActive && m_resultWriter.isOpen()) {
        const qint64 failedAtMs = QDateTime::currentMSecsSinceEpoch();
        writeHardwareErrorEvent(QStringLiteral("camera"),
                                -1,
                                0,
                                QStringLiteral("start_acquisition"),
                                reason,
                                true,
                                failedAtMs);
        writeAcquisitionPauseEvent(QStringLiteral("start_acquisition"),
                                   failedAtMs,
                                   currentDeviceStatusForResultLog(failedAtMs));
        updateCaptureState(CaptureState::Paused);
    } else {
        updateCaptureState(CaptureState::Idle);
    }
    setStatusMessage(reason.isEmpty() ? QStringLiteral("状态: 启动采集失败") : reason, UiStatusLevel::Error);
}

void DIMM::onStopCapture()
{
    if (m_captureState == CaptureState::Alignment) {
        stopAlignmentMode();
        return;
    }

    const bool automaticStop =
        m_autoAcquisitionCommandInProgress &&
        m_liveStartupOrigin == LiveStartupOrigin::AutoAcquisition;

    resetLiveStartupRecoveryState(true);

    noteManualAutoAcquisitionStopIfNeeded();

    m_captureStopRequested = true;
    cancelAutoAcquisitionPreFocus(QStringLiteral("采集已停止"));
    stopLiveCapture();
    m_reporting = false;
    if (m_reportTimer) {
        m_reportTimer->stop();
    }
    closeResultFile(automaticStop
                        ? ResultSessionEndReason::AutoWindowEnd
                        : ResultSessionEndReason::ManualStop);
    updateCaptureState(CaptureState::Idle);
    if (automaticStop) {
        closeAutoAcquisitionCameras();
    }
    setStatusMessage(QStringLiteral("状态: 已停止"), UiStatusLevel::Error);
    resetMeasurementState();
    if (m_fullFrameCanvas1) {
        m_fullFrameCanvas1->clear();
    }
    if (m_fullFrameCanvas2) {
        m_fullFrameCanvas2->clear();
    }
    m_cam1RoiCanvas->clear();
    m_cam2RoiCanvas->clear();
}

void DIMM::onShowMainPage()
{
    ui->stackedWidget->setCurrentIndex(0);
    ui->btnFullFrame->setChecked(true);
    if (ui->btnROI) {
        ui->btnROI->setChecked(false);
    }
}

void DIMM::onShowRoiPage()
{
    onShowMainPage();
}

void DIMM::onShowSettings()
{
    if (!isSettingsApplyAllowed()) {
        QMessageBox::information(this,
                                 QStringLiteral("设置"),
                                 QStringLiteral("相机连接流程进行中，请等待完成后再修改设置"));
        return;
    }

    m_settingsDialog->exposureEdit->setText(QString::number(m_configExposureUs, 'f', 0));
    m_settingsDialog->gainEdit->setText(QString::number(m_configGainDb, 'f', 1));
    m_settingsDialog->continuousFrameRateEdit->setText(
        QString::number(m_configContinuousFrameRateHz, 'f', 1));
    m_settingsDialog->triggerContinuous->setChecked(m_configTriggerMode == 0);
    m_settingsDialog->triggerHardware->setChecked(m_configTriggerMode != 0);
    if (m_settingsDialog->envSensorEnableCheck) {
        m_settingsDialog->envSensorEnableCheck->setChecked(m_environmentSensorConfig.enabled);
    }
    if (m_settingsDialog->envSensorPortEdit) {
        m_settingsDialog->envSensorPortEdit->setText(m_environmentSensorConfig.portName);
    }
    if (m_settingsDialog->envSensorBaudCombo) {
        m_settingsDialog->envSensorBaudCombo->setCurrentText(QString::number(m_environmentSensorConfig.baudRate));
    }
    if (m_settingsDialog->envSensorAddressEdit) {
        m_settingsDialog->envSensorAddressEdit->setText(QString::number(m_environmentSensorConfig.deviceAddress));
    }
    if (m_settingsDialog->envSensorPollIntervalEdit) {
        m_settingsDialog->envSensorPollIntervalEdit->setText(QString::number(m_environmentSensorConfig.pollIntervalMs));
    }
    m_settingsDialog->autoExposureCheck->setChecked(m_autoExposureConfig.enabled);
    if (m_settingsDialog->autoExpFrequencySwitchCheck) {
        m_settingsDialog->autoExpFrequencySwitchCheck->setChecked(
            m_autoExposureConfig.exposureFrequencySwitchEnabled);
    }
    m_settingsDialog->autoExpTrendConflictCheck->setChecked(m_autoExposureConfig.trendConflictEnabled);
    m_settingsDialog->autoExpTargetPeakLowEdit->setText(QString::number(m_autoExposureConfig.targetPeakLowDn, 'f', 1));
    m_settingsDialog->autoExpTargetPeakHighEdit->setText(QString::number(m_autoExposureConfig.targetPeakHighDn, 'f', 1));
    m_settingsDialog->autoExpExposureHysteresisEdit->setText(QString::number(m_autoExposureConfig.exposureHysteresisDn, 'f', 1));
    m_settingsDialog->autoExpHardSaturationEdit->setText(QString::number(m_autoExposureConfig.hardSaturationDn, 'f', 1));
    m_settingsDialog->autoExpSaturatedPixelCountEdit->setText(QString::number(m_autoExposureConfig.saturatedPixelCount));
    m_settingsDialog->autoExpDarkSnrWarningEdit->setText(QString::number(m_autoExposureConfig.darkSnrWarning, 'f', 2));
    m_settingsDialog->autoExpDarkSnrCriticalEdit->setText(QString::number(m_autoExposureConfig.darkSnrCritical, 'f', 2));
    m_settingsDialog->autoExpTrackingLostSnrEdit->setText(QString::number(m_autoExposureConfig.trackingLostSnr, 'f', 2));
    m_settingsDialog->autoExpMinValidCentroidRatioEdit->setText(QString::number(m_autoExposureConfig.minValidCentroidRatio, 'f', 2));
    m_settingsDialog->autoExpStarLostValidRatioEdit->setText(QString::number(m_autoExposureConfig.starLostValidRatio, 'f', 2));
    m_settingsDialog->autoExpBrightFrameRatioEdit->setText(QString::number(m_autoExposureConfig.brightFrameRatioThreshold, 'f', 2));
    m_settingsDialog->autoExpDarkFrameRatioEdit->setText(QString::number(m_autoExposureConfig.darkFrameRatioThreshold, 'f', 2));
    m_settingsDialog->autoExpStableFrameRatioEdit->setText(QString::number(m_autoExposureConfig.stableFrameRatioThreshold, 'f', 2));
    m_settingsDialog->autoExpHardSaturationFrameRatioEdit->setText(QString::number(m_autoExposureConfig.hardSaturationFrameRatioThreshold, 'f', 2));
    if (m_settingsDialog->autoExpSampleWindowSecEdit) {
        m_settingsDialog->autoExpSampleWindowSecEdit->setText(QString::number(m_autoExposureConfig.sampleWindowSec));
    }
    if (m_settingsDialog->autoExpSampleIntervalMsEdit) {
        m_settingsDialog->autoExpSampleIntervalMsEdit->setText(
            QString::number(m_autoExposureConfig.autoExposureSampleIntervalMs));
    }
    m_settingsDialog->autoExpMinDecisionSampleCountEdit->setText(QString::number(m_autoExposureConfig.minDecisionSampleCount));
    m_settingsDialog->autoExpStepUsEdit->setText(QString::number(m_autoExposureConfig.autoExposureStepUs, 'f', 0));
    m_settingsDialog->autoExpInitialExposureUsEdit->setText(QString::number(m_autoExposureConfig.initialExposureUs, 'f', 0));
    m_settingsDialog->autoExpDecisionCooldownMinEdit->setText(QString::number(m_autoExposureConfig.autoExposureDecisionCooldownMin));
    m_settingsDialog->autoExpTrendConflictPersistenceSecEdit->setText(QString::number(m_autoExposureConfig.trendConflictPersistenceSec));
    m_settingsDialog->autoExpMinEdit->setText(QString::number(m_autoExposureConfig.minExposureUs, 'f', 0));
    m_settingsDialog->autoExpMaxEdit->setText(QString::number(m_autoExposureConfig.maxExposureUs, 'f', 0));
    if (m_settingsDialog->autoExpFrequencyWindowsEdit) {
        m_settingsDialog->autoExpFrequencyWindowsEdit->setText(
            m_autoExposureConfig.exposureFrameRateWindows);
    }
    m_settingsDialog->autoExpMaxChangeUpEdit->setText(QString::number(m_autoExposureConfig.maxExposureChangeRatioUp, 'f', 2));
    m_settingsDialog->autoExpMaxChangeDownEdit->setText(QString::number(m_autoExposureConfig.maxExposureChangeRatioDown, 'f', 2));
    m_settingsDialog->autoExpCameraAgreementRatioEdit->setText(QString::number(m_autoExposureConfig.cameraAgreementRatio, 'f', 2));
    m_settingsDialog->autoExpPeakSupportRadiusEdit->setText(QString::number(m_autoExposureConfig.peakSupportRadiusPx));
    m_settingsDialog->autoExpPeakSupportFractionEdit->setText(QString::number(m_autoExposureConfig.peakSupportFraction, 'f', 2));
    m_settingsDialog->autoExpMinPeakSupportPixelsEdit->setText(QString::number(m_autoExposureConfig.minPeakSupportPixelCount));
    m_settingsDialog->autoExpMinNeighborPeakRatioEdit->setText(QString::number(m_autoExposureConfig.minNeighborPeakRatio, 'f', 2));
    m_settingsDialog->autoExpMaxPeakCandidateCountEdit->setText(QString::number(m_autoExposureConfig.maxPeakCandidateCount));
    m_settingsDialog->autoExpSupportedPeakPercentileEdit->setText(QString::number(m_autoExposureConfig.supportedPeakPercentile, 'f', 2));
    m_settingsDialog->autoExpExposureSettleMsEdit->setText(QString::number(m_autoExposureConfig.exposureSettleMs));
    m_settingsDialog->autoExpMinExposureDeltaEdit->setText(QString::number(m_autoExposureConfig.minExposureDeltaUs, 'f', 0));
    m_settingsDialog->autoExpMinExposureChangeRatioEdit->setText(QString::number(m_autoExposureConfig.minExposureChangeRatio, 'f', 2));
    m_settingsDialog->backgroundThresholdClipIterationsEdit->setText(
        QString::number(m_imageProcessor->backgroundThresholdClipIterations()));
    m_settingsDialog->backgroundThresholdClipSigmaEdit->setText(
        QString::number(m_imageProcessor->backgroundThresholdClipSigma(), 'f', 2));
    m_settingsDialog->backgroundThresholdSigmaMultiplierEdit->setText(
        QString::number(m_imageProcessor->backgroundThresholdSigmaMultiplier(), 'f', 2));
    if (m_settingsDialog->centroidModeCombo) {
        const int modeIndex =
            m_settingsDialog->centroidModeCombo->findData(m_imageProcessor->centroidMode());
        m_settingsDialog->centroidModeCombo->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);
    }
    if (m_settingsDialog->peakKernelRadiusEdit) {
        m_settingsDialog->peakKernelRadiusEdit->setText(
            QString::number(m_imageProcessor->peakKernelRadiusPx()));
    }
    if (m_settingsDialog->strongHotPixelExcessEdit) {
        m_settingsDialog->strongHotPixelExcessEdit->setText(
            QString::number(m_imageProcessor->strongHotPixelExcessDn(), 'f', 0));
    }
    if (m_settingsDialog->r0HistoryWindowFramesEdit) {
        m_settingsDialog->r0HistoryWindowFramesEdit->setText(
            QString::number(m_imageProcessor->atmosphereHistoryWindowFrames()));
    }
    const CdimPsdAnalysisConfig psdConfig = m_imageProcessor->psdAnalysisConfig();
    if (m_settingsDialog->psdModeCombo) {
        const int index = m_settingsDialog->psdModeCombo->findData(static_cast<int>(psdConfig.psdMode));
        m_settingsDialog->psdModeCombo->setCurrentIndex(index >= 0 ? index : 0);
    }
    if (m_settingsDialog->psdNoiseDetectionModeCombo) {
        const int index = m_settingsDialog->psdNoiseDetectionModeCombo->findData(
            static_cast<int>(psdConfig.noiseDetectionMode));
        m_settingsDialog->psdNoiseDetectionModeCombo->setCurrentIndex(index >= 0 ? index : 0);
    }
    if (m_settingsDialog->psdWelchSegmentLengthEdit) {
        m_settingsDialog->psdWelchSegmentLengthEdit->setText(
            QString::number(psdConfig.welchSegmentLength));
    }
    if (m_settingsDialog->psdWelchOverlapEdit) {
        m_settingsDialog->psdWelchOverlapEdit->setText(
            QString::number(psdConfig.welchOverlap, 'f', 2));
    }
    if (m_settingsDialog->psdNfftEdit) {
        m_settingsDialog->psdNfftEdit->setText(QString::number(psdConfig.nfft));
    }
    if (m_settingsDialog->psdNoiseCandidateStartEdit) {
        m_settingsDialog->psdNoiseCandidateStartEdit->setText(
            QString::number(psdConfig.noiseCandidateStartNyquist, 'f', 2));
    }
    if (m_settingsDialog->psdNoiseCandidateEndEdit) {
        m_settingsDialog->psdNoiseCandidateEndEdit->setText(
            QString::number(psdConfig.noiseCandidateEndNyquist, 'f', 2));
    }
    if (m_settingsDialog->psdMinimumNoiseBandBinsEdit) {
        m_settingsDialog->psdMinimumNoiseBandBinsEdit->setText(
            QString::number(psdConfig.minimumNoiseBandBins));
    }
    if (m_settingsDialog->psdMinimumNoiseBandWidthEdit) {
        m_settingsDialog->psdMinimumNoiseBandWidthEdit->setText(
            QString::number(psdConfig.minimumNoiseBandNyquistWidth, 'f', 2));
    }
    if (m_settingsDialog->psdFitNoiseDominanceKappaEdit) {
        m_settingsDialog->psdFitNoiseDominanceKappaEdit->setText(
            QString::number(psdConfig.fitNoiseDominanceKappa, 'f', 2));
    }
    m_settingsDialog->roiRecenterThresholdEdit->setText(
        QString::number(m_roiRecenteringThresholdPx, 'f', 1));
    m_settingsDialog->roiRecenterRequiredFramesEdit->setText(
        QString::number(m_roiRecenteringRequiredFrames));
    m_settingsDialog->roiRecenterCooldownMsEdit->setText(
        QString::number(m_roiRecenteringCooldownMs));
    m_settingsDialog->roiRecenterMinimumShiftEdit->setText(
        QString::number(m_roiRecenteringMinimumShiftPx, 'f', 1));
    const InitialStarDetectionConfig starConfig = currentInitialStarDetectionConfig();
    m_settingsDialog->starSigmaThresholdEdit->setText(
        QString::number(starConfig.sigmaThreshold, 'f', 2));
    m_settingsDialog->starPeakFractionEdit->setText(
        QString::number(starConfig.peakFraction, 'f', 2));
    m_settingsDialog->starMinAreaEdit->setText(QString::number(starConfig.minArea));
    m_settingsDialog->starMaxAreaEdit->setText(QString::number(starConfig.maxArea));
    m_settingsDialog->hotPixelEnableCheck->setChecked(m_hotPixelTemplatesEnabled);
    m_settingsDialog->hotPixelCam0MaskEdit->setText(PathUtils::relativizePathToAppDir(m_hotPixelCamera0MaskPath));
    m_settingsDialog->hotPixelCam0ExcessEdit->setText(PathUtils::relativizePathToAppDir(m_hotPixelCamera0ExcessPath));
    m_settingsDialog->hotPixelCam1MaskEdit->setText(PathUtils::relativizePathToAppDir(m_hotPixelCamera1MaskPath));
    m_settingsDialog->hotPixelCam1ExcessEdit->setText(PathUtils::relativizePathToAppDir(m_hotPixelCamera1ExcessPath));
    m_settingsDialog->hotPixelTemplateWidthEdit->setText(QString::number(m_hotPixelTemplateWidth));
    m_settingsDialog->hotPixelTemplateHeightEdit->setText(QString::number(m_hotPixelTemplateHeight));
    m_settingsDialog->opticsD->setText(QString::number(m_imageProcessor->apertureDiameterMm(), 'f', 1));
    m_settingsDialog->opticsBaseline->setText(QString::number(m_imageProcessor->baselineSeparationMm(), 'f', 1));
    m_settingsDialog->opticsBaselineAngle->setText(QString::number(m_imageProcessor->baselineAngleDeg(), 'f', 1));
    m_settingsDialog->opticsF->setText(QString::number(m_imageProcessor->focalLengthCm(), 'f', 1));
    m_settingsDialog->opticsZenith->setText(QString::number(m_imageProcessor->zenithAngleDeg(), 'f', 1));
    m_settingsDialog->opticsOuterScale->setText(
        QString::number(m_imageProcessor->outerScaleMeters(), 'f', 1));
    m_settingsDialog->detectorWavelength->setText(QString::number(m_imageProcessor->wavelengthNm(), 'f', 1));
    m_settingsDialog->detectorPixelSize->setText(QString::number(m_imageProcessor->pixelSizeUm(), 'f', 2));
    m_settingsDialog->alignmentAutoRadiusCheck->setChecked(m_alignmentAutoRadius);
    m_settingsDialog->alignmentFocalLengthEdit->setText(QString::number(m_alignmentFocalLengthMm, 'f', 1));
    m_settingsDialog->alignmentPixelSizeEdit->setText(QString::number(m_alignmentPixelSizeUm, 'f', 2));
    m_settingsDialog->alignmentPolarDistanceEdit->setText(
        QString::number(m_alignmentPolarisPolarDistanceArcmin, 'f', 1));
    m_settingsDialog->alignmentRadiusAdjustEdit->setText(
        QString::number(m_alignmentRadiusAdjustPx, 'f', 1));
    m_settingsDialog->alignmentPreviewRateEdit->setText(
        QString::number(m_alignmentPreviewRateHz, 'f', 1));
    m_settingsDialog->alignmentAutoSolveCheck->setChecked(m_alignmentAutoSolveEnabled);
    m_settingsDialog->alignmentShowMatchedCatalogStarsCheck->setChecked(
        m_alignmentShowMatchedCatalogStars);
    m_settingsDialog->alignmentMaxDetectedStarsEdit->setText(
        QString::number(m_alignmentMaxDetectedStars));
    m_settingsDialog->alignmentMinMatchedStarsEdit->setText(
        QString::number(m_alignmentMinMatchedStars));
    m_settingsDialog->alignmentMaxRmsEdit->setText(QString::number(m_alignmentMaxRmsPx, 'f', 1));
    m_settingsDialog->alignmentRetryIntervalEdit->setText(
        QString::number(m_alignmentRetryIntervalMs / 1000.0, 'f', 1));
    m_settingsDialog->storagePathEdit->setText(m_dataPath);
    m_settingsDialog->saveIntervalEdit->setText(QString::number(m_saveInterval));
    m_settingsDialog->parameterValidationCheck->setChecked(m_parameterValidationEnabled);
    m_settingsDialog->syncDiagnosticLogCheck->setChecked(m_syncDiagnosticLoggingEnabled);
    m_settingsDialog->setPulseGeneratorState(m_pulseGeneratorEnabled,
                                             m_pulseGeneratorPort,
                                             m_pulseGeneratorBaudRate,
                                             m_pulseGeneratorTerminalId,
                                             m_pulseGeneratorFrequencyHz,
                                             m_pulseGeneratorPulseCount,
                                             m_pulseGeneratorDutyPercent,
                                             m_pulseGeneratorRemoteControl);
    m_settingsDialog->netIpEdit->setText(m_commManager->remoteAddress());
    m_settingsDialog->netPortEdit->setText(QString::number(m_commManager->remotePort()));
    m_settingsDialog->setCommittedConfig(currentAppConfig());
    if (m_settingsDialog->applyStatusLabel) {
        m_settingsDialog->applyStatusLabel->setText(QStringLiteral("待应用"));
        m_settingsDialog->applyStatusLabel->setStyleSheet(statusLabelStyle(UiStatusLevel::Muted));
    }
    m_settingsDialog->exec();
}

void DIMM::onToggleRoiImages()
{
    setDetailViewMode(DetailViewMode::RoiOnly);
}

void DIMM::onToggleCharts()
{
    setDetailViewMode(DetailViewMode::ChartsOnly);
}

void DIMM::onSaveConfig()
{
    const QString file = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存配置"), QStringLiteral("config.json"), QStringLiteral("JSON 文件 (*.json)"));
    if (!file.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("保存配置"), QStringLiteral("配置导出功能将在后续版本中补充。\n目标路径: %1").arg(file));
    }
}

void DIMM::onLoadConfig()
{
    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("加载配置"), QString(), QStringLiteral("JSON 文件 (*.json)"));
    if (!file.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("加载配置"), QStringLiteral("配置导入功能将在后续版本中补充。\n目标路径: %1").arg(file));
    }
}

void DIMM::onExportData()
{
    const QString file = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出数据"), QStringLiteral("data.txt"), QStringLiteral("文本文件 (*.txt)"));
    if (file.isEmpty()) {
        return;
    }

    flushPendingWrites();

    if (m_resultFilePath.isEmpty() || !QFile::exists(m_resultFilePath)) {
        QMessageBox::warning(this,
                             QStringLiteral("导出数据"),
                             QStringLiteral("当前还没有可导出的采集结果文件，请先运行一次采集"));
        return;
    }

    QFile::remove(file);
    if (QFile::copy(m_resultFilePath, file)) {
        QMessageBox::information(this,
                                 QStringLiteral("导出数据"),
                                 QStringLiteral("结果数据已导出到:\n%1").arg(file));
    } else {
        QMessageBox::warning(this,
                             QStringLiteral("导出数据"),
                             QStringLiteral("导出失败，请检查目标路径是否可写"));
    }
}

void DIMM::onExportReport()
{
    const QString file = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出报告"), QStringLiteral("report.pdf"), QStringLiteral("PDF 文件 (*.pdf)"));
    if (!file.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("导出报告"), QStringLiteral("报告导出功能将在后续版本中补充。\n目标路径: %1").arg(file));
    }
}

void DIMM::onAbout()
{
    QMessageBox::about(this, QStringLiteral("关于 C-DIMM"),
                       QStringLiteral("<h3>C-DIMM 大气相干长度测量系统</h3>"
                                      "<p>版本: v1.0</p>"
                                      "<ul>"
                                      "<li>双相机同步采集</li>"
                                      "<li>实时质心计算</li>"
                                      "<li>大气参数反演 (r0 / seeing / theta0 / tau0)</li>"
                                      "<li>结果记录与通信上报</li>"
                                      "</ul>"));
}

void DIMM::updateParams()
{
    auto& runtime = activeRuntime();
    runtime.latestAtmosphere.r0 = 11.0 + QRandomGenerator::global()->generateDouble() * 3.0;
    runtime.latestAtmosphere.seeing = 0.98 * 0.55 / (runtime.latestAtmosphere.r0 / 100.0) * 206265.0 / 1000.0;
    runtime.latestAtmosphere.theta0 = 4.0 + QRandomGenerator::global()->generateDouble() * 3.0;
    runtime.latestAtmosphere.tau0 = 6.0 + QRandomGenerator::global()->generateDouble() * 4.0;
    runtime.hasValidAtmosphere = true;
    refreshMeasurementUi();
}

void DIMM::updateCameraInfo()
{
    for (int i = 0; i < 2; ++i) {
        auto* infoLabel = i == 0 ? ui->lblCam1Info : ui->lblCam2Info;
        if (!m_cameraManager->isOpen(i)) {
            infoLabel->setText(QStringLiteral("SN: -- | -- fps"));
            continue;
        }

        const double fps = m_cameraManager->measuredCaptureFrameRateHz(i);
        infoLabel->setText(QStringLiteral("SN: %1 | %2 fps")
                               .arg(m_cameraManager->getSerialNumber(i))
                               .arg(fps, 0, 'f', 0));

        if (m_lblAutoFocusCam[i]) {
            const QString position = m_autoFocusFocuserOpened[i]
                                         ? QString::number(m_autoFocusFocuserPosition[i])
                                         : QStringLiteral("--");
            const QString hfr = m_hasLatestAutoFocusSample[i]
                                    ? QString::number(m_latestAutoFocusSample[i].hfr, 'f', 2)
                                    : QStringLiteral("--");
            const AutoFocusReferenceMetrics reference = m_autoFocusController
                                                            ? m_autoFocusController->referenceMetrics(i)
                                                            : AutoFocusReferenceMetrics{};
            const QString referenceHfr = reference.hfr > 0.0
                                             ? QString::number(reference.hfr, 'f', 2)
                                             : QStringLiteral("--");
            m_lblAutoFocusCam[i]->setText(
                QStringLiteral("相机%1：%2 |HFR %3/%4")
                    .arg(i + 1)
                    .arg(position, hfr, referenceHfr));
        }
    }

    if (!m_latestEnvironment.valid) {
        ui->lblEnvironmentStatus->setText(QStringLiteral("未连接"));
        ui->lblEnvironmentStatus->setStyleSheet(
            QStringLiteral("color: %1").arg(uiStatusColor(UiStatusLevel::Muted)));
        ui->lblEnvironmentInfo->setText(QStringLiteral("-- °C  |  -- %RH  |  -- hPa"));
        return;
    }

    ui->lblEnvironmentStatus->setText(QStringLiteral("在线"));
    ui->lblEnvironmentStatus->setStyleSheet(
        QStringLiteral("color: %1").arg(uiStatusColor(UiStatusLevel::Success)));
    ui->lblEnvironmentInfo->setText(QStringLiteral("%1 °C  |  %2 %RH  |  %3 hPa")
                                        .arg(m_latestEnvironment.temperatureC, 0, 'f', 1)
                                        .arg(m_latestEnvironment.humidityRh, 0, 'f', 1)
                                        .arg(m_latestEnvironment.pressureHpa, 0, 'f', 1));
}

void DIMM::updateCurrentRoi()
{
    updateMinuteRoi(true);
}

void DIMM::resetLiveStartupRecoveryState(bool resetRetryCount)
{
    if (m_hardwareTriggerStartupTimer) {
        m_hardwareTriggerStartupTimer->stop();
    }

    if (m_liveStartupRetryTimer) {
        m_liveStartupRetryTimer->stop();
    }

    m_liveStartupConfirmed = false;
    m_liveStartupRecoveryInProgress = false;
    m_pulseBoardResponseTimedOut = false;
    m_lastPulseBoardTimeoutStatusMs = -1;

    m_hardwareTriggerStartupStage =
        HardwareTriggerStartupStage::None;

    for (int cameraIndex = 0;
         cameraIndex < 2;
         ++cameraIndex) {
        m_hardwareTriggerStageBaselineFrameCount[cameraIndex] = 0;
        m_hardwareTriggerStageFrameSeen[cameraIndex] = false;
    }

    m_internalLiveStartupRetry = false;

    if (resetRetryCount) {
        m_liveStartupRetryCount = 0;
        m_liveStartupWindowId.clear();
    }
}

bool DIMM::stopLiveCapture()
{
    if (m_captureState != CaptureState::Live) {
        return true;
    }

    if (m_hardwareTriggerStartupTimer) {
        m_hardwareTriggerStartupTimer->stop();
    }
    m_liveStartupConfirmed = false;
    m_pulseBoardResponseTimedOut = false;
    m_lastPulseBoardTimeoutStatusMs = -1;
    m_liveHardwareRoiActive = false;
    m_liveStartupPhase = LiveStartupPhase::None;
    stopAutoFocusForTrackingExit(QStringLiteral("已退出 Tracking"));

    m_hardwareTriggerStartupStage =
        HardwareTriggerStartupStage::None;

    for (int cameraIndex = 0;
         cameraIndex < 2;
         ++cameraIndex) {
        m_hardwareTriggerStageFrameSeen[cameraIndex] = false;
    }

    if (m_pulseGenerator && m_pulseGenerator->isRunning()) {
        m_pulseGenerator->stopOutput();
    }
    m_cameraManager->stopAll();
    return true;
}

void DIMM::on1hzTick()
{
    updateCameraInfo();
    verifyPendingFrequencySwitch();
    if (isTrackingForAutoFocus() && m_autoFocusController &&
        m_autoFocusConfig.masterEnabled) {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        for (int camera = 0; camera < 2; ++camera) {
            const AutoFocusRunState stateBeforeTimeout = m_autoFocusController->state(camera);
            const AutoFocusAction timeoutAction =
                m_autoFocusController->checkTimeout(camera, nowMs);
            if (timeoutAction.type != AutoFocusActionType::Failed) {
                continue;
            }
            if (stateBeforeTimeout == AutoFocusRunState::ReferenceCalibration ||
                !m_focuserManager) {
                handleAutoFocusAction(camera, timeoutAction, QStringLiteral("调焦阶段超时"));
                continue;
            }
            m_autoFocusAwaitingTimeoutStop[camera] = true;
            m_autoFocusTimeoutStopCommandFinished[camera] = false;
            m_focuserManager->stopMotion(static_cast<TelescopeSlot>(camera));
        }
    }
    if (isTrackingForAutoFocus() && m_focuserManager && m_autoFocusConfig.masterEnabled) {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        for (int camera = 0; camera < 2; ++camera) {
            if (!m_autoFocusConfig.cameraEnabled[camera] || m_autoFocusFocuserOpened[camera]) {
                m_autoFocusReconnectAfterMs[camera] = -1;
                continue;
            }
            if (m_autoFocusReconnectAfterMs[camera] < 0) {
                m_autoFocusReconnectAfterMs[camera] = nowMs + 10000;
            }
            if (nowMs >= m_autoFocusReconnectAfterMs[camera]) {
                m_focuserManager->openAssignedDevice(static_cast<TelescopeSlot>(camera));
                m_autoFocusReconnectAfterMs[camera] = nowMs + 10000;
                setStatusMessage(QStringLiteral("相机 %1 焦点器断开，正在尝试重新连接")
                                     .arg(camera + 1),
                                 UiStatusLevel::Warning);
            }
        }
    }
    auto& runtime = activeRuntime();
    const QTime now = QTime::currentTime();
    const int minuteKey = now.hour() * 60 + now.minute();
    const int second = now.second();

    if (minuteKey != runtime.chartMinuteKey) {
        runtime.chartMinuteKey = minuteKey;
        runtime.chartSecond = -1;
        if (m_r0Chart) {
            m_r0Chart->clear();
        }
        if (m_seeingChart) {
            m_seeingChart->clear();
        }
        if (m_longitudinalPsdChart) {
            m_longitudinalPsdChart->clear();
        }
        if (m_transversePsdChart) {
            m_transversePsdChart->clear();
        }
    }

    if (runtime.hasValidAtmosphere && second != runtime.chartSecond) {
        runtime.chartSecond = second;
        if (m_r0Chart) {
            m_r0Chart->setSecondValue(second, runtime.latestAtmosphere.r0);
        }
        if (m_seeingChart) {
            m_seeingChart->setSecondValue(second, runtime.latestAtmosphere.seeing);
        }
    }

    evaluateAutoAcquisitionSchedule();
}

void DIMM::verifyPendingFrequencySwitch()
{
    if (!m_frequencyVerificationPending || !m_cameraManager ||
        QDateTime::currentMSecsSinceEpoch() < m_frequencyVerificationNotBeforeMs) {
        return;
    }

    const double measuredCamera0Hz = m_cameraManager->measuredCaptureFrameRateHz(0);
    const double measuredCamera1Hz = m_cameraManager->measuredCaptureFrameRateHz(1);
    const double targetHz = m_frequencyVerificationTargetHz;
    m_frequencyVerificationPending = false;
    m_frequencyVerificationNotBeforeMs = -1;

    if (ExposureFrequencySwitchController::acceptsMeasuredFrameRates(
            targetHz, measuredCamera0Hz, measuredCamera1Hz)) {
        m_pulseGeneratorFrequencyHz = targetHz;
        if (m_settingsDialog) {
            m_settingsDialog->setPulseGeneratorState(m_pulseGeneratorEnabled,
                                                     m_pulseGeneratorPort,
                                                     m_pulseGeneratorBaudRate,
                                                     m_pulseGeneratorTerminalId,
                                                     targetHz,
                                                     m_pulseGeneratorPulseCount,
                                                     m_pulseGeneratorDutyPercent,
                                                     m_pulseGeneratorRemoteControl);
            m_settingsDialog->setCommittedConfig(currentAppConfig());
        }
        ConfigChangeSet changes;
        changes.pulseGenerator = true;
        savePersistentSettings(currentAppConfig(), changes);

        m_rateSwitchHardwareApplyMs =
            qMax(m_rateSwitchHardwareApplyMs,
                 QDateTime::currentMSecsSinceEpoch() - m_rateSwitchStartedMs);
        if (m_rateSwitchTimingPending) {
            RateSwitchTiming timing;
            timing.pauseMs = m_rateSwitchPauseMs;
            timing.hardwareApplyMs = m_rateSwitchHardwareApplyMs;
            timing.firstValidPairMs = 0;
            timing.success = true;
            logRateSwitchTiming(timing,
                                m_rateSwitchOldRateHz,
                                m_rateSwitchNewRateHz,
                                QStringLiteral("hardware_trigger"));
            m_rateSwitchTimingPending = false;
        }
        m_rateSwitchInProgress = false;
        if (m_resultSessionActive && m_resultWriter.isOpen()) {
            const qint64 resumedAtMs = QDateTime::currentMSecsSinceEpoch();
            writeAcquisitionResumeEvent(QStringLiteral("exposure_and_rate_change_complete"),
                                        resumedAtMs,
                                        currentDeviceStatusForResultLog(resumedAtMs));
        }
        setStatusMessage(QStringLiteral("自动曝光: 触发频率 %1 Hz 已由相机实际帧率确认（%2 / %3 fps）")
                             .arg(targetHz, 0, 'f', 1)
                             .arg(measuredCamera0Hz, 0, 'f', 1)
                             .arg(measuredCamera1Hz, 0, 'f', 1),
                         UiStatusLevel::Success);
        return;
    }

    if (m_rateSwitchTimingPending) {
        RateSwitchTiming timing;
        timing.pauseMs = m_rateSwitchPauseMs;
        timing.hardwareApplyMs = m_rateSwitchHardwareApplyMs;
        timing.firstValidPairMs = 0;
        timing.success = false;
        logRateSwitchTiming(timing,
                            m_rateSwitchOldRateHz,
                            m_rateSwitchNewRateHz,
                            QStringLiteral("hardware_trigger"));
        m_rateSwitchTimingPending = false;
    }
    m_rateSwitchInProgress = false;
    m_frequencyVerificationRollbackInProgress = true;
    const double rollbackExposureUs[2] = {
        m_frequencyVerificationPreviousExposureUs[0],
        m_frequencyVerificationPreviousExposureUs[1],
    };
    QString rollbackReason;
    bool rollbackSuccess = applyTrackingExposureAndFrameRate(rollbackExposureUs,
                                                              &rollbackReason);
    const double rollbackFrequencyHz = m_rateSwitchOldRateHz;
    if (!m_pulseGenerator || !std::isfinite(rollbackFrequencyHz) || rollbackFrequencyHz <= 0.0) {
        rollbackSuccess = false;
        if (rollbackReason.isEmpty()) {
            rollbackReason = QStringLiteral("自动曝光: 旧触发频率无效，无法回滚");
        }
    } else {
        PulseGeneratorManager::Config rollbackPulseConfig = m_pulseGenerator->config();
        rollbackPulseConfig.enabled = true;
        rollbackPulseConfig.frequencyHz = rollbackFrequencyHz;
        QString pulseRollbackReason;
        const bool pulseRollbackSuccess =
            m_pulseGenerator->configureAndStart(rollbackPulseConfig, &pulseRollbackReason) &&
            m_pulseGenerator->isRunningAtFrequency(rollbackFrequencyHz);
        if (!pulseRollbackSuccess) {
            rollbackSuccess = false;
            if (rollbackReason.isEmpty()) {
                rollbackReason = pulseRollbackReason.isEmpty()
                                     ? QStringLiteral("自动曝光: 旧触发频率恢复失败")
                                     : pulseRollbackReason;
            }
        } else {
            m_activeTriggerFrequencyHz = rollbackFrequencyHz;
            if (m_imageProcessor) {
                m_imageProcessor->setTargetFrameRateHz(rollbackFrequencyHz);
            }
        }
    }
    m_frequencyVerificationRollbackInProgress = false;
    setStatusMessage(
        rollbackSuccess
            ? QStringLiteral("自动曝光: %1 Hz 验证失败（%2 / %3 fps），已回滚")
                  .arg(targetHz, 0, 'f', 1)
                  .arg(measuredCamera0Hz, 0, 'f', 1)
                  .arg(measuredCamera1Hz, 0, 'f', 1)
            : QStringLiteral("自动曝光: %1 Hz 验证失败（%2 / %3 fps），回滚失败：%4")
                  .arg(targetHz, 0, 'f', 1)
                  .arg(measuredCamera0Hz, 0, 'f', 1)
                  .arg(measuredCamera1Hz, 0, 'f', 1)
                  .arg(rollbackReason),
        rollbackSuccess ? UiStatusLevel::Warning : UiStatusLevel::Error);
}

void DIMM::matchRoiTimeSlot()
{
    ui->lblROITimeCurrent->setText(
        hasValidCentroidsForRoiUpdate()
            ? QStringLiteral("已具备独立 ROI 刷新条件")
            : QStringLiteral("等待两路有效质心"));
    ui->lblROITimeNext->setText(QStringLiteral("ROI 固定尺寸: 64 x 64"));
}
