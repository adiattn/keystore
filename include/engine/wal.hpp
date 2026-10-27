#pragma once

#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "common/checksum.hpp"
#include "common/types.hpp"

namespace kvstore {

/**
 * @brief WAL operation opcodes.
 */
enum class WalOpType : uint8_t {
    NONE = 0,
    SET = 1,        ///< Key-value insertion/update
    DEL = 2,        ///< Key deletion
    CLEAR = 3,      ///< Database flush
    CHECKPOINT = 4  ///< Snapshot sync point marker
};

std::string walOpTypeToString(WalOpType op);

/**
 * @brief Structured record in the Write-Ahead Log.
 * Wire Format: [Magic: 0xAA (1B)] [LSN (8B)] [Op (1B)] [KeyLen (4B)] [Key] [Value] [CRC32 (4B)]
 */
struct WalRecord {
    uint64_t lsn{0};
    WalOpType op{WalOpType::NONE};
    std::string key;
    Value value;
    uint32_t checksum{0};

    /// Encodes record into binary format with embedded CRC32 checksum.
    std::vector<uint8_t> encode() const;

    /// Decodes binary buffer into WalRecord, validating Magic byte and CRC32 checksum.
    static bool decode(const uint8_t* data, size_t size, WalRecord& out_record, size_t& bytes_consumed);
};

/**
 * @brief Write-Ahead Log manager for durability and crash recovery.
 *
 * OS & Storage Features:
 * - Append-Only Disk Logging: Sequential disk writes via POSIX open/write.
 * - Hardware Durability: Flushes OS page cache to disk via fdatasync(2).
 * - Crash Recovery: Scans CRC32 checksums on startup and trims partial writes via ftruncate(2).
 */
class WAL {
public:
    static constexpr uint8_t WAL_MAGIC = 0xAA;

    WAL(std::string wal_path, bool sync_on_write = true);
    ~WAL();

    /// Opens log file in append mode.
    bool open();

    /// Closes log file.
    void close();

    /// Appends an operation record to disk with Log Sequence Number (LSN) and CRC32.
    bool appendRecord(WalOpType op, uint64_t lsn, const std::string& key = "", const Value& val = Value{});

    bool appendSet(uint64_t lsn, const std::string& key, const Value& val) {
        return appendRecord(WalOpType::SET, lsn, key, val);
    }
    bool appendDel(uint64_t lsn, const std::string& key) { return appendRecord(WalOpType::DEL, lsn, key); }
    bool appendClear(uint64_t lsn) { return appendRecord(WalOpType::CLEAR, lsn); }

    /// Flushes dirty page cache to disk using fdatasync(2).
    void sync();

    /// Replays all valid records with LSN >= min_lsn; trims corrupted trailing bytes.
    std::vector<WalRecord> recover(uint64_t min_lsn = 0);

    /// Truncates WAL to 0 bytes after a snapshot checkpoint.
    bool truncate();

    const std::string& getPath() const { return wal_path_; }
    size_t getFileSize() const;

private:
    bool ensureOpenLocked();

    std::string wal_path_;
    bool sync_on_write_;
    int fd_{-1};
    mutable std::mutex write_mutex_;
};

}  // namespace kvstore
