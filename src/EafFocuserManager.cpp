#include "EafFocuserManager.h"
#include "EafSdkLoader.h"

#include <QThread>
#include <QTimer>
#include <QMetaObject>
#include <QSettings>
#include <QDateTime>
#include <QDebug>
#include <algorithm>

namespace {
constexpr unsigned long kEafWorkerShutdownTimeoutMs = 3000;
}

// ============================================================
// EafFocuserWorker — lives in the worker thread
// ============================================================
class EafFocuserWorker : public QObject {
    Q_OBJECT
public:
    explicit EafFocuserWorker(EafSdkLoader* sdk, QObject* parent = nullptr);
    ~EafFocuserWorker() override;

    struct SlotState {
        bool opened = false;
        int deviceId = -1;
        int assignedEnumIndex = -1;
        QString serialHex;
        QString assignedSerialHex;
        int pollingIntervalMs = 1000;
        bool motionAllowed = true;
        int commandedTarget = 0;
    };

public slots:
    void doInitialize();
    void doShutdown();
    void doShutdownAndQuit();
    void doRefreshDevices();
    void doOpenDevice(TelescopeSlot slot, int enumerationIndex, int deviceId, QString serialHex);
    void doCloseDevice(TelescopeSlot slot);
    void doPollState(TelescopeSlot slot);
    void doMoveAbsolute(TelescopeSlot slot, int target);
    void doMoveRelative(TelescopeSlot slot, int delta);
    void doStopMotion(TelescopeSlot slot);
    void doResetPosition(TelescopeSlot slot, int value);
    void doSetMaxStep(TelescopeSlot slot, int value);
    void doSetReverse(TelescopeSlot slot, bool value);
    void doSetBacklash(TelescopeSlot slot, int value);
    void doSetBeep(TelescopeSlot slot, bool value);
    void doSetLed(TelescopeSlot slot, bool value);
    void doSetMotionAllowed(bool allowed);
    void doRequestStateRefresh(TelescopeSlot slot);

signals:
    void sdkAvailabilityChanged(bool available, QString detail);
    void deviceListChanged(QVector<EafDeviceDescriptor> devices);
    void stateChanged(TelescopeSlot slot, EafDeviceState state);
    void commandStarted(TelescopeSlot slot, QString command);
    void commandFinished(TelescopeSlot slot, QString command);
    void commandFailed(TelescopeSlot slot, QString command, QString error);
    void deviceRemoved(TelescopeSlot slot, QString serialHex);

private:
    EafDeviceState readDeviceState(int deviceId);
    void startPolling(TelescopeSlot slot, int intervalMs);
    void stopPolling(TelescopeSlot slot);
    bool checkMotionAllowed(TelescopeSlot slot, const QString& command);

    EafSdkLoader* m_sdk;
    QTimer* m_pollTimers[2] = {nullptr, nullptr};
    SlotState m_slots[2];
    bool m_initialized = false;
};

EafFocuserWorker::EafFocuserWorker(EafSdkLoader* sdk, QObject* parent)
    : QObject(parent)
    , m_sdk(sdk)
{
}

EafFocuserWorker::~EafFocuserWorker()
{
    doShutdown();
}

void EafFocuserWorker::doInitialize()
{
    if (m_initialized) return;

    if (!m_sdk->isLoaded()) {
        const bool loaded = m_sdk->load();
        emit sdkAvailabilityChanged(loaded, loaded ? QStringLiteral("SDK loaded") : m_sdk->loadError());
        if (!loaded) {
            return;
        }
    }

    m_initialized = true;
    emit sdkAvailabilityChanged(true, m_sdk->sdkVersion());
}

void EafFocuserWorker::doShutdown()
{
    for (int i = 0; i < 2; ++i) {
        stopPolling(static_cast<TelescopeSlot>(i));
        if (m_slots[i].opened) {
            const int id = m_slots[i].deviceId;
            m_sdk->EAFClose(id);
            m_slots[i].opened = false;
            m_slots[i].deviceId = -1;
        }
        m_slots[i].assignedSerialHex.clear();
    }
    m_initialized = false;
}

