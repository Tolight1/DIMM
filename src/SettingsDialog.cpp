#include "SettingsDialog.h"
#include "AppConfig.h"
#include "ConfigApplicationController.h"
#include "ConfigValidator.h"
#include "InitialStarDetectionConfig.h"
#include "PathUtils.h"
#include <algorithm>
#include <cmath>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStringList>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
namespace {
constexpr double kAlignmentDefaultPolarisPolarDistanceArcmin = 37.6;
QString uiStatusColor(UiStatusLevel level)
{
    switch (level) {
    case UiStatusLevel::Info:
        return QStringLiteral("#56d4ff");
    case UiStatusLevel::Success:
        return QStringLiteral("#95dd6b");
    case UiStatusLevel::Warning:
        return QStringLiteral("#ffbe55");
    case UiStatusLevel::Error:
        return QStringLiteral("#ff5c57");
    case UiStatusLevel::Muted:
    default:
        return QStringLiteral("#8ea5bb");
    }
}
QString statusLabelStyle(const QString& color)
{
    return QStringLiteral("color: %1; background: transparent; padding: 0 12px 8px 12px;").arg(color);
}
QString statusLabelStyle(UiStatusLevel level)
{
    return statusLabelStyle(uiStatusColor(level));
}
} // namespace
SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("设置"));
    resize(920, 760);
    setMinimumSize(860, 680);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setDocumentMode(true);

    auto* camTab = new QWidget();
    auto* camLayout = new QVBoxLayout(camTab);
    camLayout->setContentsMargins(12, 12, 12, 12);
    camLayout->setSpacing(14);
    auto* infoGroup = new QGroupBox(QStringLiteral("连接说明"));
    auto* infoLayout = new QVBoxLayout(infoGroup);
    auto* infoLabel = new QLabel(QStringLiteral(
        "1. 确保两台相机均已连接后再开始实时采集。\n"
        "2. 网络参数用于连接上位机或远端控制端。\n"
        "3. 点击应用后将立即写入当前运行配置。\n"
        "4. ROI 固定为 64x64，启动后两台相机分别全画幅定位，再切换各自独立 ROI。"));
    infoLabel->setWordWrap(true);
    infoLayout->addWidget(infoLabel);
    camLayout->addWidget(infoGroup);

    auto* acqGroup = new QGroupBox(QStringLiteral("采集参数"));
    auto* acqLayout = new QFormLayout(acqGroup);
    acqLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    acqLayout->setFormAlignment(Qt::AlignTop);
    acqLayout->setHorizontalSpacing(16);
    acqLayout->setVerticalSpacing(12);
    exposureEdit = new QLineEdit(QStringLiteral("1000"));
    acqLayout->addRow(QStringLiteral("曝光时间 (μs):"), exposureEdit);
    gainEdit = new QLineEdit(QStringLiteral("10.0"));
    acqLayout->addRow(QStringLiteral("增益 (dB):"), gainEdit);
    continuousFrameRateEdit = new QLineEdit(QStringLiteral("200"));
    acqLayout->addRow(QStringLiteral("连续采集帧率 (fps):"), continuousFrameRateEdit);
    camLayout->addWidget(acqGroup);

    auto* autoExposureGroup = new QGroupBox(QStringLiteral("自动曝光"));
    auto* autoExposureLayout = new QVBoxLayout(autoExposureGroup);
    autoExposureLayout->setContentsMargins(10, 12, 10, 10);
    autoExposureLayout->setSpacing(8);
    auto makeAutoExposureSection = [&](const QString& title, bool expanded) {
        auto* section = new QWidget(autoExposureGroup);
        auto* sectionLayout = new QVBoxLayout(section);
        sectionLayout->setContentsMargins(0, 0, 0, 0);
        sectionLayout->setSpacing(4);

        auto* header = new QToolButton(section);
        header->setText(title);
        header->setCheckable(true);
        header->setChecked(expanded);
        header->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        auto* content = new QWidget(section);
        auto* form = new QFormLayout(content);
        form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        form->setFormAlignment(Qt::AlignTop);
        form->setHorizontalSpacing(16);
        form->setVerticalSpacing(8);
        form->setContentsMargins(18, 2, 4, 8);
        content->setVisible(expanded);

        connect(header, &QToolButton::toggled, content, &QWidget::setVisible);
        connect(header, &QToolButton::toggled, this, [header](bool checked) {
            header->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
        });

        sectionLayout->addWidget(header);
        sectionLayout->addWidget(content);
        autoExposureLayout->addWidget(section);
        return form;
    };
    auto addToolTipRow = [](QFormLayout* layout, const QString& labelText, QWidget* widget, const QString& tip) {
        auto* label = new QLabel(labelText);
        label->setToolTip(tip);
        if (widget) {
            widget->setToolTip(tip);
        }
        layout->addRow(label, widget);
    };
    auto addToolTipCheck = [](QFormLayout* layout, QCheckBox* check, const QString& tip) {
        if (check) {
            check->setToolTip(tip);
        }
        layout->addRow(check);
    };

    auto* autoExpBasicLayout = makeAutoExposureSection(QStringLiteral("基础开关"), true);
    auto* autoExpCoreLayout = makeAutoExposureSection(QStringLiteral("核心调光"), true);
    auto* autoExpRangeLayout = makeAutoExposureSection(QStringLiteral("曝光范围与单步限制"), false);
    auto* autoExpWindowLayout = makeAutoExposureSection(QStringLiteral("窗口判定"), true);
    auto* autoExpSyncLayout = makeAutoExposureSection(QStringLiteral("双相机同步"), false);
    auto* autoExpDarkWeakLayout = makeAutoExposureSection(QStringLiteral("暗弱与丢星判定"), false);
    auto* autoExpPeakSupportLayout = makeAutoExposureSection(QStringLiteral("峰值支持检查"), false);

    autoExposureCheck = new QCheckBox(QStringLiteral("启用自动曝光"));
    addToolTipCheck(autoExpBasicLayout,
                    autoExposureCheck,
                    QStringLiteral("开启后系统会根据 AE 峰值窗口统计自动调整相机曝光；关闭后使用手动曝光。"));
    autoExpTrendConflictCheck = new QCheckBox(QStringLiteral("启用双相机趋势同步"));
    autoExpTrendConflictCheck->setChecked(true);
    addToolTipCheck(autoExpBasicLayout,
                    autoExpTrendConflictCheck,
                    QStringLiteral("开启时两台相机同步判断并同步调整曝光；关闭时两台相机各自独立调整。"));

    autoExpTargetPeakLowEdit = new QLineEdit(QStringLiteral("3000"));
    addToolTipRow(autoExpCoreLayout,
                  QStringLiteral("过暗触发阈值 (DN):"),
                  autoExpTargetPeakLowEdit,
                  QStringLiteral("有效峰值低于该值时计入过暗帧；过暗帧比例达到阈值后触发增曝光。"));
    autoExpTargetPeakHighEdit = new QLineEdit(QStringLiteral("3600"));
    addToolTipRow(autoExpCoreLayout,
                  QStringLiteral("过曝触发阈值 (DN):"),
                  autoExpTargetPeakHighEdit,
                  QStringLiteral("有效峰值高于该值时计入过曝帧；过曝帧比例达到阈值后触发降曝光。"));
    autoExpExposureHysteresisEdit = new QLineEdit(QStringLiteral("200"));
    addToolTipRow(autoExpCoreLayout,
                  QStringLiteral("曝光滞回 (DN):"),
                  autoExpExposureHysteresisEdit,
                  QStringLiteral("触发调光后用于生成停止目标，避免曝光刚跨过触发线就停止调整。"));
    autoExpDarkAdjustmentTargetEdit = new QLineEdit(QStringLiteral("3200.0"));
    autoExpDarkAdjustmentTargetEdit->setReadOnly(true);
    addToolTipRow(autoExpCoreLayout,
                  QStringLiteral("偏暗调整目标 (DN):"),
                  autoExpDarkAdjustmentTargetEdit,
                  QStringLiteral("只读派生值，等于“过暗触发阈值 + 曝光滞回”；增曝光后达到该目标才停止。"));
    autoExpBrightAdjustmentTargetEdit = new QLineEdit(QStringLiteral("3400.0"));
    autoExpBrightAdjustmentTargetEdit->setReadOnly(true);
    addToolTipRow(autoExpCoreLayout,
                  QStringLiteral("过曝调整目标 (DN):"),
                  autoExpBrightAdjustmentTargetEdit,
                  QStringLiteral("只读派生值，等于“过曝触发阈值 - 曝光滞回”；降曝光后达到该目标才停止。"));
    autoExpHardSaturationEdit = new QLineEdit(QStringLiteral("4090"));
    addToolTipRow(autoExpCoreLayout,
                  QStringLiteral("硬饱和阈值 (DN):"),
                  autoExpHardSaturationEdit,
                  QStringLiteral("光斑支持区域达到该亮度时认为存在硬饱和风险，会优先触发强制降曝光。"));
    autoExpSaturatedPixelCountEdit = new QLineEdit(QStringLiteral("1"));
    addToolTipRow(autoExpCoreLayout,
                  QStringLiteral("硬饱和像素数:"),
                  autoExpSaturatedPixelCountEdit,
                  QStringLiteral("支持区域内达到硬饱和阈值的像素数量达到该值时，该帧计入硬饱和帧。"));

    autoExpMinEdit = new QLineEdit(QStringLiteral("500"));
    addToolTipRow(autoExpRangeLayout,
                  QStringLiteral("最小曝光 (μs):"),
                  autoExpMinEdit,
                  QStringLiteral("自动曝光计算出的目标曝光不会低于该值。"));
    autoExpMaxEdit = new QLineEdit(QStringLiteral("20000"));
    addToolTipRow(autoExpRangeLayout,
                  QStringLiteral("最大曝光 (μs):"),
                  autoExpMaxEdit,
                  QStringLiteral("自动曝光计算出的目标曝光不会高于该值。"));
    autoExpMaxChangeUpEdit = new QLineEdit(QStringLiteral("1.30"));
    addToolTipRow(autoExpRangeLayout,
                  QStringLiteral("单次调亮比例上限:"),
                  autoExpMaxChangeUpEdit,
                  QStringLiteral("每次增曝光允许的最大比例，防止一步增加过多。"));
    autoExpMaxChangeDownEdit = new QLineEdit(QStringLiteral("0.70"));
    addToolTipRow(autoExpRangeLayout,
                  QStringLiteral("单次调暗比例下限:"),
                  autoExpMaxChangeDownEdit,
                  QStringLiteral("每次降曝光允许的最低比例；硬饱和强制降曝光也使用该比例。"));
    autoExpMinExposureDeltaEdit = new QLineEdit(QStringLiteral("10"));
    addToolTipRow(autoExpRangeLayout,
                  QStringLiteral("最小曝光变化 (μs):"),
                  autoExpMinExposureDeltaEdit,
                  QStringLiteral("目标曝光与当前曝光的绝对差值小于该值时，不下发曝光命令。"));
    autoExpMinExposureChangeRatioEdit = new QLineEdit(QStringLiteral("0.02"));
    addToolTipRow(autoExpRangeLayout,
                  QStringLiteral("最小曝光变化比例:"),
                  autoExpMinExposureChangeRatioEdit,
                  QStringLiteral("目标曝光与当前曝光的相对变化小于该比例时，不下发曝光命令。"));
    autoExpExposureSettleMsEdit = new QLineEdit(QStringLiteral("750"));
    addToolTipRow(autoExpRangeLayout,
                  QStringLiteral("曝光稳定等待 (ms):"),
                  autoExpExposureSettleMsEdit,
                  QStringLiteral("曝光命令成功后等待一段时间再采纳 sample，避免旧曝光帧影响新判断。"));

    autoExpSampleWindowSecEdit = new QLineEdit(QStringLiteral("10"));
    addToolTipRow(autoExpWindowLayout,
                  QStringLiteral("统计窗口 (s):"),
                  autoExpSampleWindowSecEdit,
                  QStringLiteral("自动曝光使用最近多少秒的 sample 做统计；窗口越长越稳，响应越慢。"));
    autoExpSampleIntervalMsEdit = new QLineEdit(QStringLiteral("500"));
    addToolTipRow(autoExpWindowLayout,
                  QStringLiteral("采样间隔 (ms):"),
                  autoExpSampleIntervalMsEdit,
                  QStringLiteral("自动曝光 sample 的采集间隔。"));
    autoExpMinDecisionSampleCountEdit = new QLineEdit(QStringLiteral("20"));
    addToolTipRow(autoExpWindowLayout,
                  QStringLiteral("最小决策样本数:"),
                  autoExpMinDecisionSampleCountEdit,
                  QStringLiteral("窗口内 sample 数不足该值时，不做曝光调整。"));
    autoExpDarkFrameRatioEdit = new QLineEdit(QStringLiteral("0.50"));
    addToolTipRow(autoExpWindowLayout,
                  QStringLiteral("过暗帧比例阈值:"),
                  autoExpDarkFrameRatioEdit,
                  QStringLiteral("过暗帧占窗口决策样本的比例达到该值后触发增曝光。"));
    autoExpBrightFrameRatioEdit = new QLineEdit(QStringLiteral("0.30"));
    addToolTipRow(autoExpWindowLayout,
                  QStringLiteral("过亮帧比例阈值:"),
                  autoExpBrightFrameRatioEdit,
                  QStringLiteral("过曝帧占有效峰值样本的比例达到该值后触发降曝光。"));
    autoExpStableFrameRatioEdit = new QLineEdit(QStringLiteral("0.70"));
    addToolTipRow(autoExpWindowLayout,
                  QStringLiteral("稳定帧比例阈值:"),
                  autoExpStableFrameRatioEdit,
                  QStringLiteral("稳定帧占有效峰值样本的比例达到该值后认为曝光稳定。"));
    autoExpHardSaturationFrameRatioEdit = new QLineEdit(QStringLiteral("0.05"));
    addToolTipRow(autoExpWindowLayout,
                  QStringLiteral("硬饱和帧比例阈值:"),
                  autoExpHardSaturationFrameRatioEdit,
                  QStringLiteral("硬饱和帧占窗口决策样本的比例达到该值后优先强制降曝光。"));

    autoExpCameraAgreementRatioEdit = new QLineEdit(QStringLiteral("0.50"));
    addToolTipRow(autoExpSyncLayout,
                  QStringLiteral("双相机峰值差异上限:"),
                  autoExpCameraAgreementRatioEdit,
                  QStringLiteral("两台相机峰值差异超过该比例时，可能认为趋势冲突。"));
    autoExpTrendConflictPersistenceSecEdit = new QLineEdit(QStringLiteral("30"));
    addToolTipRow(autoExpSyncLayout,
                  QStringLiteral("趋势冲突持续时间 (s):"),
                  autoExpTrendConflictPersistenceSecEdit,
                  QStringLiteral("趋势冲突持续达到该时间后进入保护状态，暂停同步曝光调整。"));

    autoExpDarkSnrWarningEdit = new QLineEdit(QStringLiteral("8.0"));
    addToolTipRow(autoExpDarkWeakLayout,
                  QStringLiteral("偏暗 SNR 报警:"),
                  autoExpDarkSnrWarningEdit,
                  QStringLiteral("SNR 低于该值时提示偏暗风险。"));
    autoExpDarkSnrCriticalEdit = new QLineEdit(QStringLiteral("5.0"));
    addToolTipRow(autoExpDarkWeakLayout,
                  QStringLiteral("严重偏暗 SNR:"),
                  autoExpDarkSnrCriticalEdit,
                  QStringLiteral("SNR 低于该值时认为偏暗更严重；必须小于偏暗 SNR 报警。"));
    autoExpMinValidCentroidRatioEdit = new QLineEdit(QStringLiteral("0.50"));
    addToolTipRow(autoExpDarkWeakLayout,
                  QStringLiteral("最小有效质心比例:"),
                  autoExpMinValidCentroidRatioEdit,
                  QStringLiteral("有效质心比例低于该值时，说明当前窗口内跟踪质量偏差。"));
    autoExpStarLostValidRatioEdit = new QLineEdit(QStringLiteral("0.10"));
    addToolTipRow(autoExpDarkWeakLayout,
                  QStringLiteral("丢星有效质心比例:"),
                  autoExpStarLostValidRatioEdit,
                  QStringLiteral("有效质心比例低于该值时，更倾向判断为丢星或信号过弱。"));

    autoExpPeakSupportRadiusEdit = new QLineEdit(QStringLiteral("2"));
    addToolTipRow(autoExpPeakSupportLayout,
                  QStringLiteral("峰值支持半径 (px):"),
                  autoExpPeakSupportRadiusEdit,
                  QStringLiteral("检查候选峰值周围多大半径内是否存在足够支持像素。"));
    autoExpPeakSupportFractionEdit = new QLineEdit(QStringLiteral("0.50"));
    addToolTipRow(autoExpPeakSupportLayout,
                  QStringLiteral("峰值支持比例:"),
                  autoExpPeakSupportFractionEdit,
                  QStringLiteral("支持像素需要达到候选峰值的一定比例，用来区分真实光斑和孤立亮点。"));
    autoExpMinPeakSupportPixelsEdit = new QLineEdit(QStringLiteral("3"));
    addToolTipRow(autoExpPeakSupportLayout,
                  QStringLiteral("最小支持像素数:"),
                  autoExpMinPeakSupportPixelsEdit,
                  QStringLiteral("候选峰值邻域内至少需要多少个支持像素才认为峰值有效。"));
    autoExpMinNeighborPeakRatioEdit = new QLineEdit(QStringLiteral("0.35"));
    addToolTipRow(autoExpPeakSupportLayout,
                  QStringLiteral("邻域峰值比例下限:"),
                  autoExpMinNeighborPeakRatioEdit,
                  QStringLiteral("邻域最强像素相对候选峰值的比例下限，用于过滤单点热像素。"));
    autoExpMaxPeakCandidateCountEdit = new QLineEdit(QStringLiteral("8"));
    addToolTipRow(autoExpPeakSupportLayout,
                  QStringLiteral("最大峰值候选数:"),
                  autoExpMaxPeakCandidateCountEdit,
                  QStringLiteral("单帧最多检查多少个峰值候选，避免最高点异常时错过真实光斑。"));
    autoExpSupportedPeakPercentileEdit = new QLineEdit(QStringLiteral("0.95"));
    addToolTipRow(autoExpPeakSupportLayout,
                  QStringLiteral("支持区域峰值分位:"),
                  autoExpSupportedPeakPercentileEdit,
                  QStringLiteral("在支持区域内取该分位值作为 AE 峰值，降低单个异常亮点的影响。"));
    auto updateAutoExposureAdjustmentTargets = [this]() {
        bool lowOk = false;
        bool highOk = false;
        bool hysteresisOk = false;
        const double low = autoExpTargetPeakLowEdit ? autoExpTargetPeakLowEdit->text().toDouble(&lowOk) : 0.0;
        const double high = autoExpTargetPeakHighEdit ? autoExpTargetPeakHighEdit->text().toDouble(&highOk) : 0.0;
        const double hysteresis =
            autoExpExposureHysteresisEdit ? autoExpExposureHysteresisEdit->text().toDouble(&hysteresisOk) : 0.0;
        if (autoExpDarkAdjustmentTargetEdit) {
            autoExpDarkAdjustmentTargetEdit->setText(
                lowOk && hysteresisOk ? QString::number(low + std::max(0.0, hysteresis), 'f', 1)
                                      : QString());
        }
        if (autoExpBrightAdjustmentTargetEdit) {
            autoExpBrightAdjustmentTargetEdit->setText(
                highOk && hysteresisOk ? QString::number(high - std::max(0.0, hysteresis), 'f', 1)
                                       : QString());
        }
    };
    connect(autoExpTargetPeakLowEdit,
            &QLineEdit::textChanged,
            this,
            [updateAutoExposureAdjustmentTargets](const QString&) { updateAutoExposureAdjustmentTargets(); });
    connect(autoExpTargetPeakHighEdit,
            &QLineEdit::textChanged,
            this,
            [updateAutoExposureAdjustmentTargets](const QString&) { updateAutoExposureAdjustmentTargets(); });
    connect(autoExpExposureHysteresisEdit,
            &QLineEdit::textChanged,
            this,
            [updateAutoExposureAdjustmentTargets](const QString&) { updateAutoExposureAdjustmentTargets(); });
    updateAutoExposureAdjustmentTargets();
    camLayout->addWidget(autoExposureGroup);
    camLayout->addStretch();
    addSettingsPage(camTab, QStringLiteral("相机设置"));

    auto* triggerTab = new QWidget();
    auto* triggerTabLayout = new QVBoxLayout(triggerTab);
    triggerTabLayout->setContentsMargins(12, 12, 12, 12);
    triggerTabLayout->setSpacing(14);

    auto* triggerModeGroup = new QGroupBox(QStringLiteral("触发模式"));
    auto* triggerModeLayout = new QVBoxLayout(triggerModeGroup);
    triggerModeLayout->setContentsMargins(16, 14, 16, 14);
    triggerModeLayout->setSpacing(10);
    triggerContinuous = new QRadioButton(QStringLiteral("连续采集"));
    triggerContinuous->setChecked(true);
    triggerHardware = new QRadioButton(QStringLiteral("硬件触发"));
    triggerModeLayout->addWidget(triggerContinuous);
    triggerModeLayout->addWidget(triggerHardware);
    triggerTabLayout->addWidget(triggerModeGroup);

    auto* envSensorGroup = new QGroupBox(QStringLiteral("温湿压传感器"));
    auto* envSensorLayout = new QFormLayout(envSensorGroup);
    envSensorLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    envSensorLayout->setFormAlignment(Qt::AlignTop);
    envSensorLayout->setHorizontalSpacing(16);
    envSensorLayout->setVerticalSpacing(10);
    envSensorEnableCheck = new QCheckBox(QStringLiteral("启用温湿压传感器"));
    envSensorEnableCheck->setChecked(true);
    envSensorLayout->addRow(envSensorEnableCheck);
    envSensorPortEdit = new QLineEdit(QStringLiteral("COM6"));
    envSensorLayout->addRow(QStringLiteral("串口号:"), envSensorPortEdit);
    envSensorBaudCombo = new QComboBox();
    envSensorBaudCombo->addItems({QStringLiteral("9600"),
                                  QStringLiteral("19200"),
                                  QStringLiteral("38400"),
                                  QStringLiteral("57600"),
                                  QStringLiteral("115200")});
    envSensorBaudCombo->setCurrentText(QStringLiteral("9600"));
    envSensorLayout->addRow(QStringLiteral("波特率:"), envSensorBaudCombo);
    envSensorAddressEdit = new QLineEdit(QStringLiteral("1"));
    envSensorLayout->addRow(QStringLiteral("设备地址:"), envSensorAddressEdit);
    envSensorPollIntervalEdit = new QLineEdit(QStringLiteral("1000"));
    envSensorLayout->addRow(QStringLiteral("轮询间隔 (ms):"), envSensorPollIntervalEdit);
    auto* envSensorHint = new QLabel(QStringLiteral("说明: 修改串口后点击“应用”，环境温度、湿度、气压读取线程会使用新端口重新连接。"));
    envSensorHint->setWordWrap(true);
    envSensorLayout->addRow(QString(), envSensorHint);
    triggerTabLayout->addWidget(envSensorGroup);

    auto* pulseGroup = new QGroupBox(QStringLiteral("脉冲发生器"));
    auto* pulseLayout = new QFormLayout(pulseGroup);
    pulseLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    pulseLayout->setFormAlignment(Qt::AlignTop);
    pulseLayout->setHorizontalSpacing(16);
    pulseLayout->setVerticalSpacing(10);
    pulseEnableCheck = new QCheckBox(QStringLiteral("启用触发输出"));
    pulseLayout->addRow(pulseEnableCheck);
    pulsePortEdit = new QLineEdit(QStringLiteral("COM9"));
    pulseLayout->addRow(QStringLiteral("端口号:"), pulsePortEdit);
    pulseBaudCombo = new QComboBox();
    pulseBaudCombo->addItems({QStringLiteral("9600"),
                              QStringLiteral("19200"),
                              QStringLiteral("38400"),
                              QStringLiteral("57600"),
                              QStringLiteral("115200")});
    pulseBaudCombo->setCurrentText(QStringLiteral("19200"));
    pulseLayout->addRow(QStringLiteral("波特率:"), pulseBaudCombo);
    pulseTerminalEdit = new QLineEdit(QStringLiteral("1"));
    pulseLayout->addRow(QStringLiteral("终端号:"), pulseTerminalEdit);
    pulseFreqEdit = new QLineEdit(QStringLiteral("200.0"));
    pulseApplyFreqBtn = new QPushButton(QStringLiteral("修改频率"));
    auto* freqRow = new QWidget();
    auto* freqLayout = new QHBoxLayout(freqRow);
    freqLayout->setContentsMargins(0, 0, 0, 0);
    freqLayout->setSpacing(10);
    freqLayout->addWidget(pulseFreqEdit, 1);
    freqLayout->addWidget(pulseApplyFreqBtn);
    pulseLayout->addRow(QStringLiteral("输出频率 (Hz):"), freqRow);

    pulseCountEdit = new QLineEdit(QStringLiteral("2000000"));
    pulseApplyCountBtn = new QPushButton(QStringLiteral("修改脉冲个数"));
    auto* countRow = new QWidget();
    auto* countLayout = new QHBoxLayout(countRow);
    countLayout->setContentsMargins(0, 0, 0, 0);
    countLayout->setSpacing(10);
    countLayout->addWidget(pulseCountEdit, 1);
    countLayout->addWidget(pulseApplyCountBtn);
    pulseLayout->addRow(QStringLiteral("脉冲个数:"), countRow);

    pulseDutyEdit = new QLineEdit(QStringLiteral("50"));
    pulseApplyDutyBtn = new QPushButton(QStringLiteral("修改占空比"));
    auto* dutyRow = new QWidget();
    auto* dutyLayout = new QHBoxLayout(dutyRow);
    dutyLayout->setContentsMargins(0, 0, 0, 0);
    dutyLayout->setSpacing(10);
    dutyLayout->addWidget(pulseDutyEdit, 1);
    dutyLayout->addWidget(pulseApplyDutyBtn);
    pulseLayout->addRow(QStringLiteral("占空比 (%):"), dutyRow);

    auto* sourceWidget = new QWidget();
    auto* sourceLayout = new QHBoxLayout(sourceWidget);
    sourceLayout->setContentsMargins(0, 0, 0, 0);
    sourceLayout->setSpacing(16);
    pulseSourceLocal = new QRadioButton(QStringLiteral("本地"));
    pulseSourceRemote = new QRadioButton(QStringLiteral("远程"));
    pulseSourceRemote->setChecked(true);
    sourceLayout->addWidget(pulseSourceLocal);
    sourceLayout->addWidget(pulseSourceRemote);
    pulseApplySourceBtn = new QPushButton(QStringLiteral("修改控制类型"));
    sourceLayout->addWidget(pulseApplySourceBtn);
    sourceLayout->addStretch();
    pulseLayout->addRow(QStringLiteral("来源控制:"), sourceWidget);

    auto* pulseHint = new QLabel(QStringLiteral("默认建议: 波特率 19200，终端号 1，占空比 50，来源控制选择远程。"));
    pulseHint->setWordWrap(true);
    pulseLayout->addRow(QString(), pulseHint);
    auto* pulseCommitHint = new QLabel(QStringLiteral("说明: 输出频率、脉冲个数、占空比、来源控制修改后，需点击对应按钮才算生效。"));
    pulseCommitHint->setWordWrap(true);
    pulseLayout->addRow(QString(), pulseCommitHint);
    auto* pulseActionRow = new QWidget();
    auto* pulseActionLayout = new QHBoxLayout(pulseActionRow);
    pulseActionLayout->setContentsMargins(0, 4, 0, 0);
    pulseActionLayout->setSpacing(10);
    pulseStartBtn = new QPushButton(QStringLiteral("输出脉冲"));
    pulseStartBtn->setProperty("role", "primary");
    pulseStopBtn = new QPushButton(QStringLiteral("关闭脉冲"));
    pulseStopBtn->setProperty("role", "secondary");
    pulseActionLayout->addWidget(pulseStartBtn);
    pulseActionLayout->addWidget(pulseStopBtn);
    pulseActionLayout->addStretch();
    pulseLayout->addRow(QStringLiteral("即时控制:"), pulseActionRow);
    triggerTabLayout->addWidget(pulseGroup);
    triggerTabLayout->addStretch();
    addSettingsPage(triggerTab, QStringLiteral("触发设置"));

    auto* procTab = new QWidget();
    auto* procLayout = new QVBoxLayout(procTab);
    procLayout->setContentsMargins(12, 12, 12, 12);
    procLayout->setSpacing(14);
    auto* centroidGroup = new QGroupBox(QStringLiteral("质心算法"));
    auto* centroidLayout = new QFormLayout(centroidGroup);
    centroidLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    centroidLayout->setFormAlignment(Qt::AlignTop);
    centroidLayout->setHorizontalSpacing(16);
    centroidLayout->setVerticalSpacing(10);
    centroidModeCombo = new QComboBox();
    centroidModeCombo->addItem(QStringLiteral("背景阈值重心"), 0);
    centroidModeCombo->addItem(QStringLiteral("峰值小核"), 1);
    centroidModeCombo->setToolTip(QStringLiteral(
        "选择 ROI 质心处理路径。背景阈值重心保留旧流程；峰值小核只在光斑峰值周围计算质心。"));
    centroidLayout->addRow(QStringLiteral("质心处理模式:"), centroidModeCombo);
    procLayout->addWidget(centroidGroup);

    auto* preprocessGroup = new QGroupBox(QStringLiteral("背景阈值重心参数"));
    auto* preprocessLayout = new QGridLayout(preprocessGroup);
    auto* backgroundKernelLabel = new QLabel(QStringLiteral("背景核大小:"));
    backgroundKernelLabel->setToolTip(QStringLiteral("旧模式左上角背景统计核边长，运行时会夹到有效奇数尺寸。"));
    preprocessLayout->addWidget(backgroundKernelLabel, 0, 0);
    procKernelSize = new QLineEdit(QStringLiteral("5"));
    procKernelSize->setToolTip(QStringLiteral("旧模式左上角背景统计核边长，默认 5。"));
    preprocessLayout->addWidget(procKernelSize, 0, 1);
    auto* backgroundSigmaLabel = new QLabel(QStringLiteral("背景标准差倍数:"));
    backgroundSigmaLabel->setToolTip(QStringLiteral("旧模式动态阈值使用 background + k*sigma，默认 4.0。"));
    preprocessLayout->addWidget(backgroundSigmaLabel, 1, 0);
    procSigma = new QLineEdit(QStringLiteral("4.0"));
    procSigma->setToolTip(QStringLiteral("旧模式动态阈值的背景标准差倍数，必须不小于 0。"));
    preprocessLayout->addWidget(procSigma, 1, 1);
    auto* centroidPipelineHint =
        new QLabel(QStringLiteral("ROI质心流程: 热像素修正 -> 左上角背景核估计 -> 阈值去噪 -> 背景扣除重心。"));
    centroidPipelineHint->setWordWrap(true);
    preprocessLayout->addWidget(centroidPipelineHint, 2, 0, 1, 2);
    procLayout->addWidget(preprocessGroup);

    auto* peakKernelGroup = new QGroupBox(QStringLiteral("峰值小核参数"));
    auto* peakKernelLayout = new QFormLayout(peakKernelGroup);
    peakKernelLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    peakKernelLayout->setFormAlignment(Qt::AlignTop);
    peakKernelLayout->setHorizontalSpacing(16);
    peakKernelLayout->setVerticalSpacing(10);
    peakKernelMethodCombo = new QComboBox();
    peakKernelMethodCombo->addItem(QStringLiteral("光强重心"), 0);
    peakKernelMethodCombo->addItem(QStringLiteral("高斯拟合峰值"), 1);
    peakKernelMethodCombo->setCurrentIndex(1);
    peakKernelMethodCombo->setToolTip(QStringLiteral(
        "光强重心直接使用小核内扣模板后的像素强度；高斯拟合峰值忽略强固定热像素并拟合峰值坐标。"));
    peakKernelRadiusEdit = new QLineEdit(QStringLiteral("3"));
    peakKernelRadiusEdit->setToolTip(QStringLiteral(
        "以 AE 峰值坐标为中心的小核半径，默认 3，实际核大小为 (2r+1)x(2r+1)。"));
    strongHotPixelExcessEdit = new QLineEdit(QStringLiteral("100"));
    strongHotPixelExcessEdit->setToolTip(QStringLiteral(
        "只有固定热像素模板中 excess 严格大于该 DN 的点位参与小核强坏点判断。"));
    peakKernelLayout->addRow(QStringLiteral("小核算法:"), peakKernelMethodCombo);
    peakKernelLayout->addRow(QStringLiteral("小核半径(px):"), peakKernelRadiusEdit);
    peakKernelLayout->addRow(QStringLiteral("强固定热像素阈值(DN):"), strongHotPixelExcessEdit);
    procLayout->addWidget(peakKernelGroup);

    const auto updateCentroidModeControls = [preprocessGroup, peakKernelGroup](int modeIndex) {
        const bool peakKernelMode = modeIndex == 1;
        preprocessGroup->setEnabled(!peakKernelMode);
        peakKernelGroup->setEnabled(peakKernelMode);
    };
    connect(centroidModeCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            updateCentroidModeControls);
    updateCentroidModeControls(centroidModeCombo->currentIndex());

    auto* roiRecenterGroup = new QGroupBox(QStringLiteral("ROI 重居中参数"));
    auto* roiRecenterLayout = new QFormLayout(roiRecenterGroup);
    roiRecenterLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    roiRecenterLayout->setFormAlignment(Qt::AlignTop);
    roiRecenterLayout->setHorizontalSpacing(16);
    roiRecenterLayout->setVerticalSpacing(10);
    roiRecenterThresholdEdit = new QLineEdit(QStringLiteral("16.0"));
    roiRecenterLayout->addRow(QStringLiteral("距边缘阈值(px):"), roiRecenterThresholdEdit);
    roiRecenterRequiredFramesEdit = new QLineEdit(QStringLiteral("5"));
    roiRecenterLayout->addRow(QStringLiteral("连续帧数:"), roiRecenterRequiredFramesEdit);
    roiRecenterCooldownMsEdit = new QLineEdit(QStringLiteral("3000"));
    roiRecenterLayout->addRow(QStringLiteral("冷却时间(ms):"), roiRecenterCooldownMsEdit);
    roiRecenterMinimumShiftEdit = new QLineEdit(QStringLiteral("8.0"));
    roiRecenterLayout->addRow(QStringLiteral("最小位移(px):"), roiRecenterMinimumShiftEdit);
    auto* roiRecenterHint = new QLabel(QStringLiteral(
        "这些参数只控制运行中 ROI 重新居中；星点靠边或丢失时仍会进入全画幅重定位。"));
    roiRecenterHint->setWordWrap(true);
    roiRecenterLayout->addRow(QString(), roiRecenterHint);
    procLayout->addWidget(roiRecenterGroup);

    const InitialStarDetectionConfig starConfig = currentInitialStarDetectionConfig();
    auto* starDetectionGroup = new QGroupBox(QStringLiteral("全画幅找星参数"));
    auto* starDetectionLayout = new QFormLayout(starDetectionGroup);
    starDetectionLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    starDetectionLayout->setFormAlignment(Qt::AlignTop);
    starDetectionLayout->setHorizontalSpacing(16);
    starDetectionLayout->setVerticalSpacing(10);
    starThresholdAbsoluteEdit =
        new QLineEdit(QString::number(starConfig.thresholdAbsolute, 'f', 1));
    starDetectionLayout->addRow(QStringLiteral("绝对阈值 (-1 自动):"), starThresholdAbsoluteEdit);
    starSigmaThresholdEdit = new QLineEdit(QString::number(starConfig.sigmaThreshold, 'f', 2));
    starDetectionLayout->addRow(QStringLiteral("背景倍数 σ:"), starSigmaThresholdEdit);
    starPeakFractionEdit = new QLineEdit(QString::number(starConfig.peakFraction, 'f', 2));
    starDetectionLayout->addRow(QStringLiteral("峰值比例:"), starPeakFractionEdit);
    starMinimumIntensityEdit = new QLineEdit(QString::number(starConfig.minimumIntensity, 'f', 1));
    starDetectionLayout->addRow(QStringLiteral("最低亮度:"), starMinimumIntensityEdit);
    starMinAreaEdit = new QLineEdit(QString::number(starConfig.minArea));
    starDetectionLayout->addRow(QStringLiteral("最小面积:"), starMinAreaEdit);
    starMaxAreaEdit = new QLineEdit(QString::number(starConfig.maxArea));
    starDetectionLayout->addRow(QStringLiteral("最大面积:"), starMaxAreaEdit);
    auto* starDetectionHint = new QLabel(QStringLiteral(
        "这些参数只影响全画幅候选框/首次定位/重定位，不改变 ROI 内高频质心算法。"));
    starDetectionHint->setWordWrap(true);
    starDetectionLayout->addRow(QString(), starDetectionHint);
    procLayout->addWidget(starDetectionGroup);

    auto* hotPixelGroup = new QGroupBox(QStringLiteral("热像素模板"));
    auto* hotPixelLayout = new QFormLayout(hotPixelGroup);
    hotPixelLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    hotPixelLayout->setFormAlignment(Qt::AlignTop);
    hotPixelLayout->setHorizontalSpacing(16);
    hotPixelLayout->setVerticalSpacing(10);
    hotPixelEnableCheck = new QCheckBox(QStringLiteral("启用热像素修正"));
    hotPixelLayout->addRow(hotPixelEnableCheck);

    const auto makeFileRow = [this](QLineEdit** edit, const QString& title) {
        auto* row = new QWidget();
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        *edit = new QLineEdit();
        auto* browse = new QPushButton(QStringLiteral("浏览..."));
        connect(browse, &QPushButton::clicked, this, [this, edit, title]() {
            const QString file = QFileDialog::getOpenFileName(
                this,
                title,
                (*edit)->text().trimmed(),
                QStringLiteral("模板文件 (*.bin *.raw *.dat *.txt);;所有文件 (*.*)"));
            if (!file.isEmpty()) {
                (*edit)->setText(PathUtils::relativizePathToAppDir(file));
            }
        });
        layout->addWidget(*edit, 1);
        layout->addWidget(browse);
        return row;
    };

    hotPixelLayout->addRow(QStringLiteral("相机1 Mask:"), makeFileRow(&hotPixelCam0MaskEdit, QStringLiteral("选择相机1热像素 Mask")));
    hotPixelLayout->addRow(QStringLiteral("相机1 Excess:"), makeFileRow(&hotPixelCam0ExcessEdit, QStringLiteral("选择相机1热像素 Excess")));
    hotPixelLayout->addRow(QStringLiteral("相机2 Mask:"), makeFileRow(&hotPixelCam1MaskEdit, QStringLiteral("选择相机2热像素 Mask")));
    hotPixelLayout->addRow(QStringLiteral("相机2 Excess:"), makeFileRow(&hotPixelCam1ExcessEdit, QStringLiteral("选择相机2热像素 Excess")));
    hotPixelTemplateWidthEdit = new QLineEdit(QStringLiteral("0"));
    hotPixelLayout->addRow(QStringLiteral("模板宽度:"), hotPixelTemplateWidthEdit);
    hotPixelTemplateHeightEdit = new QLineEdit(QStringLiteral("0"));
    hotPixelLayout->addRow(QStringLiteral("模板高度:"), hotPixelTemplateHeightEdit);
    auto* hotPixelHint = new QLabel(QStringLiteral("启用后需提供两台相机的 mask/excess 模板和完整模板尺寸；未启用时会清空当前热像素修正。"));
    hotPixelHint->setWordWrap(true);
    hotPixelLayout->addRow(QString(), hotPixelHint);
    procLayout->addWidget(hotPixelGroup);
    procLayout->addStretch();
    addSettingsPage(procTab, QStringLiteral("图像处理"));

    auto* sysTab = new QWidget();
    auto* sysLayout = new QVBoxLayout(sysTab);
    sysLayout->setContentsMargins(12, 12, 12, 12);
    sysLayout->setSpacing(14);
    auto* opticsGroup = new QGroupBox(QStringLiteral("光学系统"));
    auto* opticsLayout = new QGridLayout(opticsGroup);
    opticsLayout->addWidget(new QLabel(QStringLiteral("子孔径直径 D (mm):")), 0, 0);
    opticsD = new QLineEdit(QStringLiteral("56"));
    opticsLayout->addWidget(opticsD, 0, 1);
    opticsLayout->addWidget(new QLabel(QStringLiteral("中心间距 d (mm):")), 1, 0);
    opticsBaseline = new QLineEdit(QStringLiteral("250"));
    opticsLayout->addWidget(opticsBaseline, 1, 1);
    opticsLayout->addWidget(new QLabel(QStringLiteral("基线方向角 α (deg):")), 2, 0);
    opticsBaselineAngle = new QLineEdit(QStringLiteral("0"));
    opticsLayout->addWidget(opticsBaselineAngle, 2, 1);
    opticsLayout->addWidget(new QLabel(QStringLiteral("焦距 f (cm):")), 3, 0);
    opticsF = new QLineEdit(QStringLiteral("26.9"));
    opticsLayout->addWidget(opticsF, 3, 1);
    opticsLayout->addWidget(new QLabel(QStringLiteral("天顶角 Z (deg):")), 4, 0);
    opticsZenith = new QLineEdit(QStringLiteral("49.6"));
    opticsLayout->addWidget(opticsZenith, 4, 1);
    sysLayout->addWidget(opticsGroup);

    auto* detectorGroup = new QGroupBox(QStringLiteral("探测器"));
    auto* detectorLayout = new QGridLayout(detectorGroup);
    detectorLayout->addWidget(new QLabel(QStringLiteral("像素尺寸 (μm):")), 0, 0);
    detectorPixelSize = new QLineEdit(QStringLiteral("2.5"));
    detectorLayout->addWidget(detectorPixelSize, 0, 1);
    detectorLayout->addWidget(new QLabel(QStringLiteral("对比波长 (nm):")), 1, 0);
    detectorWavelength = new QLineEdit(QStringLiteral("500"));
    detectorLayout->addWidget(detectorWavelength, 1, 1);
    sysLayout->addWidget(detectorGroup);

    auto* alignmentGroup = new QGroupBox(QStringLiteral("对准设置"));
    auto* alignmentLayout = new QFormLayout(alignmentGroup);
    alignmentLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    alignmentLayout->setFormAlignment(Qt::AlignTop);
    alignmentLayout->setHorizontalSpacing(16);
    alignmentLayout->setVerticalSpacing(10);
    alignmentAutoRadiusCheck = new QCheckBox(QStringLiteral("启用自动半径计算"));
    alignmentAutoRadiusCheck->setChecked(true);
    alignmentLayout->addRow(alignmentAutoRadiusCheck);
    alignmentAutoSolveCheck = new QCheckBox(QStringLiteral("启用北极星自动识别"));
    alignmentAutoSolveCheck->setChecked(true);
    alignmentLayout->addRow(alignmentAutoSolveCheck);
    alignmentShowMatchedCatalogStarsCheck = new QCheckBox(QStringLiteral("显示星表匹配星"));
    alignmentShowMatchedCatalogStarsCheck->setChecked(true);
    alignmentLayout->addRow(alignmentShowMatchedCatalogStarsCheck);
    alignmentFocalLengthEdit = new QLineEdit(QStringLiteral("269"));
    alignmentLayout->addRow(QStringLiteral("焦距 (mm):"), alignmentFocalLengthEdit);
    alignmentPixelSizeEdit = new QLineEdit(QStringLiteral("2.5"));
    alignmentLayout->addRow(QStringLiteral("像元尺寸 (μm):"), alignmentPixelSizeEdit);
    alignmentPolarDistanceEdit =
        new QLineEdit(QString::number(kAlignmentDefaultPolarisPolarDistanceArcmin, 'f', 1));
    alignmentLayout->addRow(QStringLiteral("北极星极距 (arcmin):"), alignmentPolarDistanceEdit);
    alignmentRadiusAdjustEdit = new QLineEdit(QStringLiteral("0"));
    alignmentLayout->addRow(QStringLiteral("轨道半径微调 (px):"), alignmentRadiusAdjustEdit);
    alignmentPreviewRateEdit = new QLineEdit(QStringLiteral("1.0"));
    alignmentLayout->addRow(QStringLiteral("对准预览频率 (Hz):"), alignmentPreviewRateEdit);
    alignmentMaxDetectedStarsEdit = new QLineEdit(QStringLiteral("20"));
    alignmentLayout->addRow(QStringLiteral("最大参与匹配星数:"), alignmentMaxDetectedStarsEdit);
    alignmentMinMatchedStarsEdit = new QLineEdit(QStringLiteral("5"));
    alignmentLayout->addRow(QStringLiteral("最少匹配星数:"), alignmentMinMatchedStarsEdit);
    alignmentMaxRmsEdit = new QLineEdit(QStringLiteral("3.0"));
    alignmentLayout->addRow(QStringLiteral("最大匹配 RMS (px):"), alignmentMaxRmsEdit);
    alignmentRetryIntervalEdit = new QLineEdit(QStringLiteral("3.0"));
    alignmentLayout->addRow(QStringLiteral("自动重试间隔 (s):"), alignmentRetryIntervalEdit);
    alignmentMinSpatialSpreadEdit = new QLineEdit(QStringLiteral("50.0"));
    alignmentLayout->addRow(QStringLiteral("最小空间跨度 (px):"), alignmentMinSpatialSpreadEdit);
    alignmentMinPolarisSnrEdit = new QLineEdit(QStringLiteral("5.0"));
    alignmentLayout->addRow(QStringLiteral("北极星最小 SNR:"), alignmentMinPolarisSnrEdit);
    alignmentAllowSaturatedPolarisCheck = new QCheckBox(QStringLiteral("允许饱和北极星自动确认"));
    alignmentAllowSaturatedPolarisCheck->setChecked(false);
    alignmentLayout->addRow(alignmentAllowSaturatedPolarisCheck);
    auto* alignmentHint = new QLabel(QStringLiteral(
        "对准模式只用于低频全画幅寻星，不启用 ROI、不计算大气参数、不保存测量数据。"));
    alignmentHint->setWordWrap(true);
    alignmentLayout->addRow(QString(), alignmentHint);
    sysLayout->addWidget(alignmentGroup);
    sysLayout->addStretch();
    addSettingsPage(sysTab, QStringLiteral("系统参数"));

    auto* storeTab = new QWidget();
    auto* storeLayout = new QVBoxLayout(storeTab);
    storeLayout->setContentsMargins(12, 12, 12, 12);
    storeLayout->setSpacing(14);
    auto* pathGroup = new QGroupBox(QStringLiteral("存储路径"));
    auto* pathLayout = new QHBoxLayout(pathGroup);
    storagePathEdit = new QLineEdit(QStringLiteral("D:/C-DIMM/data"));
    auto* browseBtn = new QPushButton(QStringLiteral("浏览..."));
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, QStringLiteral("选择存储路径"), storagePathEdit->text());
        if (!dir.isEmpty()) {
            storagePathEdit->setText(dir);
        }
    });
    pathLayout->addWidget(storagePathEdit);
    pathLayout->addWidget(browseBtn);
    storeLayout->addWidget(pathGroup);

    auto* paramGroup = new QGroupBox(QStringLiteral("参数存储"));
    auto* paramLayout = new QVBoxLayout(paramGroup);
    auto* intervalLayout = new QHBoxLayout();
    intervalLayout->addWidget(new QLabel(QStringLiteral("参数记录间隔 (次):")));
    saveIntervalEdit = new QLineEdit(QStringLiteral("1"));
    intervalLayout->addWidget(saveIntervalEdit);
    parameterValidationCheck = new QCheckBox(QStringLiteral("参数验证模式"));
    parameterValidationCheck->setChecked(false);
    syncDiagnosticLogCheck = new QCheckBox(QStringLiteral("保存同步诊断日志"));
    syncDiagnosticLogCheck->setChecked(false);
    syncDiagnosticLogCheck->setToolTip(
        QStringLiteral("开启后随结果文件保存 capture/submit/unpaired_drop 事件，用于定位硬件触发双目配对丢帧。"));
    parameterValidationCheck->setToolTip(
        QStringLiteral("开启后额外保存每秒双相机配对质心明细，用于核对方差和参数计算"));
    auto* paramInfo = new QLabel(QStringLiteral("仅保存计算后的质心、ROI 和大气参数，不保存全画幅或 ROI 图像。"));
    paramInfo->setWordWrap(true);
    paramLayout->addLayout(intervalLayout);
    paramLayout->addWidget(parameterValidationCheck);
    paramLayout->addWidget(syncDiagnosticLogCheck);
    paramLayout->addWidget(paramInfo);
    storeLayout->addWidget(paramGroup);

    auto* resultGroup = new QGroupBox(QStringLiteral("结果存储 (CSV)"));
    auto* resultLayout = new QVBoxLayout(resultGroup);
    auto* resultInfo = new QLabel(QStringLiteral("自动保存: 时间戳、帧号、双相机质心、ROI、峰值亮度和大气参数"));
    resultInfo->setWordWrap(true);
    resultLayout->addWidget(resultInfo);
    storeLayout->addWidget(resultGroup);
    storeLayout->addStretch();
    addSettingsPage(storeTab, QStringLiteral("数据存储"));

    auto* autoAcquisitionTab = new QWidget();
    auto* autoAcquisitionLayout = new QVBoxLayout(autoAcquisitionTab);
    autoAcquisitionLayout->setContentsMargins(12, 12, 12, 12);
    autoAcquisitionLayout->setSpacing(14);

    auto* autoAcquisitionGroup = new QGroupBox(QStringLiteral("自动采集"));
    auto* autoAcquisitionForm = new QFormLayout(autoAcquisitionGroup);
    autoAcquisitionForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    autoAcquisitionForm->setFormAlignment(Qt::AlignTop);
    autoAcquisitionForm->setHorizontalSpacing(16);
    autoAcquisitionForm->setVerticalSpacing(12);

    autoAcquisitionEnableCheck = new QCheckBox(QStringLiteral("启用自动采集"));
    autoAcquisitionForm->addRow(autoAcquisitionEnableCheck);

    autoAcquisitionLatitudeEdit = new QLineEdit(QStringLiteral("0.000000"));
    autoAcquisitionLatitudeEdit->setToolTip(QStringLiteral("观测地点纬度，北纬为正，范围 -90 到 90。"));
    autoAcquisitionForm->addRow(QStringLiteral("纬度 (deg):"), autoAcquisitionLatitudeEdit);

    autoAcquisitionLongitudeEdit = new QLineEdit(QStringLiteral("0.000000"));
    autoAcquisitionLongitudeEdit->setToolTip(QStringLiteral("观测地点经度，东经为正，范围 -180 到 180。"));
    autoAcquisitionForm->addRow(QStringLiteral("经度 (deg):"), autoAcquisitionLongitudeEdit);

    autoAcquisitionStartOffsetEdit = new QLineEdit(QStringLiteral("30"));
    autoAcquisitionStartOffsetEdit->setToolTip(QStringLiteral("当地日落后等待多少分钟再自动开始采集。"));
    autoAcquisitionForm->addRow(QStringLiteral("日落后启动 (min):"), autoAcquisitionStartOffsetEdit);

    autoAcquisitionStopOffsetEdit = new QLineEdit(QStringLiteral("30"));
    autoAcquisitionStopOffsetEdit->setToolTip(QStringLiteral("当地日出前提前多少分钟自动停止采集。"));
    autoAcquisitionForm->addRow(QStringLiteral("日出前停止 (min):"), autoAcquisitionStopOffsetEdit);

    autoAcquisitionTestOverrideCheck = new QCheckBox(QStringLiteral("启用测试时间"));
    autoAcquisitionTestOverrideCheck->setToolTip(QStringLiteral("启用后使用下面的固定时间窗口，方便实验室测试。"));
    autoAcquisitionForm->addRow(autoAcquisitionTestOverrideCheck);

    autoAcquisitionTestStartEdit = new QLineEdit(QStringLiteral("18:30"));
    autoAcquisitionTestStartEdit->setToolTip(QStringLiteral("测试开始时间，格式 HH:mm。"));
    autoAcquisitionForm->addRow(QStringLiteral("测试开始:"), autoAcquisitionTestStartEdit);

    autoAcquisitionTestStopEdit = new QLineEdit(QStringLiteral("06:00"));
    autoAcquisitionTestStopEdit->setToolTip(QStringLiteral("测试停止时间，格式 HH:mm。"));
    autoAcquisitionForm->addRow(QStringLiteral("测试停止:"), autoAcquisitionTestStopEdit);

    autoAcquisitionNextStartLabel = new QLabel(QStringLiteral("下次开始: 应用后计算"));
    autoAcquisitionNextStartLabel->setWordWrap(true);
    autoAcquisitionForm->addRow(autoAcquisitionNextStartLabel);

    autoAcquisitionNextStopLabel = new QLabel(QStringLiteral("下次停止: 应用后计算"));
    autoAcquisitionNextStopLabel->setWordWrap(true);
    autoAcquisitionForm->addRow(autoAcquisitionNextStopLabel);

    autoAcquisitionLayout->addWidget(autoAcquisitionGroup);
    autoAcquisitionLayout->addStretch();
    addSettingsPage(autoAcquisitionTab, QStringLiteral("自动采集"));

    auto* netTab = new QWidget();
    auto* netLayout = new QVBoxLayout(netTab);
    netLayout->setContentsMargins(12, 12, 12, 12);
    netLayout->setSpacing(14);
    auto* connGroup = new QGroupBox(QStringLiteral("上位机连接"));
    auto* connLayout = new QGridLayout(connGroup);
    connLayout->addWidget(new QLabel(QStringLiteral("IP地址:")), 0, 0);
    netIpEdit = new QLineEdit(QStringLiteral("192.168.10.1"));
    connLayout->addWidget(netIpEdit, 0, 1);
    connLayout->addWidget(new QLabel(QStringLiteral("端口:")), 1, 0);
    netPortEdit = new QLineEdit(QStringLiteral("5000"));
    connLayout->addWidget(netPortEdit, 1, 1);
    netConnectBtn = new QPushButton(QStringLiteral("连接上位机"));
    connLayout->addWidget(netConnectBtn, 2, 0, 1, 2);
    netStatusLabel = new QLabel(QStringLiteral("状态: 未连接"));
    netStatusLabel->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
                                      .arg(uiStatusColor(UiStatusLevel::Muted)));
    connLayout->addWidget(netStatusLabel, 3, 0, 1, 2);
    netLayout->addWidget(connGroup);

    auto* protoGroup = new QGroupBox(QStringLiteral("通信协议"));
    auto* protoLayout = new QVBoxLayout(protoGroup);
    auto* protoInfo = new QLabel(QStringLiteral(
        "协议: TCP 二进制\n"
        "帧头: 0xAA55\n"
        "校验: XOR\n\n"
        "指令:\n"
        "  上位机→设备: 0x01 开始上报 / 0x02 停止 / 0x03 查询状态\n"
        "  设备→上位机: 0x81 测量结果 / 0x82 设备状态 / 0x83 应答"));
    protoInfo->setWordWrap(true);
    protoLayout->addWidget(protoInfo);
    netLayout->addWidget(protoGroup);
    netLayout->addStretch();
    addSettingsPage(netTab, QStringLiteral("网络通信"));

    mainLayout->addWidget(m_tabWidget);

    applyStatusLabel = new QLabel(QStringLiteral("待应用"));
    applyStatusLabel->setStyleSheet(statusLabelStyle(UiStatusLevel::Muted));
    mainLayout->addWidget(applyStatusLabel);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply);
    if (auto* okBtn = buttonBox->button(QDialogButtonBox::Ok)) {
        okBtn->setProperty("role", "primary");
    }
    if (auto* cancelBtn = buttonBox->button(QDialogButtonBox::Cancel)) {
        cancelBtn->setProperty("role", "secondary");
    }
    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        if (applySettings()) {
            accept();
        }
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    if (auto* applyBtn = buttonBox->button(QDialogButtonBox::Apply)) {
        applyBtn->setProperty("role", "primary");
        connect(applyBtn, &QPushButton::clicked, this, [this]() {
            if (applySettings()) {
                updateApplyStatus(QStringLiteral("设置已应用"), UiStatusLevel::Success);
            }
        });
    }
    mainLayout->addWidget(buttonBox);

    connect(pulseApplyFreqBtn, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const double value = pulseFreqEdit->text().toDouble(&ok);
        if (!ok || value <= 0.0) {
            QMessageBox::warning(this, QStringLiteral("触发设置"), QStringLiteral("输出频率必须大于 0。"));
            updateApplyStatus(QStringLiteral("输出频率未提交"), UiStatusLevel::Error);
            return;
        }
        m_committedPulseFrequencyHz = value;
        if (applyCommittedPulseSettings(true)) {
            updateApplyStatus(QStringLiteral("输出频率已设为当前值"), UiStatusLevel::Success);
        }
    });

    connect(pulseApplyCountBtn, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const quint32 value = pulseCountEdit->text().toUInt(&ok);
        if (!ok || value == 0U) {
            QMessageBox::warning(this, QStringLiteral("触发设置"), QStringLiteral("脉冲个数必须大于 0。"));
            updateApplyStatus(QStringLiteral("脉冲个数未提交"), UiStatusLevel::Error);
            return;
        }
        m_committedPulseCount = value;
        if (applyCommittedPulseSettings(true)) {
            updateApplyStatus(QStringLiteral("脉冲个数已设为当前值"), UiStatusLevel::Success);
        }
    });

    connect(pulseApplyDutyBtn, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const double value = pulseDutyEdit->text().toDouble(&ok);
        if (!ok || value < 0.0 || value > 100.0) {
            QMessageBox::warning(this, QStringLiteral("触发设置"), QStringLiteral("占空比必须在 0 到 100 之间。"));
            updateApplyStatus(QStringLiteral("占空比未提交"), UiStatusLevel::Error);
            return;
        }
        m_committedPulseDutyPercent = value;
        if (applyCommittedPulseSettings(true)) {
            updateApplyStatus(QStringLiteral("占空比已设为当前值"), UiStatusLevel::Success);
        }
    });

    connect(pulseApplySourceBtn, &QPushButton::clicked, this, [this]() {
        m_committedPulseRemoteControl = pulseSourceRemote->isChecked();
        if (applyCommittedPulseSettings(true)) {
            updateApplyStatus(QStringLiteral("来源控制已设为当前值"), UiStatusLevel::Success);
        }
    });

    connect(pulseStartBtn, &QPushButton::clicked, this, [this]() {
        if (!pulseEnableCheck || !pulseEnableCheck->isChecked()) {
            QMessageBox::warning(this,
                                 QStringLiteral("触发设置"),
                                 QStringLiteral("请先勾选“启用触发输出”，再启动脉冲。"));
            updateApplyStatus(QStringLiteral("输出脉冲失败：未启用触发输出"), UiStatusLevel::Error);
            return;
        }

        if (!pulsePortEdit || pulsePortEdit->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("触发设置"), QStringLiteral("启动脉冲前请填写端口号。"));
            updateApplyStatus(QStringLiteral("输出脉冲失败：串口为空"), UiStatusLevel::Error);
            return;
        }

        bool ok = false;
        const int pulseBaudRate = pulseBaudCombo->currentText().toInt(&ok);
        if (!ok || pulseBaudRate <= 0) {
            QMessageBox::warning(this, QStringLiteral("触发设置"), QStringLiteral("波特率无效。"));
            updateApplyStatus(QStringLiteral("输出脉冲失败：波特率无效"), UiStatusLevel::Error);
            return;
        }

        const int pulseTerminalId = pulseTerminalEdit->text().toInt(&ok);
        if (!ok || pulseTerminalId < 1 || pulseTerminalId > 255) {
            QMessageBox::warning(this, QStringLiteral("触发设置"), QStringLiteral("终端号必须在 1 到 255 之间。"));
            updateApplyStatus(QStringLiteral("输出脉冲失败：终端号无效"), UiStatusLevel::Error);
            return;
        }

        m_committedPulseFrequencyHz = pulseFreqEdit->text().toDouble(&ok);
        if (!ok || m_committedPulseFrequencyHz <= 0.0) {
            QMessageBox::warning(this, QStringLiteral("触发设置"), QStringLiteral("输出频率必须大于 0。"));
            updateApplyStatus(QStringLiteral("输出脉冲失败：频率无效"), UiStatusLevel::Error);
            return;
        }

        m_committedPulseCount = pulseCountEdit->text().toUInt(&ok);
        if (!ok || m_committedPulseCount == 0U) {
            QMessageBox::warning(this, QStringLiteral("触发设置"), QStringLiteral("脉冲个数必须大于 0。"));
            updateApplyStatus(QStringLiteral("输出脉冲失败：脉冲个数无效"), UiStatusLevel::Error);
            return;
        }

        m_committedPulseDutyPercent = pulseDutyEdit->text().toDouble(&ok);
        if (!ok || m_committedPulseDutyPercent < 0.0 || m_committedPulseDutyPercent > 100.0) {
            QMessageBox::warning(this, QStringLiteral("触发设置"), QStringLiteral("占空比必须在 0 到 100 之间。"));
            updateApplyStatus(QStringLiteral("输出脉冲失败：占空比无效"), UiStatusLevel::Error);
            return;
        }
        m_committedPulseRemoteControl = pulseSourceRemote->isChecked();

        if (!onStartPulseOutput) {
            updateApplyStatus(QStringLiteral("当前版本未接入脉冲板启动控制"), UiStatusLevel::Error);
            return;
        }

        QString errorMessage;
        if (!onStartPulseOutput(pulsePortEdit->text().trimmed(),
                                pulseBaudRate,
                                pulseTerminalId,
                                m_committedPulseFrequencyHz,
                                m_committedPulseCount,
                                m_committedPulseDutyPercent,
                                m_committedPulseRemoteControl,
                                &errorMessage)) {
            updateApplyStatus(errorMessage.isEmpty() ? QStringLiteral("输出脉冲失败") : errorMessage,
                              UiStatusLevel::Error);
            return;
        }

        updateApplyStatus(QStringLiteral("脉冲输出已启动"), UiStatusLevel::Success);
    });

    connect(pulseStopBtn, &QPushButton::clicked, this, [this]() {
        if (!onStopPulseOutput) {
            updateApplyStatus(QStringLiteral("当前版本未接入脉冲板停止控制"), UiStatusLevel::Error);
            return;
        }

        QString errorMessage;
        if (!onStopPulseOutput(&errorMessage)) {
            updateApplyStatus(errorMessage.isEmpty() ? QStringLiteral("关闭脉冲失败") : errorMessage,
                              UiStatusLevel::Error);
            return;
        }

        updateApplyStatus(QStringLiteral("脉冲输出已关闭"), UiStatusLevel::Warning);
    });

    connect(netConnectBtn, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const quint16 port = netPortEdit->text().toUShort(&ok);
        if (!ok || port == 0) {
            QMessageBox::warning(this, QStringLiteral("网络设置"), QStringLiteral("端口必须在 1 到 65535 之间。"));
            updateApplyStatus(QStringLiteral("网络连接失败：端口无效"), UiStatusLevel::Error);
            return;
        }
        const QString ip = netIpEdit->text().trimmed();
        if (ip.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("网络设置"), QStringLiteral("IP地址不能为空。"));
            updateApplyStatus(QStringLiteral("网络连接失败：IP地址为空"), UiStatusLevel::Error);
            return;
        }
        if (onConnectNetwork) {
            onConnectNetwork(ip, port);
            updateApplyStatus(QStringLiteral("正在按当前网络参数连接上位机"), UiStatusLevel::Warning);
        }
    });
}

