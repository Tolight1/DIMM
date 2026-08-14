#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "PulseGeneratorManager.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include <QDebug>
#include <QEventLoop>
#include <QMetaObject>
#include <QObject>
#include <QScopeGuard>
#include <QThread>

namespace {
constexpr unsigned short kRegFrequencyL16 = 0xAFCC;
constexpr unsigned short kRegPulseCountL16 = 0xAFCE;
constexpr unsigned short kRegOutputType = 0xAFD0;
constexpr unsigned short kRegControlSource = 0xAFD1;
constexpr unsigned short kRegDutyCycle = 0xAFD2;
constexpr unsigned short kRegOutputEnable = 0xAFD3;

constexpr unsigned short kOutputTypePulse = 0x0001;
constexpr unsigned short kControlSourceRemote = 0x0100;
constexpr unsigned short kControlSourceLocal = 0x0000;
constexpr unsigned long kPulseWorkerShutdownTimeoutMs = 3000;
constexpr int kRegisterWriteAttempts = 3;
constexpr ULONGLONG kRegisterResponseTimeoutMs = 2000;
constexpr unsigned long kRegisterRetryDelayMs = 50;

QString normalizePortName(const QString& portName)
{
    if (portName.startsWith(QStringLiteral("\\\\.\\"))) {
        return portName;
    }
    return QStringLiteral("\\\\.\\") + portName.trimmed();
}

quint16 crc16Modbus(const std::vector<unsigned char>& bytes)
{
    quint16 crc = 0xFFFF;
    for (unsigned char byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x0001U) != 0 ? static_cast<quint16>((crc >> 1U) ^ 0xA001U)
                                       : static_cast<quint16>(crc >> 1U);
        }
    }
    return crc;
}

void appendRegister(std::vector<unsigned char>& frame, unsigned short value)
{
    frame.push_back(static_cast<unsigned char>((value >> 8U) & 0x00FFU));
    frame.push_back(static_cast<unsigned char>(value & 0x00FFU));
}

void appendCrc(std::vector<unsigned char>& frame)
{
    const quint16 crc = crc16Modbus(frame);
    frame.push_back(static_cast<unsigned char>(crc & 0x00FFU));
    frame.push_back(static_cast<unsigned char>((crc >> 8U) & 0x00FFU));
}

QString hexWord(unsigned short value)
{
    return QStringLiteral("0x%1").arg(value, 4, 16, QChar('0')).toUpper();
}

QString hexBytes(const std::vector<unsigned char>& bytes)
{
    QString result;
    for (unsigned char byte : bytes) {
        if (!result.isEmpty()) {
            result += QChar(' ');
        }
        result += QStringLiteral("%1").arg(byte, 2, 16, QChar('0')).toUpper();
    }
    return result;
}

bool writeAll(HANDLE handle, const std::vector<unsigned char>& bytes, QString* errorMessage)
{
    DWORD written = 0;
    if (!WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to write pulse-board serial data, system error %1.")
                                .arg(GetLastError());
        }
        return false;
    }
    if (written != bytes.size()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Pulse-board serial write was incomplete.");
        }
        return false;
    }
    if (FlushFileBuffers(handle) == FALSE) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to flush pulse-board serial buffer.");
        }
        return false;
    }
    return true;
}

bool readExact(HANDLE handle, std::size_t expectedBytes, std::vector<unsigned char>* result, QString* errorMessage)
{
    if (!result) {
        return false;
    }

    result->clear();
    result->reserve(expectedBytes);
    std::array<unsigned char, 64> buffer = {};
    const ULONGLONG deadline = GetTickCount64() + kRegisterResponseTimeoutMs;
    while (result->size() < expectedBytes) {
        if (GetTickCount64() >= deadline) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Timed out waiting for pulse-board response (%1/%2 bytes).")
                                    .arg(result->size())
                                    .arg(expectedBytes);
            }
            return false;
        }

        DWORD bytesRead = 0;
        const DWORD toRead = static_cast<DWORD>(std::min<std::size_t>(expectedBytes - result->size(), buffer.size()));
        if (!ReadFile(handle, buffer.data(), toRead, &bytesRead, nullptr)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Failed to read pulse-board response, system error %1.")
                                    .arg(GetLastError());
            }
            return false;
        }
        if (bytesRead == 0) {
            Sleep(10);
            continue;
        }
        result->insert(result->end(), buffer.begin(), buffer.begin() + bytesRead);
    }
    return true;
}

