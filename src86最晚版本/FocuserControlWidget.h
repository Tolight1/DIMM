#pragma once

#include <QWidget>
#include <QVector>
#include <QString>

#include "EafFocuserManager.h"

class EafFocuserManager;

class QLabel;
class QPushButton;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QGroupBox;

class FocuserControlWidget : public QWidget {
    Q_OBJECT
public:
    explicit FocuserControlWidget(QWidget* parent = nullptr);

    void setManager(EafFocuserManager* manager);

public slots:
    void setMotionAllowed(bool allowed, const QString& reason);

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

private:
    void buildUi();
    void updateDeviceCombo();
    void updateStateDisplay();
    void updateControlStates();
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

    // State
    QVector<EafDeviceDescriptor> m_devices;
    EafDeviceState m_currentState;
    QLabel* m_motionLockLabel = nullptr;
    bool m_motionAllowed = true;
};
