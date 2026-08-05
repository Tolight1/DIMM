#include "FocuserControlWidget.h"

#include "EafSdkLoader.h"

#include <QCheckBox>
#include <QComboBox>
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

    m_motionLockLabel = new QLabel();
    m_motionLockLabel->setWordWrap(true);
    m_motionLockLabel->setStyleSheet(QStringLiteral("QLabel { color: #ff6666; padding: 8px; }"));
    m_motionLockLabel->hide();
    mainLayout->addWidget(m_motionLockLabel);

    mainLayout->addStretch();

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
    if (allowed) {
        m_motionLockLabel->hide();
    } else {
        m_motionLockLabel->setText(reason);
        m_motionLockLabel->show();
    }
    updateControlStates();
}

void FocuserControlWidget::updateControlStates()
{
    const bool sdkOk = m_manager && m_manager->sdkLoader() && m_manager->sdkLoader()->isLoaded();
    const bool deviceOpened = m_currentState.opened;
    const bool moving = m_currentState.moving;
    const bool canMove = sdkOk && deviceOpened && !moving && m_motionAllowed;
    const bool canWrite = sdkOk && deviceOpened && !moving && m_motionAllowed;

    m_refreshBtn->setEnabled(true);
    m_applyMappingBtn->setEnabled(sdkOk && !m_devices.isEmpty());
    m_swapMappingBtn->setEnabled(sdkOk && m_devices.size() >= 2);
    m_openBtn->setEnabled(sdkOk && !m_devices.isEmpty() && !deviceOpened);
    m_closeBtn->setEnabled(sdkOk && deviceOpened);

    m_decreaseBtn->setEnabled(canMove);
    m_increaseBtn->setEnabled(canMove);
    m_moveToTargetBtn->setEnabled(canMove);
    m_stopBtn->setEnabled(sdkOk && deviceOpened);

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
}

int FocuserControlWidget::currentSlotIndex() const
{
    return m_telescopeCombo->currentData().toInt();
}
