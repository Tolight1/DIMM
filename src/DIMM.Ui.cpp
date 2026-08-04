#include "DIMM.h"

#include "CameraManager.h"
#include "CanvasWidgets.h"
#include "DimmRuntimeHelpers.h"
#include "FocuserControlWidget.h"
#include "ImageProcessor.h"
#include "SettingsDialog.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointF>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QWidget>


void DIMM::setupStatusBarUi()
{
    m_lblStatusState = new QLabel(this);
    m_lblStatusState->setObjectName(QStringLiteral("lblStatusState"));
    ui->statusbar->addWidget(m_lblStatusState);

    m_lblStatusROI = new QLabel(this);
    m_lblStatusROI->setObjectName(QStringLiteral("lblStatusROI"));
    ui->statusbar->addWidget(m_lblStatusROI);

    m_lblStatusFrames = new QLabel(this);
    m_lblStatusFrames->setObjectName(QStringLiteral("lblStatusFrames"));
    ui->statusbar->addWidget(m_lblStatusFrames);

}

void DIMM::setupMainWindowUi()
{
    ui->leftPanel->setMinimumWidth(248);
    ui->leftPanel->setMaximumWidth(300);
    ui->mainSplitter->setSizes({600, 340});
    ui->mainSplitter->setStretchFactor(0, 4);
    ui->mainSplitter->setStretchFactor(1, 3);
    ui->roiImagesArea->setMinimumHeight(220);
    ui->chartsArea->setMinimumHeight(320);
    ui->environmentStrip->setMinimumHeight(58);
    ui->cam1Card->setMinimumHeight(72);
    ui->cam2Card->setMinimumHeight(72);
    ui->statsCard->setMinimumHeight(150);
    for (QLabel* label : {ui->lblStatFrames,
                          ui->lblStatValid,
                          ui->lblStatLatency,
                          ui->lblStatWindow,
                          ui->lblStatExposure,
                          ui->lblStatAutoExposure}) {
        label->setWordWrap(true);
        label->setMinimumHeight(30);
    }
    for (QLabel* label : {ui->lblCam1Info, ui->lblCam2Info, ui->lblEnvironmentInfo}) {
        label->setWordWrap(false);
        label->setMinimumHeight(24);
    }
    ui->stackedWidget->setCurrentIndex(0);
    for (QFrame* card : {ui->r0Card, ui->seeingCard, ui->thetaCard, ui->tauCard}) {
        card->setMinimumHeight(70);
        card->setMaximumHeight(86);
    }
    ui->thetaCard->setVisible(true);
    ui->tauCard->setVisible(true);
    ui->lblThetaValue->setText(QStringLiteral("--"));
    ui->lblTauValue->setText(QStringLiteral("--"));
    m_statusText = QStringLiteral("状态: 就绪");

}

void DIMM::setupPreviewCanvases()
{
    setupFullFramePreviewCanvases();
    setupRoiPreviewCanvases();
    setupChartCanvases();
}