bool hasValidCrc(const std::vector<unsigned char>& frame)
{
    if (frame.size() < 4) {
        return false;
    }
    std::vector<unsigned char> body(frame.begin(), frame.end() - 2);
    const quint16 crc = crc16Modbus(body);
    const quint16 received = static_cast<quint16>(frame[frame.size() - 2]) |
                             static_cast<quint16>(frame[frame.size() - 1] << 8U);
    return crc == received;
}
}

PulseGeneratorManager::PulseGeneratorManager()
{
    ensureWorkerThread();
}

PulseGeneratorManager::~PulseGeneratorManager()
{
    shutdownWorkerThread();
}

void PulseGeneratorManager::ensureWorkerThread()
{
    if (m_workerThread) {
        return;
    }

    m_workerThread = new QThread();
    m_workerThread->setObjectName(QStringLiteral("pulseGeneratorWorker"));
    m_workerContext = new QObject();
    m_workerContext->moveToThread(m_workerThread);
    QObject::connect(m_workerThread, &QThread::finished, m_workerContext, &QObject::deleteLater);
    m_workerThread->start();
}

void PulseGeneratorManager::shutdownWorkerThread()
{
    if (!m_workerThread) {
        return;
    }

    m_workerThread->quit();
    const bool stopped = m_workerThread->wait(kPulseWorkerShutdownTimeoutMs);
    if (!stopped) {
        qWarning() << "Pulse generator worker thread did not stop within"
                   << kPulseWorkerShutdownTimeoutMs << "ms; retaining thread objects.";
        m_workerThread = nullptr;
        m_workerContext = nullptr;
        return;
    }

    delete m_workerThread;
    m_workerThread = nullptr;
    m_workerContext = nullptr;
}

bool PulseGeneratorManager::runWorkerOperation(const QString& operationName,
                                               const std::function<bool(QString*)>& operation,
                                               QString* errorMessage)
{
    ensureWorkerThread();
    if (!m_workerThread || !m_workerContext) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Pulse generator worker thread is not available.");
        }
        return false;
    }

    if (QThread::currentThread() == m_workerThread) {
        return operation(errorMessage);
    }

    if (m_operationInProgress.exchange(true)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Pulse generator is busy with another operation.");
        }
        return false;
    }
    const auto clearOperationFlag = qScopeGuard([this]() {
        m_operationInProgress.store(false);
    });

    bool completed = false;
    bool success = false;
    QString workerError;
    QEventLoop loop;
    const bool posted = QMetaObject::invokeMethod(m_workerContext,
        [operation, &completed, &success, &workerError, &loop]() {
            QString localError;
            const bool localSuccess = operation(&localError);
            QMetaObject::invokeMethod(
                &loop,
                [&completed, &success, &workerError, localSuccess, localError, &loop]() {
                    success = localSuccess;
                    workerError = localError;
                    completed = true;
                    loop.quit();
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
    if (!posted) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to queue pulse generator %1 operation.")
                                .arg(operationName);
        }
        return false;
    }

    loop.exec();
    if (!completed) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Pulse generator %1 operation did not complete.")
                                .arg(operationName);
        }
        return false;
    }
    if (!success && errorMessage) {
        *errorMessage = workerError;
    }
    return success;
}

bool PulseGeneratorManager::validateConfig(const Config& config, QString* errorMessage) const
{
    if (config.portName.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Pulse-board COM port cannot be empty.");
        }
        return false;
    }
    if (!(config.frequencyHz >= 0.1 && config.frequencyHz <= 10000000.0)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Pulse frequency must be within 0.1 Hz to 10000000 Hz.");
        }
        return false;
    }
    if (config.terminalId < 1 || config.terminalId > 255) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Terminal ID must be within 1 to 255.");
        }
        return false;
    }
    if (config.baudRate <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Baud rate must be greater than 0.");
        }
        return false;
    }
    if (config.pulseCount == 0U) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Pulse count must be greater than 0.");
        }
        return false;
    }
    if (!(config.dutyPercent >= 0.0 && config.dutyPercent <= 100.0)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Duty cycle must be within 0 to 100.");
        }
        return false;
    }
    return true;
}