void EafFocuserWorker::doShutdownAndQuit()
{
    doShutdown();
    QThread::currentThread()->quit();
}

void EafFocuserWorker::doRefreshDevices()
{
    if (!m_sdk->isLoaded()) {
        if (!m_sdk->load()) {
            emit sdkAvailabilityChanged(false, m_sdk->loadError());
            return;
        }
        emit sdkAvailabilityChanged(true, m_sdk->sdkVersion());
    }

    const int count = m_sdk->EAFGetNum();
    QVector<EafDeviceDescriptor> devices;
    devices.reserve((std::max)(0, count));

    for (int index = 0; index < count; ++index) {
        EafDeviceDescriptor desc;
        desc.enumerationIndex = index;

        int id = -1;
        if (m_sdk->EAFGetID(index, &id) != EAF_SUCCESS) {
            continue;
        }
        desc.id = id;

        // Open temporarily to read properties
        if (m_sdk->EAFOpen(id) != EAF_SUCCESS) {
            continue;
        }

        EAF_INFO info;
        if (m_sdk->EAFGetProperty(id, &info) == EAF_SUCCESS) {
            desc.name = QString::fromUtf8(info.Name);
            desc.propertyMaxStep = info.MaxStep;
        }

        // Optional: serial number (EAF_SN is EAF_ID = unsigned char id[8])
        if (m_sdk->EAFGetSerialNumber) {
            EAF_SN sn;
            if (m_sdk->EAFGetSerialNumber(id, &sn) == EAF_SUCCESS) {
                desc.serialHex = EafSdkLoader::serialToHex(sn);
                desc.serialSupported = true;
            }
        }

        // Optional: type (EAF_TYPE is struct { char type[16]; })
        if (m_sdk->EAFGetType) {
            EAF_TYPE typeInfo;
            if (m_sdk->EAFGetType(id, &typeInfo) == EAF_SUCCESS) {
                desc.type = QString::fromLatin1(typeInfo.type);
            }
        }

        // Optional: firmware version
        if (m_sdk->EAFGetFirmwareVersion) {
            unsigned char major = 0, minor = 0, buildNo = 0;
            if (m_sdk->EAFGetFirmwareVersion(id, &major, &minor, &buildNo) == EAF_SUCCESS) {
                desc.firmwareVersion = QStringLiteral("%1.%2.%3")
                    .arg(static_cast<int>(major))
                    .arg(static_cast<int>(minor))
                    .arg(static_cast<int>(buildNo));
            }
        }

        m_sdk->EAFClose(id);
        devices.append(desc);
    }

    emit deviceListChanged(devices);
}

void EafFocuserWorker::doOpenDevice(TelescopeSlot slot, int enumerationIndex, int deviceId, QString serialHex)
{
    const int idx = static_cast<int>(slot);

    // Validate deviceId
    if (deviceId < 0) {
        emit commandFailed(slot, QStringLiteral("open"), QStringLiteral("无效的设备 ID"));
        return;
    }

    // Close if already open
    if (m_slots[idx].opened) {
        doCloseDevice(slot);
    }

    const EAF_ERROR_CODE err = m_sdk->EAFOpen(deviceId);
    if (err != EAF_SUCCESS) {
        emit commandFailed(slot, QStringLiteral("open"), EafSdkLoader::errorToString(err));
        return;
    }

    m_slots[idx].opened = true;
    m_slots[idx].deviceId = deviceId;
    m_slots[idx].assignedEnumIndex = enumerationIndex;
    m_slots[idx].serialHex = serialHex;
    m_slots[idx].commandedTarget = 0; // reset

    emit commandFinished(slot, QStringLiteral("open"));

    // Start polling and read initial state
    startPolling(slot, 1000);
    doPollState(slot);
}

