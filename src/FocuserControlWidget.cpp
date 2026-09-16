#include "FocuserControlWidget.h"

#include "EafSdkLoader.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

FocuserControlWidget::FocuserControlWidget(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
}

void FocuserControlWidget::setManager(EafFocuserManager* manager)
{
    m_manager = manager;
    if (!m_manager) {
        return;
    }

    connect(m_manager, &EafFocuserManager::sdkAvailabilityChanged,
            this, &FocuserControlWidget::onSdkAvailabilityChanged);
    connect(m_manager, &EafFocuserManager::deviceListChanged,
            this, &FocuserControlWidget::onDeviceListChanged);
    connect(m_manager, &EafFocuserManager::stateChanged,
            this, &FocuserControlWidget::onStateChanged);
    connect(m_manager, &EafFocuserManager::commandFailed,
            this, &FocuserControlWidget::onCommandFailed);

    if (m_manager->sdkLoader() && m_manager->sdkLoader()->isLoaded()) {
        m_sdkStatusLabel->setText(QStringLiteral("已加载 (SDK %1)").arg(m_manager->sdkLoader()->sdkVersion()));
    } else {
        m_sdkStatusLabel->setText(QStringLiteral("未加载"));
    }
    updateControlStates();
}

void FocuserControlWidget::buildUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(14);

    auto* sdkGroup = new QGroupBox(QStringLiteral("EAF 设备"));
    auto* sdkLayout = new QFormLayout(sdkGroup);
    sdkLayout->setHorizontalSpacing(16);
    sdkLayout->setVerticalSpacing(10);

    m_sdkStatusLabel = new QLabel(QStringLiteral("未加载"));
    sdkLayout->addRow(QStringLiteral("SDK 状态:"), m_sdkStatusLabel);

    m_refreshBtn = new QPushButton(QStringLiteral("刷新设备"));
    sdkLayout->addRow(m_refreshBtn);

    m_deviceCombo = new QComboBox();
    m_deviceCombo->setMinimumWidth(240);
    sdkLayout->addRow(QStringLiteral("设备列表:"), m_deviceCombo);

    mainLayout->addWidget(sdkGroup);

    auto* mapGroup = new QGroupBox(QStringLiteral("EAF 安装位置"));
    auto* mapLayout = new QFormLayout(mapGroup);
    mapLayout->setHorizontalSpacing(16);
    mapLayout->setVerticalSpacing(10);

    m_telescopeCombo = new QComboBox();
    m_telescopeCombo->addItem(QStringLiteral("光路/望远镜 1"), static_cast<int>(TelescopeSlot::Telescope1));
    m_telescopeCombo->addItem(QStringLiteral("光路/望远镜 2"), static_cast<int>(TelescopeSlot::Telescope2));
    m_telescopeCombo->setToolTip(QStringLiteral("选择这个 EAF 实际安装在哪一路望远镜/相机光路上。只有一个 EAF 时，选它实际安装的那一路即可。"));
    mapLayout->addRow(QStringLiteral("安装位置:"), m_telescopeCombo);

    auto* mappingBtnLayout = new QHBoxLayout();
    m_applyMappingBtn = new QPushButton(QStringLiteral("绑定并打开"));
    m_applyMappingBtn->setToolTip(QStringLiteral("把当前选中的 EAF 绑定到所选安装位置，并立即打开设备。"));
    m_swapMappingBtn = new QPushButton(QStringLiteral("交换两路绑定"));
    m_swapMappingBtn->setToolTip(QStringLiteral("仅双 EAF 场景使用：交换光路 1 和光路 2 的 EAF 绑定。"));
    mappingBtnLayout->addWidget(m_applyMappingBtn);
    mappingBtnLayout->addWidget(m_swapMappingBtn);
    mapLayout->addRow(mappingBtnLayout);

    auto* openCloseLayout = new QHBoxLayout();
    m_openBtn = new QPushButton(QStringLiteral("打开当前设备"));
    m_closeBtn = new QPushButton(QStringLiteral("关闭当前设备"));
    m_closeBtn->setEnabled(false);
    openCloseLayout->addWidget(m_openBtn);
    openCloseLayout->addWidget(m_closeBtn);
    mapLayout->addRow(openCloseLayout);

    mainLayout->addWidget(mapGroup);

    auto* statusGroup = new QGroupBox(QStringLiteral("实时状态"));
    auto* statusLayout = new QFormLayout(statusGroup);
    statusLayout->setHorizontalSpacing(16);
    statusLayout->setVerticalSpacing(6);

    m_deviceNameLabel = new QLabel(QStringLiteral("--"));
    statusLayout->addRow(QStringLiteral("设备名:"), m_deviceNameLabel);
    m_deviceIdLabel = new QLabel(QStringLiteral("--"));
    statusLayout->addRow(QStringLiteral("ID:"), m_deviceIdLabel);
    m_snLabel = new QLabel(QStringLiteral("--"));
    statusLayout->addRow(QStringLiteral("序列号:"), m_snLabel);
    m_typeLabel = new QLabel(QStringLiteral("--"));
    statusLayout->addRow(QStringLiteral("类型:"), m_typeLabel);
    m_firmwareLabel = new QLabel(QStringLiteral("--"));
    statusLayout->addRow(QStringLiteral("固件:"), m_firmwareLabel);
    m_positionLabel = new QLabel(QStringLiteral("--"));
    statusLayout->addRow(QStringLiteral("当前位置:"), m_positionLabel);
    m_targetLabel = new QLabel(QStringLiteral("--"));
    statusLayout->addRow(QStringLiteral("目标位置:"), m_targetLabel);
    m_maxStepLabel = new QLabel(QStringLiteral("--"));
    statusLayout->addRow(QStringLiteral("最大位置:"), m_maxStepLabel);
    m_stepRangeLabel = new QLabel(QStringLiteral("--"));
    statusLayout->addRow(QStringLiteral("步进范围:"), m_stepRangeLabel);
    m_temperatureLabel = new QLabel(QStringLiteral("--"));
    statusLayout->addRow(QStringLiteral("温度:"), m_temperatureLabel);
    m_movingLabel = new QLabel(QStringLiteral("--"));
    statusLayout->addRow(QStringLiteral("移动状态:"), m_movingLabel);
    m_handControlLabel = new QLabel(QStringLiteral("--"));
    statusLayout->addRow(QStringLiteral("控制状态:"), m_handControlLabel);
    m_reverseLabel = new QLabel(QStringLiteral("--"));
    statusLayout->addRow(QStringLiteral("反向:"), m_reverseLabel);
    m_backlashLabel = new QLabel(QStringLiteral("--"));
    statusLayout->addRow(QStringLiteral("回差:"), m_backlashLabel);
    m_beepLabel = new QLabel(QStringLiteral("--"));
    statusLayout->addRow(QStringLiteral("蜂鸣器:"), m_beepLabel);
    m_ledLabel = new QLabel(QStringLiteral("--"));
    statusLayout->addRow(QStringLiteral("LED:"), m_ledLabel);
    m_errorLabel = new QLabel(QStringLiteral("--"));
    statusLayout->addRow(QStringLiteral("错误:"), m_errorLabel);

    mainLayout->addWidget(statusGroup);

    auto* moveGroup = new QGroupBox(QStringLiteral("Manual Move"));
    auto* moveLayout = new QFormLayout(moveGroup);
    moveLayout->setHorizontalSpacing(16);
    moveLayout->setVerticalSpacing(10);

    m_stepSizeSpin = new QSpinBox();
    m_stepSizeSpin->setRange(1, 10000);
    m_stepSizeSpin->setValue(100);
    moveLayout->addRow(QStringLiteral("Step size:"), m_stepSizeSpin);

    auto* stepBtnLayout = new QHBoxLayout();
    m_decreaseBtn = new QPushButton(QStringLiteral("Move -"));
    m_increaseBtn = new QPushButton(QStringLiteral("Move +"));
    stepBtnLayout->addWidget(m_decreaseBtn);
    stepBtnLayout->addWidget(m_increaseBtn);
    moveLayout->addRow(stepBtnLayout);

    m_targetPositionSpin = new QSpinBox();
    m_targetPositionSpin->setRange(0, 100000);
    m_targetPositionSpin->setValue(0);
    moveLayout->addRow(QStringLiteral("Target position:"), m_targetPositionSpin);

    auto* targetBtnLayout = new QHBoxLayout();
    m_moveToTargetBtn = new QPushButton(QStringLiteral("Move to target"));
    m_stopBtn = new QPushButton(QStringLiteral("Stop"));
    m_stopBtn->setStyleSheet(QStringLiteral("QPushButton { color: red; }"));
    targetBtnLayout->addWidget(m_moveToTargetBtn);
    targetBtnLayout->addWidget(m_stopBtn);
    moveLayout->addRow(targetBtnLayout);

    mainLayout->addWidget(moveGroup);

    auto* paramGroup = new QGroupBox(QStringLiteral("Device Parameters"));
    auto* paramLayout = new QFormLayout(paramGroup);
    paramLayout->setHorizontalSpacing(16);
    paramLayout->setVerticalSpacing(10);

    auto* reverseRow = new QHBoxLayout();
    m_reverseCheck = new QCheckBox();
    m_reverseApplyBtn = new QPushButton(QStringLiteral("Apply"));
    reverseRow->addWidget(m_reverseCheck);
    reverseRow->addWidget(m_reverseApplyBtn);
    paramLayout->addRow(QStringLiteral("Reverse:"), reverseRow);

    auto* backlashRow = new QHBoxLayout();
    m_backlashSpin = new QSpinBox();
    m_backlashSpin->setRange(0, 255);
    m_backlashSpin->setValue(0);
    m_backlashApplyBtn = new QPushButton(QStringLiteral("Apply"));
    backlashRow->addWidget(m_backlashSpin);
    backlashRow->addWidget(m_backlashApplyBtn);
    paramLayout->addRow(QStringLiteral("Backlash:"), backlashRow);

    auto* beepRow = new QHBoxLayout();
    m_beepCheck = new QCheckBox();
    m_beepApplyBtn = new QPushButton(QStringLiteral("Apply"));
    beepRow->addWidget(m_beepCheck);
    beepRow->addWidget(m_beepApplyBtn);
    paramLayout->addRow(QStringLiteral("Beep:"), beepRow);

    auto* ledRow = new QHBoxLayout();
    m_ledCheck = new QCheckBox();
    m_ledApplyBtn = new QPushButton(QStringLiteral("Apply"));
    ledRow->addWidget(m_ledCheck);
    ledRow->addWidget(m_ledApplyBtn);
    paramLayout->addRow(QStringLiteral("LED:"), ledRow);

    auto* resetRow = new QHBoxLayout();
    m_resetPositionSpin = new QSpinBox();
    m_resetPositionSpin->setRange(0, 100000);
    m_resetPositionBtn = new QPushButton(QStringLiteral("Set"));
    resetRow->addWidget(m_resetPositionSpin);
    resetRow->addWidget(m_resetPositionBtn);
    paramLayout->addRow(QStringLiteral("Reset position:"), resetRow);

    auto* maxStepRow = new QHBoxLayout();
    m_maxStepSpin = new QSpinBox();
    m_maxStepSpin->setRange(1, 100000);
    m_maxStepApplyBtn = new QPushButton(QStringLiteral("Write"));
    maxStepRow->addWidget(m_maxStepSpin);
    maxStepRow->addWidget(m_maxStepApplyBtn);
    paramLayout->addRow(QStringLiteral("Max step:"), maxStepRow);

    mainLayout->addWidget(paramGroup);

    auto* autoFocusGroup = new QGroupBox(QStringLiteral("自动调焦"));
    auto* autoFocusLayout = new QVBoxLayout(autoFocusGroup);
    autoFocusLayout->setSpacing(10);

    auto makeRequiredSpin = []() {
        auto* spin = new QSpinBox();
        spin->setRange(0, 1000000);
        spin->setSpecialValueText(QStringLiteral("未设置"));
        return spin;
    };
    auto makeRequiredDouble = []() {
        auto* spin = new QDoubleSpinBox();
        spin->setRange(0.0, 1000000000.0);
        spin->setDecimals(6);
        spin->setSingleStep(0.01);
        spin->setSpecialValueText(QStringLiteral("未设置"));
        return spin;
    };
    auto makeRatio = []() {
        auto* spin = new QDoubleSpinBox();
        spin->setRange(0.0, 0.499999);
        spin->setDecimals(6);
        spin->setSingleStep(0.01);
        spin->setSpecialValueText(QStringLiteral("未设置"));
        return spin;
    };

    auto* enableLayout = new QHBoxLayout();
    m_autoFocusMasterCheck = new QCheckBox(QStringLiteral("启用自动调焦"));
    m_autoFocusMasterCheck->setToolTip(
        QStringLiteral("只在 Tracking 状态使用既有的 64x64 ROI 图像。"));
    enableLayout->addWidget(m_autoFocusMasterCheck);
    enableLayout->addStretch();
    autoFocusLayout->addLayout(enableLayout);

    for (int camera = 0; camera < 2; ++camera) {
        auto* cameraGroup = new QGroupBox(QStringLiteral("相机 %1 参考标定").arg(camera + 1));
        auto* cameraLayout = new QFormLayout(cameraGroup);
        cameraLayout->setHorizontalSpacing(16);
        cameraLayout->setVerticalSpacing(8);
        m_autoFocusCameraEnabled[camera] = new QCheckBox(QStringLiteral("启用相机 %1 自动调焦").arg(camera + 1));
        cameraLayout->addRow(m_autoFocusCameraEnabled[camera]);

        const auto addReferenceRow = [&cameraLayout, &makeRequiredDouble](const QString& name,
                                                                             QDoubleSpinBox*& value,
                                                                             QCheckBox*& autoCalibrate) {
            auto* row = new QHBoxLayout();
            value = makeRequiredDouble();
            autoCalibrate = new QCheckBox(QStringLiteral("Tracking 自动标定"));
            row->addWidget(value);
            row->addWidget(autoCalibrate);
            cameraLayout->addRow(name, row);
        };
        addReferenceRow(QStringLiteral("Reference HFR:"),
                        m_autoFocusReferenceHfr[camera],
                        m_autoFocusCalibrateHfr[camera]);
        m_autoFocusReasonableHfrMinimum[camera] = makeRequiredDouble();
        cameraLayout->addRow(QStringLiteral("合理 HFR 最小值:"),
                             m_autoFocusReasonableHfrMinimum[camera]);
        addReferenceRow(QStringLiteral("Reference RMS:"),
                        m_autoFocusReferenceRms[camera],
                        m_autoFocusCalibrateRms[camera]);
        autoFocusLayout->addWidget(cameraGroup);
    }

    auto* samplingGroup = new QGroupBox(QStringLiteral("采样与触发"));
    auto* samplingLayout = new QFormLayout(samplingGroup);
    m_autoFocusFramesPerStateSpin = makeRequiredSpin();
    samplingLayout->addRow(QStringLiteral("每阶段有效帧数 N:"), m_autoFocusFramesPerStateSpin);
    m_autoFocusSettleTimeSpin = makeRequiredSpin();
    samplingLayout->addRow(QStringLiteral("停止后稳定等待 (ms):"), m_autoFocusSettleTimeSpin);
    m_autoFocusStageTimeoutSpin = makeRequiredSpin();
    samplingLayout->addRow(QStringLiteral("调焦阶段超时 (ms):"), m_autoFocusStageTimeoutSpin);
    m_autoFocusTemperatureThresholdSpin = makeRequiredDouble();
    samplingLayout->addRow(QStringLiteral("温度触发阈值 (°C):"), m_autoFocusTemperatureThresholdSpin);
    m_autoFocusStatisticsModeCombo = new QComboBox();
    m_autoFocusStatisticsModeCombo->addItem(QStringLiteral("均值"),
                                             static_cast<int>(AutoFocusStatisticsMode::Mean));
    m_autoFocusStatisticsModeCombo->addItem(QStringLiteral("中位数"),
                                             static_cast<int>(AutoFocusStatisticsMode::Median));
    m_autoFocusStatisticsModeCombo->addItem(QStringLiteral("截尾均值"),
                                             static_cast<int>(AutoFocusStatisticsMode::TrimmedMean));
    samplingLayout->addRow(QStringLiteral("统计方式:"), m_autoFocusStatisticsModeCombo);
    m_autoFocusTrimRatioSpin = makeRatio();
    samplingLayout->addRow(QStringLiteral("截尾比例 (仅截尾均值):"), m_autoFocusTrimRatioSpin);
    autoFocusLayout->addWidget(samplingGroup);

    auto* searchGroup = new QGroupBox(QStringLiteral("调焦方向与调整搜索"));
    auto* searchLayout = new QFormLayout(searchGroup);
    m_autoFocusStepSpin = makeRequiredSpin();
    searchLayout->addRow(QStringLiteral("搜索步长:"), m_autoFocusStepSpin);
    m_autoFocusMaximumRangeSpin = makeRequiredSpin();
    searchLayout->addRow(QStringLiteral("最大搜索行程:"), m_autoFocusMaximumRangeSpin);
    m_autoFocusStartupSearchRadiusSpin = makeRequiredSpin();
    searchLayout->addRow(QStringLiteral("启动调焦搜索半径 (±步):"),
                         m_autoFocusStartupSearchRadiusSpin);
    m_autoFocusMaximumIterationsSpin = makeRequiredSpin();
    searchLayout->addRow(QStringLiteral("最大搜索次数:"), m_autoFocusMaximumIterationsSpin);
    m_autoFocusDirectionImprovementSpin = makeRatio();
    searchLayout->addRow(QStringLiteral("方向搜索连续改善幅度 (比例):"),
                         m_autoFocusDirectionImprovementSpin);
    m_autoFocusDirectionWorseningSpin = makeRatio();
    searchLayout->addRow(QStringLiteral("方向搜索连续恶化幅度 (比例):"),
                         m_autoFocusDirectionWorseningSpin);
    m_autoFocusHfrImprovementSpin = makeRatio();
    searchLayout->addRow(QStringLiteral("调整阶段越焦判定容差 (比例):"),
                         m_autoFocusHfrImprovementSpin);
    m_autoFocusHfrToleranceSpin = makeRatio();
    searchLayout->addRow(QStringLiteral("HFR 最终容差 (比例):"), m_autoFocusHfrToleranceSpin);
    m_autoFocusStabilitySpin = makeRequiredDouble();
    searchLayout->addRow(QStringLiteral("HFR 稳定性阈值 (比例):"), m_autoFocusStabilitySpin);
    m_autoFocusRmsToleranceSpin = makeRatio();
    searchLayout->addRow(QStringLiteral("RMS 辅助容差 (比例):"), m_autoFocusRmsToleranceSpin);
    autoFocusLayout->addWidget(searchGroup);

    auto* callbackGroup = new QGroupBox(QStringLiteral("回调补偿"));
    auto* callbackLayout = new QFormLayout(callbackGroup);
    m_autoFocusCallbackStepSpin = makeRequiredSpin();
    callbackLayout->addRow(QStringLiteral("回调步长:"), m_autoFocusCallbackStepSpin);
    m_autoFocusCallbackHfrToleranceSpin = makeRatio();
    callbackLayout->addRow(QStringLiteral("越焦回调达标 HFR 容差 (比例):"),
                           m_autoFocusCallbackHfrToleranceSpin);
    autoFocusLayout->addWidget(callbackGroup);

    auto* autoFocusActions = new QHBoxLayout();
    m_autoFocusLoggingCheck = new QCheckBox(QStringLiteral("启用自动调焦数据日志"));
    autoFocusActions->addWidget(m_autoFocusLoggingCheck);
    autoFocusActions->addStretch();
    m_autoFocusApplyBtn = new QPushButton(QStringLiteral("应用自动调焦设置"));
    m_autoFocusManualStartBtn = new QPushButton(QStringLiteral("立即自动调焦"));
    m_autoFocusManualStartBtn->setToolTip(
        QStringLiteral("仅在 Tracking 中可用；对所有已启用且可调的相机并行开始。"));
    autoFocusActions->addWidget(m_autoFocusApplyBtn);
    autoFocusActions->addWidget(m_autoFocusManualStartBtn);
    autoFocusLayout->addLayout(autoFocusActions);
    m_autoFocusStatusLabel = new QLabel();
    m_autoFocusStatusLabel->setWordWrap(true);
    m_autoFocusStatusLabel->setStyleSheet(QStringLiteral("QLabel { color: #f0c060; }"));
    autoFocusLayout->addWidget(m_autoFocusStatusLabel);
    mainLayout->addWidget(autoFocusGroup);

    m_motionLockLabel = new QLabel();
    m_motionLockLabel->setWordWrap(true);
    m_motionLockLabel->setStyleSheet(QStringLiteral("QLabel { color: #ff6666; padding: 8px; }"));
    m_motionLockLabel->hide();
    mainLayout->addWidget(m_motionLockLabel);

    mainLayout->addStretch();

    {
        QSettings settings;
        setAutoFocusConfigUi(AutoFocusSettings::load(settings));
    }

    connect(m_refreshBtn, &QPushButton::clicked, this, &FocuserControlWidget::onRefreshDevices);
    connect(m_applyMappingBtn, &QPushButton::clicked, this, &FocuserControlWidget::onApplyMapping);
    connect(m_swapMappingBtn, &QPushButton::clicked, this, &FocuserControlWidget::onSwapMapping);
    connect(m_openBtn, &QPushButton::clicked, this, &FocuserControlWidget::onOpenDevice);
    connect(m_closeBtn, &QPushButton::clicked, this, &FocuserControlWidget::onCloseDevice);
    connect(m_decreaseBtn, &QPushButton::clicked, this, &FocuserControlWidget::onMoveDecrease);
    connect(m_increaseBtn, &QPushButton::clicked, this, &FocuserControlWidget::onMoveIncrease);
    connect(m_moveToTargetBtn, &QPushButton::clicked, this, &FocuserControlWidget::onMoveToTarget);
    connect(m_stopBtn, &QPushButton::clicked, this, &FocuserControlWidget::onStop);
    connect(m_reverseApplyBtn, &QPushButton::clicked, this, &FocuserControlWidget::onApplyReverse);
    connect(m_backlashApplyBtn, &QPushButton::clicked, this, &FocuserControlWidget::onApplyBacklash);
    connect(m_beepApplyBtn, &QPushButton::clicked, this, &FocuserControlWidget::onApplyBeep);
    connect(m_ledApplyBtn, &QPushButton::clicked, this, &FocuserControlWidget::onApplyLed);
    connect(m_resetPositionBtn, &QPushButton::clicked, this, &FocuserControlWidget::onResetPosition);
    connect(m_maxStepApplyBtn, &QPushButton::clicked, this, &FocuserControlWidget::onApplyMaxStep);
    connect(m_telescopeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FocuserControlWidget::onTelescopeSelectionChanged);
    connect(m_autoFocusApplyBtn, &QPushButton::clicked,
            this, &FocuserControlWidget::onApplyAutoFocus);
    connect(m_autoFocusManualStartBtn, &QPushButton::clicked,
            this, &FocuserControlWidget::onManualAutoFocus);
    connect(m_autoFocusMasterCheck, &QCheckBox::toggled,
            this, &FocuserControlWidget::onAutoFocusEnableChanged);
    connect(m_autoFocusCameraEnabled[0], &QCheckBox::toggled,
            this, &FocuserControlWidget::onAutoFocusEnableChanged);
    connect(m_autoFocusCameraEnabled[1], &QCheckBox::toggled,
            this, &FocuserControlWidget::onAutoFocusEnableChanged);
    connect(m_autoFocusStatisticsModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { updateAutoFocusControlStates(); });

    updateControlStates();
}

