#include "EnvironmentSensorManager.h"

#include "SerialPortWin.h"
#include "WsqSensor.h"

#include <QMetaObject>
#include <QMutexLocker>
#include <QThread>
#include <QTimer>
#include <memory>
#include <string>
#include <utility>

class EnvironmentSensorWorker : public QObject {
    Q_OBJECT
public:
    explicit EnvironmentSensorWorker(EnvironmentSensorConfig config)
        : m_config(std::move(config))
        , m_pollIntervalMs(m_config.pollIntervalMs)
    {
    }

public slots:
    void start()
    {
        if (!m_pollTimer) {
            m_pollTimer = new QTimer(this);
            m_pollTimer->setInterval(m_pollIntervalMs);
            connect(m_pollTimer, &QTimer::timeout, this, &EnvironmentSensorWorker::poll);
        }
        m_pollTimer->start(m_pollIntervalMs);
        poll();
    }

    void stop()
    {
        if (m_pollTimer) {
            m_pollTimer->stop();
        }
        m_sensor.reset();
        if (m_serial) {
            m_serial->close();
        }
        m_serial.reset();
    }

private slots:
    void poll()
    {
        std::string error;
        if (!ensureOpen(error)) {
            emitError(QString::fromStdString(error));
            return;
        }

        wsq::WsqSensorData raw;
        if (!m_sensor->readData(raw, error)) {
            emitError(QString::fromStdString(error));
            return;
        }

        EnvironmentSensorData data;
        data.valid = true;
        data.temperatureC = raw.temperatureC;
        data.humidityRh = raw.humidityRh;
        data.pressureHpa = raw.pressureHpa;
        data.timestamp = QDateTime::currentDateTime();
        emit dataReady(data);
    }

signals:
    void dataReady(EnvironmentSensorData data);
    void errorOccurred(QString error);

private:
    bool ensureOpen(std::string& error)
    {
        if (m_serial && m_serial->isOpen() && m_sensor) {
            return true;
        }

        m_sensor.reset();
        m_serial = std::make_unique<wsq::SerialPortWin>();

        wsq::SerialConfig serialConfig;
        serialConfig.portName = m_config.portName.toStdString();
        serialConfig.baudRate = m_config.baudRate;
        serialConfig.dataBits = m_config.dataBits;
        serialConfig.stopBits = m_config.stopBits;
        serialConfig.readTimeoutMs = m_config.readTimeoutMs;
        serialConfig.writeTimeoutMs = m_config.writeTimeoutMs;

        if (!m_serial->open(serialConfig, error)) {
            m_serial.reset();
            return false;
        }

        m_sensor = std::make_unique<wsq::WsqSensor>(*m_serial, m_config.deviceAddress);
        return true;
    }

    void emitError(const QString& error)
    {
        EnvironmentSensorData data;
        data.valid = false;
        data.error = error;
        data.timestamp = QDateTime::currentDateTime();
        emit dataReady(data);
        emit errorOccurred(error);
    }

    EnvironmentSensorConfig m_config;
    int m_pollIntervalMs = 1000;
    QTimer* m_pollTimer = nullptr;
    std::unique_ptr<wsq::SerialPortWin> m_serial;
    std::unique_ptr<wsq::WsqSensor> m_sensor;
};

EnvironmentSensorManager::EnvironmentSensorManager(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<EnvironmentSensorData>("EnvironmentSensorData");
    qRegisterMetaType<EnvironmentSensorConfig>("EnvironmentSensorConfig");
}

EnvironmentSensorManager::~EnvironmentSensorManager()
{
    stop();
}

void EnvironmentSensorManager::start(const EnvironmentSensorConfig& config)
{
    stop();

    {
        QMutexLocker locker(&m_mutex);
        m_latestData = EnvironmentSensorData();
    }

    m_workerThread = new QThread(this);
    m_worker = new EnvironmentSensorWorker(config);
    m_worker->moveToThread(m_workerThread);

    connect(m_workerThread, &QThread::started, m_worker, &EnvironmentSensorWorker::start);
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &EnvironmentSensorWorker::dataReady,
            this, &EnvironmentSensorManager::onWorkerDataReady, Qt::QueuedConnection);
    connect(m_worker, &EnvironmentSensorWorker::errorOccurred,
            this, &EnvironmentSensorManager::onWorkerError, Qt::QueuedConnection);

    m_workerThread->start();
}

void EnvironmentSensorManager::stop()
{
    if (!m_workerThread) {
        return;
    }

    const bool wasRunning = m_workerThread->isRunning();
    if (m_worker) {
        if (wasRunning) {
            QMetaObject::invokeMethod(m_worker, "stop", Qt::BlockingQueuedConnection);
        } else {
            m_worker->stop();
        }
    }
    m_workerThread->quit();
    m_workerThread->wait();
    if (!wasRunning && m_worker) {
        delete m_worker;
    }
    m_workerThread->deleteLater();
    m_workerThread = nullptr;
    m_worker = nullptr;
}

bool EnvironmentSensorManager::isRunning() const
{
    return m_workerThread && m_workerThread->isRunning();
}

EnvironmentSensorData EnvironmentSensorManager::latestData() const
{
    QMutexLocker locker(&m_mutex);
    return m_latestData;
}

void EnvironmentSensorManager::onWorkerDataReady(EnvironmentSensorData data)
{
    {
        QMutexLocker locker(&m_mutex);
        m_latestData = data;
    }
    emit dataUpdated(data);
}

void EnvironmentSensorManager::onWorkerError(QString error)
{
    emit errorOccurred(error);
}

#include "EnvironmentSensorManager.moc"
