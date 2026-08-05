#include "SerialPortWin.h"

#include <sstream>

namespace wsq {
namespace {

#ifdef _WIN32

std::string lastWindowsError(const std::string& prefix) {
    const DWORD code = GetLastError();
    LPSTR buffer = nullptr;
    const DWORD size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buffer),
        0,
        nullptr);

    std::ostringstream oss;
    oss << prefix << " (Windows error " << code << ")";
    if (size != 0 && buffer != nullptr) {
        oss << ": " << buffer;
        LocalFree(buffer);
    }
    return oss.str();
}

BYTE stopBitsFromConfig(int stopBits) {
    return stopBits == 2 ? TWOSTOPBITS : ONESTOPBIT;
}

BYTE parityFromConfig(SerialParity parity) {
    switch (parity) {
    case SerialParity::Even:
        return EVENPARITY;
    case SerialParity::Odd:
        return ODDPARITY;
    case SerialParity::None:
    default:
        return NOPARITY;
    }
}

#endif

}  // namespace

std::string makeWindowsPortPath(const std::string& portName) {
    if (portName.rfind("\\\\.\\", 0) == 0) {
        return portName;
    }
    return "\\\\.\\" + portName;
}

SerialPortWin::~SerialPortWin() {
    close();
}

bool SerialPortWin::open(const SerialConfig& config, std::string& error) {
    error.clear();
#ifndef _WIN32
    error = "SerialPortWin is only available on Windows";
    return false;
#else
    close();

    const std::string path = makeWindowsPortPath(config.portName);
    handle_ = CreateFileA(
        path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
        error = lastWindowsError("Failed to open serial port " + config.portName);
        return false;
    }

    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(handle_, &dcb)) {
        error = lastWindowsError("Failed to read serial port state");
        close();
        return false;
    }

    dcb.BaudRate = static_cast<DWORD>(config.baudRate);
    dcb.ByteSize = static_cast<BYTE>(config.dataBits);
    dcb.Parity = parityFromConfig(config.parity);
    dcb.StopBits = stopBitsFromConfig(config.stopBits);
    dcb.fBinary = TRUE;
    dcb.fParity = config.parity != SerialParity::None ? TRUE : FALSE;

    if (!SetCommState(handle_, &dcb)) {
        error = lastWindowsError("Failed to configure serial port");
        close();
        return false;
    }

    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = static_cast<DWORD>(config.readTimeoutMs);
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = static_cast<DWORD>(config.readTimeoutMs);
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = static_cast<DWORD>(config.writeTimeoutMs);
    if (!SetCommTimeouts(handle_, &timeouts)) {
        error = lastWindowsError("Failed to configure serial port timeouts");
        close();
        return false;
    }

    SetupComm(handle_, 1024, 1024);
    PurgeComm(handle_, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return true;
#endif
}

void SerialPortWin::close() {
#ifdef _WIN32
    if (handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
#endif
}

bool SerialPortWin::isOpen() const {
#ifdef _WIN32
    return handle_ != INVALID_HANDLE_VALUE;
#else
    return false;
#endif
}

bool SerialPortWin::writeAll(const std::vector<std::uint8_t>& bytes, std::string& error) {
    error.clear();
#ifndef _WIN32
    error = "SerialPortWin is only available on Windows";
    return false;
#else
    if (!isOpen()) {
        error = "Serial port is not open";
        return false;
    }

    std::size_t totalWritten = 0;
    while (totalWritten < bytes.size()) {
        DWORD written = 0;
        const DWORD chunkSize = static_cast<DWORD>(bytes.size() - totalWritten);
        if (!WriteFile(handle_, bytes.data() + totalWritten, chunkSize, &written, nullptr)) {
            error = lastWindowsError("Failed to write serial port");
            return false;
        }
        if (written == 0) {
            error = "Serial write timed out";
            return false;
        }
        totalWritten += written;
    }
    return true;
#endif
}

bool SerialPortWin::readExact(std::size_t count, std::vector<std::uint8_t>& bytes, std::string& error) {
    error.clear();
    bytes.clear();
#ifndef _WIN32
    error = "SerialPortWin is only available on Windows";
    return false;
#else
    if (!isOpen()) {
        error = "Serial port is not open";
        return false;
    }

    bytes.resize(count);
    std::size_t totalRead = 0;
    while (totalRead < count) {
        DWORD read = 0;
        const DWORD chunkSize = static_cast<DWORD>(count - totalRead);
        if (!ReadFile(handle_, bytes.data() + totalRead, chunkSize, &read, nullptr)) {
            error = lastWindowsError("Failed to read serial port");
            bytes.clear();
            return false;
        }
        if (read == 0) {
            error = "Serial read timed out before receiving expected bytes";
            bytes.clear();
            return false;
        }
        totalRead += read;
    }
    return true;
#endif
}

}  // namespace wsq
