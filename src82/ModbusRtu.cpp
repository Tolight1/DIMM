#include "ModbusRtu.h"

#include <sstream>

namespace wsq {
namespace {

constexpr std::uint8_t kReadHoldingRegisters = 0x03;

void appendCrc(std::vector<std::uint8_t>& frame) {
    const std::uint16_t crc = crc16(frame.data(), frame.size());
    frame.push_back(static_cast<std::uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<std::uint8_t>((crc >> 8) & 0xFF));
}

std::string hexByte(std::uint8_t value) {
    const char* digits = "0123456789ABCDEF";
    std::string text = "0x";
    text.push_back(digits[(value >> 4) & 0x0F]);
    text.push_back(digits[value & 0x0F]);
    return text;
}

}  // namespace

std::uint16_t crc16(const std::uint8_t* data, std::size_t size) {
    std::uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 0x0001) != 0) {
                crc = static_cast<std::uint16_t>((crc >> 1) ^ 0xA001);
            } else {
                crc = static_cast<std::uint16_t>(crc >> 1);
            }
        }
    }
    return crc;
}

std::vector<std::uint8_t> buildReadHoldingRegistersRequest(
    std::uint8_t deviceAddress,
    std::uint16_t startRegister,
    std::uint16_t registerCount) {
    std::vector<std::uint8_t> frame{
        deviceAddress,
        kReadHoldingRegisters,
        static_cast<std::uint8_t>((startRegister >> 8) & 0xFF),
        static_cast<std::uint8_t>(startRegister & 0xFF),
        static_cast<std::uint8_t>((registerCount >> 8) & 0xFF),
        static_cast<std::uint8_t>(registerCount & 0xFF),
    };
    appendCrc(frame);
    return frame;
}

bool parseReadHoldingRegistersResponse(
    const std::vector<std::uint8_t>& frame,
    std::uint8_t expectedAddress,
    std::uint16_t expectedRegisterCount,
    std::vector<std::uint16_t>& registers,
    std::string& error) {
    registers.clear();
    error.clear();

    if (frame.size() < 5) {
        error = "Modbus response is shorter than the minimum frame length";
        return false;
    }

    const std::uint16_t expectedCrc =
        static_cast<std::uint16_t>(frame[frame.size() - 2]) |
        static_cast<std::uint16_t>(frame[frame.size() - 1] << 8);
    const std::uint16_t actualCrc = crc16(frame.data(), frame.size() - 2);
    if (actualCrc != expectedCrc) {
        std::ostringstream oss;
        oss << "CRC mismatch: expected frame CRC 0x" << std::hex << expectedCrc
            << ", calculated 0x" << actualCrc;
        error = oss.str();
        return false;
    }

    if (frame[0] != expectedAddress) {
        error = "Response address mismatch: got " + hexByte(frame[0]) +
                ", expected " + hexByte(expectedAddress);
        return false;
    }

    if ((frame[1] & 0x80) != 0) {
        error = "Modbus exception response: function " + hexByte(frame[1]) +
                ", exception code " + hexByte(frame[2]);
        return false;
    }

    if (frame[1] != kReadHoldingRegisters) {
        error = "Response function code mismatch: got " + hexByte(frame[1]);
        return false;
    }

    const std::size_t expectedByteCount =
        static_cast<std::size_t>(expectedRegisterCount) * 2U;
    if (frame[2] != expectedByteCount) {
        error = "Response byte count mismatch";
        return false;
    }

    const std::size_t expectedFrameSize = 3U + expectedByteCount + 2U;
    if (frame.size() != expectedFrameSize) {
        error = "Response frame length mismatch";
        return false;
    }

    registers.reserve(expectedRegisterCount);
    for (std::size_t i = 0; i < expectedByteCount; i += 2U) {
        const std::uint16_t value =
            static_cast<std::uint16_t>(frame[3U + i] << 8) |
            static_cast<std::uint16_t>(frame[4U + i]);
        registers.push_back(value);
    }

    if (registers.size() != expectedRegisterCount) {
        error = "Register count mismatch";
        registers.clear();
        return false;
    }

    return true;
}

}  // namespace wsq