void FocuserControlWidget::onRefreshDevices()
{
    if (m_manager) {
        m_manager->refreshDevices();
    }
}

void FocuserControlWidget::onDeviceListChanged(QVector<EafDeviceDescriptor> devices)
{
    m_devices = devices;
    updateDeviceCombo();
    updateControlStates();
}

void FocuserControlWidget::updateDeviceCombo()
{
    m_deviceCombo->clear();
    if (m_devices.isEmpty()) {
        m_deviceCombo->addItem(QStringLiteral("未发现 EAF"), -1);
        return;
    }

    for (int i = 0; i < m_devices.size(); ++i) {
        const auto& dev = m_devices.at(i);
        QString text = dev.name;
        if (!dev.serialHex.isEmpty()) {
            text += QStringLiteral(" [SN: %1]").arg(dev.serialHex);
        }
        text += QStringLiteral(" (ID:%1)").arg(dev.id);
        if (!dev.type.isEmpty()) {
            text += QStringLiteral(" %1").arg(dev.type);
        }
        m_deviceCombo->addItem(text, i);
    }
}

void FocuserControlWidget::onApplyMapping()
{
    if (!m_manager || m_devices.isEmpty() || m_deviceCombo->currentIndex() < 0) {
        return;
    }
    const int devIndex = m_deviceCombo->currentData().toInt();
    if (devIndex < 0 || devIndex >= m_devices.size()) {
        return;
    }

    const auto& dev = m_devices.at(devIndex);
    const TelescopeSlot slot = static_cast<TelescopeSlot>(m_telescopeCombo->currentData().toInt());

    if (!dev.serialHex.isEmpty()) {
        QSettings settings;
        const int otherIdx = (static_cast<int>(slot) == 0) ? 1 : 0;
        const QString otherSerial = settings.value(
            QStringLiteral("focuser/telescope%1/serial").arg(otherIdx + 1)).toString();
        if (otherSerial == dev.serialHex) {
            QMessageBox::warning(this, QStringLiteral("绑定冲突"),
                QStringLiteral("这个 EAF 已经绑定到光路/望远镜 %1。").arg(otherIdx + 1));
            return;
        }
    }

    QSettings settings;
    const QString prefix = QStringLiteral("focuser/telescope%1/").arg(static_cast<int>(slot) + 1);
    settings.setValue(prefix + QStringLiteral("serial"), dev.serialHex);
    settings.setValue(prefix + QStringLiteral("enumIndex"), dev.enumerationIndex);
    settings.setValue(prefix + QStringLiteral("deviceId"), dev.id);

    m_manager->assignDevice(slot, dev.serialHex);
    m_manager->openDeviceForSlot(slot, dev);
}

