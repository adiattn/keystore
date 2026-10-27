#include "engine/wal.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

namespace kvstore {

std::string walOpTypeToString(WalOpType op) {
    switch (op) {
        case WalOpType::SET:
            return "SET";
        case WalOpType::DEL:
            return "DEL";
        case WalOpType::CLEAR:
            return "CLEAR";
        case WalOpType::CHECKPOINT:
            return "CHECKPOINT";
        default:
            return "NONE";
    }
}

std::vector<uint8_t> WalRecord::encode() const {
    std::vector<uint8_t> buffer;
    buffer.push_back(WAL::WAL_MAGIC);

    // Payload: [LSN (8B)] [Op (1B)] [KeyLen (4B)] [KeyData] [Value...]
    size_t payload_start = buffer.size();
    const uint8_t* lsn_bytes = reinterpret_cast<const uint8_t*>(&lsn);
    buffer.insert(buffer.end(), lsn_bytes, lsn_bytes + sizeof(lsn));
    buffer.push_back(static_cast<uint8_t>(op));

    uint32_t key_len = static_cast<uint32_t>(key.size());
    const uint8_t* key_len_bytes = reinterpret_cast<const uint8_t*>(&key_len);
    buffer.insert(buffer.end(), key_len_bytes, key_len_bytes + sizeof(key_len));
    buffer.insert(buffer.end(), key.begin(), key.end());

    value.serialize(buffer);

    // Checksum over payload
    uint32_t crc = Checksum::crc32(buffer.data() + payload_start, buffer.size() - payload_start);
    const uint8_t* crc_bytes = reinterpret_cast<const uint8_t*>(&crc);
    buffer.insert(buffer.end(), crc_bytes, crc_bytes + sizeof(crc));

    return buffer;
}

bool WalRecord::decode(const uint8_t* data, size_t size, WalRecord& out_record, size_t& bytes_consumed) {
    bytes_consumed = 0;
    if (size < 23 || data[0] != WAL::WAL_MAGIC) return false;

    size_t offset = 1;
    size_t payload_start = offset;

    uint64_t lsn = 0;
    std::memcpy(&lsn, data + offset, sizeof(uint64_t));
    offset += sizeof(uint64_t);

    WalOpType op = static_cast<WalOpType>(data[offset++]);

    uint32_t key_len = 0;
    std::memcpy(&key_len, data + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    if (offset + key_len > size) return false;
    std::string key(reinterpret_cast<const char*>(data + offset), key_len);
    offset += key_len;

    size_t val_bytes = 0;
    Value val = Value::deserialize(data + offset, size - offset, val_bytes);
    if (val_bytes == 0 && op == WalOpType::SET) return false;
    offset += val_bytes;

    if (offset + sizeof(uint32_t) > size) return false;

    uint32_t stored_crc = 0;
    std::memcpy(&stored_crc, data + offset, sizeof(uint32_t));
    uint32_t computed_crc = Checksum::crc32(data + payload_start, offset - payload_start);

    if (stored_crc != computed_crc) return false;  // Corrupted record

    offset += sizeof(uint32_t);
    bytes_consumed = offset;

    out_record = {lsn, op, std::move(key), std::move(val), stored_crc};
    return true;
}

WAL::WAL(std::string wal_path, bool sync_on_write) : wal_path_(std::move(wal_path)), sync_on_write_(sync_on_write) {}

WAL::~WAL() {
    close();
}

bool WAL::ensureOpenLocked() {
    if (fd_ >= 0) return true;
    fd_ = ::open(wal_path_.c_str(), O_CREAT | O_RDWR | O_APPEND, 0644);
    return fd_ >= 0;
}

bool WAL::open() {
    std::lock_guard<std::mutex> lock(write_mutex_);
    return ensureOpenLocked();
}

void WAL::close() {
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool WAL::appendRecord(WalOpType op, uint64_t lsn, const std::string& key, const Value& val) {
    WalRecord rec{lsn, op, key, val, 0};
    std::vector<uint8_t> encoded = rec.encode();

    std::lock_guard<std::mutex> lock(write_mutex_);
    if (fd_ < 0 && !ensureOpenLocked()) return false;

    ssize_t written = ::write(fd_, encoded.data(), encoded.size());
    if (written != static_cast<ssize_t>(encoded.size())) return false;

    if (sync_on_write_) ::fdatasync(fd_);
    return true;
}

void WAL::sync() {
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (fd_ >= 0) ::fdatasync(fd_);
}

std::vector<WalRecord> WAL::recover(uint64_t min_lsn) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    std::vector<WalRecord> recovered;

    int read_fd = ::open(wal_path_.c_str(), O_RDWR, 0644);
    if (read_fd < 0) return recovered;

    struct stat st{};
    if (::fstat(read_fd, &st) != 0 || st.st_size == 0) {
        ::close(read_fd);
        return recovered;
    }

    std::vector<uint8_t> file_data(st.st_size);
    if (::read(read_fd, file_data.data(), st.st_size) != st.st_size) {
        ::close(read_fd);
        return recovered;
    }

    size_t offset = 0, last_valid_offset = 0;
    while (offset < file_data.size()) {
        WalRecord rec;
        size_t consumed = 0;
        if (!WalRecord::decode(file_data.data() + offset, file_data.size() - offset, rec, consumed)) {
            std::cerr << "[WAL Recovery] Detected corrupted trailing record at offset " << offset << "/"
                      << file_data.size() << ". Truncating WAL via ftruncate().\n";
            ::ftruncate(read_fd, last_valid_offset);
            break;
        }

        if (rec.lsn >= min_lsn) recovered.push_back(std::move(rec));
        offset += consumed;
        last_valid_offset = offset;
    }

    ::close(read_fd);
    return recovered;
}

bool WAL::truncate() {
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    int trunc_fd = ::open(wal_path_.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (trunc_fd >= 0) {
        ::close(trunc_fd);
        return true;
    }
    return false;
}

size_t WAL::getFileSize() const {
    struct stat st{};
    return (::stat(wal_path_.c_str(), &st) == 0) ? static_cast<size_t>(st.st_size) : 0;
}

}  // namespace kvstore
