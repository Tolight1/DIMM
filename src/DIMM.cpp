#include "DIMM.h"
#include "CameraManager.h"
#include "ImageProcessor.h"
#include "CanvasWidgets.h"
#include "CommManager.h"

#include <QDebug>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QCheckBox>
#include <QPushButton>
#include <QTabWidget>
#include <QDialogButtonBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QMessageBox>
#include <QFileDialog>
#include <QRandomGenerator>
#include <QTimer>
#include <QTime>
#include <QDateTime>
#include <QDir>
#include <QShortcut>

// ============================================================
// SettingsDialog
// ============================================================

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("设置");
    setMinimumSize(700, 500);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    auto* tabWidget = new QTabWidget(this);

    // ---- Tab 1: 相机设置 ----
    auto* camTab = new QWidget();
    auto* camLayout = new QVBoxLayout(camTab);

    // 连接说明
    auto* infoGroup = new QGroupBox("连接说明");
    auto* infoLayout = new QVBoxLayout(infoGroup);
    auto* infoLabel = new QLabel(
        "1. 确保相机已通过网线连接到计算机\n"
        "2. 设置计算机IP地址与相机在同一网段\n"
        "3. 点击\"连接所有相机\"自动发现并连接\n"
        "4. 连接成功后相机状态显示为绿色");
    infoLabel->setWordWrap(true);
    infoLayout->addWidget(infoLabel);
    camLayout->addWidget(infoGroup);

    // 采集参数
    auto* acqGroup = new QGroupBox("采集参数");
    auto* acqLayout = new QGridLayout(acqGroup);
    acqLayout->addWidget(new QLabel("曝光时间 (μs):"), 0, 0);
    exposureEdit = new QLineEdit("2000");
    acqLayout->addWidget(exposureEdit, 0, 1);
    acqLayout->addWidget(new QLabel("增益 (dB):"), 1, 0);
    gainEdit = new QLineEdit("10.0");
    acqLayout->addWidget(gainEdit, 1, 1);
    camLayout->addWidget(acqGroup);

    // 触发模式
    auto* triggerGroup = new QGroupBox("触发模式");
    auto* triggerLayout = new QVBoxLayout(triggerGroup);
    triggerContinuous = new QRadioButton("连续采集");
    triggerContinuous->setChecked(true);
    triggerHardware = new QRadioButton("硬件触发");
    triggerLayout->addWidget(triggerContinuous);
    triggerLayout->addWidget(triggerHardware);
    camLayout->addWidget(triggerGroup);

    camLayout->addStretch();
    tabWidget->addTab(camTab, "📷 相机设置");

    // ---- Tab 2: 图像处理 ----
    auto* procTab = new QWidget();
    auto* procLayout = new QVBoxLayout(procTab);

    auto* centroidGroup = new QGroupBox("质心算法");
    auto* centroidLayout = new QVBoxLayout(centroidGroup);
    procGravity = new QRadioButton("重心法 (默认)");
    procGravity->setChecked(true);
    procGaussian = new QRadioButton("高斯拟合");
    centroidLayout->addWidget(procGravity);
    centroidLayout->addWidget(procGaussian);
    procLayout->addWidget(centroidGroup);

    auto* preprocessGroup = new QGroupBox("预处理参数");
    auto* preprocessLayout = new QGridLayout(preprocessGroup);
    preprocessLayout->addWidget(new QLabel("高斯滤波核大小:"), 0, 0);
    procKernelSize = new QLineEdit("3");
    preprocessLayout->addWidget(procKernelSize, 0, 1);
    preprocessLayout->addWidget(new QLabel("高斯标准差 σ:"), 1, 0);
    procSigma = new QLineEdit("1.0");
    preprocessLayout->addWidget(procSigma, 1, 1);
    procLayout->addWidget(preprocessGroup);

    procLayout->addStretch();
    tabWidget->addTab(procTab, "🖼️ 图像处理");

    // ---- Tab 3: 系统参数 ----
    auto* sysTab = new QWidget();
    auto* sysLayout = new QVBoxLayout(sysTab);

    auto* opticsGroup = new QGroupBox("光学系统");
    auto* opticsLayout = new QGridLayout(opticsGroup);
    opticsLayout->addWidget(new QLabel("望远镜口径 D (cm):"), 0, 0);
    opticsD = new QLineEdit("56");
    opticsLayout->addWidget(opticsD, 0, 1);
    opticsLayout->addWidget(new QLabel("焦距 f (cm):"), 1, 0);
    opticsF = new QLineEdit("269");
    opticsLayout->addWidget(opticsF, 1, 1);
    sysLayout->addWidget(opticsGroup);

    auto* detectorGroup = new QGroupBox("探测器");
    auto* detectorLayout = new QGridLayout(detectorGroup);
    detectorLayout->addWidget(new QLabel("像素尺寸 (μm):"), 0, 0);
    detectorPixelSize = new QLineEdit("2.5");
    detectorLayout->addWidget(detectorPixelSize, 0, 1);
    detectorLayout->addWidget(new QLabel("工作波长 (nm):"), 1, 0);
    detectorWavelength = new QLineEdit("550");
    detectorLayout->addWidget(detectorWavelength, 1, 1);
    sysLayout->addWidget(detectorGroup);

    sysLayout->addStretch();
    tabWidget->addTab(sysTab, "🔭 系统参数");

    // ---- Tab 4: 数据存储 ----
    auto* storeTab = new QWidget();
    auto* storeLayout = new QVBoxLayout(storeTab);

    auto* pathGroup = new QGroupBox("存储路径");
    auto* pathLayout = new QHBoxLayout(pathGroup);
    storagePathEdit = new QLineEdit("D:/C-DIMM/data");
    auto* browseBtn = new QPushButton("浏览...");
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, "选择存储路径", storagePathEdit->text());
        if (!dir.isEmpty()) storagePathEdit->setText(dir);
    });
    pathLayout->addWidget(storagePathEdit);
    pathLayout->addWidget(browseBtn);
    storeLayout->addWidget(pathGroup);

    auto* imageGroup = new QGroupBox("图像存储");
    auto* imageLayout = new QVBoxLayout(imageGroup);
    saveImagesCheck = new QCheckBox("保存原始图像");
    saveImagesCheck->setChecked(false);
    imageLayout->addWidget(saveImagesCheck);
    auto* intervalLayout = new QHBoxLayout();
    intervalLayout->addWidget(new QLabel("保存间隔 (帧):"));
    saveIntervalEdit = new QLineEdit("10");
    intervalLayout->addWidget(saveIntervalEdit);
    imageLayout->addLayout(intervalLayout);
    storeLayout->addWidget(imageGroup);

    auto* resultGroup = new QGroupBox("结果存储 (CSV)");
    auto* resultLayout = new QVBoxLayout(resultGroup);
    auto* resultInfo = new QLabel("自动保存: 帧号、时间戳、质心坐标、大气参数");
    resultInfo->setWordWrap(true);
    resultLayout->addWidget(resultInfo);
    storeLayout->addWidget(resultGroup);

    storeLayout->addStretch();
    tabWidget->addTab(storeTab, "💾 数据存储");

    // ---- Tab 5: 网络通信 ----
    auto* netTab = new QWidget();
    auto* netLayout = new QVBoxLayout(netTab);

    auto* connGroup = new QGroupBox("上位机连接");
    auto* connLayout = new QGridLayout(connGroup);
    connLayout->addWidget(new QLabel("IP地址:"), 0, 0);
    netIpEdit = new QLineEdit("192.168.1.100");
    connLayout->addWidget(netIpEdit, 0, 1);
    connLayout->addWidget(new QLabel("端口:"), 1, 0);
    netPortEdit = new QLineEdit("5000");
    connLayout->addWidget(netPortEdit, 1, 1);
    netStatusLabel = new QLabel("状态: 未连接");
    netStatusLabel->setStyleSheet("color: #888");
    connLayout->addWidget(netStatusLabel, 2, 0, 1, 2);
    netLayout->addWidget(connGroup);

    auto* protoGroup = new QGroupBox("通信协议");
    auto* protoLayout = new QVBoxLayout(protoGroup);
    auto* protoInfo = new QLabel(
        "协议: TCP 二进制\n"
        "帧头: 0xAA55\n"
        "校验: XOR\n\n"
        "指令:\n"
        "  上位机→设备: 0x01 开始上报 / 0x02 停止 / 0x03 查询状态\n"
        "  设备→上位机: 0x81 测量结果 / 0x82 设备状态 / 0x83 应答");
    protoInfo->setWordWrap(true);
    protoLayout->addWidget(protoInfo);
    netLayout->addWidget(protoGroup);

    netLayout->addStretch();
    tabWidget->addTab(netTab, "🌐 网络通信");

    mainLayout->addWidget(tabWidget);

    // 按钮
    auto* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply);
    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        applySettings();
        accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* applyBtn = buttonBox->button(QDialogButtonBox::Apply);
    if (applyBtn) connect(applyBtn, &QPushButton::clicked, this, &SettingsDialog::applySettings);
    mainLayout->addWidget(buttonBox);
}

