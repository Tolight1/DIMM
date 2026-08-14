#include "CommProtocol.h"

#include <array>
#include <cstring>

namespace {

constexpr std::array<std::uint8_t, 4> kSof = {0x49, 0x96, 0x02, 0xD2};
constexpr std::array<std::uint8_t, 6> kDestination = {0x01, 0x03, 0x03, 0x02, 0x00, 0x00};
constexpr std::array<std::uint8_t, 6> kSource = {0x01, 0x03, 0x03, 0x05, 0x00, 0x00};
constexpr std::array<std::uint8_t, 4> kEof = {0xB6, 0x69, 0xFD, 0x2E};

void appendU32(QByteArray* output, std::uint32_t value)
{
    output->append(static_cast<char>((value >> 24) & 0xFF));
    output->append(static_cast<char>((value >> 16) & 0xFF));
    output->append(static_cast<char>((value >> 8) & 0xFF));
    output->append(static_cast<char>(value & 0xFF));
}

void appendU64(QByteArray* output, std::uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8) {
        output->append(static_cast<char>((value >> shift) & 0xFF));
    }
}

void appendFloat32(QByteArray* output, float value)
{
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    appendU32(output, bits);
}

template <std::size_t N>
void appendBytes(QByteArray* output, const std::array<std::uint8_t, N>& bytes)
{
    for (const std::uint8_t byte : bytes) {
        output->append(static_cast<char>(byte));
    }
}

}  // namespace

namespace CommProtocol {

std::uint32_t crc32IsoHdlc(const QByteArray& data)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const char byte : data) {
        crc ^= static_cast<std::uint8_t>(byte);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1u) != 0u ? (crc >> 1u) ^ 0xEDB88320u : crc >> 1u;
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

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
                     std::uint32_t deviceStatus)
{
    QByteArray data;
    data.reserve(DATA_SIZE);
    appendFloat32(&data, temperatureC);
    appendFloat32(&data, humidityRh);
    appendFloat32(&data, pressureHpa);
    appendFloat32(&data, r0);
    appendFloat32(&data, seeing);
    appendFloat32(&data, theta0);
    appendFloat32(&data, tau0);
    appendFloat32(&data, peakBrightnessCameraA);
    appendFloat32(&data, peakBrightnessCameraB);
    appendFloat32(&data, exposureTimeCameraAUs);
    appendFloat32(&data, exposureTimeCameraBUs);
    appendFloat32(&data, frameRateHz);
    appendU32(&data, deviceStatus);
    return data;
}

QByteArray buildMonitoringFrame(std::uint32_t sequence,
                                std::uint64_t timestampMs,
                                const QByteArray& data)
{
    if (data.size() != DATA_SIZE) {
        return QByteArray();
    }

    QByteArray body;
    body.reserve(LEN_VALUE - EOF_SIZE);
    appendBytes(&body, kDestination);
    appendBytes(&body, kSource);
    body.append(static_cast<char>(MSG_TYPE));
    appendU32(&body, sequence);
    appendU64(&body, timestampMs);
    body.append(data);

    QByteArray crcInput;
    crcInput.reserve(LEN_SIZE + body.size());
    appendU32(&crcInput, static_cast<std::uint32_t>(LEN_VALUE));
    crcInput.append(body);

    QByteArray frame;
    frame.reserve(FRAME_SIZE);
    appendBytes(&frame, kSof);
    appendU32(&frame, static_cast<std::uint32_t>(LEN_VALUE));
    frame.append(body);
    appendU32(&frame, crc32IsoHdlc(crcInput));
    appendBytes(&frame, kEof);
    return frame;
}

}  // namespace CommProtocol