void DIMM::setupFullFramePreviewCanvases()
{
    if (auto* previewLayout = ui->previewCanvas->layout()) {
        while (previewLayout->count() > 0) {
            auto* item = previewLayout->takeAt(0);
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }
        delete previewLayout;
    }

    auto* previewCanvasLayout = new QVBoxLayout(ui->previewCanvas);
    previewCanvasLayout->setContentsMargins(10, 10, 10, 10);
    previewCanvasLayout->setSpacing(10);
    ui->lblFullframeLabel = new QLabel(QStringLiteral("双相机全画幅预览"), ui->previewCanvas);
    ui->lblFullframeLabel->setAlignment(Qt::AlignCenter);
    previewCanvasLayout->addWidget(ui->lblFullframeLabel);

    auto* dualPreviewLayout = new QHBoxLayout();
    dualPreviewLayout->setContentsMargins(0, 0, 0, 0);
    dualPreviewLayout->setSpacing(10);

    auto* cam1Panel = new QFrame(ui->previewCanvas);
    cam1Panel->setObjectName(QStringLiteral("fullFrameCam1Panel"));
    auto* cam1PanelLayout = new QVBoxLayout(cam1Panel);
    cam1PanelLayout->setContentsMargins(0, 0, 0, 0);
    cam1PanelLayout->setSpacing(6);
    m_lblFullFrameCam1 = new QLabel(QStringLiteral("全画幅预览- 相机1"), cam1Panel);
    m_lblFullFrameCam1->setAlignment(Qt::AlignCenter);
    m_lblAlignmentSolveCam1 = new QLabel(QStringLiteral("自动识别: 未启用"), cam1Panel);
    m_lblAlignmentSolveCam1->setAlignment(Qt::AlignCenter);
    m_lblAlignmentSolveCam1->setStyleSheet(QStringLiteral("color: %1").arg(uiStatusColor(UiStatusLevel::Muted)));
    m_lblAlignmentSolveCam1->setVisible(false);
    auto* cam1AlignmentControls = new QWidget(cam1Panel);
    auto* cam1AlignmentControlsLayout = new QHBoxLayout(cam1AlignmentControls);
    cam1AlignmentControlsLayout->setContentsMargins(0, 0, 0, 0);
    cam1AlignmentControlsLayout->setSpacing(6);
    m_btnRetryCamera1PolarisSolve = new QPushButton(QStringLiteral("重新识别"), cam1AlignmentControls);
    m_btnConfirmCamera1Polaris = new QPushButton(QStringLiteral("人工确认"), cam1AlignmentControls);
    m_btnRetryCamera1PolarisSolve->setVisible(false);
    m_btnConfirmCamera1Polaris->setVisible(false);
    cam1AlignmentControlsLayout->addWidget(m_btnRetryCamera1PolarisSolve);
    cam1AlignmentControlsLayout->addWidget(m_btnConfirmCamera1Polaris);
    m_fullFrameCanvas1 = new FullFrameCanvas(cam1Panel);
    cam1PanelLayout->addWidget(m_lblFullFrameCam1);
    cam1PanelLayout->addWidget(m_lblAlignmentSolveCam1);
    cam1PanelLayout->addWidget(cam1AlignmentControls);
    cam1PanelLayout->addWidget(m_fullFrameCanvas1, 1);

    auto* cam2Panel = new QFrame(ui->previewCanvas);
    cam2Panel->setObjectName(QStringLiteral("fullFrameCam2Panel"));
    auto* cam2PanelLayout = new QVBoxLayout(cam2Panel);
    cam2PanelLayout->setContentsMargins(0, 0, 0, 0);
    cam2PanelLayout->setSpacing(6);
    m_lblFullFrameCam2 = new QLabel(QStringLiteral("全画幅预览- 相机2"), cam2Panel);
    m_lblFullFrameCam2->setAlignment(Qt::AlignCenter);
    m_lblAlignmentSolveCam2 = new QLabel(QStringLiteral("自动识别: 未启用"), cam2Panel);
    m_lblAlignmentSolveCam2->setAlignment(Qt::AlignCenter);
    m_lblAlignmentSolveCam2->setStyleSheet(QStringLiteral("color: %1").arg(uiStatusColor(UiStatusLevel::Muted)));
    m_lblAlignmentSolveCam2->setVisible(false);
    auto* cam2AlignmentControls = new QWidget(cam2Panel);
    auto* cam2AlignmentControlsLayout = new QHBoxLayout(cam2AlignmentControls);
    cam2AlignmentControlsLayout->setContentsMargins(0, 0, 0, 0);
    cam2AlignmentControlsLayout->setSpacing(6);
    m_btnRetryCamera2PolarisSolve = new QPushButton(QStringLiteral("重新识别"), cam2AlignmentControls);
    m_btnConfirmCamera2Polaris = new QPushButton(QStringLiteral("人工确认"), cam2AlignmentControls);
    m_btnRetryCamera2PolarisSolve->setVisible(false);
    m_btnConfirmCamera2Polaris->setVisible(false);
    cam2AlignmentControlsLayout->addWidget(m_btnRetryCamera2PolarisSolve);
    cam2AlignmentControlsLayout->addWidget(m_btnConfirmCamera2Polaris);
    m_fullFrameCanvas2 = new FullFrameCanvas(cam2Panel);
    cam2PanelLayout->addWidget(m_lblFullFrameCam2);
    cam2PanelLayout->addWidget(m_lblAlignmentSolveCam2);
    cam2PanelLayout->addWidget(cam2AlignmentControls);
    cam2PanelLayout->addWidget(m_fullFrameCanvas2, 1);

    dualPreviewLayout->addWidget(cam1Panel, 1);
    dualPreviewLayout->addWidget(cam2Panel, 1);
    previewCanvasLayout->addLayout(dualPreviewLayout, 1);
    m_btnRetryBothPolarisSolve = new QPushButton(QStringLiteral("双相机重新识别"), ui->previewCanvas);
    m_btnRetryBothPolarisSolve->setVisible(false);
    previewCanvasLayout->addWidget(m_btnRetryBothPolarisSolve);

    m_btnToggleCoarseAlignment = new QPushButton(QStringLiteral("开始粗对准"), ui->previewCanvas);
    m_btnToggleCoarseAlignment->setVisible(false);
    previewCanvasLayout->addWidget(m_btnToggleCoarseAlignment);
    connect(m_btnToggleCoarseAlignment, &QPushButton::clicked, this, [this]() {
        if (m_actionToggleCoarseAlignment) {
            m_actionToggleCoarseAlignment->trigger();
        }
    });

    connect(m_btnConfirmCamera1Polaris,
            &QPushButton::clicked,
            this,
            &DIMM::onConfirmCamera1PolarisCandidate);
    connect(m_btnConfirmCamera2Polaris,
            &QPushButton::clicked,
            this,
            &DIMM::onConfirmCamera2PolarisCandidate);
    connect(m_btnRetryCamera1PolarisSolve, &QPushButton::clicked, this, [this]() {
        m_actionRetryCamera1PolarisSolve->trigger();
    });
    connect(m_btnRetryCamera2PolarisSolve, &QPushButton::clicked, this, [this]() {
        m_actionRetryCamera2PolarisSolve->trigger();
    });
    connect(m_btnRetryBothPolarisSolve, &QPushButton::clicked, this, [this]() {
        m_actionRetryBothPolarisSolve->trigger();
    });

}

