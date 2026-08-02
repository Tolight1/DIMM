#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wsq {

enum class SerialParity {
    None,
    Even,
    Odd,
};

struct SerialConfig {
    std::string portName = "COM5";
    int baudRate = 9600;
    int dataBits = 8;
    int stopBits = 1;
    SerialParity parity = SerialParity::None;
    int readTimeoutMs = 500;
    int writeTimeoutMs = 500;
};

class ISerialPort {
public:
    virtual ~ISerialPort() = default;
    virtual bool open(const SerialConfig& config, std::string& error) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual bool writeAll(const std::vector<std::uint8_t>& bytes, std::string& error) = 0;
    virtual bool readExact(std::size_t count, std::vector<std::uint8_t>& bytes, std::string& error) = 0;
};

}  // namespace wsq
