#pragma once

#include "SerialPort.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <string>

namespace wsq {

std::string makeWindowsPortPath(const std::string& portName);

class SerialPortWin final : public ISerialPort {
public:
    SerialPortWin() = default;
    ~SerialPortWin() override;

    SerialPortWin(const SerialPortWin&) = delete;
    SerialPortWin& operator=(const SerialPortWin&) = delete;

    bool open(const SerialConfig& config, std::string& error) override;
    void close() override;
    bool isOpen() const override;
    bool writeAll(const std::vector<std::uint8_t>& bytes, std::string& error) override;
    bool readExact(std::size_t count, std::vector<std::uint8_t>& bytes, std::string& error) override;

private:
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#endif
};

}  // namespace wsq