void SettingsDialog::applySettings()
{
    if (onApplyCamera && exposureEdit && gainEdit) {
        onApplyCamera(exposureEdit->text().toDouble(), gainEdit->text().toDouble());
    }
    if (onApplyTriggerMode && triggerContinuous && triggerHardware) {
        int mode = triggerContinuous->isChecked() ? 0 : 1;
        onApplyTriggerMode(mode);
    }
    if (onApplyProcessing && procGravity && procGaussian && procKernelSize && procSigma) {
        int method = procGravity->isChecked() ? 0 : 1;
        onApplyProcessing(procKernelSize->text().toInt(), procSigma->text().toDouble(), method);
    }
    if (onApplyOptics && opticsD && opticsF && detectorPixelSize && detectorWavelength) {
        onApplyOptics(opticsD->text().toDouble(), opticsF->text().toDouble(),
                      detectorWavelength->text().toDouble(), detectorPixelSize->text().toDouble());
    }
    if (onApplyStorage && storagePathEdit) {
        onApplyStorage(storagePathEdit->text(), saveImagesCheck->isChecked(),
                       saveIntervalEdit->text().toInt());
    }
    if (onApplyNetwork && netIpEdit && netPortEdit) {
        onApplyNetwork(netIpEdit->text(), netPortEdit->text().toUShort());
    }
}

// ============================================================
// DIMM 主窗口
// ============================================================