void DIMM::setupRoiPreviewCanvases()
{
    m_cam1RoiCanvas = new RoiStarCanvas(ui->cam1ROICanvas);
    if (auto* cam1Layout = ui->cam1ROICanvas->layout()) {
        while (cam1Layout->count() > 0) {
            auto* item = cam1Layout->takeAt(0);
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }
        delete cam1Layout;
    }
    auto* newCam1Layout = new QVBoxLayout(ui->cam1ROICanvas);
    newCam1Layout->setContentsMargins(0, 0, 0, 0);
    newCam1Layout->setSpacing(4);
    ui->lblCam1ROICoord = new QLabel(QStringLiteral("(0.0, 0.0)"), ui->cam1ROICanvas);
    ui->lblCam1ROICoord->setAlignment(Qt::AlignCenter);
    newCam1Layout->addWidget(ui->lblCam1ROICoord);
    newCam1Layout->addWidget(m_cam1RoiCanvas);

    m_cam2RoiCanvas = new RoiStarCanvas(ui->cam2ROICanvas);
    if (auto* cam2Layout = ui->cam2ROICanvas->layout()) {
        while (cam2Layout->count() > 0) {
            auto* item = cam2Layout->takeAt(0);
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }
        delete cam2Layout;
    }
    auto* newCam2Layout = new QVBoxLayout(ui->cam2ROICanvas);
    newCam2Layout->setContentsMargins(0, 0, 0, 0);
    newCam2Layout->setSpacing(4);
    ui->lblCam2ROICoord = new QLabel(QStringLiteral("(0.0, 0.0)"), ui->cam2ROICanvas);
    ui->lblCam2ROICoord->setAlignment(Qt::AlignCenter);
    newCam2Layout->addWidget(ui->lblCam2ROICoord);
    newCam2Layout->addWidget(m_cam2RoiCanvas);

}

