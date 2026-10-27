#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/types.hpp"

namespace kvstore {

/**
 * @brief High-performance sharded concurrent in-memory hash index.
 *
 * OS & Concurrency Design:
 * - Sharded Architecture: Partitions keys across N independent shards (default: 32)
 *   to minimize lock contention across CPU cores.
 * - Read-Write Locks: Uses std::shared_mutex per shard. Multiple readers run concurrently
 *   under shared_lock, while writers acquire an exclusive unique_lock on the target shard only.
 */
class HashIndex {
public:
    /**
     * @brief Constructs hash index with N independent shards.
     * @param num_shards Number of shards (default: 32).
     */
    explicit HashIndex(size_t num_shards = 32);
    ~HashIndex() = default;

    // ##### Key-Value Operations #####

    /// Inserts or updates key-value pair under shard write lock.
    void set(const std::string& key, Value val);

    /// Reads value for key under shard shared read lock (zero copy contention).
    std::optional<Value> get(const std::string& key) const;

    /// Deletes key if present under shard write lock.
    bool del(const std::string& key);

    /// Checks key existence under shard read lock.
    bool exists(const std::string& key) const;

    /// Returns data type of key under shard read lock.
    DataType type(const std::string& key) const;

    // ##### Atomic Operations #####

    /// Atomically increments integer value by delta under shard write lock.
    std::optional<int64_t> incrByInt(const std::string& key, int64_t delta);

    /// Atomically increments float value by delta under shard write lock.
    std::optional<double> incrByFloat(const std::string& key, double delta);

    /// Atomic Compare-And-Swap (updates to desired if current equals expected).
    bool compareAndSwap(const std::string& key, const Value& expected, const Value& desired);

    // ##### Key Scanning & Snapshots #####

    /// Returns keys matching wildcard pattern (e.g. "user:*") using fnmatch(3).
    std::vector<std::string> getKeys(const std::string& pattern = "*") const;

    /// Returns total key count across all shards.
    size_t size() const;

    /// Clears all keys across all shards.
    void clear();

    /// Exports all key-value entries across all shards for checkpointing.
    std::vector<std::pair<std::string, Value>> getAllEntries() const;

    /// Bulk loads entries during startup crash recovery.
    void loadEntries(const std::vector<std::pair<std::string, Value>>& entries);

private:
    struct Shard {
        mutable std::shared_mutex mutex;
        std::unordered_map<std::string, Value> map;
    };

    size_t getShardIndex(const std::string& key) const;

    size_t num_shards_;
    std::vector<std::unique_ptr<Shard>> shards_;
};

}  // namespace kvstore