void EafFocuserWorker::doCloseDevice(TelescopeSlot slot)
{
    const int idx = static_cast<int>(slot);
    stopPolling(slot);

    if (m_slots[idx].opened) {
        m_sdk->EAFClose(m_slots[idx].deviceId);
        m_slots[idx].opened = false;
    }
    m_slots[idx].deviceId = -1;
    m_slots[idx].assignedEnumIndex = -1;
    m_slots[idx].serialHex.clear();
}

void EafFocuserWorker::doPollState(TelescopeSlot slot)
{
    const int idx = static_cast<int>(slot);
    if (!m_slots[idx].opened) {
        return;
    }

    const int deviceId = m_slots[idx].deviceId;
    EafDeviceState state = readDeviceState(deviceId);
    state.commandedTarget = m_slots[idx].commandedTarget;
    state.deviceId = deviceId;
    emit stateChanged(slot, state);

    // Adjust polling interval based on moving state
    const int newInterval = state.moving ? 250 : 1000;
    if (m_pollTimers[idx] && m_pollTimers[idx]->interval() != newInterval) {
        m_pollTimers[idx]->setInterval(newInterval);
    }
}

EafDeviceState EafFocuserWorker::readDeviceState(int deviceId)
{
    EafDeviceState state;
    state.opened = true;
    state.lastUpdatedMs = QDateTime::currentMSecsSinceEpoch();

    // Position
    int position = 0;
    if (m_sdk->EAFGetPosition(deviceId, &position) == EAF_SUCCESS) {
        state.currentPosition = position;
    }

    // Moving
    bool moving = false, handControl = false;
    EAF_ERROR_CODE err = m_sdk->EAFIsMoving(deviceId, &moving, &handControl);
    if (err == EAF_SUCCESS) {
        state.moving = moving;
        state.handControl = handControl;
    } else if (err == EAF_ERROR_REMOVED) {
        state.lastError = QStringLiteral("Device removed");
        state.opened = false;
    }

    // Max step
    int maxStep = 0;
    if (m_sdk->EAFGetMaxStep(deviceId, &maxStep) == EAF_SUCCESS) {
        state.maxStep = maxStep;
    }

    // Step range
    int stepRange = 0;
    if (m_sdk->EAFStepRange(deviceId, &stepRange) == EAF_SUCCESS) {
        state.stepRange = stepRange;
    }

    // Reverse
    bool reverse = false;
    if (m_sdk->EAFGetReverse(deviceId, &reverse) == EAF_SUCCESS) {
        state.reverse = reverse;
    }

    // Backlash
    int backlash = 0;
    if (m_sdk->EAFGetBacklash(deviceId, &backlash) == EAF_SUCCESS) {
        state.backlash = backlash;
    }

    // Temperature
    float temp = 0.0f;
    if (m_sdk->EAFGetTemp(deviceId, &temp) == EAF_SUCCESS && temp > -200.0f) {
        state.temperatureC = temp;
        state.temperatureValid = true;
    }

    // Optional: beep
    if (m_sdk->EAFGetBeep) {
        bool beep = false;
        if (m_sdk->EAFGetBeep(deviceId, &beep) == EAF_SUCCESS) {
            state.beep = beep;
        }
    }

    // Optional: LED
    if (m_sdk->EAFGetLedState) {
        bool led = false;
        if (m_sdk->EAFGetLedState(deviceId, &led) == EAF_SUCCESS) {
            state.led = led;
        }
    }

    // Optional: error code (EAF_ERROR_MSG has char motor_error_code[3], battery_error_code[3])
    if (m_sdk->EAFGetErrorCode) {
        EAF_ERROR_MSG errMsg;
        if (m_sdk->EAFGetErrorCode(deviceId, &errMsg) == EAF_SUCCESS) {
            if (errMsg.motor_error_code[0] != '\0') {
                state.motorErrorCode = QString::fromLatin1(errMsg.motor_error_code, 3).trimmed();
            }
            if (errMsg.battery_error_code[0] != '\0') {
                state.batteryErrorCode = QString::fromLatin1(errMsg.battery_error_code, 3).trimmed();
            }
        }
    }

    return state;
}

