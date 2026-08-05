#pragma once

#include "SerialPort.h"

#include <cstdint>
#include <string>

namespace wsq {

struct WsqSensorData {
    double temperatureC = 0.0;
    double humidityRh = 0.0;
    double pressureHpa = 0.0;
};

class WsqSensor {
public:
    WsqSensor(ISerialPort& serial, std::uint8_t deviceAddress = 1);

    bool readData(WsqSensorData& data, std::string& error);

private:
    ISerialPort& serial_;
    std::uint8_t deviceAddress_;
};

}  // namespace wsq