void SettingsDialog::addSettingsPage(QWidget* page, const QString& title)
{
    if (!m_tabWidget || !page) {
        return;
    }

    auto* scrollArea = new QScrollArea(m_tabWidget);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidget(page);
    m_tabWidget->addTab(scrollArea, title);
}

void SettingsDialog::setPulseGeneratorState(bool enabled,
                                            const QString& portName,
                                            int baudRate,
                                            int terminalId,
                                            double frequencyHz,
                                            quint32 pulseCount,
                                            double dutyPercent,
                                            bool remoteControl)
{
    if (pulseEnableCheck) {
        pulseEnableCheck->setChecked(enabled);
    }
    if (pulsePortEdit) {
        pulsePortEdit->setText(portName);
    }
    if (pulseBaudCombo) {
        pulseBaudCombo->setCurrentText(QString::number(baudRate));
    }
    if (pulseTerminalEdit) {
        pulseTerminalEdit->setText(QString::number(terminalId));
    }
    if (pulseFreqEdit) {
        pulseFreqEdit->setText(QString::number(frequencyHz, 'f', 1));
    }
    if (pulseCountEdit) {
        pulseCountEdit->setText(QString::number(pulseCount));
    }
    if (pulseDutyEdit) {
        pulseDutyEdit->setText(QString::number(dutyPercent, 'f', 0));
    }
    if (pulseSourceRemote && pulseSourceLocal) {
        pulseSourceRemote->setChecked(remoteControl);
        pulseSourceLocal->setChecked(!remoteControl);
    }

    m_committedPulseFrequencyHz = frequencyHz;
    m_committedPulseCount = pulseCount;
    m_committedPulseDutyPercent = dutyPercent;
    m_committedPulseRemoteControl = remoteControl;
}