void EafFocuserWorker::doMoveAbsolute(TelescopeSlot slot, int target)
{
    if (!checkMotionAllowed(slot, QStringLiteral("move"))) return;

    const int idx = static_cast<int>(slot);
    if (!m_slots[idx].opened) {
        emit commandFailed(slot, QStringLiteral("move"), QStringLiteral("设备未打开"));
        return;
    }

    const int deviceId = m_slots[idx].deviceId;

    // Read max step for clamping
    int maxStep = 0;
    EAF_ERROR_CODE err = m_sdk->EAFGetMaxStep(deviceId, &maxStep);
    if (err == EAF_SUCCESS) {
        if (target < 0) target = 0;
        if (target > maxStep) target = maxStep;
    }

    // Check if currently moving
    bool moving = false, handControl = false;
    if (m_sdk->EAFIsMoving(deviceId, &moving, &handControl) == EAF_SUCCESS && moving) {
        emit commandFailed(slot, QStringLiteral("move"), QStringLiteral("Device is moving"));
        return;
    }

    emit commandStarted(slot, QStringLiteral("move"));
    err = m_sdk->EAFMove(deviceId, target);
    if (err == EAF_SUCCESS) {
        m_slots[idx].commandedTarget = target;
        startPolling(slot, 250);
        emit commandFinished(slot, QStringLiteral("move"));
    } else {
        emit commandFailed(slot, QStringLiteral("move"), EafSdkLoader::errorToString(err));
    }
}

void EafFocuserWorker::doMoveRelative(TelescopeSlot slot, int delta)
{
    const int idx = static_cast<int>(slot);
    if (!m_slots[idx].opened) {
        emit commandFailed(slot, QStringLiteral("moveRelative"), QStringLiteral("设备未打开"));
        return;
    }

    int position = 0;
    if (m_sdk->EAFGetPosition(m_slots[idx].deviceId, &position) != EAF_SUCCESS) {
        emit commandFailed(slot, QStringLiteral("moveRelative"), QStringLiteral("无法读取当前位置"));
        return;
    }

    int maxStep = 0;
    if (m_sdk->EAFGetMaxStep(m_slots[idx].deviceId, &maxStep) != EAF_SUCCESS) {
        maxStep = 100000;
    }

    const int target = (std::clamp)(position + delta, 0, maxStep);
    doMoveAbsolute(slot, target);
}

void EafFocuserWorker::doStopMotion(TelescopeSlot slot)
{
    const int idx = static_cast<int>(slot);
    if (!m_slots[idx].opened) {
        return;
    }

    const int deviceId = m_slots[idx].deviceId;

    bool moving = false, handControl = false;
    if (m_sdk->EAFIsMoving(deviceId, &moving, &handControl) == EAF_SUCCESS && handControl) {
        emit commandFailed(slot, QStringLiteral("stop"), QStringLiteral("Hand-control motion cannot be stopped by software"));
        return;
    }

    emit commandStarted(slot, QStringLiteral("stop"));

    if (m_sdk->EAFStopAndWait) {
        const EAF_ERROR_CODE err = m_sdk->EAFStopAndWait(deviceId, 2000);
        if (err != EAF_SUCCESS) {
            m_sdk->EAFStop(deviceId);
        }
    } else {
        m_sdk->EAFStop(deviceId);
    }

    startPolling(slot, 250);
    emit commandFinished(slot, QStringLiteral("stop"));
}

void EafFocuserWorker::doResetPosition(TelescopeSlot slot, int value)
{
    if (!checkMotionAllowed(slot, QStringLiteral("resetPosition"))) return;

    const int idx = static_cast<int>(slot);
    if (!m_slots[idx].opened) {
        emit commandFailed(slot, QStringLiteral("resetPosition"), QStringLiteral("设备未打开"));
        return;
    }

    emit commandStarted(slot, QStringLiteral("resetPosition"));
    const EAF_ERROR_CODE err = m_sdk->EAFResetPostion(m_slots[idx].deviceId, value);
    if (err == EAF_SUCCESS) {
        emit commandFinished(slot, QStringLiteral("resetPosition"));
    } else {
        emit commandFailed(slot, QStringLiteral("resetPosition"), EafSdkLoader::errorToString(err));
    }
}

