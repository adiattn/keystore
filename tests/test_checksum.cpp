#include <cassert>
#include <cstring>
#include <iostream>

#include "common/checksum.hpp"

int main() {
    std::cout << "[Test] Running Checksum CRC32 tests...\n";

    // Test 1: Empty string CRC32 should be 0
    const char* empty_str = "";
    uint32_t crc_empty = kvstore::Checksum::crc32(empty_str, 0);
    assert(crc_empty == 0x00000000);

    // Test 2: Known test vector "123456789" -> standard IEEE 802.3 CRC-32 is 0xCBF43926
    const char* vector1 = "123456789";
    uint32_t crc1 = kvstore::Checksum::crc32(vector1, std::strlen(vector1));
    assert(crc1 == 0xCBF43926);

    // Test 3: Incremental / streaming update matches one-shot computation
    const char* part1 = "12345";
    const char* part2 = "6789";
    uint32_t stream_crc = kvstore::Checksum::crc32(part1, std::strlen(part1));
    stream_crc = kvstore::Checksum::crc32(part2, std::strlen(part2), stream_crc);
    assert(stream_crc == crc1);

    // Test 4: Checksum difference on single bit flip
    const char* mod_vector = "123456788";
    uint32_t crc_mod = kvstore::Checksum::crc32(mod_vector, std::strlen(mod_vector));
    assert(crc1 != crc_mod);

    std::cout << "[Test] Checksum CRC32 tests PASSED!\n";
    return 0;
}
