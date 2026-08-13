#include "DIMM.h"
#include "DimmRuntimeHelpers.h"

#include "AlignmentCameraCoordinator.h"
#include "AlignmentCoarseController.h"
#include "AlignmentController.h"
#include "AlignmentFrameCoordinator.h"
#include "AlignmentLocalTracker.h"
#include "AlignmentTaskManager.h"
#include "AlignmentUiPresenter.h"
#include "CameraManager.h"
#include "CanvasWidgets.h"
#include "CommManager.h"
#include "AppConfigPersistence.h"
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
    m_environmentSensor = new EnvironmentSensorManager(this);
    connect(m_environmentSensor, &EnvironmentSensorManager::dataUpdated, this, [this](EnvironmentSensorData data) {
        m_latestEnvironment = data;
        updateCameraInfo();
    });
    connect(m_environmentSensor, &EnvironmentSensorManager::errorOccurred, this, [this](const QString& error) {
        qDebug() << "[EnvironmentSensor]" << error;
    });
    if (m_environmentSensorConfig.enabled) {
        m_environmentSensor->start(m_environmentSensorConfig);
    }

}

void DIMM::setupRuntimeActions()
{
    m_simulationTimer = new QTimer(this);
    m_simulationTimer->setInterval(kSimulationFrameIntervalMs);
    connect(m_simulationTimer, &QTimer::timeout, this, &DIMM::onUpdateSimulation);

    m_actionStartSimulation = new QAction(QStringLiteral("模拟采集"), this);
    m_actionStartSimulation->setObjectName(QStringLiteral("btnStartSimulation"));
    if (ui->toolbar) {
        ui->toolbar->insertAction(ui->btnStop, m_actionStartSimulation);
    }
    if (ui->menuTools) {
        ui->menuTools->insertAction(ui->actionROISchedule, m_actionStartSimulation);
        ui->menuTools->insertSeparator(ui->actionROISchedule);
    }

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
    m_actionRetryCamera1PolarisSolve = new QAction(QStringLiteral("重新自动识别相机1"), this);
    m_actionRetryCamera1PolarisSolve->setObjectName(QStringLiteral("btnRetryCamera1PolarisSolve"));
    m_actionRetryCamera2PolarisSolve = new QAction(QStringLiteral("重新自动识别相机2"), this);
    m_actionRetryCamera2PolarisSolve->setObjectName(QStringLiteral("btnRetryCamera2PolarisSolve"));
    m_actionRetryBothPolarisSolve = new QAction(QStringLiteral("重新自动识别双相机"), this);
    m_actionRetryBothPolarisSolve->setObjectName(QStringLiteral("btnRetryBothPolarisSolve"));
    if (ui->menuTools) {
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
            &ImageProcessor::roiThresholdReady,
            this,
            [this](int cameraIndex, double otsuThreshold, double actualThreshold) {
                setRoiThresholdDisplay(cameraIndex, otsuThreshold, actualThreshold);
            });
    connect(m_imageProcessor,
            &ImageProcessor::acquisitionStopRequested,
            this,
            [this](const QString& reason) {
                setStatusMessage(QStringLiteral("Status: %1").arg(reason), UiStatusLevel::Error);
                onStopCapture();
            });
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
        const bool hadBothCentroids = hasValidCentroidsForRoiUpdate();
        const bool usable = isUsableCentroidSample(camIdx,
                                                   x,
                                                   y,
                                                   peakValue,
                                                   totalFlux,
                                                   background,
                                                   threshold,
                                                   signalPixelCount,
                                                   false);
        const bool liveTrackingEdgeCentroid =
            m_captureState == CaptureState::Live &&
            m_liveStartupPhase == LiveStartupPhase::Tracking &&
            isCentroidNearCurrentRoiEdge(camIdx, x, y);
        auto* label = camIdx == 0 ? ui->lblCam1ROICoord : ui->lblCam2ROICoord;
        label->setText(QStringLiteral("(%1, %2)").arg(x, 0, 'f', 1).arg(y, 0, 'f', 1));
        if (!usable || liveTrackingEdgeCentroid) {
            runtime.hasValidCentroid[camIdx] = false;
            if (liveTrackingEdgeCentroid) {
                handleLiveRoiCentroidLoss(camIdx);
            }
            return;
        }

        runtime.centroidX[camIdx] = x;
        runtime.centroidY[camIdx] = y;
        runtime.peakBrightness[camIdx] = peakValue;
        runtime.hasValidCentroid[camIdx] = true;
        if (m_captureState == CaptureState::Live && m_liveStartupPhase == LiveStartupPhase::Tracking) {
            runtime.lastTargetPosition[camIdx] = QPointF(x, y);
            runtime.hasLastTargetPosition[camIdx] = true;
        }
        if (m_captureState == CaptureState::Simulation &&
            !runtime.simulationRoiSeeded &&
            !hadBothCentroids &&
            hasValidCentroidsForRoiUpdate()) {
            updateMinuteRoi(true);
            runtime.simulationRoiSeeded = true;
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
        if (centroidValid) {
            ++runtime.validCentroidCount;
            ++runtime.validCentroidCountPerCamera[camIdx];
            runtime.lostCentroidFrameCount[camIdx] = 0;
            runtime.lostCentroidSinceMs[camIdx] = -1;
        } else {
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
                   double r0TransverseCm,
                   quint64 sampleCount,
                   bool partialWindow,
                   bool riskFlag,
                   quint64 targetSampleCount,
                   const QString& riskReason) {
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
        runtime.latestAtmosphere.sampleCount = sampleCount;
        runtime.latestAtmosphere.partialWindow = partialWindow;
        runtime.latestAtmosphere.riskFlag = riskFlag;
        runtime.latestAtmosphere.targetSampleCount = targetSampleCount;
        runtime.latestAtmosphere.riskReason = riskReason;
        refreshMeasurementUi();

        saveResultRow(runtime.frameCount);
    });

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
    if (m_simulationTimer) {
        m_simulationTimer->stop();
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
    if (m_cameraManager) {
        disconnect(m_cameraManager, nullptr, this, nullptr);
        m_cameraManager->stopAll();
        m_cameraManager->closeAll();
    }
    if (m_pulseGenerator) {
        m_pulseGenerator->stop();
        delete m_pulseGenerator;
        m_pulseGenerator = nullptr;
    }
    closeResultFile();
    delete ui;
}

void DIMM::setupConnections()
{
    connect(ui->btnStart, &QAction::triggered, this, &DIMM::onStartCapture);
    connect(m_actionStartSimulation, &QAction::triggered, this, &DIMM::onStartSimulation);
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

    auto* simulationShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+M")), this);
    connect(simulationShortcut, &QShortcut::activated, this, &DIMM::onStartSimulation);

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
    advanceLiveAcquisitionGeneration();
    ui->lblCam1ROICoord->setText(QStringLiteral("(0.0, 0.0)"));
    ui->lblCam2ROICoord->setText(QStringLiteral("(0.0, 0.0)"));
    refreshMeasurementUi();
}

void DIMM::updateCaptureState(CaptureState state)
{
    m_captureState = state;
    const bool focuserMotionAllowed =
        m_captureState == CaptureState::Idle || m_captureState == CaptureState::Paused;
    const QString reason = focuserMotionAllowed
                               ? QString()
                               : QStringLiteral("实时采集、模拟或对准模式中禁止移动焦点。请先暂停或停止采集");
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
    return isSimulationCaptureActive() ? m_simulationRuntime : m_liveRuntime;
}

const DIMM::CaptureRuntimeContext& DIMM::activeRuntime() const
{
    return isSimulationCaptureActive() ? m_simulationRuntime : m_liveRuntime;
}

DIMM::CaptureRuntimeContext& DIMM::runtimeForState(CaptureState state)
{
    return state == CaptureState::Simulation ? m_simulationRuntime : m_liveRuntime;
}

const DIMM::CaptureRuntimeContext& DIMM::runtimeForState(CaptureState state) const
{
    return state == CaptureState::Simulation ? m_simulationRuntime : m_liveRuntime;
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
           m_captureState == CaptureState::Simulation ||
           m_captureState == CaptureState::Alignment;
}

bool DIMM::isLiveCaptureActive() const
{
    return m_captureState == CaptureState::Live;
}

bool DIMM::isSimulationCaptureActive() const
{
    return m_captureState == CaptureState::Simulation;
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
    case CaptureState::Simulation:
        return QStringLiteral("simulation");
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
    case CaptureState::Simulation:
        return QStringLiteral("模拟采集");
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
    case CaptureState::Simulation:
        return QStringLiteral("simulation");
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
        if (m_autoAcquisitionStartedCurrentRun && m_captureState == CaptureState::Live) {
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

    if (m_captureState == CaptureState::Alignment || m_captureState == CaptureState::Simulation) {
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
    if (!canStartLiveCapture(&reason)) {
        setAutoAcquisitionStatus(reason.isEmpty()
                                     ? QStringLiteral("自动采集等待相机连接")
                                     : QStringLiteral("自动采集等待: %1").arg(reason),
                                 UiStatusLevel::Warning,
                                 QStringLiteral("waiting-start-readiness"));
        return;
    }

    resetAutoExposureState(true);

    m_liveStartupOrigin =
        LiveStartupOrigin::AutoAcquisition;

    m_liveStartupWindowId =
        window.windowId;

    m_liveStartupRetryCount = 0;
    m_liveStartupConfirmed = false;
    m_liveStartupRecoveryInProgress = false;
    m_pulseBoardResponseTimedOut = false;

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

void DIMM::stopAutoAcquisitionScanUntilNextInterval(const QString& reason,
                                                    bool manualSelectionRequired)
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (manualSelectionRequired) {
        m_autoAcquisitionRecovery.noteManualSelectionRequired(nowMs);
    } else {
        m_autoAcquisitionRecovery.noteScanFoundNoStar(nowMs);
    }

    const bool previousCommandState = m_autoAcquisitionCommandInProgress;
    m_autoAcquisitionCommandInProgress = true;
    if (m_captureState == CaptureState::Live) {
        onStopCapture();
    }
    m_autoAcquisitionCommandInProgress = previousCommandState;

    m_autoAcquisitionStartedCurrentRun = false;
    m_autoAcquisitionActiveWindowId.clear();
    m_liveStartupOrigin = LiveStartupOrigin::Manual;
    m_liveStartupWindowId.clear();
    m_liveStartupConfirmed = false;
    m_liveStartupRecoveryInProgress = false;
    m_liveHardwareRoiActive = false;
    m_liveStartupPhase = LiveStartupPhase::None;

    setAutoAcquisitionStatus(reason,
                             manualSelectionRequired ? UiStatusLevel::Warning
                                                     : UiStatusLevel::Info,
                             manualSelectionRequired
                                 ? QStringLiteral("auto-manual-selection-hold")
                                 : QStringLiteral("auto-wait-next-scan"));
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

    closeResultFile();
    updateCaptureState(CaptureState::Idle);
    resetMeasurementState();

    m_autoAcquisitionCommandInProgress =
        previousCommandState;

    const bool retryAllowed =
        m_liveStartupRetryCount <
            kLiveStartupMaxImmediateRetries &&
        shouldRetryFailedLiveStartup();

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

        noteManualAutoAcquisitionStopIfNeeded();
        stopLiveCapture();
        updateCaptureState(CaptureState::Paused);
        setStatusMessage(QStringLiteral("状态: 已暂停"), UiStatusLevel::Warning);
        return;
    }

    if (m_captureState == CaptureState::Simulation) {
        stopSimulationCapture();
        m_reporting = false;
        if (m_reportTimer) {
            m_reportTimer->stop();
        }
        updateCaptureState(CaptureState::Idle);
    }

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

    closeResultFile();
    resetMeasurementState();
    m_liveHardwareRoiActive = false;
    m_liveStartupPhase = LiveStartupPhase::None;
    updateMinuteRoi(true);

    if (!configureLiveCameras(&reason)) {
        updateCaptureState(CaptureState::Idle);
        setStatusMessage(reason, UiStatusLevel::Error);
        QMessageBox::warning(this, QStringLiteral("开始采集"), reason);
        return;
    }

    const bool liveStarted =
        m_configTriggerMode == 0 ? startDualCameraLocalization(&reason) : m_cameraManager->startAll();

    if (liveStarted) {
        updateCaptureState(CaptureState::Live);
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

                m_hardwareTriggerStartupStage =
                    HardwareTriggerStartupStage::None;

                if (m_hardwareTriggerStartupTimer) {
                    m_hardwareTriggerStartupTimer->stop();
                }

                m_liveStartupConfirmed = false;
                m_pulseBoardResponseTimedOut = false;

                m_cameraManager->stopAll();

                updateCaptureState(CaptureState::Idle);

                setStatusMessage(
                    reason.isEmpty()
                        ? QStringLiteral("状态: 全画幅低频触发启动失败")
                        : reason,
                    UiStatusLevel::Error);

                QMessageBox::warning(
                    this,
                    QStringLiteral("开始采集"),
                    reason.isEmpty()
                        ? QStringLiteral("全画幅低频触发启动失败。")
                        : reason);

                return;
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

    updateCaptureState(CaptureState::Idle);
    setStatusMessage(reason.isEmpty() ? QStringLiteral("状态: 启动采集失败") : reason, UiStatusLevel::Error);
}

void DIMM::onStopCapture()
{
    if (m_captureState == CaptureState::Alignment) {
        stopAlignmentMode();
        return;
    }

    resetLiveStartupRecoveryState(true);

    noteManualAutoAcquisitionStopIfNeeded();

    stopLiveCapture();
    stopSimulationCapture();
    m_reporting = false;
    if (m_reportTimer) {
        m_reportTimer->stop();
    }
    closeResultFile();
    updateCaptureState(CaptureState::Idle);
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
    m_settingsDialog->autoExpTrendConflictCheck->setChecked(m_autoExposureConfig.trendConflictEnabled);
    m_settingsDialog->autoExpTargetPeakLowEdit->setText(QString::number(m_autoExposureConfig.targetPeakLowDn, 'f', 1));
    m_settingsDialog->autoExpTargetPeakHighEdit->setText(QString::number(m_autoExposureConfig.targetPeakHighDn, 'f', 1));
    m_settingsDialog->autoExpExposureHysteresisEdit->setText(QString::number(m_autoExposureConfig.exposureHysteresisDn, 'f', 1));
    m_settingsDialog->autoExpHardSaturationEdit->setText(QString::number(m_autoExposureConfig.hardSaturationDn, 'f', 1));
    m_settingsDialog->autoExpSaturatedPixelCountEdit->setText(QString::number(m_autoExposureConfig.saturatedPixelCount));
    m_settingsDialog->autoExpDarkSnrWarningEdit->setText(QString::number(m_autoExposureConfig.darkSnrWarning, 'f', 2));
    m_settingsDialog->autoExpDarkSnrCriticalEdit->setText(QString::number(m_autoExposureConfig.darkSnrCritical, 'f', 2));
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
    m_settingsDialog->procKernelSize->setText(QString::number(m_imageProcessor->backgroundDenoiseKernelSize()));
    m_settingsDialog->procSigma->setText(QString::number(m_imageProcessor->backgroundDenoiseSigmaMultiplier(), 'f', 2));
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
                             QStringLiteral("当前还没有可导出的采集结果文件，请先运行一次模拟采集"));
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

        const double fps = m_cameraManager->getFrameRate(i);
        infoLabel->setText(QStringLiteral("SN: %1 | %2 fps")
                               .arg(m_cameraManager->getSerialNumber(i))
                               .arg(fps, 0, 'f', 0));
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

    m_hardwareTriggerStartupStage =
        HardwareTriggerStartupStage::None;

    for (int cameraIndex = 0;
         cameraIndex < 2;
         ++cameraIndex) {
        m_hardwareTriggerStageFrameSeen[cameraIndex] = false;
    }

    if (m_pulseGenerator && m_pulseGenerator->isRunning()) {
        m_pulseGenerator->stop();
    }
    m_cameraManager->stopAll();
    return true;
}

void DIMM::on1hzTick()
{
    updateCameraInfo();
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

void DIMM::matchRoiTimeSlot()
{
    ui->lblROITimeCurrent->setText(
        hasValidCentroidsForRoiUpdate()
            ? QStringLiteral("已具备独立 ROI 刷新条件")
            : QStringLiteral("等待两路有效质心"));
    ui->lblROITimeNext->setText(QStringLiteral("ROI 固定尺寸: 64 x 64"));
}