DIMM::DIMM(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui_DIMM)
    , m_frameCount(0)
    , m_isCapturing(false)
    , m_currentCamera(1)
{
    ui->setupUi(this);

    // 设置对话框
    m_settingsDialog = new SettingsDialog(this);

    // 创建状态栏标签
    m_lblStatusState = new QLabel("状态: 就绪");
    m_lblStatusState->setObjectName("lblStatusState");
    ui->statusbar->addWidget(m_lblStatusState);

    m_lblStatusROI = new QLabel("当前ROI: (1024, 768) 64×64");
    m_lblStatusROI->setObjectName("lblStatusROI");
    ui->statusbar->addWidget(m_lblStatusROI);

    m_lblStatusFrames = new QLabel("已采集: 0 帧");
    m_lblStatusFrames->setObjectName("lblStatusFrames");
    ui->statusbar->addWidget(m_lblStatusFrames);

    // 模拟数据定时器
    m_simulationTimer = new QTimer(this);
    connect(m_simulationTimer, &QTimer::timeout, this, &DIMM::onUpdateSimulation);

    // 初始化ROI表格
    ui->roiTable->setColumnCount(5);
    ui->roiTable->setHorizontalHeaderLabels({"时段", "X", "Y", "宽度", "高度"});
    ui->roiTable->horizontalHeader()->setStretchLastSection(true);

    // 初始ROI数据
    struct { const char* time; int x, y, w, h; } roiData[] = {
        {"20:00", 820,  650,  64, 64},
        {"20:30", 1024, 768,  64, 64},
        {"21:00", 1228, 886,  64, 64},
        {"21:30", 1432, 1004, 64, 64},
        {"22:00", 1636, 1122, 64, 64},
    };
    for (auto& r : roiData) {
        int row = ui->roiTable->rowCount();
        ui->roiTable->insertRow(row);
        ui->roiTable->setItem(row, 0, new QTableWidgetItem(r.time));
        ui->roiTable->setItem(row, 1, new QTableWidgetItem(QString::number(r.x)));
        ui->roiTable->setItem(row, 2, new QTableWidgetItem(QString::number(r.y)));
        ui->roiTable->setItem(row, 3, new QTableWidgetItem(QString::number(r.w)));
        ui->roiTable->setItem(row, 4, new QTableWidgetItem(QString::number(r.h)));
    }

    // 分割器初始大小
    ui->mainSplitter->setSizes({500, 400});

    // 初始化时段显示为当前时间
    ui->lblROITimeCurrent->setText(QString("当前时间: %1").arg(QTime::currentTime().toString("HH:mm")));
    ui->lblROITimeNext->setText("等待匹配ROI时段...");

    // 初始隐藏底部区域
    ui->roiImagesArea->setVisible(false);
    ui->chartsArea->setVisible(false);
    ui->stackedWidget->setCurrentIndex(0);

    // 连接信号槽
    setupConnections();

    // ---- 初始化项目组件 ----

    // 相机管理器
    m_cameraManager = &CameraManager::instance();
    m_cameraManager->init();

    // 图像处理器
    m_imageProcessor = new ImageProcessor(this);

    // 创建Canvas组件并替换占位符
    // 全画幅预览Canvas
    m_fullFrameCanvas = new FullFrameCanvas(ui->previewCanvas);
    // 删除旧布局，让canvas通过resizeEvent自适应父容器
    auto* previewLayout = ui->previewCanvas->layout();
    if (previewLayout) {
        while (previewLayout->count() > 0) {
            auto* item = previewLayout->takeAt(0);
            if (item->widget()) item->widget()->deleteLater();
            delete item;
        }
        delete previewLayout;
    }

    // ROI星图Canvas（相机1）
    m_cam1RoiCanvas = new RoiStarCanvas(ui->cam1ROICanvas);
    m_cam1RoiCanvas->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_cam1RoiCanvas->setMinimumSize(0, 0);
    auto* cam1Layout = ui->cam1ROICanvas->layout();
    if (cam1Layout) {
        while (cam1Layout->count() > 0) {
            auto* item = cam1Layout->takeAt(0);
            if (item->widget()) item->widget()->deleteLater();
            delete item;
        }
    }
    auto* newCam1Layout = new QVBoxLayout(ui->cam1ROICanvas);
    newCam1Layout->setContentsMargins(0, 0, 0, 0);
    newCam1Layout->setSpacing(0);
    newCam1Layout->addWidget(m_cam1RoiCanvas, 1);

    // ROI星图Canvas（相机2）
    m_cam2RoiCanvas = new RoiStarCanvas(ui->cam2ROICanvas);
    m_cam2RoiCanvas->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* cam2Layout = ui->cam2ROICanvas->layout();
    if (cam2Layout) {
        while (cam2Layout->count() > 0) {
            auto* item = cam2Layout->takeAt(0);
            if (item->widget()) item->widget()->deleteLater();
            delete item;
        }
    }
    auto* newCam2Layout = new QVBoxLayout(ui->cam2ROICanvas);
    newCam2Layout->setContentsMargins(0, 0, 0, 0);
    newCam2Layout->setSpacing(0);
    newCam2Layout->addWidget(m_cam2RoiCanvas);

    // 参数曲线：替换占位QFrame为ChartWidget
    m_r0Chart = new ChartWidget(ui->r0ChartCanvas);
    auto* r0ChartLayout = new QVBoxLayout(ui->r0ChartCanvas);
    r0ChartLayout->setContentsMargins(0, 0, 0, 0);
    r0ChartLayout->addWidget(m_r0Chart);

    m_seeingChart = new ChartWidget(ui->seeingChartCanvas);
    auto* seeingChartLayout = new QVBoxLayout(ui->seeingChartCanvas);
    seeingChartLayout->setContentsMargins(0, 0, 0, 0);
    seeingChartLayout->addWidget(m_seeingChart);

    // 设置初始ROI到ImageProcessor
    updateCurrentRoi();

    // 设置设置对话框回调
    m_settingsDialog->onApplyCamera = [this](double exposure, double gain) {
        if (m_cameraManager->isOpen(0)) {
            m_cameraManager->setExposure(0, exposure);
            m_cameraManager->setGain(0, gain);
        }
        if (m_cameraManager->isOpen(1)) {
            m_cameraManager->setExposure(1, exposure);
            m_cameraManager->setGain(1, gain);
        }
    };
    m_settingsDialog->onApplyStorage = [this](QString path, bool saveImages, int interval) {
        m_dataPath = path;
        m_saveImages = saveImages;
        m_saveInterval = qMax(1, interval);
    };
    m_settingsDialog->onApplyTriggerMode = [this](int mode) {
        // 0=连续采集, 1=硬件触发
        for (int i = 0; i < 2; ++i) {
            if (m_cameraManager->isOpen(i)) {
                m_cameraManager->setTriggerMode(i,
                    mode == 0 ? TriggerMode::Continuous
                              : TriggerMode::Hardware);
            }
        }
    };
    m_settingsDialog->onApplyProcessing = [this](int kernelSize, double sigma, int method) {
        m_imageProcessor->setGaussianKernelSize(kernelSize);
        m_imageProcessor->setGaussianSigma(sigma);
        m_imageProcessor->setCentroidMethod(method);
    };
    m_settingsDialog->onApplyOptics = [this](double D, double f, double lambda, double pixelSize) {
        m_imageProcessor->setOpticalParams(D, f, lambda, pixelSize);
    };

    // 连接相机信号（QueuedConnection确保回调在UI线程处理）
    connect(m_cameraManager, &CameraManager::frameReady,
            this, &DIMM::onFrameReady, Qt::QueuedConnection);
    connect(m_cameraManager, &CameraManager::cameraConnected,
            this, &DIMM::onCameraConnected, Qt::QueuedConnection);
    connect(m_cameraManager, &CameraManager::cameraDisconnected,
            this, &DIMM::onCameraDisconnected, Qt::QueuedConnection);
    connect(m_cameraManager, &CameraManager::cameraError,
            this, &DIMM::onCameraError, Qt::QueuedConnection);

    // 连接图像处理器信号
    connect(m_imageProcessor, &ImageProcessor::centroidReady,
            this, [this](int camIdx, double x, double y) {
                if (camIdx < 0 || camIdx >= 2) return;
                m_centroidX[camIdx] = x;
                m_centroidY[camIdx] = y;
                if (camIdx == 0) {
                    ui->lblCam1ROICoord->setText(
                        QString("(%1, %2)").arg(x, 0, 'f', 1).arg(y, 0, 'f', 1));
                } else {
                    ui->lblCam2ROICoord->setText(
                        QString("(%1, %2)").arg(x, 0, 'f', 1).arg(y, 0, 'f', 1));
                }
            });

    connect(m_imageProcessor, &ImageProcessor::roiImageReady,
            this, [this](int camIdx, cv::Mat roiImage) {
                if (camIdx < 0 || camIdx >= 2) return;
                if (camIdx == 0) {
                    m_cam1RoiCanvas->setRoiImage(roiImage);
                    m_cam1RoiCanvas->setCentroid(m_centroidX[0], m_centroidY[0]);
                } else {
                    m_cam2RoiCanvas->setRoiImage(roiImage);
                    m_cam2RoiCanvas->setCentroid(m_centroidX[1], m_centroidY[1]);
                }
            });

    connect(m_imageProcessor, &ImageProcessor::atmosphereReady,
            this, [this](double r0, double seeing, double theta0, double tau0) {
                ui->lblR0Value->setText(QString::number(r0, 'f', 1));
                ui->lblSeeingValue->setText(QString::number(seeing, 'f', 2));
                ui->lblThetaValue->setText(QString::number(theta0, 'f', 1));
                ui->lblTauValue->setText(QString::number(tau0, 'f', 1));

                // 更新图表
                if (m_r0Chart) m_r0Chart->addDataPoint(r0, seeing, m_centroidX[0], m_centroidY[0]);
                if (m_seeingChart) m_seeingChart->addDataPoint(r0, seeing, m_centroidX[1], m_centroidY[1]);

                // 保存结果数据
                saveResultRow(m_frameCount, 0, m_centroidX[0], m_centroidY[0], r0, seeing);
            });

    // 连接全画幅Canvas鼠标位置信号
    connect(m_fullFrameCanvas, &FullFrameCanvas::mousePositionChanged,
            this, [this](int x, int y) {
                m_lblStatusROI->setText(
                    QString("鼠标: (%1, %2)").arg(x).arg(y));
            });

    // 合并的1Hz定时器（相机信息 + ROI时段匹配）
    m_1hzTimer = new QTimer(this);
    connect(m_1hzTimer, &QTimer::timeout, this, &DIMM::on1hzTick);
    m_1hzTimer->start(1000);

    // 异步文件刷盘定时器（每2秒刷一次，避免频繁IO阻塞主线程）
    m_fileFlushTimer = new QTimer(this);
    connect(m_fileFlushTimer, &QTimer::timeout, this, &DIMM::flushPendingWrites);
    m_fileFlushTimer->start(2000);

    // ---- 通信管理 ----
    m_commManager = new CommManager(this);
    connect(m_commManager, &CommManager::connected, this, [this]() {
        m_lblStatusState->setText("上位机已连接");
        m_lblStatusState->setStyleSheet("color: #8bc34a");
        if (m_settingsDialog->netStatusLabel)
            m_settingsDialog->netStatusLabel->setText("状态: 已连接");
    });
    connect(m_commManager, &CommManager::disconnected, this, [this]() {
        m_lblStatusState->setText("上位机已断开");
        m_lblStatusState->setStyleSheet("color: #ff9800");
        if (m_settingsDialog->netStatusLabel)
            m_settingsDialog->netStatusLabel->setText("状态: 未连接");
        m_reporting = false;
        m_reportTimer->stop();
    });
    connect(m_commManager, &CommManager::connectionError, this, [this](const QString& msg) {
        m_lblStatusState->setText("通信错误: " + msg);
        m_lblStatusState->setStyleSheet("color: #f44336");
    });
    connect(m_commManager, &CommManager::commandReceived, this, &DIMM::onCommCommand);

    // 上报定时器（1Hz）
    m_reportTimer = new QTimer(this);
    connect(m_reportTimer, &QTimer::timeout, this, [this]() {
        reportMeasurement();
        reportDeviceStatus();
    });
    m_startTimeMs = QDateTime::currentMSecsSinceEpoch();

    // 网络设置回调
    m_settingsDialog->onApplyNetwork = [this](QString ip, quint16 port) {
        m_commManager->setRemoteAddress(ip, port);
    };

    // 自动连接上位机
    m_commManager->connectToHost("192.168.1.100", 5000);
}

