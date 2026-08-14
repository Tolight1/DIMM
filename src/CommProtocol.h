#pragma once

#include <QByteArray>

#include <cstdint>

namespace CommProtocol {

constexpr int SOF_SIZE = 4;
constexpr int LEN_SIZE = 4;
constexpr int ADDRESS_SIZE = 6;
constexpr int MSG_TYPE_SIZE = 1;
constexpr int SEQ_SIZE = 4;
constexpr int TIMESTAMP_SIZE = 8;
constexpr int DATA_FLOAT_COUNT = 12;
constexpr int DATA_FLOAT_SIZE = DATA_FLOAT_COUNT * 4;
constexpr int DEVICE_STATUS_SIZE = 4;
constexpr int DATA_SIZE = DATA_FLOAT_SIZE + DEVICE_STATUS_SIZE;
constexpr int CRC_SIZE = 4;
constexpr int EOF_SIZE = 4;
constexpr int LEN_VALUE = ADDRESS_SIZE + ADDRESS_SIZE + MSG_TYPE_SIZE + SEQ_SIZE +
                          TIMESTAMP_SIZE + DATA_SIZE + CRC_SIZE + EOF_SIZE;
constexpr int FRAME_SIZE = SOF_SIZE + LEN_SIZE + LEN_VALUE;
constexpr uint8_t MSG_TYPE = 0x07;

constexpr std::uint32_t DEVICE_STATUS_NORMAL = 0x00000000u;
constexpr std::uint32_t DEVICE_STATUS_CAMERA_A_CONNECTION = 0x00000001u;
constexpr std::uint32_t DEVICE_STATUS_CAMERA_B_CONNECTION = 0x00000002u;
constexpr std::uint32_t DEVICE_STATUS_TRIGGER = 0x00000004u;
constexpr std::uint32_t DEVICE_STATUS_CAMERA_A_CAPTURE = 0x00000008u;
constexpr std::uint32_t DEVICE_STATUS_CAMERA_B_CAPTURE = 0x00000010u;
constexpr std::uint32_t DEVICE_STATUS_CAMERA_A_NO_STAR = 0x00000020u;
constexpr std::uint32_t DEVICE_STATUS_CAMERA_B_NO_STAR = 0x00000040u;
constexpr std::uint32_t DEVICE_STATUS_CAMERA_A_LOW_BRIGHTNESS = 0x00000080u;
constexpr std::uint32_t DEVICE_STATUS_CAMERA_B_LOW_BRIGHTNESS = 0x00000100u;
constexpr std::uint32_t DEVICE_STATUS_ENVIRONMENT_SENSOR = 0x00000200u;
constexpr std::uint32_t DEVICE_STATUS_EXPOSURE = 0x00000400u;
constexpr std::uint32_t DEVICE_STATUS_FRAME_RATE = 0x00000800u;
constexpr std::uint32_t DEVICE_STATUS_MEASUREMENT = 0x00001000u;
constexpr std::uint32_t DEVICE_STATUS_DATA_SAVE = 0x00002000u;

QByteArray buildData(float temperatureC,
                     float humidityRh,
                     float pressureHpa,
                     float r0,
                     float seeing,
                     float theta0,
                     float tau0,
                     float peakBrightnessCameraA,
                     float peakBrightnessCameraB,
                     float exposureTimeCameraAUs,
                     float exposureTimeCameraBUs,
                     float frameRateHz,
                     std::uint32_t deviceStatus);

QByteArray buildMonitoringFrame(std::uint32_t sequence,
                                std::uint64_t timestampMs,
                                const QByteArray& data);

std::uint32_t crc32IsoHdlc(const QByteArray& data);

}  // namespace CommProtocol