void EafFocuserWorker::doSetMaxStep(TelescopeSlot slot, int value)
{
    if (!checkMotionAllowed(slot, QStringLiteral("setMaxStep"))) return;

    const int idx = static_cast<int>(slot);
    if (!m_slots[idx].opened) {
        emit commandFailed(slot, QStringLiteral("setMaxStep"), QStringLiteral("设备未打开"));
        return;
    }

    // Ensure maxStep >= current position
    int position = 0;
    if (m_sdk->EAFGetPosition(m_slots[idx].deviceId, &position) == EAF_SUCCESS && value < position) {
        emit commandFailed(slot, QStringLiteral("setMaxStep"),
            QStringLiteral("MaxStep 不能小于当前位置 (%1)").arg(position));
        return;
    }

    emit commandStarted(slot, QStringLiteral("setMaxStep"));
    const EAF_ERROR_CODE err = m_sdk->EAFSetMaxStep(m_slots[idx].deviceId, value);
    if (err == EAF_SUCCESS) {
        emit commandFinished(slot, QStringLiteral("setMaxStep"));
    } else {
        emit commandFailed(slot, QStringLiteral("setMaxStep"), EafSdkLoader::errorToString(err));
    }
}

void EafFocuserWorker::doSetReverse(TelescopeSlot slot, bool value)
{
    if (!checkMotionAllowed(slot, QStringLiteral("setReverse"))) return;

    const int idx = static_cast<int>(slot);
    if (!m_slots[idx].opened) {
        emit commandFailed(slot, QStringLiteral("setReverse"), QStringLiteral("设备未打开"));
        return;
    }

    emit commandStarted(slot, QStringLiteral("setReverse"));
    const EAF_ERROR_CODE err = m_sdk->EAFSetReverse(m_slots[idx].deviceId, value);
    if (err == EAF_SUCCESS) {
        emit commandFinished(slot, QStringLiteral("setReverse"));
    } else {
        emit commandFailed(slot, QStringLiteral("setReverse"), EafSdkLoader::errorToString(err));
    }
}

void EafFocuserWorker::doSetBacklash(TelescopeSlot slot, int value)
{
    if (!checkMotionAllowed(slot, QStringLiteral("setBacklash"))) return;

    const int idx = static_cast<int>(slot);
    if (!m_slots[idx].opened) {
        emit commandFailed(slot, QStringLiteral("setBacklash"), QStringLiteral("设备未打开"));
        return;
    }

    value = (std::clamp)(value, 0, 255);

    emit commandStarted(slot, QStringLiteral("setBacklash"));
    const EAF_ERROR_CODE err = m_sdk->EAFSetBacklash(m_slots[idx].deviceId, value);
    if (err == EAF_SUCCESS) {
        emit commandFinished(slot, QStringLiteral("setBacklash"));
    } else {
        emit commandFailed(slot, QStringLiteral("setBacklash"), EafSdkLoader::errorToString(err));
    }
}

void EafFocuserWorker::doSetBeep(TelescopeSlot slot, bool value)
{
    if (!checkMotionAllowed(slot, QStringLiteral("setBeep"))) return;

    const int idx = static_cast<int>(slot);
    if (!m_slots[idx].opened) {
        emit commandFailed(slot, QStringLiteral("setBeep"), QStringLiteral("设备未打开"));
        return;
    }

    if (!m_sdk->EAFSetBeep) {
        emit commandFailed(slot, QStringLiteral("setBeep"), QStringLiteral("Not supported"));
        return;
    }

    emit commandStarted(slot, QStringLiteral("setBeep"));
    const EAF_ERROR_CODE err = m_sdk->EAFSetBeep(m_slots[idx].deviceId, value);
    if (err == EAF_SUCCESS) {
        emit commandFinished(slot, QStringLiteral("setBeep"));
    } else {
        emit commandFailed(slot, QStringLiteral("setBeep"), EafSdkLoader::errorToString(err));
    }
}

