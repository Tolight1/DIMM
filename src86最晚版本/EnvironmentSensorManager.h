#pragma once

#include "AppConfig.h"

#include <QDateTime>
#include <QMutex>
#include <QObject>
#include <QString>

struct EnvironmentSensorData {
    bool valid = false;
    double temperatureC = 0.0;
    double humidityRh = 0.0;
    double pressureHpa = 0.0;
    QString error;
    QDateTime timestamp;
};

Q_DECLARE_METATYPE(EnvironmentSensorData)
Q_DECLARE_METATYPE(EnvironmentSensorConfig)

class QThread;
class EnvironmentSensorWorker;

class EnvironmentSensorManager : public QObject {
    Q_OBJECT
public:
    explicit EnvironmentSensorManager(QObject* parent = nullptr);
    ~EnvironmentSensorManager() override;

    void start(const EnvironmentSensorConfig& config = EnvironmentSensorConfig());
    void stop();
    bool isRunning() const;
    EnvironmentSensorData latestData() const;

signals:
    void dataUpdated(EnvironmentSensorData data);
    void errorOccurred(QString error);

private slots:
    void onWorkerDataReady(EnvironmentSensorData data);
    void onWorkerError(QString error);

private:
    mutable QMutex m_mutex;
    EnvironmentSensorData m_latestData;
    QThread* m_workerThread = nullptr;
    EnvironmentSensorWorker* m_worker = nullptr;
};