void FocuserControlWidget::onSwapMapping()
{
    if (!m_manager) {
        return;
    }

    QSettings settings;
    const QString serial1 = settings.value(QStringLiteral("focuser/telescope1/serial")).toString();
    const QString serial2 = settings.value(QStringLiteral("focuser/telescope2/serial")).toString();
    const int enum1 = settings.value(QStringLiteral("focuser/telescope1/enumIndex"), -1).toInt();
    const int enum2 = settings.value(QStringLiteral("focuser/telescope2/enumIndex"), -1).toInt();
    const int id1 = settings.value(QStringLiteral("focuser/telescope1/deviceId"), -1).toInt();
    const int id2 = settings.value(QStringLiteral("focuser/telescope2/deviceId"), -1).toInt();

    if (serial1.isEmpty() && serial2.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("交换两路绑定"), QStringLiteral("当前还没有任何 EAF 绑定。"));
        return;
    }

    const auto reply = QMessageBox::question(this, QStringLiteral("交换两路绑定"),
        QStringLiteral("确定要交换光路 1 和光路 2 的 EAF 绑定吗？"),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        return;
    }

    m_manager->closeAssignedDevice(TelescopeSlot::Telescope1);
    m_manager->closeAssignedDevice(TelescopeSlot::Telescope2);

    settings.setValue(QStringLiteral("focuser/telescope1/serial"), serial2);
    settings.setValue(QStringLiteral("focuser/telescope1/enumIndex"), enum2);
    settings.setValue(QStringLiteral("focuser/telescope1/deviceId"), id2);
    settings.setValue(QStringLiteral("focuser/telescope2/serial"), serial1);
    settings.setValue(QStringLiteral("focuser/telescope2/enumIndex"), enum1);
    settings.setValue(QStringLiteral("focuser/telescope2/deviceId"), id1);

    QMessageBox::information(this, QStringLiteral("交换两路绑定"),
        QStringLiteral("绑定已交换，请重新打开设备。"));
}