void EafFocuserWorker::doSetLed(TelescopeSlot slot, bool value)
{
    if (!checkMotionAllowed(slot, QStringLiteral("setLed"))) return;

    const int idx = static_cast<int>(slot);
    if (!m_slots[idx].opened) {
        emit commandFailed(slot, QStringLiteral("setLed"), QStringLiteral("设备未打开"));
        return;
    }

    if (!m_sdk->EAFSetLedState) {
        emit commandFailed(slot, QStringLiteral("setLed"), QStringLiteral("Not supported"));
        return;
    }

    emit commandStarted(slot, QStringLiteral("setLed"));
    const EAF_ERROR_CODE err = m_sdk->EAFSetLedState(m_slots[idx].deviceId, value);
    if (err == EAF_SUCCESS) {
        emit commandFinished(slot, QStringLiteral("setLed"));
    } else {
        emit commandFailed(slot, QStringLiteral("setLed"), EafSdkLoader::errorToString(err));
    }
}

void EafFocuserWorker::doSetMotionAllowed(bool allowed)
{
    for (int i = 0; i < 2; ++i) {
        m_slots[i].motionAllowed = allowed;
    }
}

void EafFocuserWorker::doRequestStateRefresh(TelescopeSlot slot)
{
    doPollState(slot);
}

void EafFocuserWorker::startPolling(TelescopeSlot slot, int intervalMs)
{
    const int idx = static_cast<int>(slot);
    if (!m_pollTimers[idx]) {
        m_pollTimers[idx] = new QTimer(this);
        m_pollTimers[idx]->setObjectName(QStringLiteral("pollTimer_%1").arg(idx));
        connect(m_pollTimers[idx], &QTimer::timeout, this, [this, slot]() {
            doPollState(slot);
        });
    }
    m_pollTimers[idx]->setInterval(intervalMs);
    if (!m_pollTimers[idx]->isActive()) {
        m_pollTimers[idx]->start();
    }
}

void EafFocuserWorker::stopPolling(TelescopeSlot slot)
{
    const int idx = static_cast<int>(slot);
    if (m_pollTimers[idx]) {
        m_pollTimers[idx]->stop();
    }
}

bool EafFocuserWorker::checkMotionAllowed(TelescopeSlot slot, const QString& command)
{
    Q_UNUSED(command);
    const int idx = static_cast<int>(slot);
    if (!m_slots[idx].motionAllowed) {
        emit commandFailed(slot, QStringLiteral("motion"),
            QStringLiteral("Focuser motion is disabled while capture, simulation, or alignment is active."));
        return false;
    }
    return true;
}

// ============================================================
// EafFocuserManager — lives in GUI thread
// ============================================================

EafFocuserManager::EafFocuserManager(QObject* parent)
    : QObject(parent)
    , m_sdk(new EafSdkLoader())
{
    qRegisterMetaType<TelescopeSlot>("TelescopeSlot");
    qRegisterMetaType<EafDeviceDescriptor>("EafDeviceDescriptor");
    qRegisterMetaType<EafDeviceState>("EafDeviceState");
    qRegisterMetaType<QVector<EafDeviceDescriptor>>("QVector<EafDeviceDescriptor>");
}

EafFocuserManager::~EafFocuserManager()
{
    shutdown();
    if (m_workerShutdownTimedOut) {
        qWarning() << "EAF worker thread still running; retaining SDK loader until process exit.";
        if (m_workerThread) {
            m_workerThread->setParent(nullptr);
        }
        m_workerThread = nullptr;
        m_worker = nullptr;
        m_sdk = nullptr;
        return;
    }

    delete m_sdk;
    m_sdk = nullptr;
}