DIMM::~DIMM()
{
    // 刷盘未写入的数据
    flushPendingWrites();
    delete m_resultStream;
    m_resultStream = nullptr;
    delete ui;
}

void DIMM::setupConnections()
{
    // ---- 工具栏 ----
    connect(ui->btnStart,    &QAction::triggered, this, &DIMM::onStartCapture);
    connect(ui->btnStop,     &QAction::triggered, this, &DIMM::onStopCapture);
    connect(ui->btnFullFrame,&QAction::triggered, this, &DIMM::onShowMainPage);
    connect(ui->btnROI,      &QAction::triggered, this, &DIMM::onShowRoiPage);
    connect(ui->btnSettings, &QAction::triggered, this, &DIMM::onShowSettings);

    // ---- 底部按钮 ----
    connect(ui->btnToggleROI,    &QPushButton::clicked, this, &DIMM::onToggleRoiImages);
    connect(ui->btnToggleCharts, &QPushButton::clicked, this, &DIMM::onToggleCharts);

    // ---- 相机切换 ----
    connect(ui->btnCam1, &QPushButton::clicked, this, &DIMM::onSwitchCamera1);
    connect(ui->btnCam2, &QPushButton::clicked, this, &DIMM::onSwitchCamera2);

    // ---- 菜单 - 文件 ----
    connect(ui->actionSaveConfig,  &QAction::triggered, this, &DIMM::onSaveConfig);
    connect(ui->actionLoadConfig,  &QAction::triggered, this, &DIMM::onLoadConfig);
    connect(ui->actionExportData,  &QAction::triggered, this, &DIMM::onExportData);
    connect(ui->actionExportReport,&QAction::triggered, this, &DIMM::onExportReport);
    connect(ui->actionExit,        &QAction::triggered, this, &QMainWindow::close);

    // ---- 菜单 - 相机 ----
    connect(ui->actionConnectAll,     &QAction::triggered, this, &DIMM::onConnectAll);
    connect(ui->actionDisconnectAll,  &QAction::triggered, this, &DIMM::onDisconnectAll);
    connect(ui->actionCameraSettings, &QAction::triggered, this, &DIMM::onShowSettings);

    // ---- 菜单 - 视图 ----
    connect(ui->actionViewMain,    &QAction::triggered, this, &DIMM::onShowMainPage);
    connect(ui->actionViewROI,     &QAction::triggered, this, &DIMM::onShowRoiPage);
    connect(ui->actionViewSettings,&QAction::triggered, this, &DIMM::onShowSettings);
    connect(ui->actionToggleROIImages, &QAction::triggered, this, &DIMM::onToggleRoiImages);
    connect(ui->actionToggleCharts,    &QAction::triggered, this, &DIMM::onToggleCharts);

    // ---- 菜单 - 工具 ----
    connect(ui->actionROISchedule,   &QAction::triggered, this, &DIMM::onShowRoiPage);
    connect(ui->actionTrajectoryCalc,&QAction::triggered, this, [this]() {
        QMessageBox::information(this, "星点轨迹计算", "正在计算星点轨迹...");
    });

    // ---- 菜单 - 帮助 ----
    connect(ui->actionAbout, &QAction::triggered, this, &DIMM::onAbout);

    // ---- ROI表格按钮 ----
    connect(ui->btnAddROI, &QPushButton::clicked, this, [this]() {
        int row = ui->roiTable->rowCount();
        ui->roiTable->insertRow(row);
        ui->roiTable->setItem(row, 0, new QTableWidgetItem("--:--"));
        ui->roiTable->setItem(row, 1, new QTableWidgetItem("0"));
        ui->roiTable->setItem(row, 2, new QTableWidgetItem("0"));
        ui->roiTable->setItem(row, 3, new QTableWidgetItem("64"));
        ui->roiTable->setItem(row, 4, new QTableWidgetItem("64"));
        ui->roiTable->selectRow(row);
    });

    connect(ui->btnDeleteROI, &QPushButton::clicked, this, [this]() {
        int row = ui->roiTable->currentRow();
        if (row >= 0) {
            ui->roiTable->removeRow(row);
        }
    });

    connect(ui->btnImportTrajectory, &QPushButton::clicked, this, [this]() {
        QString file = QFileDialog::getOpenFileName(
            this, "导入轨迹文件", "", "文本文件 (*.txt *.csv)");
        if (!file.isEmpty()) {
            QMessageBox::information(this, "导入", "已选择: " + file);
        }
    });

    // ---- ROI表格联动 ----
    connect(ui->roiTable, &QTableWidget::cellChanged, this, [this](int row, int col) {
        Q_UNUSED(col);
        if (row < 0) return;
        updateCurrentRoi();
        auto* xi = ui->roiTable->item(row, 1);
        auto* yi = ui->roiTable->item(row, 2);
        auto* wi = ui->roiTable->item(row, 3);
        auto* hi = ui->roiTable->item(row, 4);
        if (xi && yi && wi && hi) {
            ui->lblROIXValue->setText(xi->text());
            ui->lblROIYValue->setText(yi->text());
            ui->lblROIWValue->setText(wi->text());
            ui->lblROIHValue->setText(hi->text());
            m_lblStatusROI->setText(
                QString("当前ROI: (%1, %2) %3×%4")
                    .arg(xi->text(), yi->text(), wi->text(), hi->text()));
        }
    });

    connect(ui->roiTable, &QTableWidget::currentCellChanged, this,
        [this](int row, int, int, int) {
            if (row < 0) return;
            updateCurrentRoi();
            auto* xi = ui->roiTable->item(row, 1);
            auto* yi = ui->roiTable->item(row, 2);
            auto* wi = ui->roiTable->item(row, 3);
            auto* hi = ui->roiTable->item(row, 4);
            if (xi && yi && wi && hi) {
                ui->lblROIXValue->setText(xi->text());
                ui->lblROIYValue->setText(yi->text());
                ui->lblROIWValue->setText(wi->text());
                ui->lblROIHValue->setText(hi->text());
                m_lblStatusROI->setText(
                    QString("当前ROI: (%1, %2) %3×%4")
                        .arg(xi->text(), yi->text(), wi->text(), hi->text()));
            }
        });

    // ---- 快捷键 ----
    auto* spaceShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(spaceShortcut, &QShortcut::activated, this, &DIMM::onStartCapture);

    auto* escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escShortcut, &QShortcut::activated, this, &DIMM::onStopCapture);

    auto* key1 = new QShortcut(QKeySequence(Qt::Key_1), this);
    connect(key1, &QShortcut::activated, this, &DIMM::onSwitchCamera1);

    auto* key2 = new QShortcut(QKeySequence(Qt::Key_2), this);
    connect(key2, &QShortcut::activated, this, &DIMM::onSwitchCamera2);
}