void FocuserControlWidget::onOpenDevice()
{
    if (!m_manager || m_devices.isEmpty() || m_deviceCombo->currentIndex() < 0) {
        return;
    }
    const int devIndex = m_deviceCombo->currentData().toInt();
    if (devIndex < 0 || devIndex >= m_devices.size()) {
        return;
    }

    const auto& dev = m_devices.at(devIndex);
    const TelescopeSlot slot = static_cast<TelescopeSlot>(m_telescopeCombo->currentData().toInt());
    m_manager->openDeviceForSlot(slot, dev);
}

void FocuserControlWidget::onCloseDevice()
{
    if (!m_manager) {
        return;
    }
    const TelescopeSlot slot = static_cast<TelescopeSlot>(m_telescopeCombo->currentData().toInt());
    m_manager->closeAssignedDevice(slot);
}

void FocuserControlWidget::onStateChanged(TelescopeSlot slot, EafDeviceState state)
{
    const int currentSlot = m_telescopeCombo->currentData().toInt();
    if (static_cast<int>(slot) != currentSlot) {
        return;
    }

    m_currentState = state;
    updateStateDisplay();
    updateControlStates();
}

void FocuserControlWidget::updateStateDisplay()
{
    const auto& s = m_currentState;
    m_deviceNameLabel->setText(!s.deviceName.isEmpty() ? s.deviceName
        : s.opened ? QStringLiteral("已连接") : QStringLiteral("--"));
    m_deviceIdLabel->setText(s.deviceId >= 0 ? QString::number(s.deviceId) : QStringLiteral("--"));
    m_snLabel->setText(!s.serialHex.isEmpty() ? s.serialHex : QStringLiteral("--"));
    m_typeLabel->setText(!s.deviceType.isEmpty() ? s.deviceType : QStringLiteral("--"));
    m_firmwareLabel->setText(!s.firmwareVersion.isEmpty() ? s.firmwareVersion : QStringLiteral("--"));
    m_positionLabel->setText(QString::number(s.currentPosition));
    m_targetLabel->setText(QString::number(s.commandedTarget));
    m_maxStepLabel->setText(QString::number(s.maxStep));
    m_stepRangeLabel->setText(QString::number(s.stepRange));
    m_temperatureLabel->setText(s.temperatureValid
        ? QStringLiteral("%1 C").arg(s.temperatureC, 0, 'f', 1)
        : QStringLiteral("--"));
    m_movingLabel->setText(s.moving ? QStringLiteral("移动中") : QStringLiteral("停止"));
    m_handControlLabel->setText(s.handControl ? QStringLiteral("手柄控制") : QStringLiteral("软件控制"));
    m_reverseLabel->setText(s.reverse ? QStringLiteral("已启用") : QStringLiteral("已禁用"));
    m_backlashLabel->setText(QString::number(s.backlash));
    m_beepLabel->setText(s.beep ? QStringLiteral("已启用") : QStringLiteral("已禁用"));
    m_ledLabel->setText(s.led ? QStringLiteral("已开启") : QStringLiteral("已关闭"));

    QString errors;
    if (!s.motorErrorCode.isEmpty()) {
        errors += QStringLiteral("电机: %1 ").arg(s.motorErrorCode);
    }
    if (!s.batteryErrorCode.isEmpty()) {
        errors += QStringLiteral("电池: %1 ").arg(s.batteryErrorCode);
    }
    if (!s.lastError.isEmpty()) {
        errors += s.lastError;
    }
    m_errorLabel->setText(errors.isEmpty() ? QStringLiteral("无") : errors);

    m_reverseCheck->setChecked(s.reverse);
    m_backlashSpin->setValue(s.backlash);
    m_beepCheck->setChecked(s.beep);
    m_ledCheck->setChecked(s.led);
    m_maxStepSpin->setValue(s.maxStep);

    m_targetPositionSpin->setRange(0, s.maxStep > 0 ? s.maxStep : 100000);
    m_resetPositionSpin->setRange(0, s.maxStep > 0 ? s.maxStep : 100000);
}