void EafFocuserManager::initialize()
{
    if (m_workerThread) {
        return;
    }

    m_worker = new EafFocuserWorker(m_sdk);
    m_workerThread = new QThread(this);
    m_workerThread->setObjectName(QStringLiteral("eafWorker"));

    // Move worker to thread
    m_worker->moveToThread(m_workerThread);

    // Forward signals from worker to this (cross-thread)
    connect(m_worker, &EafFocuserWorker::sdkAvailabilityChanged,
            this, &EafFocuserManager::sdkAvailabilityChanged);
    connect(m_worker, &EafFocuserWorker::deviceListChanged,
            this, &EafFocuserManager::deviceListChanged);
    // Forward stateChanged with device identity enriched from slot mapping
    connect(m_worker, &EafFocuserWorker::stateChanged,
            this, [this](TelescopeSlot slot, EafDeviceState state) {
        const int idx = static_cast<int>(slot);
        state.deviceId = m_slotMapping[idx].id;
        state.deviceName = m_slotMapping[idx].name;
        state.serialHex = m_slotMapping[idx].serialHex;
        state.deviceType = m_slotMapping[idx].type;
        state.firmwareVersion = m_slotMapping[idx].firmwareVersion;
        emit stateChanged(slot, state);
    });
    connect(m_worker, &EafFocuserWorker::commandStarted,
            this, &EafFocuserManager::commandStarted);
    connect(m_worker, &EafFocuserWorker::commandFinished,
            this, &EafFocuserManager::commandFinished);
    connect(m_worker, &EafFocuserWorker::commandFailed,
            this, &EafFocuserManager::commandFailed);
    connect(m_worker, &EafFocuserWorker::deviceRemoved,
            this, &EafFocuserManager::deviceRemoved);

    // Cleanup on thread finish
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

    m_workerThread->start();

    // Initialize on worker thread
    QMetaObject::invokeMethod(m_worker, "doInitialize", Qt::QueuedConnection);
}

void EafFocuserManager::shutdown()
{
    if (!m_workerThread) {
        return;
    }
    if (m_workerShutdownTimedOut) {
        return;
    }

    if (m_worker && m_workerThread->isRunning()) {
        const bool posted = QMetaObject::invokeMethod(m_worker, "doShutdownAndQuit", Qt::QueuedConnection);
        if (!posted) {
            qWarning() << "Failed to post EAF worker shutdown request.";
            m_workerThread->quit();
        }

        const bool stopped = m_workerThread->wait(kEafWorkerShutdownTimeoutMs);
        if (!stopped) {
            m_workerShutdownTimedOut = true;
            qWarning() << "EAF worker thread did not stop within"
                       << kEafWorkerShutdownTimeoutMs << "ms; SDK loader retained.";
            emit sdkAvailabilityChanged(
                false,
                QStringLiteral("EAF worker thread did not stop within 3000 ms; SDK loader retained."));
            return;
        }
    }

    m_workerThread->deleteLater();
    m_workerThread = nullptr;
    m_worker = nullptr;
}

EafSdkLoader* EafFocuserManager::sdkLoader() const
{
    return m_sdk;
}

void EafFocuserManager::openDeviceForSlot(TelescopeSlot slot, const EafDeviceDescriptor& desc)
{
    const int idx = static_cast<int>(slot);
    m_slotMapping[idx] = desc;

    if (!m_worker) return;

    // Close any existing device on this slot first
    QMetaObject::invokeMethod(m_worker, "doCloseDevice", Qt::BlockingQueuedConnection,
        Q_ARG(TelescopeSlot, slot));

    // Now open the new device with real parameters
    QMetaObject::invokeMethod(m_worker, "doOpenDevice", Qt::QueuedConnection,
        Q_ARG(TelescopeSlot, slot),
        Q_ARG(int, desc.enumerationIndex),
        Q_ARG(int, desc.id),
        Q_ARG(QString, desc.serialHex));
}

void EafFocuserManager::refreshDevices()
{
    if (!m_worker) return;
    QMetaObject::invokeMethod(m_worker, "doRefreshDevices", Qt::QueuedConnection);
}

void EafFocuserManager::assignDevice(TelescopeSlot slot, QString serialHex)
{
    emit assignmentChanged(slot, serialHex);
}

