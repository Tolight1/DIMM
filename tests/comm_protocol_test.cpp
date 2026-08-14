#include "CommProtocol.h"

#include <QByteArray>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace {

float readFloat32Be(const QByteArray& data, int offset)
{
    std::uint32_t bits = 0;
    for (int i = 0; i < 4; ++i) {
        bits = (bits << 8) | static_cast<unsigned char>(data.at(offset + i));
    }
    float value = 0.0f;
    static_assert(sizeof(value) == sizeof(bits));
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

}  // namespace

int main()
{
    assert(CommProtocol::crc32IsoHdlc(QByteArrayLiteral("123456789")) == 0xCBF43926u);

    const QByteArray data = CommProtocol::buildData(
        12.5f,
        45.25f,
        700.5f,
        10.0f,
        1.5f,
        2.5f,
        3.5f,
        88.0f,
        77.0f,
        1200.0f,
        1250.0f,
        200.0f,
        CommProtocol::DEVICE_STATUS_CAMERA_B_LOW_BRIGHTNESS);
    assert(data.size() == CommProtocol::DATA_SIZE);
    assert(readFloat32Be(data, 0) == 12.5f);
    assert(readFloat32Be(data, 4) == 45.25f);
    assert(readFloat32Be(data, 8) == 700.5f);
    assert(readFloat32Be(data, 12) == 10.0f);
    assert(readFloat32Be(data, 16) == 1.5f);
    assert(readFloat32Be(data, 20) == 2.5f);
    assert(readFloat32Be(data, 24) == 3.5f);
    assert(readFloat32Be(data, 28) == 88.0f);
    assert(readFloat32Be(data, 32) == 77.0f);
    assert(readFloat32Be(data, 36) == 1200.0f);
    assert(readFloat32Be(data, 40) == 1250.0f);
    assert(readFloat32Be(data, 44) == 200.0f);
    assert(data.mid(48, 4) == QByteArray::fromHex("00000100"));

    const QByteArray invalidData = CommProtocol::buildData(
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN(),
        0x00001000u);
    for (int offset = 0; offset < CommProtocol::DATA_FLOAT_SIZE; offset += 4) {
        assert(std::isnan(readFloat32Be(invalidData, offset)));
    }
    assert(invalidData.mid(48, 4) == QByteArray::fromHex("00001000"));

    const QByteArray frame = CommProtocol::buildMonitoringFrame(0x01020304u,
                                                                  0x0102030405060708ULL,
                                                                  data);
    assert(frame.size() == CommProtocol::FRAME_SIZE);
    assert(frame.left(4) == QByteArray::fromHex("499602D2"));
    assert(frame.mid(4, 4) == QByteArray::fromHex("00000055"));
    assert(frame.mid(8, 6) == QByteArray::fromHex("010303020000"));
    assert(frame.mid(14, 6) == QByteArray::fromHex("010303050000"));
    assert(static_cast<unsigned char>(frame[20]) == 0x07u);
    assert(frame.mid(21, 4) == QByteArray::fromHex("01020304"));
    assert(frame.mid(25, 8) == QByteArray::fromHex("0102030405060708"));
    assert(frame.right(4) == QByteArray::fromHex("B669FD2E"));

    const QByteArray crcInput = frame.mid(4, 4 + 6 + 6 + 1 + 4 + 8 + 52);
    const std::uint32_t expectedCrc = CommProtocol::crc32IsoHdlc(crcInput);
    assert(frame.mid(85, 4) == QByteArray::fromHex(
                                QByteArray::number(expectedCrc, 16).rightJustified(8, '0')));
    return 0;
}
