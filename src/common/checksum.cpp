#include "common/checksum.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace kvstore {

namespace {
constexpr std::array<uint32_t, 256> generateCrc32Table() {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (uint32_t j = 0; j < 8; ++j) {
            crc = (crc & 1) ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
        }
        table[i] = crc;
    }
    return table;
}

constexpr auto CRC32_TABLE = generateCrc32Table();
}  // namespace

uint32_t Checksum::crc32(const void* data, size_t length, uint32_t previous_crc) {
    uint32_t crc = ~previous_crc;
    const auto* current = static_cast<const uint8_t*>(data);
    while (length--) {
        crc = (crc >> 8) ^ CRC32_TABLE[(crc & 0xFF) ^ *current++];
    }
    return ~crc;
}

uint32_t Checksum::update(uint32_t crc, const void* data, size_t length) {
    const auto* current = static_cast<const uint8_t*>(data);
    while (length--) {
        crc = (crc >> 8) ^ CRC32_TABLE[(crc & 0xFF) ^ *current++];
    }
    return crc;
}

}  // namespace kvstore