void FocuserControlWidget::onMoveDecrease()
{
    if (!m_manager) {
        return;
    }
    const TelescopeSlot slot = static_cast<TelescopeSlot>(m_telescopeCombo->currentData().toInt());
    m_manager->moveRelative(slot, -m_stepSizeSpin->value());
}

void FocuserControlWidget::onMoveIncrease()
{
    if (!m_manager) {
        return;
    }
    const TelescopeSlot slot = static_cast<TelescopeSlot>(m_telescopeCombo->currentData().toInt());
    m_manager->moveRelative(slot, m_stepSizeSpin->value());
}

void FocuserControlWidget::onMoveToTarget()
{
    if (!m_manager) {
        return;
    }
    const TelescopeSlot slot = static_cast<TelescopeSlot>(m_telescopeCombo->currentData().toInt());
    m_manager->moveAbsolute(slot, m_targetPositionSpin->value());
}

void FocuserControlWidget::onStop()
{
    if (!m_manager) {
        return;
    }
    const TelescopeSlot slot = static_cast<TelescopeSlot>(m_telescopeCombo->currentData().toInt());
    m_manager->stopMotion(slot);
}

void FocuserControlWidget::onApplyReverse()
{
    if (!m_manager) {
        return;
    }
    const TelescopeSlot slot = static_cast<TelescopeSlot>(m_telescopeCombo->currentData().toInt());
    m_manager->setReverse(slot, m_reverseCheck->isChecked());
}

