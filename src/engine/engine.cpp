#include "engine/engine.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>

namespace kvstore {

KVEngine::KVEngine(std::string data_dir, bool sync_wal)
    : data_dir_(std::move(data_dir)),
      sync_wal_(sync_wal),
      wal_path_(data_dir_ + "/kvstore.wal"),
      snapshot_path_(data_dir_ + "/kvstore.snapshot") {}

KVEngine::~KVEngine() {
    close();
}

bool KVEngine::open() {
    ::mkdir(data_dir_.c_str(), 0755);
    wal_ = std::make_unique<WAL>(wal_path_, sync_wal_);
    snapshot_mgr_ = std::make_unique<SnapshotManager>(snapshot_path_);

    recover();
    return wal_->open();
}

void KVEngine::close() {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    if (wal_) {
        wal_->sync();
        wal_->close();
    }
}

void KVEngine::recover() {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    size_t snap_keys = 0;
    uint64_t snap_lsn = 0;

    // 1. Recover Snapshot if present
    if (snapshot_mgr_->snapshotExists()) {
        std::vector<std::pair<std::string, Value>> entries;
        if (snapshot_mgr_->loadSnapshot(snap_lsn, entries)) {
            index_.loadEntries(entries);
            current_lsn_ = snap_lsn;
            snap_keys = entries.size();
            std::cout << "[Engine Recovery] Loaded " << snap_keys << " entries from snapshot at LSN " << snap_lsn
                      << ".\n";
        }
    }

    // 2. Replay subsequent WAL entries
    std::vector<WalRecord> records = wal_->recover(current_lsn_ + 1);
    for (const auto& rec : records) {
        if (rec.op == WalOpType::SET)
            index_.set(rec.key, rec.value);
        else if (rec.op == WalOpType::DEL)
            index_.del(rec.key);
        else if (rec.op == WalOpType::CLEAR)
            index_.clear();

        if (rec.lsn > current_lsn_) current_lsn_ = rec.lsn;
    }

    std::cout << "[Engine Recovery] Replayed " << records.size() << " WAL records. Current LSN: " << current_lsn_
              << ", Total Keys: " << index_.size() << ".\n";
}

bool KVEngine::set(const std::string& key, Value val) {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    uint64_t lsn = ++current_lsn_;
    if (!wal_->appendSet(lsn, key, val)) return false;
    index_.set(key, std::move(val));
    return true;
}

std::optional<Value> KVEngine::get(const std::string& key) const {
    return index_.get(key);
}

bool KVEngine::del(const std::string& key) {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    if (!index_.exists(key)) return false;
    uint64_t lsn = ++current_lsn_;
    if (!wal_->appendDel(lsn, key)) return false;
    return index_.del(key);
}

bool KVEngine::exists(const std::string& key) const {
    return index_.exists(key);
}

DataType KVEngine::type(const std::string& key) const {
    return index_.type(key);
}

std::optional<int64_t> KVEngine::incrByInt(const std::string& key, int64_t delta) {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    auto res = index_.incrByInt(key, delta);
    if (res.has_value()) {
        uint64_t lsn = ++current_lsn_;
        wal_->appendSet(lsn, key, Value::fromInt(res.value()));
    }
    return res;
}

std::optional<double> KVEngine::incrByFloat(const std::string& key, double delta) {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    auto res = index_.incrByFloat(key, delta);
    if (res.has_value()) {
        uint64_t lsn = ++current_lsn_;
        wal_->appendSet(lsn, key, Value::fromFloat(res.value()));
    }
    return res;
}

bool KVEngine::compareAndSwap(const std::string& key, const Value& expected, const Value& desired) {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    if (index_.compareAndSwap(key, expected, desired)) {
        uint64_t lsn = ++current_lsn_;
        wal_->appendSet(lsn, key, desired);
        return true;
    }
    return false;
}

std::vector<std::string> KVEngine::getKeys(const std::string& pattern) const {
    return index_.getKeys(pattern);
}

size_t KVEngine::size() const {
    return index_.size();
}

void KVEngine::flush() {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    uint64_t lsn = ++current_lsn_;
    wal_->appendClear(lsn);
    index_.clear();
}

bool KVEngine::save() {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    uint64_t snap_lsn = current_lsn_.load();
    if (snapshot_mgr_->createSnapshot(snap_lsn, index_.getAllEntries())) {
        wal_->truncate();
        return true;
    }
    return false;
}

bool KVEngine::bgSave() {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    return snapshot_mgr_->bgSaveFork(current_lsn_.load(), index_.getAllEntries()) > 0;
}

bool KVEngine::checkBgSave(int& exit_code) {
    return snapshot_mgr_->checkBgSaveStatus(exit_code);
}

EngineStats KVEngine::getStats() const {
    EngineStats st;
    st.key_count = index_.size();
    st.current_lsn = current_lsn_.load();
    st.wal_size_bytes = wal_ ? wal_->getFileSize() : 0;
    st.snapshot_exists = snapshot_mgr_ && snapshot_mgr_->snapshotExists();
    int dummy = 0;
    st.bgsave_in_progress =
        snapshot_mgr_ && const_cast<SnapshotManager*>(snapshot_mgr_.get())->checkBgSaveStatus(dummy);
    return st;
}

static bool parseTypedSet(const std::string& cmd, const std::string& raw, Value& out_val, std::string& err) {
    if (cmd == "SET") {
        out_val = Value::parseAuto(raw);
        return true;
    } else if (cmd == "SETSTR") {
        out_val = Value::fromString(raw);
        return true;
    } else if (cmd == "SETINT") {
        try {
            out_val = Value::fromInt(std::stoll(raw));
            return true;
        } catch (...) {
            err = "value is not a valid integer";
            return false;
        }
    } else if (cmd == "SETFLOAT") {
        try {
            out_val = Value::fromFloat(std::stod(raw));
            return true;
        } catch (...) {
            err = "value is not a valid float";
            return false;
        }
    } else if (cmd == "SETBOOL") {
        std::string l = raw;
        std::transform(l.begin(), l.end(), l.begin(), ::tolower);
        out_val = Value::fromBool(l == "true" || l == "1");
        return true;
    }
    return false;
}

Response KVEngine::executeCommand(const std::vector<std::string>& args) {
    if (args.empty()) return Response::error("Empty command");

    std::string cmd = args[0];
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);