void DIMM::setupChartCanvases()
{
    m_r0Chart = new ChartWidget(ChartWidget::SeriesKind::R0, ui->r0ChartCanvas);
    if (auto* r0Layout = ui->r0ChartCanvas->layout()) {
        while (r0Layout->count() > 0) {
            auto* item = r0Layout->takeAt(0);
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }
        delete r0Layout;
    }
    auto* r0ChartLayout = new QVBoxLayout(ui->r0ChartCanvas);
    r0ChartLayout->setContentsMargins(0, 0, 0, 0);
    r0ChartLayout->addWidget(m_r0Chart);

    m_seeingChart = new ChartWidget(ChartWidget::SeriesKind::Seeing, ui->seeingChartCanvas);
    if (auto* seeingLayout = ui->seeingChartCanvas->layout()) {
        while (seeingLayout->count() > 0) {
            auto* item = seeingLayout->takeAt(0);
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }
        delete seeingLayout;
    }
    auto* seeingChartLayout = new QVBoxLayout(ui->seeingChartCanvas);
    seeingChartLayout->setContentsMargins(0, 0, 0, 0);
    seeingChartLayout->addWidget(m_seeingChart);

}

void DIMM::setupCanvasMouseStatusConnections()
{
    const auto bindMouseStatus = [this](FullFrameCanvas* canvas, const QString& cameraLabel) {
        if (!canvas) {
            return;
        }
        connect(canvas, &FullFrameCanvas::mousePositionChanged, this, [this, cameraLabel](int x, int y) {
            if (!hasActiveCapture()) {
                return;
            }
            m_lblStatusROI->setText(QStringLiteral("%1 鼠标: (%2, %3)").arg(cameraLabel).arg(x).arg(y));
        });
    };
    bindMouseStatus(m_fullFrameCanvas1, QStringLiteral("相机1"));
    bindMouseStatus(m_fullFrameCanvas2, QStringLiteral("相机2"));

}

void DIMM::refreshUi()
{
    refreshStatusUi();
    refreshCameraUi();
    refreshMeasurementUi();
    refreshPanelUi();
    refreshActionStates();
    syncCameraSelectionUi();
}

void DIMM::refreshStatusUi()
{
    if (m_lblStatusState) {
        m_lblStatusState->setText(m_statusText);
        m_lblStatusState->setStyleSheet(QStringLiteral("color: %1").arg(m_statusColor));
    }

    if (m_lblStatusFrames) {
        m_lblStatusFrames->setText(QStringLiteral("帧数: %1 帧").arg(activeRuntime().frameCount));
    }

    if (m_settingsDialog && m_settingsDialog->netStatusLabel) {
        QString netText;
        if (isSimulationCaptureActive()) {
            netText = m_commConnected
                          ? QStringLiteral("状态: 已连接/ 模拟模式不对外上报")
                          : QStringLiteral("状态: 模拟模式本地运行");
        } else if (m_commConnected) {
            netText = QStringLiteral("状态: 已连接");
        } else if (m_commConnecting) {
            netText = QStringLiteral("状态: 正在连接");
        } else {
            netText = QStringLiteral("状态: 未连接");
        }
        if (m_reporting && !isSimulationCaptureActive()) {
            netText += QStringLiteral(" / 正在上报");
        }
        UiStatusLevel netLevel = UiStatusLevel::Muted;
        if (isSimulationCaptureActive()) {
            netLevel = UiStatusLevel::Info;
        } else if (m_commConnected) {
            netLevel = UiStatusLevel::Success;
        } else if (m_commConnecting) {
            netLevel = UiStatusLevel::Warning;
        }
        m_settingsDialog->netStatusLabel->setText(netText);
        m_settingsDialog->netStatusLabel->setStyleSheet(
            QStringLiteral("color: %1").arg(uiStatusColor(netLevel)));
    }
    if (m_settingsDialog && m_settingsDialog->netConnectBtn) {
        m_settingsDialog->netConnectBtn->setText(m_commConnected
                                                     ? QStringLiteral("重新连接上位机")
                                                     : QStringLiteral("连接上位机"));
        m_settingsDialog->netConnectBtn->setEnabled(!m_connectingCameras && !m_commConnecting);
    }
}

