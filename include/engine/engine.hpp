#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "common/protocol.hpp"
#include "common/types.hpp"
#include "engine/hash_index.hpp"
#include "engine/snapshot.hpp"
#include "engine/wal.hpp"

namespace kvstore {

/**
 * @brief Diagnostic telemetry and runtime performance statistics.
 */
struct EngineStats {
    size_t key_count{0};             ///< Current active keys in memory
    uint64_t current_lsn{0};         ///< Highest Log Sequence Number committed
    size_t wal_size_bytes{0};        ///< Size of kvstore.wal in bytes
    bool snapshot_exists{false};     ///< Whether kvstore.snapshot is present
    bool bgsave_in_progress{false};  ///< Whether background BGSAVE is running
};

/**
 * @brief Main Database Storage Engine.
 * Coordinates in-memory sharded indexing, append-only WAL logging,
 * and point-in-time snapshot recovery.
 */
class KVEngine {
public:
    explicit KVEngine(std::string data_dir = "./data", bool sync_wal = true);
    ~KVEngine();

    /// Initializes storage, runs crash recovery, and opens WAL.
    bool open();

    /// Flushes dirty pages and closes storage engine cleanly.
    void close();

    // ##### Key-Value Operations #####

    /// Commits key-value to WAL and updates in-memory index.
    bool set(const std::string& key, Value val);

    /// Reads value directly from in-memory index under shared read lock.
    std::optional<Value> get(const std::string& key) const;

    /// Deletes key, logging DEL to WAL.
    bool del(const std::string& key);

    /// Checks if key exists.
    bool exists(const std::string& key) const;

    /// Returns data type of key.
    DataType type(const std::string& key) const;

    // ##### Atomic Operations #####

    /// Atomically increments integer key and updates WAL.
    std::optional<int64_t> incrByInt(const std::string& key, int64_t delta);

    /// Atomically increments float key and updates WAL.
    std::optional<double> incrByFloat(const std::string& key, double delta);

    /// Atomic Compare-And-Swap with WAL persistence.
    bool compareAndSwap(const std::string& key, const Value& expected, const Value& desired);

    // ##### Key Management #####

    /// Lists keys matching pattern (wildcard support).
    std::vector<std::string> getKeys(const std::string& pattern = "*") const;

    /// Returns total active key count.
    size_t size() const;

    /// Flushes all keys and logs CLEAR to WAL.
    void flush();

    // ##### Persistence & Checkpointing #####

    /// Synchronously creates snapshot and truncates WAL.
    bool save();

    /// Asynchronously creates snapshot via fork() Copy-On-Write.
    bool bgSave();

    /// Checks status of background snapshot child.
    bool checkBgSave(int& exit_code);

    // ##### Command Dispatch & Telemetry #####

    /// Parses and executes Redis command line arguments (e.g. {"SET", "k", "v"}).
    Response executeCommand(const std::vector<std::string>& args);

    /// Returns engine runtime performance statistics.
    EngineStats getStats() const;

private:
    void recover();

    std::string data_dir_;
    bool sync_wal_;
    std::string wal_path_;
    std::string snapshot_path_;

    HashIndex index_;
    std::unique_ptr<WAL> wal_;
    std::unique_ptr<SnapshotManager> snapshot_mgr_;

    std::atomic<uint64_t> current_lsn_{0};
    mutable std::mutex engine_mutex_;
};

}  // namespace kvstore
