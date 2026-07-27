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
    const ULONGLONG deadline = GetTickCount64() + 1000;
    while (result->size() < expectedBytes) {
        if (GetTickCount64() >= deadline) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Timed out waiting for pulse-board response.");
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

bool PulseGeneratorManager::applyConfig(const Config& config, QString* errorMessage)
{
    if (!config.enabled) {
        const bool shouldStopOutput = m_running || m_config.enabled;
        m_config = config;
        if (!shouldStopOutput) {
            m_running = false;
            return true;
        }
        const bool stopped = stop(errorMessage);
        m_running = false;
        return stopped;
    }

    m_config = config;
    m_running = false;

    if (config.portName.trimmed().isEmpty()) {
        return false;
    }
    return configureDevice(config, false, errorMessage);
}

bool PulseGeneratorManager::configureAndStart(const Config& config, QString* errorMessage)
{
    if (!config.enabled) {
        return applyConfig(config, errorMessage);
    }
    if (!configureDevice(config, true, errorMessage)) {
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
    if (m_config.portName.trimmed().isEmpty()) {
        m_running = false;
        return true;
    }

    void* rawHandle = nullptr;
    if (!openPort(m_config.portName, m_config.baudRate, &rawHandle, errorMessage)) {
        m_running = false;
        return false;
    }

    HANDLE handle = static_cast<HANDLE>(rawHandle);
    const unsigned short deviceAddress = static_cast<unsigned short>(m_config.terminalId & 0x00FF);
    const bool success = writeRegister16(handle, deviceAddress, kRegOutputEnable, 0x0000, errorMessage);
    closePort(handle);
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

    if (!writeAll(handle, request, errorMessage)) {
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("Failed to write pulse-board register.");
        }
        return false;
    }

    std::vector<unsigned char> response;
    if (!readExact(handle, 8, &response, errorMessage)) {
        return false;
    }
    if (!hasValidCrc(response) || response != request) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Pulse-board response CRC check failed.");
        }
        return false;
    }
    return true;
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