void FocuserControlWidget::onApplyBacklash()
{
    if (!m_manager) {
        return;
    }
    const TelescopeSlot slot = static_cast<TelescopeSlot>(m_telescopeCombo->currentData().toInt());
    m_manager->setBacklash(slot, m_backlashSpin->value());
}

void FocuserControlWidget::onApplyBeep()
{
    if (!m_manager) {
        return;
    }
    const TelescopeSlot slot = static_cast<TelescopeSlot>(m_telescopeCombo->currentData().toInt());
    m_manager->setBeep(slot, m_beepCheck->isChecked());
}

void FocuserControlWidget::onApplyLed()
{
    if (!m_manager) {
        return;
    }
    const TelescopeSlot slot = static_cast<TelescopeSlot>(m_telescopeCombo->currentData().toInt());
    m_manager->setLed(slot, m_ledCheck->isChecked());
}

void FocuserControlWidget::onResetPosition()
{
    if (!m_manager) {
        return;
    }

    const auto reply = QMessageBox::question(this, QStringLiteral("重置位置"),
        QStringLiteral("确定要把 EAF 当前位置重置为 %1 吗？这会影响位置基准。").arg(m_resetPositionSpin->value()),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        return;
    }

    const TelescopeSlot slot = static_cast<TelescopeSlot>(m_telescopeCombo->currentData().toInt());
    m_manager->resetPosition(slot, m_resetPositionSpin->value());
}

void FocuserControlWidget::onApplyMaxStep()
{
    if (!m_manager) {
        return;
    }

    const auto reply = QMessageBox::question(this, QStringLiteral("设置 MaxStep"),
        QStringLiteral("确定要把 MaxStep 设置为 %1 吗？这会影响运动范围。").arg(m_maxStepSpin->value()),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        return;
    }

    const TelescopeSlot slot = static_cast<TelescopeSlot>(m_telescopeCombo->currentData().toInt());
    m_manager->setMaxStep(slot, m_maxStepSpin->value());
}

void FocuserControlWidget::onTelescopeSelectionChanged(int index)
{
    Q_UNUSED(index);
    if (m_manager) {
        const TelescopeSlot slot = static_cast<TelescopeSlot>(m_telescopeCombo->currentData().toInt());
        m_manager->requestStateRefresh(slot);
    }
    updateControlStates();
}