void DIMM::refreshCameraUi()
{
    for (int i = 0; i < 2; ++i) {
        auto* statusLabel = i == 0 ? ui->lblCam1Status : ui->lblCam2Status;
        auto* infoLabel = i == 0 ? ui->lblCam1Info : ui->lblCam2Info;
        const bool online = m_cameraManager && m_cameraManager->isOpen(i);

        statusLabel->setText(cameraStatusText(online));
        statusLabel->setStyleSheet(QStringLiteral("color: %1").arg(uiStatusColor(cameraStatusLevel(online))));

        if (!online) {
            infoLabel->setText(QStringLiteral("SN: -- | -- fps"));
        }
    }
    updateCameraInfo();
}

void DIMM::refreshMeasurementUi()
{
    auto& runtime = activeRuntime();
    ui->lblPreviewMode->setText(currentPreviewModeText());
    if (m_lblStatusFrames) {
        m_lblStatusFrames->setText(QStringLiteral("帧数: %1 帧").arg(runtime.frameCount));
    }
    ui->lblStatFrames->setText(
        QStringLiteral("原始/入处理 %1 / %2")
            .arg(QString::number(runtime.frameCount),
                 QString::number(runtime.processedFrameCount)));
    ui->lblStatValid->setText(
        QStringLiteral("质心/配对: %1 / %2")
            .arg(QString::number(runtime.validCentroidCount),
                 QString::number(runtime.pairedSampleCount)));
    ui->lblStatLatency->setText(
        QStringLiteral("未配对丢帧延迟: %1 / %2 ms")
            .arg(QString::number(runtime.droppedUnpairedSampleCount),
                 QString::number(runtime.averageProcessingLatencyMs, 'f', 2)));
    ui->lblStatWindow->setText(
        QStringLiteral("同步抖动: %1 μs")
            .arg(QString::number(runtime.averageSyncJitterUs, 'f', 1)));
    ui->lblStatExposure->setText(
        QStringLiteral("曝光: %1/%2 μs | 模板: %3/%4 μs | AE: %5")
            .arg(QString::number(m_cameraExposureUs[0], 'f', 0),
                 QString::number(m_cameraExposureUs[1], 'f', 0),
                 QString::number(m_hotPixelTemplateExposureUs[0]),
                 QString::number(m_hotPixelTemplateExposureUs[1]),
                 autoExposureUiStatusText()));
    ui->lblStatAutoExposure->setText(
        m_autoExposureConfig.enabled
            ? QStringLiteral("ROI峰值 %1/%2 | AE质量: %3/%4%")
                  .arg(QString::number(m_latestAutoExposurePeakDn[0], 'f', 0),
                       QString::number(m_latestAutoExposurePeakDn[1], 'f', 0),
                       QString::number(m_latestAutoExposureUsableRatio[0] * 100.0, 'f', 0),
                       QString::number(m_latestAutoExposureUsableRatio[1] * 100.0, 'f', 0))
            : QStringLiteral("ROI峰值 --/-- | AE质量: --/--%"));

    if (!runtime.hasValidAtmosphere) {
        ui->lblR0Value->setText(QStringLiteral("--"));
        ui->lblSeeingValue->setText(QStringLiteral("--"));
        ui->lblThetaValue->setText(QStringLiteral("--"));
        ui->lblTauValue->setText(QStringLiteral("--"));
        return;
    }

    ui->lblR0Value->setText(QString::number(runtime.latestAtmosphere.r0, 'f', 1));
    ui->lblSeeingValue->setText(QString::number(runtime.latestAtmosphere.seeing, 'f', 2));
    ui->lblThetaValue->setText(QString::number(runtime.latestAtmosphere.theta0, 'f', 2));
    ui->lblTauValue->setText(QString::number(runtime.latestAtmosphere.tau0, 'f', 2));
}

void DIMM::refreshPanelUi()
{
    const bool roiVisible = (static_cast<int>(m_detailViewMode) & static_cast<int>(DetailViewMode::RoiOnly)) != 0;
    const bool chartsVisible = (static_cast<int>(m_detailViewMode) & static_cast<int>(DetailViewMode::ChartsOnly)) != 0;

    ui->topArea->setVisible(true);
    ui->roiImagesArea->setVisible(roiVisible);
    ui->chartsArea->setVisible(chartsVisible);

    if (chartsVisible) {
        ui->mainSplitter->setSizes({380, 560});
    } else if (roiVisible) {
        ui->mainSplitter->setSizes({600, 340});
    } else {
        ui->mainSplitter->setSizes({760, 140});
    }

    ui->btnToggleROI->setStyleSheet(toggleButtonStyle(roiVisible));
    ui->btnToggleCharts->setStyleSheet(toggleButtonStyle(chartsVisible));
}