// ============================================================
// 采集控制
// ============================================================

void DIMM::onStartCapture()
{
    if (!m_isCapturing) {
        // 尝试启动真实相机采集
        bool cameraStarted = false;
        qDebug() << "[DIMM] 开始采集: 相机0 open=" << m_cameraManager->isOpen(0)
                 << "相机1 open=" << m_cameraManager->isOpen(1);
        if (m_cameraManager->isOpen(0) || m_cameraManager->isOpen(1)) {
            cameraStarted = m_cameraManager->startAll();
            qDebug() << "[DIMM] startAll结果:" << cameraStarted;
        }

        // 如果没有真实相机，使用模拟模式
        if (!cameraStarted) {
            qDebug() << "[DIMM] 无真实相机，启动模拟模式";
            m_simulationTimer->start(1000);
        }

        m_isCapturing = true;
        ui->btnStart->setText("⏸ 暂停");
        m_lblStatusState->setText(cameraStarted ? "状态: 相机采集中" : "状态: 模拟采集中");
        m_lblStatusState->setStyleSheet("color: #8bc34a");
    } else {
        m_isCapturing = false;
        m_cameraManager->stopAll();
        m_simulationTimer->stop();

        ui->btnStart->setText("▶ 继续");
        m_lblStatusState->setText("状态: 已暂停");
        m_lblStatusState->setStyleSheet("color: #ff9800");
    }
}

void DIMM::onStopCapture()
{
    m_isCapturing = false;
    m_frameCount = 0;

    // 停止真实相机
    m_cameraManager->stopAll();
    m_simulationTimer->stop();

    ui->btnStart->setText("▶ 开始采集");
    m_lblStatusState->setText("状态: 已停止");
    m_lblStatusState->setStyleSheet("color: #f44336");

    ui->lblStatFrames->setText("已采集帧: 0");
    ui->lblStatValid->setText("有效帧: 0");
    ui->lblStatLatency->setText("处理延迟: 0.0 ms");
    ui->lblR0Value->setText("0.0");
    ui->lblSeeingValue->setText("0.00");
    ui->lblThetaValue->setText("0.0");
    ui->lblTauValue->setText("0.0");
    m_lblStatusFrames->setText("已采集: 0 帧");

    // 清除Canvas
    m_fullFrameCanvas->clear();
    m_cam1RoiCanvas->clear();
    m_cam2RoiCanvas->clear();
}

// ============================================================
// 页面切换
// ============================================================

void DIMM::onShowMainPage()
{
    ui->stackedWidget->setCurrentIndex(0);
    ui->btnFullFrame->setChecked(true);
    ui->btnROI->setChecked(false);
}

void DIMM::onShowRoiPage()
{
    ui->stackedWidget->setCurrentIndex(1);
    ui->btnFullFrame->setChecked(false);
    ui->btnROI->setChecked(true);
}

void DIMM::onShowSettings()
{
    m_settingsDialog->exec();
}

// ============================================================
// 显示控制（互斥）
// ============================================================

void DIMM::onToggleRoiImages()
{
    bool visible = !ui->roiImagesArea->isVisible();
    ui->roiImagesArea->setVisible(visible);
    ui->btnToggleROI->setStyleSheet(visible ?
        "background-color: #1976d2; color: #fff;" : "");
    if (visible) {
        ui->chartsArea->setVisible(false);
        ui->btnToggleCharts->setStyleSheet("");
    }
}

void DIMM::onToggleCharts()
{
    bool visible = !ui->chartsArea->isVisible();
    ui->chartsArea->setVisible(visible);
    ui->btnToggleCharts->setStyleSheet(visible ?
        "background-color: #1976d2; color: #fff;" : "");
    if (visible) {
        ui->roiImagesArea->setVisible(false);
        ui->btnToggleROI->setStyleSheet("");
    }
}

// ============================================================
// 相机切换
// ============================================================

void DIMM::onSwitchCamera1()
{
    m_currentCamera = 1;
    ui->btnCam1->setChecked(true);
    ui->btnCam2->setChecked(false);
    ui->lblCameraIndicator->setText("当前: 相机1");
    ui->lblFullframeLabel->setText("2048 × 2048 全画幅 - 相机1");
}