void SettingsDialog::updateApplyStatus(const QString& text, const QString& color)
{
    if (!applyStatusLabel) {
        return;
    }
    applyStatusLabel->setText(text);
    applyStatusLabel->setStyleSheet(statusLabelStyle(color));
}

void SettingsDialog::updateApplyStatus(const QString& text, UiStatusLevel level)
{
    updateApplyStatus(text, uiStatusColor(level));
}

bool SettingsDialog::applyCommittedPulseSettings(bool requireEnabledPort)
{
    if (!onApplyPulseGenerator || !pulseEnableCheck || !pulsePortEdit || !pulseBaudCombo || !pulseTerminalEdit) {
        return true;
    }

    bool ok = false;
    const int pulseBaudRate = pulseBaudCombo->currentText().toInt(&ok);
    if (!ok || pulseBaudRate <= 0) {
        updateApplyStatus(QStringLiteral("触发设置中的波特率无效"), UiStatusLevel::Error);
        return false;
    }
    const int pulseTerminalId = pulseTerminalEdit->text().toInt(&ok);
    if (!ok || pulseTerminalId < 1 || pulseTerminalId > 255) {
        updateApplyStatus(QStringLiteral("终端号必须在 1 到 255 之间"), UiStatusLevel::Error);
        return false;
    }
    if (requireEnabledPort && pulseEnableCheck->isChecked() && pulsePortEdit->text().trimmed().isEmpty()) {
        updateApplyStatus(QStringLiteral("启用脉冲板时，串口不能为空"), UiStatusLevel::Error);
        return false;
    }

    QString errorMessage;
    if (!onApplyPulseGenerator(pulseEnableCheck->isChecked(),
                               pulsePortEdit->text().trimmed(),
                               pulseBaudRate,
                               pulseTerminalId,
                               m_committedPulseFrequencyHz,
                               m_committedPulseCount,
                               m_committedPulseDutyPercent,
                               m_committedPulseRemoteControl,
                               &errorMessage)) {
        updateApplyStatus(errorMessage.isEmpty() ? QStringLiteral("触发设置下发失败") : errorMessage,
                          UiStatusLevel::Error);
        return false;
    }
    if (!errorMessage.isEmpty()) {
        updateApplyStatus(errorMessage, UiStatusLevel::Warning);
    }
    return true;
}