    if (cmd == "PING") return Response::simpleString(args.size() > 1 ? args[1] : "PONG");

    if (cmd == "SET" || cmd == "SETSTR" || cmd == "SETINT" || cmd == "SETFLOAT" || cmd == "SETBOOL") {
        if (args.size() < 3) return Response::error("wrong number of arguments for '" + cmd + "' command");
        Value val;
        std::string err;
        if (!parseTypedSet(cmd, args[2], val, err)) return Response::error(err);
        return set(args[1], val) ? Response::ok() : Response::error("Failed to write to WAL");
    }

    if (cmd == "GET") {
        if (args.size() != 2) return Response::error("usage: GET <key>");
        auto val = get(args[1]);
        return val.has_value() ? Response::fromValue(val.value()) : Response::nil();
    }

    if (cmd == "DEL") {
        if (args.size() < 2) return Response::error("usage: DEL <key> [key2 ...]");
        int64_t count = 0;
        for (size_t i = 1; i < args.size(); ++i)
            if (del(args[i])) count++;
        return Response::integer(count);
    }

    if (cmd == "EXISTS") {
        if (args.size() < 2) return Response::error("usage: EXISTS <key> [key2 ...]");
        int64_t count = 0;
        for (size_t i = 1; i < args.size(); ++i)
            if (exists(args[i])) count++;
        return Response::integer(count);
    }

    if (cmd == "TYPE") {
        if (args.size() != 2) return Response::error("usage: TYPE <key>");
        std::string tname = dataTypeToString(type(args[1]));
        std::transform(tname.begin(), tname.end(), tname.begin(), ::tolower);
        return Response::simpleString(tname);
    }