void DIMM::onSwitchCamera2()
{
    m_currentCamera = 2;
    ui->btnCam1->setChecked(false);
    ui->btnCam2->setChecked(true);
    ui->lblCameraIndicator->setText("当前: 相机2");
    ui->lblFullframeLabel->setText("2048 × 2048 全画幅 - 相机2");
}

// ============================================================
// 菜单操作
// ============================================================

void DIMM::onSaveConfig()
{
    QString file = QFileDialog::getSaveFileName(
        this, "保存配置", "config.json", "JSON文件 (*.json)");
    if (!file.isEmpty()) {
        QMessageBox::information(this, "保存配置", "功能尚未实现\n目标路径: " + file);
    }
}

void DIMM::onLoadConfig()
{
    QString file = QFileDialog::getOpenFileName(
        this, "加载配置", "", "JSON文件 (*.json)");
    if (!file.isEmpty()) {
        QMessageBox::information(this, "加载配置", "功能尚未实现\n目标路径: " + file);
    }
}

void DIMM::onExportData()
{
    QString file = QFileDialog::getSaveFileName(
        this, "导出数据", "data.csv", "CSV文件 (*.csv)");
    if (!file.isEmpty()) {
        QMessageBox::information(this, "导出数据", "功能尚未实现\n目标路径: " + file);
    }
}

void DIMM::onExportReport()
{
    QString file = QFileDialog::getSaveFileName(
        this, "导出报告", "report.pdf", "PDF文件 (*.pdf)");
    if (!file.isEmpty()) {
        QMessageBox::information(this, "导出报告", "功能尚未实现\n目标路径: " + file);
    }
}

void DIMM::onConnectAll()
{
    // 枚举并连接所有相机
    m_lblStatusState->setText("正在枚举相机设备...");
    m_lblStatusState->setStyleSheet("color: #ff9800");
    QApplication::processEvents();

    auto devices = m_cameraManager->enumerateDevices();

    if (devices.isEmpty()) {
        m_lblStatusState->setText("未发现相机");
        m_lblStatusState->setStyleSheet("color: #f44336");
        QMessageBox::warning(this, "连接相机",
            "未发现可用相机设备。\n\n请检查：\n"
            "1. 相机是否通电（指示灯是否亮）\n"
            "2. 网线是否连接\n"
            "3. 电脑IP是否与相机在同一网段\n"
            "4. Galaxy SDK驱动是否正常\n"
            "5. 防火墙是否拦截");
        return;
    }

    m_lblStatusState->setText(QString("发现 %1 个设备，正在连接...").arg(devices.size()));
    QApplication::processEvents();

    bool success = m_cameraManager->openAll();
    if (success) {
        QString msg = "已连接所有相机:\n";
        for (int i = 0; i < devices.size(); ++i) {
            msg += QString("\n相机%1: %2 (%3) [%4]")
                .arg(i + 1)
                .arg(devices[i].serialNumber)
                .arg(devices[i].modelName)
                .arg(devices[i].ipAddress);
        }
        m_lblStatusState->setText(QString("已连接 %1 台相机").arg(devices.size()));
        m_lblStatusState->setStyleSheet("color: #8bc34a");
        QMessageBox::information(this, "连接相机", msg);
    } else {
        m_lblStatusState->setText("部分相机连接失败");
        m_lblStatusState->setStyleSheet("color: #f44336");
        QMessageBox::warning(this, "连接相机", "部分相机连接失败。\n请确认相机未被其他程序占用。");
    }
}

void DIMM::onDisconnectAll()
{
    m_cameraManager->closeAll();
    QMessageBox::information(this, "断开相机", "已断开所有相机连接");
}

void DIMM::onAbout()
{
    QMessageBox::about(this, "关于 C-DIMM",
        "<h3>C-DIMM 大气相干长度测量系统</h3>"
        "<p>版本: v1.0</p>"
        "<p>功能特点:</p>"
        "<ul>"
        "<li>双相机同步采集</li>"
        "<li>实时质心计算</li>"
        "<li>大气参数反演 (r₀/ε/θ₀/τ₀)</li>"
        "<li>数据记录与导出</li>"
        "</ul>");
}

// ============================================================
// 模拟数据更新
// ============================================================

void DIMM::onUpdateSimulation()
{
    m_frameCount += 200;
    ui->lblStatFrames->setText(QString("已采集帧: %1").arg(m_frameCount));
    ui->lblStatValid->setText(QString("有效帧: %1").arg(static_cast<int>(m_frameCount * 0.98)));
    m_lblStatusFrames->setText(QString("已采集: %1 帧").arg(m_frameCount));
    updateParams();

    // 模拟模式下也保存数据（不影响真实采集，真实采集走atmosphereReady路径）
    double r0 = ui->lblR0Value->text().toDouble();
    double seeing = ui->lblSeeingValue->text().toDouble();
    saveResultRow(m_frameCount, 0, 0.0, 0.0, r0, seeing);
}

void DIMM::updateParams()
{
    double r0 = 11.0 + QRandomGenerator::global()->generateDouble() * 3.0;
    double seeing = 0.98 * 0.55 / (r0 / 100.0) * 206265.0 / 1000.0;
    double theta = 4.0 + QRandomGenerator::global()->generateDouble() * 3.0;
    double tau = 6.0 + QRandomGenerator::global()->generateDouble() * 4.0;
    double latency = 1.0 + QRandomGenerator::global()->generateDouble() * 0.5;

    ui->lblR0Value->setText(QString::number(r0, 'f', 1));
    ui->lblSeeingValue->setText(QString::number(seeing, 'f', 2));
    ui->lblThetaValue->setText(QString::number(theta, 'f', 1));
    ui->lblTauValue->setText(QString::number(tau, 'f', 1));
    ui->lblStatLatency->setText(QString("处理延迟: %1 ms").arg(latency, 0, 'f', 1));
}

// ============================================================
// 相机帧处理
// ============================================================

void DIMM::onFrameReady(int cameraIndex, cv::Mat frame)
{
    if (frame.empty()) return;

    m_frameCount++;

    // 更新全画幅预览（显示当前选中的相机 + ROI红框）
    if (cameraIndex == m_currentCamera - 1) {
        m_fullFrameCanvas->setImage(frame);
        // 叠加当前ROI红框
        RoiRect roi = m_imageProcessor->getCurrentRoi(cameraIndex);
        QVector<RoiRect> rois;
        rois.append(roi);
        m_fullFrameCanvas->setRoiList(rois);
        m_fullFrameCanvas->setCurrentRoi(0);
    }

    // 图像处理（质心计算）
    m_imageProcessor->processFrame(cameraIndex, frame);

    // 更新统计信息
    ui->lblStatFrames->setText(QString("已采集帧: %1").arg(m_frameCount));
    ui->lblStatValid->setText(QString("有效帧: %1").arg(static_cast<int>(m_frameCount * 0.98)));
    m_lblStatusFrames->setText(QString("已采集: %1 帧").arg(m_frameCount));
}