bool SettingsDialog::applySettings()
{
    auto showInvalid = [this](const QString& message) {
        updateApplyStatus(message, UiStatusLevel::Error);
        QMessageBox::warning(this, QStringLiteral("参数错误"), message);
    };

    bool ok = false;
    const double exposure = exposureEdit->text().toDouble(&ok);
    if (!ok || exposure <= 0.0) {
        showInvalid(QStringLiteral("曝光时间必须大于 0。"));
        return false;
    }

    const double gain = gainEdit->text().toDouble(&ok);
    if (!ok || gain < 0.0) {
        showInvalid(QStringLiteral("增益必须大于或等于 0。"));
        return false;
    }

    const double continuousFrameRate =
        continuousFrameRateEdit ? continuousFrameRateEdit->text().toDouble(&ok) : 200.0;
    if (!ok || continuousFrameRate < 0.1 || continuousFrameRate > 1000.0) {
        showInvalid(QStringLiteral("连续采集帧率必须在 0.1 到 1000 fps 之间。"));
        return false;
    }

    AutoExposureConfig autoExposureConfig;
    autoExposureConfig.enabled = autoExposureCheck && autoExposureCheck->isChecked();
    autoExposureConfig.trendConflictEnabled =
        autoExpTrendConflictCheck && autoExpTrendConflictCheck->isChecked();

    auto readDoubleField = [&](QLineEdit* edit, const QString& name, double* value) {
        bool fieldOk = false;
        const double parsed = edit ? edit->text().toDouble(&fieldOk) : 0.0;
        if (!fieldOk || !std::isfinite(parsed)) {
            showInvalid(QStringLiteral("%1 必须是有效数字。").arg(name));
            return false;
        }
        *value = parsed;
        return true;
    };
    auto readIntField = [&](QLineEdit* edit, const QString& name, int* value) {
        bool fieldOk = false;
        const int parsed = edit ? edit->text().toInt(&fieldOk) : 0;
        if (!fieldOk) {
            showInvalid(QStringLiteral("%1 必须是有效整数。").arg(name));
            return false;
        }
        *value = parsed;
        return true;
    };

    if (!readDoubleField(autoExpTargetPeakLowEdit, QStringLiteral("过暗触发阈值"), &autoExposureConfig.targetPeakLowDn) ||
        !readDoubleField(autoExpTargetPeakHighEdit, QStringLiteral("过曝触发阈值"), &autoExposureConfig.targetPeakHighDn) ||
        !readDoubleField(autoExpExposureHysteresisEdit, QStringLiteral("曝光滞回"), &autoExposureConfig.exposureHysteresisDn) ||
        !readDoubleField(autoExpHardSaturationEdit, QStringLiteral("硬饱和阈值"), &autoExposureConfig.hardSaturationDn) ||
        !readIntField(autoExpSaturatedPixelCountEdit, QStringLiteral("硬饱和像素数"), &autoExposureConfig.saturatedPixelCount) ||
        !readDoubleField(autoExpDarkSnrWarningEdit, QStringLiteral("偏暗 SNR 报警"), &autoExposureConfig.darkSnrWarning) ||
        !readDoubleField(autoExpDarkSnrCriticalEdit, QStringLiteral("严重偏暗 SNR"), &autoExposureConfig.darkSnrCritical) ||
        !readDoubleField(autoExpMinValidCentroidRatioEdit, QStringLiteral("最小有效质心比例"), &autoExposureConfig.minValidCentroidRatio) ||
        !readDoubleField(autoExpStarLostValidRatioEdit, QStringLiteral("丢星有效质心比例"), &autoExposureConfig.starLostValidRatio) ||
        !readDoubleField(autoExpBrightFrameRatioEdit, QStringLiteral("过亮帧比例阈值"), &autoExposureConfig.brightFrameRatioThreshold) ||
        !readDoubleField(autoExpDarkFrameRatioEdit, QStringLiteral("过暗帧比例阈值"), &autoExposureConfig.darkFrameRatioThreshold) ||
        !readDoubleField(autoExpStableFrameRatioEdit, QStringLiteral("稳定帧比例阈值"), &autoExposureConfig.stableFrameRatioThreshold) ||
        !readDoubleField(autoExpHardSaturationFrameRatioEdit, QStringLiteral("硬饱和帧比例阈值"), &autoExposureConfig.hardSaturationFrameRatioThreshold) ||
        !readIntField(autoExpSampleWindowSecEdit, QStringLiteral("统计窗口"), &autoExposureConfig.sampleWindowSec) ||
        !readIntField(autoExpSampleIntervalMsEdit, QStringLiteral("采样间隔"), &autoExposureConfig.autoExposureSampleIntervalMs) ||
        !readIntField(autoExpMinDecisionSampleCountEdit, QStringLiteral("最小决策样本数"), &autoExposureConfig.minDecisionSampleCount) ||
        !readIntField(autoExpTrendConflictPersistenceSecEdit, QStringLiteral("趋势冲突持续时间"), &autoExposureConfig.trendConflictPersistenceSec) ||
        !readDoubleField(autoExpMinEdit, QStringLiteral("最小曝光"), &autoExposureConfig.minExposureUs) ||
        !readDoubleField(autoExpMaxEdit, QStringLiteral("最大曝光"), &autoExposureConfig.maxExposureUs) ||
        !readDoubleField(autoExpMaxChangeUpEdit, QStringLiteral("单次调亮比例上限"), &autoExposureConfig.maxExposureChangeRatioUp) ||
        !readDoubleField(autoExpMaxChangeDownEdit, QStringLiteral("单次调暗比例下限"), &autoExposureConfig.maxExposureChangeRatioDown) ||
        !readDoubleField(autoExpCameraAgreementRatioEdit, QStringLiteral("双相机峰值差异上限"), &autoExposureConfig.cameraAgreementRatio) ||
        !readIntField(autoExpPeakSupportRadiusEdit, QStringLiteral("峰值支持半径"), &autoExposureConfig.peakSupportRadiusPx) ||
        !readDoubleField(autoExpPeakSupportFractionEdit, QStringLiteral("峰值支持比例"), &autoExposureConfig.peakSupportFraction) ||
        !readIntField(autoExpMinPeakSupportPixelsEdit, QStringLiteral("最小支持像素数"), &autoExposureConfig.minPeakSupportPixelCount) ||
        !readDoubleField(autoExpMinNeighborPeakRatioEdit, QStringLiteral("邻域峰值比例下限"), &autoExposureConfig.minNeighborPeakRatio) ||
        !readIntField(autoExpMaxPeakCandidateCountEdit, QStringLiteral("最大峰值候选数"), &autoExposureConfig.maxPeakCandidateCount) ||
        !readDoubleField(autoExpSupportedPeakPercentileEdit, QStringLiteral("支持区域峰值分位"), &autoExposureConfig.supportedPeakPercentile) ||
        !readIntField(autoExpExposureSettleMsEdit, QStringLiteral("曝光稳定等待"), &autoExposureConfig.exposureSettleMs) ||
        !readDoubleField(autoExpMinExposureDeltaEdit, QStringLiteral("最小曝光变化"), &autoExposureConfig.minExposureDeltaUs) ||
        !readDoubleField(autoExpMinExposureChangeRatioEdit, QStringLiteral("最小曝光变化比例"), &autoExposureConfig.minExposureChangeRatio)) {
        return false;
    }

    if (!(0.0 <= autoExposureConfig.targetPeakLowDn &&
          autoExposureConfig.targetPeakLowDn < autoExposureConfig.targetPeakHighDn &&
          autoExposureConfig.targetPeakHighDn <= autoExposureConfig.hardSaturationDn &&
          autoExposureConfig.hardSaturationDn <= 4095.0)) {
        showInvalid(QStringLiteral("自动曝光 DN 阈值必须满足 0 <= 过暗触发阈值 < 过曝触发阈值 <= 硬饱和阈值 <= 4095。"));
        return false;
    }
    if (autoExposureConfig.exposureHysteresisDn < 0.0) {
        showInvalid(QStringLiteral("曝光滞回必须大于或等于 0。"));
        return false;
    }
    if (!(autoExposureConfig.targetPeakLowDn + autoExposureConfig.exposureHysteresisDn <
          autoExposureConfig.targetPeakHighDn - autoExposureConfig.exposureHysteresisDn)) {
        showInvalid(QStringLiteral("自动曝光滞回过大，必须满足 过暗触发阈值 + 曝光滞回 < 过曝触发阈值 - 曝光滞回。"));
        return false;
    }
    if (autoExposureConfig.saturatedPixelCount < 1) {
        showInvalid(QStringLiteral("硬饱和像素数必须大于或等于 1。"));
        return false;
    }
    if (!(autoExposureConfig.darkSnrCritical > 0.0 &&
          autoExposureConfig.darkSnrWarning > autoExposureConfig.darkSnrCritical)) {
        showInvalid(QStringLiteral("偏暗 SNR 报警必须大于严重偏暗 SNR，且严重偏暗 SNR 必须大于 0。"));
        return false;
    }
    if (!(0.0 <= autoExposureConfig.starLostValidRatio &&
          autoExposureConfig.starLostValidRatio <= autoExposureConfig.minValidCentroidRatio &&
          autoExposureConfig.minValidCentroidRatio <= 1.0)) {
        showInvalid(QStringLiteral("有效质心比例必须满足 0 <= 丢星比例 <= 最小有效比例 <= 1。"));
        return false;
    }
    if (autoExposureConfig.brightFrameRatioThreshold < 0.0 ||
        autoExposureConfig.brightFrameRatioThreshold > 1.0 ||
        autoExposureConfig.darkFrameRatioThreshold < 0.0 ||
        autoExposureConfig.darkFrameRatioThreshold > 1.0 ||
        autoExposureConfig.stableFrameRatioThreshold < 0.0 ||
        autoExposureConfig.stableFrameRatioThreshold > 1.0 ||
        autoExposureConfig.hardSaturationFrameRatioThreshold < 0.0 ||
        autoExposureConfig.hardSaturationFrameRatioThreshold > 1.0) {
        showInvalid(QStringLiteral("自动曝光帧比例阈值必须在 0 到 1 之间。"));
        return false;
    }
    if (autoExposureConfig.sampleWindowSec < 1 ||
        autoExposureConfig.autoExposureSampleIntervalMs < 200 ||
        autoExposureConfig.autoExposureSampleIntervalMs > 10000 ||
        autoExposureConfig.minDecisionSampleCount < 1 ||
        autoExposureConfig.trendConflictPersistenceSec < 1) {
        showInvalid(QStringLiteral("自动曝光时间窗口、采样间隔和持续时间不满足约束。"));
        return false;
    }
    if ((autoExposureConfig.sampleWindowSec * 1000) / autoExposureConfig.autoExposureSampleIntervalMs <
        autoExposureConfig.minDecisionSampleCount) {
        showInvalid(QStringLiteral("统计窗口内可获得的样本数必须大于或等于最小决策样本数。"));
        return false;
    }
    if (autoExposureConfig.minExposureUs <= 0.0 ||
        autoExposureConfig.maxExposureUs < autoExposureConfig.minExposureUs) {
        showInvalid(QStringLiteral("自动曝光范围必须满足最小曝光 > 0 且最大曝光 >= 最小曝光。"));
        return false;
    }
    if (autoExposureConfig.maxExposureChangeRatioUp < 1.0 ||
        autoExposureConfig.maxExposureChangeRatioDown <= 0.0 ||
        autoExposureConfig.maxExposureChangeRatioDown > 1.0 ||
        autoExposureConfig.cameraAgreementRatio <= 0.0 ||
        autoExposureConfig.peakSupportRadiusPx < 1 ||
        autoExposureConfig.peakSupportFraction <= 0.0 ||
        autoExposureConfig.peakSupportFraction > 1.0 ||
        autoExposureConfig.minPeakSupportPixelCount < 1 ||
        autoExposureConfig.minNeighborPeakRatio < 0.0 ||
        autoExposureConfig.minNeighborPeakRatio > 1.0 ||
        autoExposureConfig.maxPeakCandidateCount < 1 ||
        autoExposureConfig.supportedPeakPercentile < 0.0 ||
        autoExposureConfig.supportedPeakPercentile > 1.0 ||
        autoExposureConfig.exposureSettleMs < 0 ||
        autoExposureConfig.minExposureDeltaUs < 0.0 ||
        autoExposureConfig.minExposureChangeRatio < 0.0) {
        showInvalid(QStringLiteral("自动曝光调整限制参数不满足约束。"));
        return false;
    }
    autoExposureConfig.lowThreshold = autoExposureConfig.targetPeakLowDn;
    autoExposureConfig.highThreshold = autoExposureConfig.targetPeakHighDn;
    autoExposureConfig.darkRatio = autoExposureConfig.maxExposureChangeRatioUp;
    autoExposureConfig.brightRatio = autoExposureConfig.maxExposureChangeRatioDown;

    const int kernelSize = procKernelSize->text().toInt(&ok);
    if (!ok || kernelSize <= 0) {
        showInvalid(QStringLiteral("背景核大小必须为正整数。"));
        return false;
    }

    const double sigma = procSigma->text().toDouble(&ok);
    if (!ok || sigma < 0.0) {
        showInvalid(QStringLiteral("背景标准差倍数必须不小于 0。"));
        return false;
    }

    const int centroidMode = centroidModeCombo ? centroidModeCombo->currentData().toInt() : 0;
    const int peakKernelMethod = peakKernelMethodCombo ? peakKernelMethodCombo->currentData().toInt() : 1;
    if (centroidMode < 0 || centroidMode > 1 || peakKernelMethod < 0 || peakKernelMethod > 1) {
        showInvalid(QStringLiteral("质心模式参数无效，请重新选择。"));
        return false;
    }

    const int peakKernelRadius = peakKernelRadiusEdit ? peakKernelRadiusEdit->text().toInt(&ok) : 3;
    if (!ok || peakKernelRadius < 1 || peakKernelRadius > 20) {
        showInvalid(QStringLiteral("小核半径必须在 1 到 20 像素之间。"));
        return false;
    }

    const double strongHotPixelExcess =
        strongHotPixelExcessEdit ? strongHotPixelExcessEdit->text().toDouble(&ok) : 100.0;
    if (!ok || strongHotPixelExcess <= 0.0 || strongHotPixelExcess > 4095.0) {
        showInvalid(QStringLiteral("强固定热像素阈值必须大于 0 且不超过 4095 DN。"));
        return false;
    }

    const double roiRecenterThreshold = roiRecenterThresholdEdit->text().toDouble(&ok);
    if (!ok || roiRecenterThreshold < 1.0 || roiRecenterThreshold > 31.0) {
        showInvalid(QStringLiteral("ROI 距边缘重居中阈值必须在 1 到 31 px 之间。"));
        return false;
    }

    const int roiRecenterRequiredFrames = roiRecenterRequiredFramesEdit->text().toInt(&ok);
    if (!ok || roiRecenterRequiredFrames < 1 || roiRecenterRequiredFrames > 100) {
        showInvalid(QStringLiteral("ROI 重居中连续帧数必须在 1 到 100 之间。"));
        return false;
    }

    const int roiRecenterCooldownMs = roiRecenterCooldownMsEdit->text().toInt(&ok);
    if (!ok || roiRecenterCooldownMs < 0 || roiRecenterCooldownMs > 60000) {
        showInvalid(QStringLiteral("ROI 重居中冷却时间必须在 0 到 60000 ms 之间。"));
        return false;
    }

    const double roiRecenterMinimumShift = roiRecenterMinimumShiftEdit->text().toDouble(&ok);
    if (!ok || roiRecenterMinimumShift < 0.0 || roiRecenterMinimumShift > 31.0) {
        showInvalid(QStringLiteral("ROI 重居中最小位移必须在 0 到 31 px 之间。"));
        return false;
    }

    const double starThresholdAbsolute = starThresholdAbsoluteEdit->text().toDouble(&ok);
    if (!ok ||
        !(qFuzzyCompare(starThresholdAbsolute, -1.0) ||
          (starThresholdAbsolute >= 0.0 && starThresholdAbsolute <= 4095.0))) {
        showInvalid(QStringLiteral("全画幅找星绝对阈值必须为 -1 或 0 到 4095 之间的数值。"));
        return false;
    }

    const double starSigmaThreshold = starSigmaThresholdEdit->text().toDouble(&ok);
    if (!ok || starSigmaThreshold < 0.0 || starSigmaThreshold > 20.0) {
        showInvalid(QStringLiteral("全画幅找星背景倍数必须在 0 到 20 之间。"));
        return false;
    }

    const double starPeakFraction = starPeakFractionEdit->text().toDouble(&ok);
    if (!ok || starPeakFraction < 0.01 || starPeakFraction > 0.95) {
        showInvalid(QStringLiteral("全画幅找星峰值比例必须在 0.01 到 0.95 之间。"));
        return false;
    }

    const double starMinimumIntensity = starMinimumIntensityEdit->text().toDouble(&ok);
    if (!ok || starMinimumIntensity < 0.0 || starMinimumIntensity > 4095.0) {
        showInvalid(QStringLiteral("全画幅找星最低亮度必须在 0 到 4095 之间。"));
        return false;
    }

    const int starMinArea = starMinAreaEdit->text().toInt(&ok);
    if (!ok || starMinArea < 1 || starMinArea > 100000) {
        showInvalid(QStringLiteral("全画幅找星最小面积必须为正整数。"));
        return false;
    }

    const int starMaxArea = starMaxAreaEdit->text().toInt(&ok);
    if (!ok || starMaxArea < starMinArea || starMaxArea > 100000) {
        showInvalid(QStringLiteral("全画幅找星最大面积必须大于或等于最小面积。"));
        return false;
    }

    const bool hotPixelEnabled = hotPixelEnableCheck && hotPixelEnableCheck->isChecked();
    const QString hotCam0Mask = hotPixelCam0MaskEdit ? hotPixelCam0MaskEdit->text().trimmed() : QString();
    const QString hotCam0Excess = hotPixelCam0ExcessEdit ? hotPixelCam0ExcessEdit->text().trimmed() : QString();
    const QString hotCam1Mask = hotPixelCam1MaskEdit ? hotPixelCam1MaskEdit->text().trimmed() : QString();
    const QString hotCam1Excess = hotPixelCam1ExcessEdit ? hotPixelCam1ExcessEdit->text().trimmed() : QString();
    const int hotTemplateWidth =
        hotPixelTemplateWidthEdit ? hotPixelTemplateWidthEdit->text().toInt(&ok) : 0;
    if (hotPixelEnabled && (!ok || hotTemplateWidth <= 0)) {
        showInvalid(QStringLiteral("启用热像素修正时，模板宽度必须为正整数。"));
        return false;
    }
    const int hotTemplateHeight =
        hotPixelTemplateHeightEdit ? hotPixelTemplateHeightEdit->text().toInt(&ok) : 0;
    if (hotPixelEnabled && (!ok || hotTemplateHeight <= 0)) {
        showInvalid(QStringLiteral("启用热像素修正时，模板高度必须为正整数。"));
        return false;
    }
    if (hotPixelEnabled) {
        const QStringList hotPixelFiles = {hotCam0Mask, hotCam0Excess, hotCam1Mask, hotCam1Excess};
        const QStringList hotPixelNames = {
            QStringLiteral("相机1 Mask"),
            QStringLiteral("相机1 Excess"),
            QStringLiteral("相机2 Mask"),
            QStringLiteral("相机2 Excess")
        };
        for (int i = 0; i < hotPixelFiles.size(); ++i) {
            if (hotPixelFiles[i].isEmpty()) {
                showInvalid(QStringLiteral("%1 文件不能为空。").arg(hotPixelNames[i]));
                return false;
            }
            const QString resolvedHotPixelFile = PathUtils::resolvePathFromAppDir(hotPixelFiles[i]);
            if (!QFileInfo::exists(resolvedHotPixelFile)) {
                showInvalid(QStringLiteral("%1 文件不存在: %2").arg(hotPixelNames[i], resolvedHotPixelFile));
                return false;
            }
        }
    }

    const double diameter = opticsD->text().toDouble(&ok);
    if (!ok || diameter <= 0.0) {
        showInvalid(QStringLiteral("口径 D 必须大于 0。"));
        return false;
    }

    const double baseline = opticsBaseline->text().toDouble(&ok);
    if (!ok || baseline <= diameter) {
        showInvalid(QStringLiteral("中心间距 d 必须大于子孔径直径 D。"));
        return false;
    }

    const double baselineAngle = opticsBaselineAngle->text().toDouble(&ok);
    if (!ok || !std::isfinite(baselineAngle) || baselineAngle < -360.0 || baselineAngle > 360.0) {
        showInvalid(QStringLiteral("基线方向角必须在 -360 到 360 度之间。"));
        return false;
    }

    const double focal = opticsF->text().toDouble(&ok);
    if (!ok || focal <= 0.0) {
        showInvalid(QStringLiteral("焦距 f 必须大于 0。"));
        return false;
    }

    const double zenithAngle = opticsZenith->text().toDouble(&ok);
    if (!ok || zenithAngle < 0.0 || zenithAngle >= 90.0) {
        showInvalid(QStringLiteral("天顶角 Z 必须在 0 到 90 度之间。"));
        return false;
    }

    const double wavelength = detectorWavelength->text().toDouble(&ok);
    if (!ok || wavelength <= 0.0) {
        showInvalid(QStringLiteral("对比波长必须大于 0。"));
        return false;
    }

    const double pixelSize = detectorPixelSize->text().toDouble(&ok);
    if (!ok || pixelSize <= 0.0) {
        showInvalid(QStringLiteral("像素尺寸必须大于 0。"));
        return false;
    }

    const double alignmentFocalLength =
        alignmentFocalLengthEdit ? alignmentFocalLengthEdit->text().toDouble(&ok) : 269.0;
    if (!ok || alignmentFocalLength <= 0.0) {
        showInvalid(QStringLiteral("对准焦距必须大于 0。"));
        return false;
    }

    const double alignmentPixelSize =
        alignmentPixelSizeEdit ? alignmentPixelSizeEdit->text().toDouble(&ok) : 2.5;
    if (!ok || alignmentPixelSize <= 0.0) {
        showInvalid(QStringLiteral("对准像元尺寸必须大于 0。"));
        return false;
    }

    const double alignmentPolarDistance =
        alignmentPolarDistanceEdit ? alignmentPolarDistanceEdit->text().toDouble(&ok)
                                   : kAlignmentDefaultPolarisPolarDistanceArcmin;
    if (!ok || alignmentPolarDistance <= 0.0) {
        showInvalid(QStringLiteral("北极星极距必须大于 0。"));
        return false;
    }

    const double alignmentRadiusAdjust =
        alignmentRadiusAdjustEdit ? alignmentRadiusAdjustEdit->text().toDouble(&ok) : 0.0;
    if (!ok) {
        showInvalid(QStringLiteral("轨道半径微调必须是有效数字。"));
        return false;
    }

    const double alignmentPreviewRate =
        alignmentPreviewRateEdit ? alignmentPreviewRateEdit->text().toDouble(&ok) : 1.0;
    if (!ok || alignmentPreviewRate <= 0.0 || alignmentPreviewRate > 10.0) {
        showInvalid(QStringLiteral("对准预览频率必须在 0 到 10 Hz 之间。"));
        return false;
    }

    const int alignmentMaxDetectedStars =
        alignmentMaxDetectedStarsEdit ? alignmentMaxDetectedStarsEdit->text().toInt(&ok) : 20;
    if (!ok || alignmentMaxDetectedStars < 6 || alignmentMaxDetectedStars > 40) {
        showInvalid(QStringLiteral("最大参与匹配星数必须在 6 到 40 之间。"));
        return false;
    }

    const int alignmentMinMatchedStars =
        alignmentMinMatchedStarsEdit ? alignmentMinMatchedStarsEdit->text().toInt(&ok) : 5;
    if (!ok || alignmentMinMatchedStars < 4 || alignmentMinMatchedStars > alignmentMaxDetectedStars) {
        showInvalid(QStringLiteral("最少匹配星数必须在 4 到最大参与匹配星数之间。"));
        return false;
    }

    const double alignmentMaxRms =
        alignmentMaxRmsEdit ? alignmentMaxRmsEdit->text().toDouble(&ok) : 3.0;
    if (!ok || alignmentMaxRms < 0.5 || alignmentMaxRms > 10.0) {
        showInvalid(QStringLiteral("最大匹配 RMS 必须在 0.5 到 10 px 之间。"));
        return false;
    }

    const double alignmentRetryIntervalSeconds =
        alignmentRetryIntervalEdit ? alignmentRetryIntervalEdit->text().toDouble(&ok) : 3.0;
    if (!ok || alignmentRetryIntervalSeconds < 1.0 || alignmentRetryIntervalSeconds > 30.0) {
        showInvalid(QStringLiteral("自动重试间隔必须在 1 到 30 秒之间。"));
        return false;
    }

    const double alignmentMinSpatialSpread =
        alignmentMinSpatialSpreadEdit ? alignmentMinSpatialSpreadEdit->text().toDouble(&ok) : 50.0;
    if (!ok || alignmentMinSpatialSpread < 0.0 || alignmentMinSpatialSpread > 1000.0) {
        showInvalid(QStringLiteral("最小空间跨度必须在 0 到 1000 px 之间。"));
        return false;
    }

    const double alignmentMinPolarisSnr =
        alignmentMinPolarisSnrEdit ? alignmentMinPolarisSnrEdit->text().toDouble(&ok) : 5.0;
    if (!ok || alignmentMinPolarisSnr < 0.0 || alignmentMinPolarisSnr > 100.0) {
        showInvalid(QStringLiteral("北极星最小 SNR 必须在 0 到 100 之间。"));
        return false;
    }

    const int interval = saveIntervalEdit->text().toInt(&ok);
    if (!ok || interval <= 0) {
        showInvalid(QStringLiteral("保存间隔必须为正整数。"));
        return false;
    }

    const quint16 port = netPortEdit->text().toUShort(&ok);
    if (!ok || port == 0) {
        showInvalid(QStringLiteral("端口必须在 1 到 65535 之间。"));
        return false;
    }

    if (netIpEdit->text().trimmed().isEmpty()) {
        showInvalid(QStringLiteral("IP地址不能为空。"));
        return false;
    }

    if (storagePathEdit->text().trimmed().isEmpty()) {
        showInvalid(QStringLiteral("存储路径不能为空。"));
        return false;
    }

    EnvironmentSensorConfig environmentSensorConfig;
    environmentSensorConfig.enabled = envSensorEnableCheck && envSensorEnableCheck->isChecked();
    environmentSensorConfig.portName = envSensorPortEdit ? envSensorPortEdit->text().trimmed() : QString();
    if (environmentSensorConfig.enabled && environmentSensorConfig.portName.isEmpty()) {
        showInvalid(QStringLiteral("温湿压传感器串口不能为空。"));
        return false;
    }
    environmentSensorConfig.baudRate = envSensorBaudCombo ? envSensorBaudCombo->currentText().toInt(&ok) : 9600;
    if (!ok || environmentSensorConfig.baudRate <= 0) {
        showInvalid(QStringLiteral("温湿压传感器波特率无效。"));
        return false;
    }
    environmentSensorConfig.deviceAddress =
        envSensorAddressEdit ? envSensorAddressEdit->text().toInt(&ok) : 1;
    if (!ok || environmentSensorConfig.deviceAddress < 1 || environmentSensorConfig.deviceAddress > 255) {
        showInvalid(QStringLiteral("温湿压传感器设备地址必须在 1 到 255 之间。"));
        return false;
    }
    environmentSensorConfig.pollIntervalMs =
        envSensorPollIntervalEdit ? envSensorPollIntervalEdit->text().toInt(&ok) : 1000;
    if (!ok || environmentSensorConfig.pollIntervalMs < 200 || environmentSensorConfig.pollIntervalMs > 60000) {
        showInvalid(QStringLiteral("温湿压传感器轮询间隔必须在 200 到 60000 ms 之间。"));
        return false;
    }

    AutoAcquisitionConfig autoAcquisitionConfig;
    autoAcquisitionConfig.enabled = autoAcquisitionEnableCheck && autoAcquisitionEnableCheck->isChecked();
    autoAcquisitionConfig.testTimeOverrideEnabled =
        autoAcquisitionTestOverrideCheck && autoAcquisitionTestOverrideCheck->isChecked();

    autoAcquisitionConfig.latitudeDeg =
        autoAcquisitionLatitudeEdit ? autoAcquisitionLatitudeEdit->text().toDouble(&ok) : 0.0;
    if (!ok || autoAcquisitionConfig.latitudeDeg < -90.0 || autoAcquisitionConfig.latitudeDeg > 90.0) {
        showInvalid(QStringLiteral("纬度必须在 -90 到 90 之间。"));
        return false;
    }

    autoAcquisitionConfig.longitudeDeg =
        autoAcquisitionLongitudeEdit ? autoAcquisitionLongitudeEdit->text().toDouble(&ok) : 0.0;
    if (!ok || autoAcquisitionConfig.longitudeDeg < -180.0 || autoAcquisitionConfig.longitudeDeg > 180.0) {
        showInvalid(QStringLiteral("经度必须在 -180 到 180 之间。"));
        return false;
    }

    autoAcquisitionConfig.startOffsetMinutesAfterSunset =
        autoAcquisitionStartOffsetEdit ? autoAcquisitionStartOffsetEdit->text().toInt(&ok) : 30;
    if (!ok || autoAcquisitionConfig.startOffsetMinutesAfterSunset < 0 ||
        autoAcquisitionConfig.startOffsetMinutesAfterSunset > 240) {
        showInvalid(QStringLiteral("日落后启动偏移必须在 0 到 240 分钟之间。"));
        return false;
    }

    autoAcquisitionConfig.stopOffsetMinutesBeforeSunrise =
        autoAcquisitionStopOffsetEdit ? autoAcquisitionStopOffsetEdit->text().toInt(&ok) : 30;
    if (!ok || autoAcquisitionConfig.stopOffsetMinutesBeforeSunrise < 0 ||
        autoAcquisitionConfig.stopOffsetMinutesBeforeSunrise > 240) {
        showInvalid(QStringLiteral("日出前停止偏移必须在 0 到 240 分钟之间。"));
        return false;
    }

    autoAcquisitionConfig.testStartTime =
        QTime::fromString(autoAcquisitionTestStartEdit->text().trimmed(), QStringLiteral("HH:mm"));
    if (!autoAcquisitionConfig.testStartTime.isValid()) {
        showInvalid(QStringLiteral("测试开始时间格式必须为 HH:mm。"));
        return false;
    }

    autoAcquisitionConfig.testStopTime =
        QTime::fromString(autoAcquisitionTestStopEdit->text().trimmed(), QStringLiteral("HH:mm"));
    if (!autoAcquisitionConfig.testStopTime.isValid()) {
        showInvalid(QStringLiteral("测试停止时间格式必须为 HH:mm。"));
        return false;
    }

    const CameraConfig cameraConfig{
        exposure,
        gain,
        continuousFrameRate
    };
    const TriggerConfig triggerConfig{
        triggerContinuous && triggerContinuous->isChecked() ? 0 : 1
    };
    const ProcessingConfig processingConfig{
        kernelSize,
        sigma,
        centroidMode,
        peakKernelMethod,
        peakKernelRadius,
        strongHotPixelExcess
    };
    const RoiRecenteringConfig roiRecenteringConfig{
        roiRecenterThreshold,
        roiRecenterRequiredFrames,
        roiRecenterCooldownMs,
        roiRecenterMinimumShift
    };
    const StarDetectionConfig starDetectionConfig{
        starThresholdAbsolute,
        starSigmaThreshold,
        starPeakFraction,
        starMinimumIntensity,
        starMinArea,
        starMaxArea
    };
    const HotPixelConfig hotPixelConfig{
        hotPixelEnabled,
        hotCam0Mask,
        hotCam0Excess,
        hotCam1Mask,
        hotCam1Excess,
        hotPixelEnabled ? hotTemplateWidth : 0,
        hotPixelEnabled ? hotTemplateHeight : 0
    };
    const OpticalConfig opticalConfig{
        diameter,
        baseline,
        baselineAngle,
        focal,
        zenithAngle,
        wavelength,
        pixelSize
    };
    const AlignmentConfig alignmentConfig{
        alignmentAutoRadiusCheck ? alignmentAutoRadiusCheck->isChecked() : true,
        alignmentFocalLength,
        alignmentPixelSize,
        alignmentPolarDistance,
        alignmentRadiusAdjust,
        alignmentPreviewRate
    };
    const PolarisSolverSettingsConfig polarisSolverConfig{
        alignmentAutoSolveCheck ? alignmentAutoSolveCheck->isChecked() : true,
        alignmentShowMatchedCatalogStarsCheck
            ? alignmentShowMatchedCatalogStarsCheck->isChecked()
            : true,
        alignmentMaxDetectedStars,
        alignmentMinMatchedStars,
        alignmentMaxRms,
        static_cast<int>(std::lround(alignmentRetryIntervalSeconds * 1000.0)),
        alignmentMinSpatialSpread,
        alignmentMinPolarisSnr,
        alignmentAllowSaturatedPolarisCheck
            ? alignmentAllowSaturatedPolarisCheck->isChecked()
            : false
    };
    const StorageConfig storageConfig{
        storagePathEdit->text().trimmed(),
        interval,
        parameterValidationCheck && parameterValidationCheck->isChecked(),
        syncDiagnosticLogCheck && syncDiagnosticLogCheck->isChecked()
    };
    const NetworkConfig networkConfig{
        netIpEdit->text().trimmed(),
        port
    };
    AppConfig appConfig;
    appConfig.camera = cameraConfig;
    appConfig.autoExposure = autoExposureConfig;
    appConfig.trigger = triggerConfig;
    appConfig.processing = processingConfig;
    appConfig.roiRecentering = roiRecenteringConfig;
    appConfig.starDetection = starDetectionConfig;
    appConfig.hotPixel = hotPixelConfig;
    appConfig.optical = opticalConfig;
    appConfig.alignment = alignmentConfig;
    appConfig.polarisSolver = polarisSolverConfig;
    appConfig.storage = storageConfig;
    appConfig.environmentSensor = environmentSensorConfig;
    appConfig.autoAcquisition = autoAcquisitionConfig;
    appConfig.network = networkConfig;

    ConfigApplicationCallbacks configCallbacks;
    configCallbacks.applyCamera = onApplyCamera;
    configCallbacks.applyAutoExposure = onApplyAutoExposure;
    configCallbacks.applyTriggerMode = onApplyTriggerMode;
    if (!triggerContinuous || !triggerHardware) {
        configCallbacks.applyTriggerMode = nullptr;
    }
    configCallbacks.applyProcessing = onApplyProcessing;
    if (!centroidModeCombo || !peakKernelMethodCombo || !procKernelSize || !procSigma ||
        !peakKernelRadiusEdit || !strongHotPixelExcessEdit) {
        configCallbacks.applyProcessing = nullptr;
    }
    configCallbacks.applyRoiRecentering = onApplyRoiRecentering;
    configCallbacks.applyFullFrameStarDetection = onApplyFullFrameStarDetection;
    configCallbacks.applyHotPixelTemplates = onApplyHotPixelTemplates;
    configCallbacks.applyOptics = onApplyOptics;
    configCallbacks.applyAlignment = onApplyAlignment;
    configCallbacks.applyPolarisSolver = onApplyPolarisSolver;
    configCallbacks.applyStorage = onApplyStorage;
    configCallbacks.applyEnvironmentSensor = onApplyEnvironmentSensor;
    configCallbacks.applyAutoAcquisition = onApplyAutoAcquisition;
    configCallbacks.applyNetwork = onApplyNetwork;

    ConfigApplicationController::applyPreValidationConfig(appConfig, configCallbacks);
    if (pulseEnableCheck->isChecked() && pulsePortEdit->text().trimmed().isEmpty()) {
        showInvalid(QStringLiteral("启用脉冲板时，串口不能为空。"));
        return false;
    }

    const double pulseFrequency = pulseFreqEdit->text().toDouble(&ok);
    if (!ok || pulseFrequency <= 0.0) {
        showInvalid(QStringLiteral("输出频率必须大于 0。"));
        return false;
    }

    const quint32 pulseCount = pulseCountEdit->text().toUInt(&ok);
    if (!ok || pulseCount == 0U) {
        showInvalid(QStringLiteral("脉冲个数必须为正整数。"));
        return false;
    }

    const double pulseDuty = pulseDutyEdit->text().toDouble(&ok);
    if (!ok || pulseDuty <= 0.0 || pulseDuty >= 100.0) {
        showInvalid(QStringLiteral("占空比必须在 0 到 100 之间。"));
        return false;
    }

    const PulseGeneratorConfig pulseGeneratorConfig{
        pulseEnableCheck->isChecked(),
        pulsePortEdit->text().trimmed(),
        pulseBaudCombo ? pulseBaudCombo->currentText().toInt() : 19200,
        pulseTerminalEdit ? pulseTerminalEdit->text().toInt() : 1,
        pulseFrequency,
        pulseCount,
        pulseDuty,
        pulseSourceRemote && pulseSourceRemote->isChecked()
    };
    appConfig.pulseGenerator = pulseGeneratorConfig;

    const AppConfigDraft configDraft{appConfig};
    const ConfigValidationResult validation =
        ConfigValidator::acceptValidatedConfig(configDraft);
    if (!validation.valid) {
        showInvalid(validation.message);
        return false;
    }
    appConfig = validation.config;

    m_committedPulseFrequencyHz = appConfig.pulseGenerator.frequencyHz;
    m_committedPulseCount = appConfig.pulseGenerator.pulseCount;
    m_committedPulseDutyPercent = appConfig.pulseGenerator.dutyPercent;
    m_committedPulseRemoteControl = appConfig.pulseGenerator.remoteControl;

    if (!applyCommittedPulseSettings(true)) {
        const QString pulseMessage =
            (applyStatusLabel && !applyStatusLabel->text().trimmed().isEmpty())
                ? applyStatusLabel->text().trimmed()
                : QStringLiteral("触发设置存在未完成提交或参数无效。");
        QMessageBox::warning(this, QStringLiteral("参数错误"), pulseMessage);
        return false;
    }
    ConfigApplicationController::applyValidatedConfig(appConfig, configCallbacks);
    if (onAfterApply) {
        onAfterApply();
    } else if (applyStatusLabel) {
        applyStatusLabel->setText(QStringLiteral("设置已应用到当前配置"));
        applyStatusLabel->setStyleSheet(statusLabelStyle(UiStatusLevel::Success));
    }
    return true;
}