void EafFocuserManager::openAssignedDevice(TelescopeSlot slot)
{
    // Use stored slot mapping if available; otherwise do nothing
    const int idx = static_cast<int>(slot);
    if (m_slotMapping[idx].id >= 0) {
        openDeviceForSlot(slot, m_slotMapping[idx]);
    } else {
        emit commandFailed(slot, QStringLiteral("open"), QStringLiteral("No mapped focuser for this telescope. Apply a mapping first."));
    }
}

void EafFocuserManager::closeAssignedDevice(TelescopeSlot slot)
{
    if (!m_worker) return;
    QMetaObject::invokeMethod(m_worker, "doCloseDevice", Qt::QueuedConnection, Q_ARG(TelescopeSlot, slot));
}

void EafFocuserManager::requestStateRefresh(TelescopeSlot slot)
{
    if (!m_worker) return;
    QMetaObject::invokeMethod(m_worker, "doRequestStateRefresh", Qt::QueuedConnection, Q_ARG(TelescopeSlot, slot));
}

void EafFocuserManager::moveAbsolute(TelescopeSlot slot, int target)
{
    if (!m_worker) return;
    QMetaObject::invokeMethod(m_worker, "doMoveAbsolute", Qt::QueuedConnection,
        Q_ARG(TelescopeSlot, slot), Q_ARG(int, target));
}

void EafFocuserManager::moveRelative(TelescopeSlot slot, int delta)
{
    if (!m_worker) return;
    QMetaObject::invokeMethod(m_worker, "doMoveRelative", Qt::QueuedConnection,
        Q_ARG(TelescopeSlot, slot), Q_ARG(int, delta));
}

void EafFocuserManager::stopMotion(TelescopeSlot slot)
{
    if (!m_worker) return;
    QMetaObject::invokeMethod(m_worker, "doStopMotion", Qt::QueuedConnection, Q_ARG(TelescopeSlot, slot));
}

void EafFocuserManager::resetPosition(TelescopeSlot slot, int value)
{
    if (!m_worker) return;
    QMetaObject::invokeMethod(m_worker, "doResetPosition", Qt::QueuedConnection,
        Q_ARG(TelescopeSlot, slot), Q_ARG(int, value));
}

void EafFocuserManager::setMaxStep(TelescopeSlot slot, int value)
{
    if (!m_worker) return;
    QMetaObject::invokeMethod(m_worker, "doSetMaxStep", Qt::QueuedConnection,
        Q_ARG(TelescopeSlot, slot), Q_ARG(int, value));
}

void EafFocuserManager::setReverse(TelescopeSlot slot, bool value)
{
    if (!m_worker) return;
    QMetaObject::invokeMethod(m_worker, "doSetReverse", Qt::QueuedConnection,
        Q_ARG(TelescopeSlot, slot), Q_ARG(bool, value));
}

void EafFocuserManager::setBacklash(TelescopeSlot slot, int value)
{
    if (!m_worker) return;
    QMetaObject::invokeMethod(m_worker, "doSetBacklash", Qt::QueuedConnection,
        Q_ARG(TelescopeSlot, slot), Q_ARG(int, value));
}

void EafFocuserManager::setBeep(TelescopeSlot slot, bool value)
{
    if (!m_worker) return;
    QMetaObject::invokeMethod(m_worker, "doSetBeep", Qt::QueuedConnection,
        Q_ARG(TelescopeSlot, slot), Q_ARG(bool, value));
}

void EafFocuserManager::setLed(TelescopeSlot slot, bool value)
{
    if (!m_worker) return;
    QMetaObject::invokeMethod(m_worker, "doSetLed", Qt::QueuedConnection,
        Q_ARG(TelescopeSlot, slot), Q_ARG(bool, value));
}

void EafFocuserManager::setMotionAllowed(bool allowed, QString reason)
{
    m_motionAllowed = allowed;
    m_motionDisallowedReason = reason;
    if (!m_worker) return;
    QMetaObject::invokeMethod(m_worker, "doSetMotionAllowed", Qt::QueuedConnection,
        Q_ARG(bool, allowed));
}

#include "EafFocuserManager.moc"
