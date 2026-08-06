#pragma once

#include <QString>

#include <functional>

class QObject;
class QThread;

class PulseGeneratorManager
{
public:
    struct Config {
        bool enabled = false;
        QString portName;
        int baudRate = 19200;
        int terminalId = 1;
        double frequencyHz = 200.0;
        quint32 pulseCount = 2000000U;
        double dutyPercent = 50.0;
        bool remoteControl = true;
    };

    PulseGeneratorManager();
    ~PulseGeneratorManager();

    bool applyConfig(const Config& config, QString* errorMessage = nullptr);
    bool configureAndStart(const Config& config, QString* errorMessage = nullptr);
    bool stop(QString* errorMessage = nullptr);
    bool isRunning() const;
    const Config& config() const;

private:
    bool validateConfig(const Config& config, QString* errorMessage) const;
    bool configureDevice(const Config& config, bool enableOutput, QString* errorMessage);
    bool stopDevice(const Config& config, QString* errorMessage);
    bool runWorkerOperation(const QString& operationName,
                            const std::function<bool(QString*)>& operation,
                            QString* errorMessage);
    void ensureWorkerThread();
    void shutdownWorkerThread();
    bool writeRegister16(void* handle,
                         unsigned short deviceAddress,
                         unsigned short reg,
                         unsigned short value,
                         QString* errorMessage) const;
    bool writeRegister32LowWordFirst(void* handle,
                                     unsigned short deviceAddress,
                                     unsigned short startReg,
                                     quint32 value,
                                     QString* errorMessage) const;
    bool openPort(const QString& portName, int baudRate, void** handle, QString* errorMessage) const;
    void closePort(void* handle) const;

    Config m_config;
    bool m_running = false;
    QThread* m_workerThread = nullptr;
    QObject* m_workerContext = nullptr;
};