bool PulseGeneratorManager::configureDevice(const Config& config, bool enableOutput, QString* errorMessage)
{
    if (!validateConfig(config, errorMessage)) {
        return false;
    }

    void* rawHandle = nullptr;
    if (!openPort(config.portName, config.baudRate, &rawHandle, errorMessage)) {
        return false;
    }

    HANDLE handle = static_cast<HANDLE>(rawHandle);
    const unsigned short deviceAddress = static_cast<unsigned short>(config.terminalId & 0x00FF);
    const unsigned short controlSource = config.remoteControl ? kControlSourceRemote : kControlSourceLocal;
    const unsigned short dutyValue = static_cast<unsigned short>(std::round(config.dutyPercent * 10.0));
    bool success = writeRegister16(handle, deviceAddress, kRegControlSource, controlSource, errorMessage) &&
                   writeRegister16(handle, deviceAddress, kRegOutputEnable, 0x0000, errorMessage) &&
                   writeRegister16(handle, deviceAddress, kRegOutputType, kOutputTypePulse, errorMessage) &&
                   writeRegister16(handle, deviceAddress, kRegDutyCycle, dutyValue, errorMessage) &&
                   writeRegister32LowWordFirst(handle,
                                               deviceAddress,
                                               kRegFrequencyL16,
                                               static_cast<quint32>(std::round(config.frequencyHz * 10.0)),
                                               errorMessage) &&
                   writeRegister32LowWordFirst(handle, deviceAddress, kRegPulseCountL16, config.pulseCount, errorMessage);
    if (success && enableOutput) {
        success = writeRegister16(handle, deviceAddress, kRegOutputEnable, 0x0001, errorMessage);
    }
    closePort(handle);
    return success;
}

bool PulseGeneratorManager::setControlSourceDevice(const Config& config, QString* errorMessage)
{
    if (!validateConfig(config, errorMessage)) {
        return false;
    }

    void* rawHandle = nullptr;
    if (!openPort(config.portName, config.baudRate, &rawHandle, errorMessage)) {
        return false;
    }

    HANDLE handle = static_cast<HANDLE>(rawHandle);
    const unsigned short deviceAddress = static_cast<unsigned short>(config.terminalId & 0x00FF);
    const unsigned short controlSource = config.remoteControl ? kControlSourceRemote : kControlSourceLocal;
    const bool success = writeRegister16(handle, deviceAddress, kRegControlSource, controlSource, errorMessage);
    closePort(handle);
    return success;
}

bool PulseGeneratorManager::stopDevice(const Config& config, QString* errorMessage)
{
    if (config.portName.trimmed().isEmpty()) {
        return true;
    }

    void* rawHandle = nullptr;
    if (!openPort(config.portName, config.baudRate, &rawHandle, errorMessage)) {
        return false;
    }

    HANDLE handle = static_cast<HANDLE>(rawHandle);
    const unsigned short deviceAddress = static_cast<unsigned short>(config.terminalId & 0x00FF);
    const bool success = writeRegister16(handle, deviceAddress, kRegOutputEnable, 0x0000, errorMessage);
    closePort(handle);
    return success;
}

bool PulseGeneratorManager::applyConfig(const Config& config, QString* errorMessage)
{
    if (!config.enabled) {
        const bool shouldStopOutput = m_running || m_config.enabled;
        const Config previousConfig = m_config;
        m_config = config;
        if (!shouldStopOutput) {
            m_running = false;
            return true;
        }
        const bool stopped = runWorkerOperation(
            QStringLiteral("stop"),
            [this, previousConfig](QString* workerError) {
                return stopDevice(previousConfig, workerError);
            },
            errorMessage);
        m_running = false;
        return stopped;
    }

    m_config = config;
    m_running = false;

    if (config.portName.trimmed().isEmpty()) {
        return false;
    }
    const Config requestConfig = config;
    return runWorkerOperation(
        QStringLiteral("applyConfig"),
        [this, requestConfig](QString* workerError) {
            return configureDevice(requestConfig, false, workerError);
        },
        errorMessage);
}

bool PulseGeneratorManager::setControlSource(const Config& config,
                                             bool remoteControl,
                                             QString* errorMessage)
{
    Config requestConfig = config;
    requestConfig.remoteControl = remoteControl;
    if (!validateConfig(requestConfig, errorMessage)) {
        return false;
    }

    if (!runWorkerOperation(
            QStringLiteral("setControlSource"),
            [this, requestConfig](QString* workerError) {
                return setControlSourceDevice(requestConfig, workerError);
            },
            errorMessage)) {
        return false;
    }

    m_config = requestConfig;
    return true;
}

bool PulseGeneratorManager::configureAndStart(const Config& config, QString* errorMessage)
{
    if (!config.enabled) {
        return applyConfig(config, errorMessage);
    }
    const Config requestConfig = config;
    if (!runWorkerOperation(
            QStringLiteral("configureAndStart"),
            [this, requestConfig](QString* workerError) {
                return configureDevice(requestConfig, true, workerError);
            },
            errorMessage)) {
        m_config = config;
        m_running = false;
        return false;
    }
    m_config = config;
    m_running = true;
    return true;
}

