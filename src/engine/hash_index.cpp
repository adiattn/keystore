#include "engine/hash_index.hpp"

#include <fnmatch.h>

#include <algorithm>

namespace kvstore {

HashIndex::HashIndex(size_t num_shards) : num_shards_(num_shards == 0 ? 32 : num_shards) {
    shards_.reserve(num_shards_);
    for (size_t i = 0; i < num_shards_; ++i) {
        shards_.push_back(std::make_unique<Shard>());
    }
}

size_t HashIndex::getShardIndex(const std::string& key) const {
    return std::hash<std::string>{}(key) % num_shards_;
}

void HashIndex::set(const std::string& key, Value val) {
    size_t idx = getShardIndex(key);
    std::unique_lock<std::shared_mutex> lock(shards_[idx]->mutex);
    shards_[idx]->map[key] = std::move(val);
}

std::optional<Value> HashIndex::get(const std::string& key) const {
    size_t idx = getShardIndex(key);
    std::shared_lock<std::shared_mutex> lock(shards_[idx]->mutex);
    auto it = shards_[idx]->map.find(key);
    if (it != shards_[idx]->map.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool HashIndex::del(const std::string& key) {
    size_t idx = getShardIndex(key);
    std::unique_lock<std::shared_mutex> lock(shards_[idx]->mutex);
    return shards_[idx]->map.erase(key) > 0;
}

bool HashIndex::exists(const std::string& key) const {
    size_t idx = getShardIndex(key);
    std::shared_lock<std::shared_mutex> lock(shards_[idx]->mutex);
    return shards_[idx]->map.find(key) != shards_[idx]->map.end();
}

DataType HashIndex::type(const std::string& key) const {
    size_t idx = getShardIndex(key);
    std::shared_lock<std::shared_mutex> lock(shards_[idx]->mutex);
    auto it = shards_[idx]->map.find(key);
    if (it != shards_[idx]->map.end()) {
        return it->second.getType();
    }
    return DataType::NONE;
}

std::optional<int64_t> HashIndex::incrByInt(const std::string& key, int64_t delta) {
    size_t idx = getShardIndex(key);
    std::unique_lock<std::shared_mutex> lock(shards_[idx]->mutex);
    auto it = shards_[idx]->map.find(key);
    if (it == shards_[idx]->map.end()) {
        shards_[idx]->map[key] = Value::fromInt(delta);
        return delta;
    }

    if (!it->second.isInt()) {
        return std::nullopt;
    }

    int64_t new_val = it->second.asInt() + delta;
    it->second = Value::fromInt(new_val);
    return new_val;
}

std::optional<double> HashIndex::incrByFloat(const std::string& key, double delta) {
    size_t idx = getShardIndex(key);
    std::unique_lock<std::shared_mutex> lock(shards_[idx]->mutex);
    auto it = shards_[idx]->map.find(key);
    if (it == shards_[idx]->map.end()) {
        shards_[idx]->map[key] = Value::fromFloat(delta);
        return delta;
    }

    double current_val = 0.0;
    if (it->second.isFloat()) {
        current_val = it->second.asFloat();
    } else if (it->second.isInt()) {
        current_val = static_cast<double>(it->second.asInt());
    } else {
        return std::nullopt;
    }

    double new_val = current_val + delta;
    it->second = Value::fromFloat(new_val);
    return new_val;
}

bool HashIndex::compareAndSwap(const std::string& key, const Value& expected, const Value& desired) {
    size_t idx = getShardIndex(key);
    std::unique_lock<std::shared_mutex> lock(shards_[idx]->mutex);
    auto it = shards_[idx]->map.find(key);
    if (it == shards_[idx]->map.end()) {
        if (expected.isNull()) {
            shards_[idx]->map[key] = desired;
            return true;
        }
        return false;
    }

    if (it->second == expected) {
        shards_[idx]->map[key] = desired;
        return true;
    }
    return false;
}

std::vector<std::string> HashIndex::getKeys(const std::string& pattern) const {
    std::vector<std::string> result;
    bool match_all = (pattern == "*");

    for (const auto& shard : shards_) {
        std::shared_lock<std::shared_mutex> lock(shard->mutex);
        for (const auto& [k, _] : shard->map) {
            if (match_all || fnmatch(pattern.c_str(), k.c_str(), 0) == 0) {
                result.push_back(k);
            }
        }
    }
    return result;
}

size_t HashIndex::size() const {
    size_t total = 0;
    for (const auto& shard : shards_) {
        std::shared_lock<std::shared_mutex> lock(shard->mutex);
        total += shard->map.size();
    }
    return total;
}

void HashIndex::clear() {
    for (const auto& shard : shards_) {
        std::unique_lock<std::shared_mutex> lock(shard->mutex);
        shard->map.clear();
    }
}

std::vector<std::pair<std::string, Value>> HashIndex::getAllEntries() const {
    std::vector<std::pair<std::string, Value>> entries;
    for (const auto& shard : shards_) {
        std::shared_lock<std::shared_mutex> lock(shard->mutex);
        for (const auto& pair : shard->map) {
            entries.push_back(pair);
        }
    }
    return entries;
}

void HashIndex::loadEntries(const std::vector<std::pair<std::string, Value>>& entries) {
    clear();
    for (const auto& [k, v] : entries) {
        set(k, v);
    }
}

}  // namespace kvstore