void DIMM::refreshActionStates()
{
    const bool activeCapture = hasActiveCapture();
    const bool busy = m_connectingCameras;
    ui->btnStop->setEnabled(m_captureState != CaptureState::Idle && !busy);
    ui->actionConnectAll->setEnabled(!activeCapture && !busy);
    ui->actionDisconnectAll->setEnabled(!activeCapture && !busy);
    ui->btnSettings->setEnabled(!busy);
    ui->actionCameraSettings->setEnabled(!busy);
    ui->actionViewSettings->setEnabled(!busy);
    ui->btnStart->setEnabled(!busy);
    if (m_actionStartSimulation) {
        m_actionStartSimulation->setEnabled(!busy);
    }
    if (m_actionAlignmentMode) {
        const bool alignmentActive = m_captureState == CaptureState::Alignment;
        m_actionAlignmentMode->setChecked(alignmentActive);
        m_actionAlignmentMode->setText(alignmentActive ? QStringLiteral("退出对准")
                                                       : QStringLiteral("对准模式"));
        m_actionAlignmentMode->setEnabled(!busy && m_captureState != CaptureState::Live &&
                                          m_captureState != CaptureState::Simulation);
    }
    if (m_actionConfirmCamera1Polaris) {
        m_actionConfirmCamera1Polaris->setEnabled(m_captureState == CaptureState::Alignment &&
                                                  !busy &&
                                                  !m_alignmentCoarseActive);
        if (m_liveRuntime.hasConfirmedPolarisPosition[0]) {
            const QPointF pos = m_liveRuntime.confirmedPolarisPosition[0];
            m_actionConfirmCamera1Polaris->setText(
                QStringLiteral("相机1北极星 已确认(%1, %2)")
                    .arg(pos.x(), 0, 'f', 1)
                    .arg(pos.y(), 0, 'f', 1));
        } else {
            m_actionConfirmCamera1Polaris->setText(QStringLiteral("相机1北极星 未确认"));
        }
    }
    if (m_actionConfirmCamera2Polaris) {
        m_actionConfirmCamera2Polaris->setEnabled(m_captureState == CaptureState::Alignment &&
                                                  !busy &&
                                                  !m_alignmentCoarseActive);
        if (m_liveRuntime.hasConfirmedPolarisPosition[1]) {
            const QPointF pos = m_liveRuntime.confirmedPolarisPosition[1];
            m_actionConfirmCamera2Polaris->setText(
                QStringLiteral("相机2北极星 已确认(%1, %2)")
                    .arg(pos.x(), 0, 'f', 1)
                    .arg(pos.y(), 0, 'f', 1));
        } else {
            m_actionConfirmCamera2Polaris->setText(QStringLiteral("相机2北极星 未确认"));
        }
    }
    if (m_actionRetryCamera1PolarisSolve) {
        m_actionRetryCamera1PolarisSolve->setEnabled(m_captureState == CaptureState::Alignment &&
                                                     !busy &&
                                                     !m_alignmentCoarseActive &&
                                                     !m_alignmentSession.camera(0).lastFrame.empty());
    }
    if (m_actionRetryCamera2PolarisSolve) {
        m_actionRetryCamera2PolarisSolve->setEnabled(m_captureState == CaptureState::Alignment &&
                                                     !busy &&
                                                     !m_alignmentCoarseActive &&
                                                     !m_alignmentSession.camera(1).lastFrame.empty());
    }
    if (m_actionRetryBothPolarisSolve) {
        m_actionRetryBothPolarisSolve->setEnabled(m_captureState == CaptureState::Alignment &&
                                                  !busy &&
                                                  !m_alignmentCoarseActive &&
                                                  (!m_alignmentSession.camera(0).lastFrame.empty() ||
                                                   !m_alignmentSession.camera(1).lastFrame.empty()));
    }
    if (m_actionToggleCoarseAlignment) {
        const bool alignmentActive = m_captureState == CaptureState::Alignment;
        m_actionToggleCoarseAlignment->setEnabled(alignmentActive && !busy);
        m_actionToggleCoarseAlignment->setChecked(m_alignmentCoarseActive);
        m_actionToggleCoarseAlignment->setText(m_alignmentCoarseActive
                                                   ? QStringLiteral("停止粗对准")
                                                   : QStringLiteral("开始粗对准"));
    }
    const bool alignmentControlsVisible = m_captureState == CaptureState::Alignment;
    if (m_btnConfirmCamera1Polaris) {
        m_btnConfirmCamera1Polaris->setVisible(alignmentControlsVisible);
        m_btnConfirmCamera1Polaris->setEnabled(m_actionConfirmCamera1Polaris &&
                                               m_actionConfirmCamera1Polaris->isEnabled());
        m_btnConfirmCamera1Polaris->setText(m_liveRuntime.hasConfirmedPolarisPosition[0]
                                                ? QStringLiteral("重新确认")
                                                : QStringLiteral("人工确认"));
    }
    if (m_btnConfirmCamera2Polaris) {
        m_btnConfirmCamera2Polaris->setVisible(alignmentControlsVisible);
        m_btnConfirmCamera2Polaris->setEnabled(m_actionConfirmCamera2Polaris &&
                                               m_actionConfirmCamera2Polaris->isEnabled());
        m_btnConfirmCamera2Polaris->setText(m_liveRuntime.hasConfirmedPolarisPosition[1]
                                                ? QStringLiteral("重新确认")
                                                : QStringLiteral("人工确认"));
    }
    if (m_btnRetryCamera1PolarisSolve) {
        m_btnRetryCamera1PolarisSolve->setVisible(alignmentControlsVisible);
        m_btnRetryCamera1PolarisSolve->setEnabled(m_actionRetryCamera1PolarisSolve &&
                                                  m_actionRetryCamera1PolarisSolve->isEnabled());
    }
    if (m_btnRetryCamera2PolarisSolve) {
        m_btnRetryCamera2PolarisSolve->setVisible(alignmentControlsVisible);
        m_btnRetryCamera2PolarisSolve->setEnabled(m_actionRetryCamera2PolarisSolve &&
                                                  m_actionRetryCamera2PolarisSolve->isEnabled());
    }
    if (m_btnRetryBothPolarisSolve) {
        m_btnRetryBothPolarisSolve->setVisible(alignmentControlsVisible);
        m_btnRetryBothPolarisSolve->setEnabled(m_actionRetryBothPolarisSolve &&
                                               m_actionRetryBothPolarisSolve->isEnabled());
    }
    if (m_btnToggleCoarseAlignment) {
        m_btnToggleCoarseAlignment->setVisible(alignmentControlsVisible);
        m_btnToggleCoarseAlignment->setEnabled(m_actionToggleCoarseAlignment &&
                                               m_actionToggleCoarseAlignment->isEnabled());
        m_btnToggleCoarseAlignment->setText(m_alignmentCoarseActive
                                                ? QStringLiteral("停止粗对准")
                                                : QStringLiteral("开始粗对准"));
    }

    switch (m_captureState) {
    case CaptureState::Idle:
        ui->btnStart->setText(QStringLiteral("开始采集"));
        if (m_actionStartSimulation) {
            m_actionStartSimulation->setText(QStringLiteral("模拟采集"));
        }
        break;
    case CaptureState::Paused:
        ui->btnStart->setText(QStringLiteral("继续采集"));
        if (m_actionStartSimulation) {
            m_actionStartSimulation->setText(QStringLiteral("模拟采集"));
        }
        break;
    case CaptureState::Live:
        ui->btnStart->setText(QStringLiteral("暂停采集"));
        if (m_actionStartSimulation) {
            m_actionStartSimulation->setText(QStringLiteral("切换到模拟"));
        }
        break;
    case CaptureState::Simulation:
        ui->btnStart->setText(QStringLiteral("开始采集"));
        if (m_actionStartSimulation) {
            m_actionStartSimulation->setText(QStringLiteral("暂停模拟"));
        }
        break;
    case CaptureState::Alignment:
        ui->btnStart->setText(QStringLiteral("开始采集"));
        if (m_actionStartSimulation) {
            m_actionStartSimulation->setText(QStringLiteral("模拟采集"));
        }
        break;
    }
}

