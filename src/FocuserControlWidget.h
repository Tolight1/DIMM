#pragma once

#include <QWidget>
#include <QVector>
#include <QString>

#include "AutoFocusSettings.h"
#include "EafFocuserManager.h"

class EafFocuserManager;

class QLabel;
class QPushButton;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QGroupBox;

class FocuserControlWidget : public QWidget {
    Q_OBJECT
public:
    explicit FocuserControlWidget(QWidget* parent = nullptr);

    void setManager(EafFocuserManager* manager);
    void setAutoFocusConfigUi(const AutoFocusConfig& config);

public slots:
    void setMotionAllowed(bool allowed, const QString& reason);
    void setAutoFocusMotionLocked(TelescopeSlot slot, bool locked);
    void setAutoFocusTrackingAvailable(bool available);

signals:
    void autoFocusConfigApplied(AutoFocusConfig config);
    void manualAutoFocusRequested();
    // -1 means the master switch was disabled; 0/1 are camera-specific.
    void autoFocusDisabled(int cameraIndex);

private slots:
    void onRefreshDevices();
    void onDeviceListChanged(QVector<EafDeviceDescriptor> devices);
    void onApplyMapping();
    void onSwapMapping();
    void onOpenDevice();
    void onCloseDevice();
    void onStateChanged(TelescopeSlot slot, EafDeviceState state);
    void onMoveDecrease();
    void onMoveIncrease();
    void onMoveToTarget();
    void onStop();
    void onApplyReverse();
    void onApplyBacklash();
    void onApplyBeep();
    void onApplyLed();
    void onResetPosition();
    void onApplyMaxStep();
    void onTelescopeSelectionChanged(int index);
    void onSdkAvailabilityChanged(bool available, QString detail);
    void onCommandFailed(TelescopeSlot slot, QString command, QString error);
    void onApplyAutoFocus();
    void onManualAutoFocus();
    void onAutoFocusEnableChanged(bool enabled);

private:
    void buildUi();
    void updateDeviceCombo();
    void updateStateDisplay();
    void updateControlStates();
    void updateAutoFocusControlStates();
    AutoFocusConfig autoFocusConfigFromUi() const;
    bool applyAutoFocusConfiguration(bool showError);
    int currentSlotIndex() const;
    EafFocuserManager* manager() const { return m_manager; }

    EafFocuserManager* m_manager = nullptr;

    // SDK & Devices group
    QLabel* m_sdkStatusLabel = nullptr;
    QPushButton* m_refreshBtn = nullptr;
    QComboBox* m_deviceCombo = nullptr;

    // Mapping group
    QComboBox* m_telescopeCombo = nullptr;
    QPushButton* m_applyMappingBtn = nullptr;
    QPushButton* m_swapMappingBtn = nullptr;
    QPushButton* m_openBtn = nullptr;
    QPushButton* m_closeBtn = nullptr;

    // Status group
    QLabel* m_deviceNameLabel = nullptr;
    QLabel* m_deviceIdLabel = nullptr;
    QLabel* m_snLabel = nullptr;
    QLabel* m_typeLabel = nullptr;
    QLabel* m_firmwareLabel = nullptr;
    QLabel* m_positionLabel = nullptr;
    QLabel* m_targetLabel = nullptr;
    QLabel* m_maxStepLabel = nullptr;
    QLabel* m_stepRangeLabel = nullptr;
    QLabel* m_temperatureLabel = nullptr;
    QLabel* m_movingLabel = nullptr;
    QLabel* m_handControlLabel = nullptr;
    QLabel* m_reverseLabel = nullptr;
    QLabel* m_backlashLabel = nullptr;
    QLabel* m_beepLabel = nullptr;
    QLabel* m_ledLabel = nullptr;
    QLabel* m_errorLabel = nullptr;

    // Manual move group
    QSpinBox* m_stepSizeSpin = nullptr;
    QPushButton* m_decreaseBtn = nullptr;
    QPushButton* m_increaseBtn = nullptr;
    QSpinBox* m_targetPositionSpin = nullptr;
    QPushButton* m_moveToTargetBtn = nullptr;
    QPushButton* m_stopBtn = nullptr;