void FocuserControlWidget::onSdkAvailabilityChanged(bool available, QString detail)
{
    if (available) {
        m_sdkStatusLabel->setText(QStringLiteral("已加载 (%1)").arg(detail));
    } else {
        m_sdkStatusLabel->setText(QStringLiteral("加载失败: %1").arg(detail));
    }
    updateControlStates();
}

void FocuserControlWidget::onCommandFailed(TelescopeSlot slot, QString command, QString error)
{
    Q_UNUSED(slot);
    QMessageBox::warning(this, QStringLiteral("命令失败"),
        QStringLiteral("操作 '%1' 失败: %2").arg(command, error));
}

void FocuserControlWidget::setMotionAllowed(bool allowed, const QString& reason)
{
    m_motionAllowed = allowed;
    m_motionLockReason = reason;
    updateControlStates();
}

void FocuserControlWidget::setAutoFocusMotionLocked(TelescopeSlot slot, bool locked)
{
    const int index = static_cast<int>(slot);
    if (index < 0 || index >= 2) {
        return;
    }
    m_autoFocusMotionLocked[index] = locked;
    updateControlStates();
}

void FocuserControlWidget::setAutoFocusTrackingAvailable(bool available)
{
    m_autoFocusTrackingAvailable = available;
    updateAutoFocusControlStates();
}

AutoFocusConfig FocuserControlWidget::autoFocusConfigFromUi() const
{
    AutoFocusConfig config;
    config.masterEnabled = m_autoFocusMasterCheck->isChecked();
    for (int camera = 0; camera < 2; ++camera) {
        config.cameraEnabled[camera] = m_autoFocusCameraEnabled[camera]->isChecked();
        config.reference[camera].hfr = m_autoFocusReferenceHfr[camera]->value();
        config.reference[camera].rms = m_autoFocusReferenceRms[camera]->value();
        config.reasonableHfrMinimum[camera] = m_autoFocusReasonableHfrMinimum[camera]->value();
        config.autoCalibrateHfr[camera] = m_autoFocusCalibrateHfr[camera]->isChecked();
        config.autoCalibrateRms[camera] = m_autoFocusCalibrateRms[camera]->isChecked();
    }
    config.framesPerState = m_autoFocusFramesPerStateSpin->value();
    config.settleTimeMs = m_autoFocusSettleTimeSpin->value();
    config.focuserStageTimeoutMs = m_autoFocusStageTimeoutSpin->value();
    config.temperatureTriggerThreshold = m_autoFocusTemperatureThresholdSpin->value();
    config.statisticsMode = static_cast<AutoFocusStatisticsMode>(
        m_autoFocusStatisticsModeCombo->currentData().toInt());
    config.trimRatio = m_autoFocusTrimRatioSpin->value();
    config.focusStep = m_autoFocusStepSpin->value();
    config.maximumSearchRange = m_autoFocusMaximumRangeSpin->value();
    config.startupSearchRadius = m_autoFocusStartupSearchRadiusSpin->value();
    config.maximumIterations = m_autoFocusMaximumIterationsSpin->value();
    config.directionImprovementThreshold = m_autoFocusDirectionImprovementSpin->value();
    config.directionWorseningThreshold = m_autoFocusDirectionWorseningSpin->value();
    config.hfrImprovementThreshold = m_autoFocusHfrImprovementSpin->value();
    config.callbackHfrTolerance = m_autoFocusCallbackHfrToleranceSpin->value();
    config.hfrFinalTolerance = m_autoFocusHfrToleranceSpin->value();
    config.focusStabilityThreshold = m_autoFocusStabilitySpin->value();
    config.rmsAuxiliaryThreshold = m_autoFocusRmsToleranceSpin->value();
    config.callbackStep = m_autoFocusCallbackStepSpin->value();
    config.dataLoggingEnabled = m_autoFocusLoggingCheck->isChecked();
    return config;
}

// Kept out of line because DIMM refreshes these values after a reference calibration.
void FocuserControlWidget::setAutoFocusConfigUi(const AutoFocusConfig& config)
{
    m_autoFocusAppliedConfig = config;
    m_autoFocusMasterCheck->setChecked(config.masterEnabled);
    for (int camera = 0; camera < 2; ++camera) {
        m_autoFocusCameraEnabled[camera]->setChecked(config.cameraEnabled[camera]);
        m_autoFocusReferenceHfr[camera]->setValue(config.reference[camera].hfr);
        m_autoFocusReferenceRms[camera]->setValue(config.reference[camera].rms);
        m_autoFocusReasonableHfrMinimum[camera]->setValue(config.reasonableHfrMinimum[camera]);
        m_autoFocusCalibrateHfr[camera]->setChecked(config.autoCalibrateHfr[camera]);
        m_autoFocusCalibrateRms[camera]->setChecked(config.autoCalibrateRms[camera]);
    }
    m_autoFocusFramesPerStateSpin->setValue(config.framesPerState);
    m_autoFocusSettleTimeSpin->setValue(config.settleTimeMs);
    m_autoFocusStageTimeoutSpin->setValue(config.focuserStageTimeoutMs);
    m_autoFocusTemperatureThresholdSpin->setValue(config.temperatureTriggerThreshold);
    const int statisticsIndex = m_autoFocusStatisticsModeCombo->findData(
        static_cast<int>(config.statisticsMode));
    m_autoFocusStatisticsModeCombo->setCurrentIndex(statisticsIndex >= 0 ? statisticsIndex : 0);
    m_autoFocusTrimRatioSpin->setValue(config.trimRatio);
    m_autoFocusStepSpin->setValue(config.focusStep);
    m_autoFocusMaximumRangeSpin->setValue(config.maximumSearchRange);
    m_autoFocusStartupSearchRadiusSpin->setValue(config.startupSearchRadius);
    m_autoFocusMaximumIterationsSpin->setValue(config.maximumIterations);
    m_autoFocusDirectionImprovementSpin->setValue(config.directionImprovementThreshold);
    m_autoFocusDirectionWorseningSpin->setValue(config.directionWorseningThreshold);
    m_autoFocusHfrImprovementSpin->setValue(config.hfrImprovementThreshold);
    m_autoFocusCallbackHfrToleranceSpin->setValue(config.callbackHfrTolerance);
    m_autoFocusHfrToleranceSpin->setValue(config.hfrFinalTolerance);
    m_autoFocusStabilitySpin->setValue(config.focusStabilityThreshold);
    m_autoFocusRmsToleranceSpin->setValue(config.rmsAuxiliaryThreshold);
    m_autoFocusCallbackStepSpin->setValue(config.callbackStep);
    m_autoFocusLoggingCheck->setChecked(config.dataLoggingEnabled);
    updateAutoFocusControlStates();
}