void DIMM::syncCameraSelectionUi()
{
    ui->lblFullframeLabel->setText(QStringLiteral("双相机全画幅预览"));
    ui->lblPreviewMode->setText(currentPreviewModeText());

    if (m_lblFullFrameCam1) {
        m_lblFullFrameCam1->setText(QStringLiteral("全画幅预览- 相机1"));
    }
    if (m_lblFullFrameCam2) {
        m_lblFullFrameCam2->setText(QStringLiteral("全画幅预览- 相机2"));
    }
}

QString DIMM::currentPreviewModeText() const
{
    if (m_captureState == CaptureState::Alignment) {
        return QStringLiteral("对准模式 (双相机/ 低频全画幅/ 不计算不保存)");
    }

    if (m_captureState == CaptureState::Simulation) {
        return QStringLiteral("模拟模式 (双相机/ 30s 预览 / 1Hz 计算)");
    }

    if (m_captureState != CaptureState::Live) {
        return QStringLiteral("实时模式 (双相机/ 30s 预览 / 实时采集)");
    }

    const auto& runtime = activeRuntime();
    if (runtime.frameCount <= 0) {
        if (m_liveStartupPhase == LiveStartupPhase::LocatePair) {
            return QStringLiteral("实时模式 (双相机全画幅定位 / 等待首帧)");
        }
        return m_configTriggerMode == 0
                   ? QStringLiteral("实时模式 (双相机/ 30s 预览 / 连续采集 / 等待首帧)")
                   : QStringLiteral("实时模式 (双相机/ 30s 预览 / 硬件触发 / 等待外部触发)");
    }

    if (m_configTriggerMode != 0) {
        const bool cam1Ready = runtime.frameCountPerCamera[0] > 0;
        const bool cam2Ready = runtime.frameCountPerCamera[1] > 0;
        if (cam1Ready && !cam2Ready) {
            return QStringLiteral("实时模式 (硬件触发 / 相机1已到达/ 相机2等待触发)");
        }
        if (!cam1Ready && cam2Ready) {
            return QStringLiteral("实时模式 (硬件触发 / 相机2已到达/ 相机1等待触发)");
        }
    }

    if (m_liveStartupPhase == LiveStartupPhase::LocatePair) {
        return QStringLiteral("实时模式 (双相机全画幅定位中/ 等待独立 ROI 确认)");
    }

    const bool previewRefreshed =
        runtime.lastLivePreviewUpdateMs[0] >= 0 || runtime.lastLivePreviewUpdateMs[1] >= 0;
    if (previewRefreshed) {
        return QStringLiteral("实时模式 (双相机 / 30s 预览 / 已收到图像 / 预览30s刷新)");
    }
    return QStringLiteral("实时模式 (双相机/ 30s 预览 / 已收到图像");
}

void DIMM::setStatusMessage(const QString& text, const QString& color)
{
    m_statusText = text;
    m_statusColor = color;
    refreshStatusUi();
}

void DIMM::setStatusMessage(const QString& text, UiStatusLevel level)
{
    setStatusMessage(text, uiStatusColor(level));
}

void DIMM::setAlignmentSolveLabel(int cameraIndex, const QString& text, UiStatusLevel level)
{
    if (!isValidCameraIndex(cameraIndex)) {
        return;
    }

    QLabel* label = cameraIndex == 0 ? m_lblAlignmentSolveCam1 : m_lblAlignmentSolveCam2;
    if (!label) {
        return;
    }

    label->setText(text);
    label->setStyleSheet(QStringLiteral("color: %1").arg(uiStatusColor(level)));
    label->setVisible(m_captureState == CaptureState::Alignment);
}

void DIMM::setDetailViewMode(DetailViewMode mode)
{
    m_detailViewMode = mode;
    refreshPanelUi();
}
