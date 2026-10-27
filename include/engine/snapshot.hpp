#pragma once

#include <sys/types.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "common/types.hpp"

namespace kvstore {

/**
 * @brief Manages database snapshots and point-in-time checkpoints.
 *
 * OS Concepts:
 * - Copy-On-Write (COW): fork(2) spawns a child process that writes snapshot memory to disk
 *   in the background without locking parent request processing.
 * - Atomic File Replacement: Writes to temporary file and invokes rename(2) for atomic swap.
 */
class SnapshotManager {
public:
    static constexpr uint32_t SNAPSHOT_MAGIC = 0x4B565353;  // "KVSS"
    static constexpr uint32_t SNAPSHOT_VERSION = 1;

    explicit SnapshotManager(std::string snapshot_path);
    ~SnapshotManager();

    /// Synchronously creates snapshot on disk and atomically swaps file.
    bool createSnapshot(uint64_t lsn, const std::vector<std::pair<std::string, Value>>& entries);

    /// Asynchronously creates snapshot in child process via fork() Copy-On-Write.
    pid_t bgSaveFork(uint64_t lsn, const std::vector<std::pair<std::string, Value>>& entries);

    /// Reaps and checks status of background BGSAVE child via waitpid(WNOHANG).
    bool checkBgSaveStatus(int& exit_code);

    /// Loads and validates snapshot file from disk during startup recovery.
    bool loadSnapshot(uint64_t& out_lsn, std::vector<std::pair<std::string, Value>>& out_entries);

    /// Checks if a valid snapshot file exists on disk.
    bool snapshotExists() const;

    const std::string& getPath() const { return snapshot_path_; }

private:
    std::string snapshot_path_;
    pid_t bgsave_pid_{-1};
};

}  // namespace kvstore