void DIMM::onCameraConnected(int index, QString serial, QString model)
{
    Q_UNUSED(model);
    if (index == 0) {
        ui->lblCam1Status->setText("● 在线");
        ui->lblCam1Status->setStyleSheet("color: #8bc34a");
    } else if (index == 1) {
        ui->lblCam2Status->setText("● 在线");
        ui->lblCam2Status->setStyleSheet("color: #8bc34a");
    }
    m_lblStatusState->setText(
        QString("相机%1已连接: %2").arg(index + 1).arg(serial));
}

void DIMM::onCameraDisconnected(int index)
{
    if (index == 0) {
        ui->lblCam1Status->setText("● 离线");
        ui->lblCam1Status->setStyleSheet("color: #666");
    } else if (index == 1) {
        ui->lblCam2Status->setText("● 离线");
        ui->lblCam2Status->setStyleSheet("color: #666");
    }
}

void DIMM::onCameraError(int index, int errorCode, QString message)
{
    Q_UNUSED(errorCode);
    QString errorText = QString("相机%1错误: %2").arg(index + 1).arg(message);
    m_lblStatusState->setText(errorText);
    m_lblStatusState->setStyleSheet("color: #f44336");

    // 5秒后恢复状态文字
    QTimer::singleShot(5000, this, [this]() {
        if (!m_isCapturing) {
            m_lblStatusState->setText("状态: 就绪");
            m_lblStatusState->setStyleSheet("color: #e0e0e0");
        }
    });
}

// ============================================================
// 相机信息定时更新（1Hz，主线程安全）
// ============================================================

void DIMM::updateCameraInfo()
{
    for (int i = 0; i < 2; ++i) {
        if (!m_cameraManager->isOpen(i)) continue;
        double fps = m_cameraManager->getFrameRate(i);
        double temp = m_cameraManager->getTemperature(i);
        QString info = QString("序列号: %1\n帧率: %2 fps | 温度: %3°C")
            .arg(m_cameraManager->getSerialNumber(i))
            .arg(fps, 0, 'f', 0)
            .arg(temp, 0, 'f', 1);
        if (i == 0) ui->lblCam1Info->setText(info);
        else ui->lblCam2Info->setText(info);
    }
}

// ============================================================
// 辅助方法
// ============================================================

void DIMM::updateCurrentRoi()
{
    if (!m_imageProcessor) return; // 构造期间可能被ROI表信号触发

    int row = ui->roiTable->currentRow();
    if (row < 0) row = 0;
    auto* xi = ui->roiTable->item(row, 1);
    auto* yi = ui->roiTable->item(row, 2);
    auto* wi = ui->roiTable->item(row, 3);
    auto* hi = ui->roiTable->item(row, 4);
    if (xi && yi && wi && hi) {
        RoiRect roi;
        roi.x = xi->text().toInt();
        roi.y = yi->text().toInt();
        roi.w = wi->text().toInt();
        roi.h = hi->text().toInt();
        m_imageProcessor->setCurrentRoi(0, roi);
        m_imageProcessor->setCurrentRoi(1, roi);
    }
}

void DIMM::initResultFile()
{
    if (m_resultFile) return;
    QDir dir(m_dataPath);
    if (!dir.exists()) dir.mkpath(".");
    QString filename = QString("%1/ISM_%2.txt")
        .arg(m_dataPath, QDateTime::currentDateTime().toString("yyyy-MM-ddThhmm'Z'"));
    m_resultFile = new QFile(filename, this);
    if (m_resultFile->open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_resultStream = new QTextStream(m_resultFile);
        // 按参考格式：31个字段，逗号分隔，无表头
        // 有数据的字段填写实际值，缺失的填0
        m_resultStream->flush();
    }
}

void DIMM::saveResultRow(int frame, int camIdx, double cx, double cy, double r0, double seeing)
{
    Q_UNUSED(frame); Q_UNUSED(camIdx); Q_UNUSED(cx); Q_UNUSED(cy);
    if (!m_resultFile) initResultFile();
    if (!m_resultStream) return;

    // 按参考格式输出31个字段（逗号分隔）
    // 有实际数据的填值，未接入传感器的填0
    QDateTime now = QDateTime::currentDateTime();
    QString line = QString(
        "%1,%2,"                    // 1-2: datetime, status
        "%3,%4,%5,%6,"             // 3-6: night seeing, night r0, scintillation, flux
        "%7,%8,%9,"                // 7-9: day seeing, day r0, day input signal
        "%10,%11,%12,"             // 10-12: internal temp, pressure, altitude
        "%13,%14,%15,"             // 13-15: humidity, sky temp, sky temp ambient
        "%16,%17,%18,"             // 16-18: irradiance, Lux, day gain
        "%19,%20,%21,"             // 19-21: wind direction, wind speed, wind gust
        "%22,%23,%24,"             // 22-24: external temp, humidity, rain
        "%25,%26,%27,"             // 25-27: rain rate, pyrgeometer temp, pyrgeometer ambient
        "%28,%29,%30,%31"          // 28-31: PWC, downwelling IR, day seeing stddev, isoplanatic angle
    )
    .arg(now.toString("dd/MM/yyyy,hh:mm:ss"))  // 1: datetime
    .arg(m_isCapturing ? "Running" : "Idle")     // 2: status
    .arg(seeing, 0, 'f', 3)                      // 3: night seeing (arcsec)
    .arg(r0, 0, 'f', 3)                          // 4: night r0 (cm)
    .arg(0.0, 0, 'f', 2)                         // 5: scintillation (暂无)
    .arg(0.0, 0, 'f', 2)                         // 6: flux (暂无)
    .arg(0.0, 0, 'f', 2)                         // 7: day seeing
    .arg(0.0, 0, 'f', 2)                         // 8: day r0
    .arg(0.0, 0, 'f', 2)                         // 9: day input signal
    .arg(0.0, 0, 'f', 2)                         // 10: internal temp
    .arg(0.0, 0, 'f', 2)                         // 11: pressure
    .arg(0.0, 0, 'f', 2)                         // 12: altitude
    .arg(0.0, 0, 'f', 2)                         // 13: humidity
    .arg(0.0, 0, 'f', 2)                         // 14: sky temp
    .arg(0.0, 0, 'f', 2)                         // 15: sky temp ambient
    .arg(0.0, 0, 'f', 2)                         // 16: irradiance
    .arg(0)                                       // 17: Lux
    .arg(0)                                       // 18: day gain
    .arg(0)                                       // 19: wind direction
    .arg(0.0, 0, 'f', 2)                         // 20: wind speed
    .arg(0.0, 0, 'f', 2)                         // 21: wind gust (deprecated)
    .arg(0.0, 0, 'f', 2)                         // 22: external temp
    .arg(0.0, 0, 'f', 2)                         // 23: external humidity
    .arg(0.0, 0, 'f', 2)                         // 24: rain
    .arg(0.0, 0, 'f', 2)                         // 25: rain rate
    .arg(0.0, 0, 'f', 2)                         // 26: pyrgeometer temp
    .arg(0.0, 0, 'f', 2)                         // 27: pyrgeometer ambient
    .arg(0.0, 0, 'f', 2)                         // 28: PWC
    .arg(0.0, 0, 'f', 2)                         // 29: downwelling IR
    .arg(0.0, 0, 'f', 2)                         // 30: day seeing stddev
    .arg(ui->lblThetaValue->text().toDouble(), 0, 'f', 3);  // 31: isoplanatic angle (theta0)

    m_pendingWrites.append(line);
}