    if (cmd == "INCR" || cmd == "INCRBY") {
        if (args.size() < 2) return Response::error("usage: " + cmd + " <key> [increment]");
        try {
            int64_t delta = (cmd == "INCRBY" && args.size() >= 3) ? std::stoll(args[2]) : 1;
            auto res = incrByInt(args[1], delta);
            return res.has_value() ? Response::integer(res.value()) : Response::error("value is not an integer");
        } catch (...) {
            return Response::error("value is not an integer");
        }
    }

    if (cmd == "INCRBYFLOAT") {
        if (args.size() != 3) return Response::error("usage: INCRBYFLOAT <key> <increment>");
        try {
            auto res = incrByFloat(args[1], std::stod(args[2]));
            return res.has_value() ? Response::floating(res.value()) : Response::error("value is not a valid float");
        } catch (...) {
            return Response::error("value is not a valid float");
        }
    }

    if (cmd == "CAS") {
        if (args.size() != 4) return Response::error("usage: CAS <key> <expected_val> <desired_val>");
        return Response::integer(compareAndSwap(args[1], Value::parseAuto(args[2]), Value::parseAuto(args[3])) ? 1 : 0);
    }

    if (cmd == "KEYS") {
        std::vector<std::string> key_list = getKeys(args.size() > 1 ? args[1] : "*");
        std::vector<Response> resp_list;
        resp_list.reserve(key_list.size());
        for (const auto& k : key_list) resp_list.push_back(Response::bulkString(k));
        return Response::array(std::move(resp_list));
    }

    if (cmd == "DBSIZE") return Response::integer(static_cast<int64_t>(size()));
    if (cmd == "FLUSHDB" || cmd == "FLUSHALL") {
        flush();
        return Response::ok();
    }
    if (cmd == "SAVE") return save() ? Response::ok() : Response::error("SAVE checkpoint failed");
    if (cmd == "BGSAVE")
        return bgSave() ? Response::simpleString("Background saving started") : Response::error("BGSAVE failed");

    if (cmd == "INFO" || cmd == "STATS") {
        EngineStats st = getStats();
        std::ostringstream oss;
        oss << "# KVStore Server Stats\n"
            << "keys: " << st.key_count << "\n"
            << "current_lsn: " << st.current_lsn << "\n"
            << "wal_size_bytes: " << st.wal_size_bytes << "\n"
            << "snapshot_exists: " << (st.snapshot_exists ? "yes" : "no") << "\n"
            << "bgsave_in_progress: " << (st.bgsave_in_progress ? "yes" : "no") << "\n"
            << "supported_types: STRING, INTEGER, FLOAT, BOOLEAN\n"
            << "sharding: 32 shards with shared_mutex\n"
            << "integrity: CRC32 checksums\n";
        return Response::bulkString(oss.str());
    }

    if (cmd == "HELP") {
        return Response::bulkString(
            "Supported commands:\n"
            "  SET / SETSTR / SETINT / SETFLOAT / SETBOOL <key> <val>\n"
            "  GET <key> / DEL <key> / EXISTS <key> / TYPE <key>\n"
            "  INCR <key> / INCRBY <key> <delta> / INCRBYFLOAT <key> <delta>\n"
            "  CAS <key> <exp> <des>\n"
            "  KEYS [pattern] / DBSIZE / FLUSHDB\n"
            "  SAVE / BGSAVE / STATS / PING / QUIT\n");
    }

    if (cmd == "CONFIG") {
        if (args.size() >= 3 && (args[1] == "GET" || args[1] == "get")) {
            return Response::array({Response::bulkString(args[2]), Response::bulkString("")});
        }
        return Response::ok();
    }
    if (cmd == "CLIENT" || cmd == "SELECT" || cmd == "AUTH" || cmd == "COMMAND") {
        return Response::ok();
    }

    if (cmd == "QUIT") return Response::ok();
    return Response::error("unknown command '" + args[0] + "'");
}

}  // namespace kvstore
