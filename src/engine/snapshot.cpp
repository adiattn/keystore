#include "engine/snapshot.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

#include "common/checksum.hpp"

namespace kvstore {

#pragma pack(push, 1)
struct SnapshotHeader {
    uint32_t magic;
    uint32_t version;
    uint64_t lsn;
    uint64_t count;
};
#pragma pack(pop)

SnapshotManager::SnapshotManager(std::string snapshot_path) : snapshot_path_(std::move(snapshot_path)) {}

SnapshotManager::~SnapshotManager() {
    if (bgsave_pid_ > 0) {
        int status = 0;
        ::waitpid(bgsave_pid_, &status, WNOHANG);
    }
}

bool SnapshotManager::snapshotExists() const {
    struct stat st{};
    return ::stat(snapshot_path_.c_str(), &st) == 0 && st.st_size > 0;
}

bool SnapshotManager::createSnapshot(uint64_t lsn, const std::vector<std::pair<std::string, Value>>& entries) {
    std::string tmp_path = snapshot_path_ + ".tmp";
    int fd = ::open(tmp_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) return false;

    std::vector<uint8_t> buffer;

    // Header
    SnapshotHeader header{SNAPSHOT_MAGIC, SNAPSHOT_VERSION, lsn, static_cast<uint64_t>(entries.size())};
    const auto* h_bytes = reinterpret_cast<const uint8_t*>(&header);
    buffer.insert(buffer.end(), h_bytes, h_bytes + sizeof(header));

    // Payload
    for (const auto& [k, val] : entries) {
        uint32_t key_len = static_cast<uint32_t>(k.size());
        const auto* klen_bytes = reinterpret_cast<const uint8_t*>(&key_len);
        buffer.insert(buffer.end(), klen_bytes, klen_bytes + sizeof(key_len));
        buffer.insert(buffer.end(), k.begin(), k.end());
        val.serialize(buffer);
    }

    // CRC32 Checksum trailer
    uint32_t crc = Checksum::crc32(buffer.data(), buffer.size());
    const auto* crc_bytes = reinterpret_cast<const uint8_t*>(&crc);
    buffer.insert(buffer.end(), crc_bytes, crc_bytes + sizeof(crc));

    // Write, fsync, and atomic rename
    ssize_t written = ::write(fd, buffer.data(), buffer.size());
    ::fsync(fd);
    ::close(fd);

    if (written != static_cast<ssize_t>(buffer.size()) || ::rename(tmp_path.c_str(), snapshot_path_.c_str()) != 0) {
        ::unlink(tmp_path.c_str());
        return false;
    }
    return true;
}

pid_t SnapshotManager::bgSaveFork(uint64_t lsn, const std::vector<std::pair<std::string, Value>>& entries) {
    if (bgsave_pid_ > 0) {
        int status = 0;
        if (::waitpid(bgsave_pid_, &status, WNOHANG) == 0) return -1;  // Still active
        bgsave_pid_ = -1;
    }

    pid_t pid = ::fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        // Child: write snapshot using OS Copy-On-Write memory pages
        bool ok = createSnapshot(lsn, entries);
        ::_exit(ok ? 0 : 1);
    }

    bgsave_pid_ = pid;
    return pid;
}

bool SnapshotManager::checkBgSaveStatus(int& exit_code) {
    if (bgsave_pid_ <= 0) return false;

    int status = 0;
    pid_t res = ::waitpid(bgsave_pid_, &status, WNOHANG);
    if (res == 0) return true;  // Still running

    if (res == bgsave_pid_) {
        bgsave_pid_ = -1;
        exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        return false;
    }
    bgsave_pid_ = -1;
    return false;
}

bool SnapshotManager::loadSnapshot(uint64_t& out_lsn, std::vector<std::pair<std::string, Value>>& out_entries) {
    out_entries.clear();
    out_lsn = 0;

    int fd = ::open(snapshot_path_.c_str(), O_RDONLY);
    if (fd < 0) return false;

    struct stat st{};
    if (::fstat(fd, &st) != 0 || st.st_size < static_cast<off_t>(sizeof(SnapshotHeader) + sizeof(uint32_t))) {
        ::close(fd);
        return false;
    }

    std::vector<uint8_t> buffer(st.st_size);
    ssize_t bytes_read = ::read(fd, buffer.data(), st.st_size);
    ::close(fd);

    if (bytes_read != st.st_size) return false;

    // Verify CRC32
    size_t payload_len = buffer.size() - sizeof(uint32_t);
    uint32_t stored_crc = 0;
    std::memcpy(&stored_crc, buffer.data() + payload_len, sizeof(uint32_t));
    if (stored_crc != Checksum::crc32(buffer.data(), payload_len)) {
        std::cerr << "[Snapshot] Checksum mismatch! Corrupted snapshot.\n";
        return false;
    }

    SnapshotHeader header{};
    std::memcpy(&header, buffer.data(), sizeof(header));
    if (header.magic != SNAPSHOT_MAGIC || header.version != SNAPSHOT_VERSION) return false;

    size_t offset = sizeof(SnapshotHeader);
    out_entries.reserve(header.count);

    for (uint64_t i = 0; i < header.count; ++i) {
        if (offset + sizeof(uint32_t) > payload_len) return false;

        uint32_t key_len = 0;
        std::memcpy(&key_len, buffer.data() + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        if (offset + key_len > payload_len) return false;
        std::string key(reinterpret_cast<const char*>(buffer.data() + offset), key_len);
        offset += key_len;

        size_t val_bytes = 0;
        Value val = Value::deserialize(buffer.data() + offset, payload_len - offset, val_bytes);
        if (val_bytes == 0) return false;
        offset += val_bytes;

        out_entries.emplace_back(std::move(key), std::move(val));
    }

    out_lsn = header.lsn;
    return true;
}

}  // namespace kvstore