bool FocuserControlWidget::applyAutoFocusConfiguration(bool showError)
{
    const AutoFocusConfig config = autoFocusConfigFromUi();
    const QString error = AutoFocusSettings::validationError(config);
    if (!error.isEmpty()) {
        const QString text = QStringLiteral("自动调焦设置无效：%1").arg(error);
        m_autoFocusStatusLabel->setText(text);
        if (showError) {
            QMessageBox::warning(this, QStringLiteral("自动调焦设置"), text);
        }
        return false;
    }

    QSettings settings;
    AutoFocusSettings::save(settings, config);
    settings.sync();
    m_autoFocusAppliedConfig = config;
    m_autoFocusStatusLabel->setText(
        QStringLiteral("自动调焦设置已应用；非禁用参数将在当前轮次结束后生效。"));
    emit autoFocusConfigApplied(config);
    return true;
}

void FocuserControlWidget::onApplyAutoFocus()
{
    applyAutoFocusConfiguration(true);
}

void FocuserControlWidget::onManualAutoFocus()
{
    if (!m_autoFocusTrackingAvailable || !m_motionAllowed) {
        return;
    }
    if (!applyAutoFocusConfiguration(true)) {
        return;
    }
    emit manualAutoFocusRequested();
}

void FocuserControlWidget::onAutoFocusEnableChanged(bool enabled)
{
    if (enabled) {
        return;
    }

    int cameraIndex = -1;
    if (sender() == m_autoFocusCameraEnabled[0]) {
        cameraIndex = 0;
    } else if (sender() == m_autoFocusCameraEnabled[1]) {
        cameraIndex = 1;
    }

    QSettings settings;
    if (cameraIndex < 0) {
        m_autoFocusAppliedConfig.masterEnabled = false;
        settings.setValue(QStringLiteral("autofocus/master_enabled"), false);
    } else {
        m_autoFocusAppliedConfig.cameraEnabled[cameraIndex] = false;
        settings.setValue(QStringLiteral("autofocus/camera%1/enabled").arg(cameraIndex + 1), false);
    }
    settings.sync();
    m_autoFocusStatusLabel->setText(
        QStringLiteral("自动调焦已停止；相关焦点器人工控制已恢复。"));
    emit autoFocusDisabled(cameraIndex);
}

void FocuserControlWidget::updateAutoFocusControlStates()
{
    m_autoFocusTrimRatioSpin->setEnabled(
        static_cast<AutoFocusStatisticsMode>(
            m_autoFocusStatisticsModeCombo->currentData().toInt()) ==
        AutoFocusStatisticsMode::TrimmedMean);
    const bool manualStartAvailable =
        AutoFocusSettings::manualStartAvailable(m_autoFocusTrackingAvailable) && m_motionAllowed;
    m_autoFocusManualStartBtn->setEnabled(manualStartAvailable);
    if (!m_autoFocusTrackingAvailable) {
        m_autoFocusStatusLabel->setText(
            QStringLiteral("仅在 Tracking 状态可立即调焦或接收自动调焦 ROI 帧。"));
    }
}

void FocuserControlWidget::updateControlStates()
{
    const bool sdkOk = m_manager && m_manager->sdkLoader() && m_manager->sdkLoader()->isLoaded();
    const bool deviceOpened = m_currentState.opened;
    const bool moving = m_currentState.moving;
    const int selectedSlot = currentSlotIndex();
    const bool autoFocusLocked = selectedSlot >= 0 && selectedSlot < 2 &&
                                 m_autoFocusMotionLocked[selectedSlot];
    const bool motionAllowed = m_motionAllowed && !autoFocusLocked;
    if (!motionAllowed) {
        m_motionLockLabel->setText(
            autoFocusLocked
                ? QStringLiteral("当前光路正在自动调焦。请先关闭该路自动调焦，再进行人工焦点器操作。")
                : m_motionLockReason);
        m_motionLockLabel->show();
    } else {
        m_motionLockLabel->hide();
    }
    const bool canMove = sdkOk && deviceOpened && !moving && motionAllowed;
    const bool canWrite = sdkOk && deviceOpened && !moving && motionAllowed;

    m_refreshBtn->setEnabled(true);
    m_applyMappingBtn->setEnabled(sdkOk && !m_devices.isEmpty());
    m_swapMappingBtn->setEnabled(sdkOk && m_devices.size() >= 2);
    m_openBtn->setEnabled(sdkOk && !m_devices.isEmpty() && !deviceOpened);
    m_closeBtn->setEnabled(sdkOk && deviceOpened && motionAllowed);

    m_decreaseBtn->setEnabled(canMove);
    m_increaseBtn->setEnabled(canMove);
    m_moveToTargetBtn->setEnabled(canMove);
    m_stopBtn->setEnabled(canMove);

    m_reverseApplyBtn->setEnabled(canWrite);
    m_backlashApplyBtn->setEnabled(canWrite);
    m_beepApplyBtn->setEnabled(canWrite);
    m_ledApplyBtn->setEnabled(canWrite);
    m_resetPositionBtn->setEnabled(canWrite);
    m_maxStepApplyBtn->setEnabled(canWrite);

    m_backlashSpin->setEnabled(canWrite);
    m_maxStepSpin->setEnabled(canWrite);
    m_resetPositionSpin->setEnabled(canWrite);
    m_reverseCheck->setEnabled(canWrite);
    m_beepCheck->setEnabled(canWrite);
    m_ledCheck->setEnabled(canWrite);

    updateAutoFocusControlStates();
}

int FocuserControlWidget::currentSlotIndex() const
{
    return m_telescopeCombo->currentData().toInt();
}