bool PulseGeneratorManager::stop(QString* errorMessage)
{
    const Config requestConfig = m_config;
    if (requestConfig.portName.trimmed().isEmpty()) {
        m_running = false;
        return true;
    }

    const bool success = runWorkerOperation(
        QStringLiteral("stop"),
        [this, requestConfig](QString* workerError) {
            return stopDevice(requestConfig, workerError);
        },
        errorMessage);
    m_running = false;
    return success;
}

bool PulseGeneratorManager::isRunning() const
{
    return m_running;
}

const PulseGeneratorManager::Config& PulseGeneratorManager::config() const
{
    return m_config;
}

bool PulseGeneratorManager::writeRegister16(void* rawHandle,
                                            unsigned short deviceAddress,
                                            unsigned short reg,
                                            unsigned short value,
                                            QString* errorMessage) const
{
    HANDLE handle = static_cast<HANDLE>(rawHandle);
    std::vector<unsigned char> request;
    request.push_back(static_cast<unsigned char>(deviceAddress & 0x00FF));
    request.push_back(0x06);
    appendRegister(request, reg);
    appendRegister(request, value);
    appendCrc(request);

    QString lastError;
    std::vector<unsigned char> response;
    for (int attempt = 1; attempt <= kRegisterWriteAttempts; ++attempt) {
        response.clear();
        QString attemptError;
        PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);

        if (!writeAll(handle, request, &attemptError)) {
            if (attemptError.isEmpty()) {
                attemptError = QStringLiteral("Failed to write pulse-board register.");
            }
        } else if (!readExact(handle, 8, &response, &attemptError)) {
            if (attemptError.isEmpty()) {
                attemptError = QStringLiteral("Failed to read pulse-board register response.");
            }
        } else if (!hasValidCrc(response) || response != request) {
            attemptError = QStringLiteral("Pulse-board response mismatch or CRC failure.");
        } else {
            return true;
        }

        lastError = attemptError;
        qWarning().noquote()
            << QStringLiteral("Pulse-board register write failed: attempt %1/%2, device %3, reg %4, value %5, tx [%6], rx [%7], error: %8")
                   .arg(attempt)
                   .arg(kRegisterWriteAttempts)
                   .arg(deviceAddress)
                   .arg(hexWord(reg))
                   .arg(hexWord(value))
                   .arg(hexBytes(request))
                   .arg(hexBytes(response))
                   .arg(attemptError);
        PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
        if (attempt < kRegisterWriteAttempts) {
            Sleep(kRegisterRetryDelayMs);
        }
    }

    if (errorMessage) {
        *errorMessage = lastError.isEmpty()
                            ? QStringLiteral("Pulse-board register write failed.")
                            : lastError;
    }
    return false;
}

bool PulseGeneratorManager::writeRegister32LowWordFirst(void* rawHandle,
                                                        unsigned short deviceAddress,
                                                        unsigned short startReg,
                                                        quint32 value,
                                                        QString* errorMessage) const
{
    return writeRegister16(rawHandle, deviceAddress, startReg, static_cast<unsigned short>(value & 0xFFFFU), errorMessage) &&
           writeRegister16(rawHandle,
                           deviceAddress,
                           static_cast<unsigned short>(startReg + 1U),
                           static_cast<unsigned short>((value >> 16U) & 0xFFFFU),
                           errorMessage);
}

bool PulseGeneratorManager::openPort(const QString& portName, int baudRate, void** rawHandle, QString* errorMessage) const
{
    if (!rawHandle) {
        return false;
    }

    const QString deviceName = normalizePortName(portName);
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(deviceName.utf16()),
                                GENERIC_READ | GENERIC_WRITE,
                                0,
                                nullptr,
                                OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL,
                                nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to open pulse-board port %1, system error %2.")
                                .arg(portName, QString::number(GetLastError()));
        }
        return false;
    }

    if (!SetupComm(handle, 4096, 4096)) {
        closePort(handle);
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to initialize pulse-board serial buffer.");
        }
        return false;
    }

    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(handle, &dcb)) {
        closePort(handle);
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to read pulse-board serial state.");
        }
        return false;
    }

    dcb.BaudRate = static_cast<DWORD>(baudRate);
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    if (!SetCommState(handle, &dcb)) {
        closePort(handle);
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to configure pulse-board serial parameters.");
        }
        return false;
    }

    COMMTIMEOUTS timeouts = {};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.ReadTotalTimeoutConstant = 100;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 500;
    if (!SetCommTimeouts(handle, &timeouts)) {
        closePort(handle);
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to configure pulse-board serial timeout.");
        }
        return false;
    }

    PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    *rawHandle = handle;
    return true;
}

void PulseGeneratorManager::closePort(void* rawHandle) const
{
    if (rawHandle) {
        CloseHandle(static_cast<HANDLE>(rawHandle));
    }
}
