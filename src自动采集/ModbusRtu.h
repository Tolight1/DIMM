#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wsq {

std::uint16_t crc16(const std::uint8_t* data, std::size_t size);

std::vector<std::uint8_t> buildReadHoldingRegistersRequest(
    std::uint8_t deviceAddress,
    std::uint16_t startRegister,
    std::uint16_t registerCount);

bool parseReadHoldingRegistersResponse(
    const std::vector<std::uint8_t>& frame,
    std::uint8_t expectedAddress,
    std::uint16_t expectedRegisterCount,
    std::vector<std::uint16_t>& registers,
    std::string& error);

}  // namespace wsq
