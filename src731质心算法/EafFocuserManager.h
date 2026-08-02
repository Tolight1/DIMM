#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QPointF>
#include <QMetaType>
#include <QtGlobal>

class EafSdkLoader;
class QThread;
class QTimer;

enum class TelescopeSlot {
    Telescope1 = 0,
    Telescope2 = 1
};

struct EafDeviceDescriptor {
    int enumerationIndex = -1;
    int id = -1;
    QString name;
    QString serialHex;
    QString type;
    QString firmwareVersion;
    int propertyMaxStep = 0;
    bool serialSupported = false;
};

struct EafDeviceState {
    bool sdkLoaded = false;
    bool present = false;
    bool opened = false;
    bool moving = false;
    bool handControl = false;
    bool temperatureValid = false;
    int currentPosition = 0;
    int commandedTarget = 0;
    int maxStep = 0;
    int stepRange = 0;
    int backlash = 0;
    float temperatureC = 0.0f;
    bool reverse = false;
    bool beep = false;
    bool led = false;
    // Device identity (populated by manager from slot mapping)
    int deviceId = -1;
    QString deviceName;
    QString serialHex;
    QString deviceType;
    QString firmwareVersion;
    // Error info
    QString motorErrorCode;
    QString batteryErrorCode;
    QString lastError;
    qint64 lastUpdatedMs = 0;
};

struct FocuserAssignment {
    TelescopeSlot slot = TelescopeSlot::Telescope1;
    QString preferredSerialHex;
    int activeDeviceId = -1;
    bool assigned = false;
};

Q_DECLARE_METATYPE(TelescopeSlot)
Q_DECLARE_METATYPE(EafDeviceDescriptor)
Q_DECLARE_METATYPE(EafDeviceState)
Q_DECLARE_METATYPE(QVector<EafDeviceDescriptor>)

class EafFocuserWorker;

class EafFocuserManager : public QObject {
    Q_OBJECT
public:
    explicit EafFocuserManager(QObject* parent = nullptr);
    ~EafFocuserManager() override;

    void initialize();
    void shutdown();

    EafSdkLoader* sdkLoader() const;

    /// Store device mapping and open device for a slot
    void openDeviceForSlot(TelescopeSlot slot, const EafDeviceDescriptor& desc);

public slots:
    void refreshDevices();
    void assignDevice(TelescopeSlot slot, QString serialHex);
    void openAssignedDevice(TelescopeSlot slot);
    void closeAssignedDevice(TelescopeSlot slot);
    void requestStateRefresh(TelescopeSlot slot);
    void moveAbsolute(TelescopeSlot slot, int target);
    void moveRelative(TelescopeSlot slot, int delta);
    void stopMotion(TelescopeSlot slot);
    void resetPosition(TelescopeSlot slot, int value);
    void setMaxStep(TelescopeSlot slot, int value);
    void setReverse(TelescopeSlot slot, bool value);
    void setBacklash(TelescopeSlot slot, int value);
    void setBeep(TelescopeSlot slot, bool value);
    void setLed(TelescopeSlot slot, bool value);
    void setMotionAllowed(bool allowed, QString reason);

signals:
    void sdkAvailabilityChanged(bool available, QString detail);
    void deviceListChanged(QVector<EafDeviceDescriptor> devices);
    void assignmentChanged(TelescopeSlot slot, QString serialHex);
    void stateChanged(TelescopeSlot slot, EafDeviceState state);
    void commandStarted(TelescopeSlot slot, QString command);
    void commandFinished(TelescopeSlot slot, QString command);
    void commandFailed(TelescopeSlot slot, QString command, QString error);
    void deviceRemoved(TelescopeSlot slot, QString serialHex);

private:
    EafSdkLoader* m_sdk = nullptr;
    EafFocuserWorker* m_worker = nullptr;
    QThread* m_workerThread = nullptr;
    bool m_motionAllowed = true;
    QString m_motionDisallowedReason;
    EafDeviceDescriptor m_slotMapping[2];
};