void DIMM::flushPendingWrites()
{
    if (m_pendingWrites.isEmpty() || !m_resultStream) return;
    for (const QString& line : m_pendingWrites) {
        *m_resultStream << line << "\n";
    }
    m_resultStream->flush();
    m_pendingWrites.clear();
}

// ============================================================
// 合并的1Hz定时器
// ============================================================

void DIMM::on1hzTick()
{
    updateCameraInfo();
    matchRoiTimeSlot();
}

void DIMM::matchRoiTimeSlot()
{
    QTime now = QTime::currentTime();
    int nowMin = now.hour() * 60 + now.minute();
    QString nowStr = now.toString("HH:mm");

    for (int i = 0; i < ui->roiTable->rowCount(); ++i) {
        auto* item = ui->roiTable->item(i, 0);
        if (!item) continue;
        QStringList parts = item->text().split(":");
        if (parts.size() != 2) continue;
        int startMin = parts[0].toInt() * 60 + parts[1].toInt();
        int endMin = startMin + 30;
        if (nowMin >= startMin && nowMin < endMin) {
            ui->lblROITimeCurrent->setText(
                QString("%1 - %2")
                    .arg(item->text())
                    .arg(QString("%1:%2")
                        .arg(endMin / 60, 2, 10, QChar('0'))
                        .arg(endMin % 60, 2, 10, QChar('0'))));
            if (i + 1 < ui->roiTable->rowCount()) {
                auto* nextItem = ui->roiTable->item(i + 1, 0);
                ui->lblROITimeNext->setText(
                    QString("下一个ROI: %1").arg(nextItem ? nextItem->text() : "未知"));
            } else {
                ui->lblROITimeNext->setText("下一个ROI: 无");
            }
            auto* xi = ui->roiTable->item(i, 1);
            auto* yi = ui->roiTable->item(i, 2);
            auto* wi = ui->roiTable->item(i, 3);
            auto* hi = ui->roiTable->item(i, 4);
            if (xi && yi && wi && hi) {
                ui->lblROIXValue->setText(xi->text());
                ui->lblROIYValue->setText(yi->text());
                ui->lblROIWValue->setText(wi->text());
                ui->lblROIHValue->setText(hi->text());
                m_lblStatusROI->setText(
                    QString("当前ROI: (%1, %2) %3×%4")
                        .arg(xi->text(), yi->text(), wi->text(), hi->text()));
            }
            ui->roiTable->selectRow(i);
            return;
        }
    }

    ui->lblROITimeCurrent->setText(QString("当前时间: %1").arg(nowStr));

    int nextMin = -1;
    QString nextTimeStr;
    for (int i = 0; i < ui->roiTable->rowCount(); ++i) {
        auto* item = ui->roiTable->item(i, 0);
        if (!item) continue;
        QStringList parts = item->text().split(":");
        if (parts.size() != 2) continue;
        int startMin = parts[0].toInt() * 60 + parts[1].toInt();
        if (startMin > nowMin) {
            nextMin = startMin;
            nextTimeStr = item->text();
            break;
        }
    }
    if (nextMin < 0 && ui->roiTable->rowCount() > 0) {
        auto* firstItem = ui->roiTable->item(0, 0);
        if (firstItem) {
            nextTimeStr = firstItem->text();
            QStringList p = nextTimeStr.split(":");
            if (p.size() == 2) nextMin = p[0].toInt() * 60 + p[1].toInt();
        }
    }
    if (nextMin >= 0) {
        int diff = nextMin - nowMin;
        if (diff < 0) diff += 24 * 60;
        int hours = diff / 60;
        int mins = diff % 60;
        ui->lblROITimeNext->setText(
            QString("下一个ROI: %1 (还有%2h%3m)")
                .arg(nextTimeStr).arg(hours).arg(mins));
    } else {
        ui->lblROITimeNext->setText("无ROI计划");
    }
}

// ============================================================
// 通信管理
// ============================================================

void DIMM::onCommCommand(uint8_t cmd)
{
    using namespace CommProtocol;
    switch (cmd) {
    case CMD_START_REPORT:
        qDebug() << "[DIMM] 收到开始上报指令";
        m_commManager->sendAck(CMD_START_REPORT, 0);
        m_reporting = true;
        m_reportTimer->start(1000);
        break;
    case CMD_STOP_REPORT:
        qDebug() << "[DIMM] 收到停止上报指令";
        m_commManager->sendAck(CMD_STOP_REPORT, 0);
        m_reporting = false;
        m_reportTimer->stop();
        break;
    case CMD_QUERY_STATUS:
        qDebug() << "[DIMM] 收到状态查询指令";
        reportDeviceStatus();
        break;
    default:
        qDebug() << "[DIMM] 未知指令: 0x" << QString::number(cmd, 16);
        break;
    }
}

void DIMM::reportMeasurement()
{
    if (!m_commManager->isConnected()) return;

    // 获取最新的大气参数（从标签读取）
    double r0 = ui->lblR0Value->text().toDouble();
    double seeing = ui->lblSeeingValue->text().toDouble();
    double theta0 = ui->lblThetaValue->text().toDouble();
    double tau0 = ui->lblTauValue->text().toDouble();

    // 使用相机0的质心（如果有的话）
    double cx = m_centroidX[0];
    double cy = m_centroidY[0];

    m_commManager->sendMeasurement(r0, seeing, theta0, tau0,
        cx, cy, 0, static_cast<uint32_t>(m_frameCount));
}

void DIMM::reportDeviceStatus()
{
    if (!m_commManager->isConnected()) return;

    float temp = 0, fps = 0;
    if (m_cameraManager->isOpen(0)) {
        temp = static_cast<float>(m_cameraManager->getTemperature(0));
        fps = static_cast<float>(m_cameraManager->getFrameRate(0));
    }

    uint32_t uptimeMs = static_cast<uint32_t>(
        QDateTime::currentMSecsSinceEpoch() - m_startTimeMs);

    m_commManager->sendDeviceStatus(temp, fps,
        m_cameraManager->isOpen(0), m_cameraManager->isOpen(1),
        m_isCapturing, uptimeMs);
}