    // Parameters group
    QCheckBox* m_reverseCheck = nullptr;
    QPushButton* m_reverseApplyBtn = nullptr;
    QSpinBox* m_backlashSpin = nullptr;
    QPushButton* m_backlashApplyBtn = nullptr;
    QCheckBox* m_beepCheck = nullptr;
    QPushButton* m_beepApplyBtn = nullptr;
    QCheckBox* m_ledCheck = nullptr;
    QPushButton* m_ledApplyBtn = nullptr;
    QSpinBox* m_resetPositionSpin = nullptr;
    QPushButton* m_resetPositionBtn = nullptr;
    QSpinBox* m_maxStepSpin = nullptr;
    QPushButton* m_maxStepApplyBtn = nullptr;

    // Autofocus configuration. Values default to zero and show "未设置";
    // zero is rejected for every enabled field by AutoFocusController.
    QCheckBox* m_autoFocusMasterCheck = nullptr;
    QCheckBox* m_autoFocusCameraEnabled[2] = {nullptr, nullptr};
    QDoubleSpinBox* m_autoFocusReferenceHfr[2] = {nullptr, nullptr};
    QDoubleSpinBox* m_autoFocusReferenceRms[2] = {nullptr, nullptr};
    QDoubleSpinBox* m_autoFocusReasonableHfrMinimum[2] = {nullptr, nullptr};
    QCheckBox* m_autoFocusCalibrateHfr[2] = {nullptr, nullptr};
    QCheckBox* m_autoFocusCalibrateRms[2] = {nullptr, nullptr};
    QSpinBox* m_autoFocusFramesPerStateSpin = nullptr;
    QSpinBox* m_autoFocusSettleTimeSpin = nullptr;
    QSpinBox* m_autoFocusStageTimeoutSpin = nullptr;
    QDoubleSpinBox* m_autoFocusTemperatureThresholdSpin = nullptr;
    QComboBox* m_autoFocusStatisticsModeCombo = nullptr;
    QDoubleSpinBox* m_autoFocusTrimRatioSpin = nullptr;
    QSpinBox* m_autoFocusStepSpin = nullptr;
    QSpinBox* m_autoFocusMaximumRangeSpin = nullptr;
    QSpinBox* m_autoFocusMaximumIterationsSpin = nullptr;
    QDoubleSpinBox* m_autoFocusDirectionImprovementSpin = nullptr;
    QDoubleSpinBox* m_autoFocusDirectionWorseningSpin = nullptr;
    QDoubleSpinBox* m_autoFocusHfrImprovementSpin = nullptr;
    QDoubleSpinBox* m_autoFocusCallbackHfrToleranceSpin = nullptr;
    QDoubleSpinBox* m_autoFocusHfrToleranceSpin = nullptr;
    QDoubleSpinBox* m_autoFocusStabilitySpin = nullptr;
    QDoubleSpinBox* m_autoFocusRmsToleranceSpin = nullptr;
    QSpinBox* m_autoFocusCallbackStepSpin = nullptr;
    QSpinBox* m_autoFocusStartupSearchRadiusSpin = nullptr;
    QCheckBox* m_autoFocusLoggingCheck = nullptr;
    QPushButton* m_autoFocusApplyBtn = nullptr;
    QPushButton* m_autoFocusManualStartBtn = nullptr;
    QLabel* m_autoFocusStatusLabel = nullptr;

    // State
    QVector<EafDeviceDescriptor> m_devices;
    EafDeviceState m_currentState;
    QLabel* m_motionLockLabel = nullptr;
    bool m_motionAllowed = true;
    QString m_motionLockReason;
    bool m_autoFocusMotionLocked[2] = {false, false};
    bool m_autoFocusTrackingAvailable = false;
    AutoFocusConfig m_autoFocusAppliedConfig;
};
