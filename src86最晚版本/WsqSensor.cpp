#include "WsqSensor.h"

#include "ModbusRtu.h"

#include <vector>

namespace wsq {
namespace {

constexpr std::uint16_t kStartRegister = 0x0000;
constexpr std::uint16_t kRegisterCount = 3;
constexpr double kScale = 0.1;

int signed16(std::uint16_t value) {
    if (value >= 0x8000) {
        return static_cast<int>(value) - 0x10000;
    }
    return static_cast<int>(value);
}

}  // namespace

WsqSensor::WsqSensor(ISerialPort& serial, std::uint8_t deviceAddress)
    : serial_(serial), deviceAddress_(deviceAddress) {}

bool WsqSensor::readData(WsqSensorData& data, std::string& error) {
    error.clear();

    const std::vector<std::uint8_t> request =
        buildReadHoldingRegistersRequest(deviceAddress_, kStartRegister, kRegisterCount);
    if (!serial_.writeAll(request, error)) {
        if (error.empty()) {
            error = "Failed to write WSQ request";
        }
        return false;
    }

    std::vector<std::uint8_t> header;
    if (!serial_.readExact(3, header, error)) {
        if (error.empty()) {
            error = "Failed to read WSQ response header";
        }
        return false;
    }

    std::size_t tailSize = 2;
    if ((header[1] & 0x80) == 0) {
        tailSize = static_cast<std::size_t>(header[2]) + 2U;
    }

    std::vector<std::uint8_t> tail;
    if (!serial_.readExact(tailSize, tail, error)) {
        if (error.empty()) {
            error = "Failed to read WSQ response body";
        }
        return false;
    }

    std::vector<std::uint8_t> frame;
    frame.reserve(header.size() + tail.size());
    frame.insert(frame.end(), header.begin(), header.end());
    frame.insert(frame.end(), tail.begin(), tail.end());

    std::vector<std::uint16_t> registers;
    if (!parseReadHoldingRegistersResponse(frame, deviceAddress_, kRegisterCount, registers, error)) {
        return false;
    }

    if (registers.size() != kRegisterCount) {
        error = "WSQ response did not contain temperature, humidity, and pressure";
        return false;
    }

    data.temperatureC = static_cast<double>(signed16(registers[0])) * kScale;
    data.humidityRh = static_cast<double>(registers[1]) * kScale;
    data.pressureHpa = static_cast<double>(registers[2]) * kScale;
    return true;
}

}  // namespace wsq
