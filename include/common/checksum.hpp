#pragma once

#include <cstddef>
#include <cstdint>

namespace kvstore {

/**
 * @brief Computes standard IEEE 802.3 CRC32 checksums.
 * Used to detect data corruption in Write-Ahead Log (WAL) records and snapshots.
 */
class Checksum {
public:
    /**
     * @brief Computes CRC32 checksum over a memory buffer.
     * @param data Pointer to input data bytes.
     * @param length Size of data in bytes.
     * @param previous_crc Running seed for streaming calculations (default: 0).
     */
    static uint32_t crc32(const void* data, size_t length, uint32_t previous_crc = 0);

    /**
     * @brief Incrementally updates an existing CRC32 checksum with new data.
     */
    static uint32_t update(uint32_t crc, const void* data, size_t length);
};

}  // namespace kvstore
