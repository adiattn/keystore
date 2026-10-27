#include "common/types.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace kvstore {

std::string dataTypeToString(DataType type) {
    switch (type) {
        case DataType::STRING:
            return "STRING";
        case DataType::INTEGER:
            return "INTEGER";
        case DataType::FLOAT:
            return "FLOAT";
        case DataType::BOOLEAN:
            return "BOOLEAN";
        default:
            return "NONE";
    }
}

DataType stringToDataType(const std::string& str) {
    std::string upper = str;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    if (upper == "STRING" || upper == "STR") return DataType::STRING;
    if (upper == "INTEGER" || upper == "INT") return DataType::INTEGER;
    if (upper == "FLOAT" || upper == "DOUBLE") return DataType::FLOAT;
    if (upper == "BOOLEAN" || upper == "BOOL") return DataType::BOOLEAN;
    return DataType::NONE;
}

Value::Value() : type_(DataType::NONE), data_(std::monostate{}) {}
Value::Value(std::string str) : type_(DataType::STRING), data_(std::move(str)) {}
Value::Value(const char* str) : type_(DataType::STRING), data_(std::string(str)) {}
Value::Value(int64_t val) : type_(DataType::INTEGER), data_(val) {}
Value::Value(int val) : type_(DataType::INTEGER), data_(static_cast<int64_t>(val)) {}
Value::Value(double val) : type_(DataType::FLOAT), data_(val) {}
Value::Value(bool val) : type_(DataType::BOOLEAN), data_(val) {}

Value Value::fromString(std::string str) {
    return Value(std::move(str));
}
Value Value::fromInt(int64_t val) {
    return Value(val);
}
Value Value::fromFloat(double val) {
    return Value(val);
}
Value Value::fromBool(bool val) {
    return Value(val);
}

Value Value::parseAuto(const std::string& str) {
    if (str.empty()) return Value::fromString(str);

    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "true") return Value::fromBool(true);
    if (lower == "false") return Value::fromBool(false);

    try {
        size_t idx = 0;
        int64_t ival = std::stoll(str, &idx);
        if (idx == str.size()) return Value::fromInt(ival);
    } catch (...) {
    }

    try {
        size_t idx = 0;
        double fval = std::stod(str, &idx);
        if (idx == str.size() && (str.find('.') != std::string::npos || str.find('e') != std::string::npos ||
                                  str.find('E') != std::string::npos)) {
            return Value::fromFloat(fval);
        }
    } catch (...) {
    }

    return Value::fromString(str);
}

const std::string& Value::asString() const {
    if (type_ == DataType::STRING) return std::get<std::string>(data_);
    throw std::runtime_error("Value is not a STRING");
}

int64_t Value::asInt() const {
    if (type_ == DataType::INTEGER) return std::get<int64_t>(data_);
    throw std::runtime_error("Value is not an INTEGER");
}

double Value::asFloat() const {
    if (type_ == DataType::FLOAT) return std::get<double>(data_);
    throw std::runtime_error("Value is not a FLOAT");
}

bool Value::asBool() const {
    if (type_ == DataType::BOOLEAN) return std::get<bool>(data_);
    throw std::runtime_error("Value is not a BOOLEAN");
}

std::string Value::toString() const {
    switch (type_) {
        case DataType::STRING:
            return std::get<std::string>(data_);
        case DataType::INTEGER:
            return std::to_string(std::get<int64_t>(data_));
        case DataType::FLOAT: {
            std::ostringstream oss;
            oss << std::setprecision(15) << std::get<double>(data_);
            return oss.str();
        }
        case DataType::BOOLEAN:
            return std::get<bool>(data_) ? "true" : "false";
        default:
            return "(nil)";
    }
}

std::string Value::typeName() const {
    return dataTypeToString(type_);
}

static void appendBytes(std::vector<uint8_t>& buf, const void* src, size_t len) {
    const auto* p = static_cast<const uint8_t*>(src);
    buf.insert(buf.end(), p, p + len);
}

void Value::serialize(std::vector<uint8_t>& buffer) const {
    buffer.push_back(static_cast<uint8_t>(type_));

    switch (type_) {
        case DataType::STRING: {
            const auto& s = std::get<std::string>(data_);
            uint32_t len = static_cast<uint32_t>(s.size());
            appendBytes(buffer, &len, sizeof(len));
            appendBytes(buffer, s.data(), len);
            break;
        }
        case DataType::INTEGER: {
            int64_t v = std::get<int64_t>(data_);
            uint32_t len = sizeof(v);
            appendBytes(buffer, &len, sizeof(len));
            appendBytes(buffer, &v, sizeof(v));
            break;
        }
        case DataType::FLOAT: {
            double v = std::get<double>(data_);
            uint32_t len = sizeof(v);
            appendBytes(buffer, &len, sizeof(len));
            appendBytes(buffer, &v, sizeof(v));
            break;
        }
        case DataType::BOOLEAN: {
            uint8_t v = std::get<bool>(data_) ? 1 : 0;
            uint32_t len = 1;
            appendBytes(buffer, &len, sizeof(len));
            buffer.push_back(v);
            break;
        }
        default: {
            uint32_t len = 0;
            appendBytes(buffer, &len, sizeof(len));
            break;
        }
    }
}

Value Value::deserialize(const uint8_t* data, size_t size, size_t& bytes_read) {
    bytes_read = 0;
    if (size < 5) return Value();

    DataType type = static_cast<DataType>(data[0]);
    uint32_t len = 0;
    std::memcpy(&len, data + 1, sizeof(uint32_t));
    if (size < 5 + len) return Value();

    bytes_read = 5 + len;
    const uint8_t* payload = data + 5;

    switch (type) {
        case DataType::STRING:
            return Value::fromString(std::string(reinterpret_cast<const char*>(payload), len));
        case DataType::INTEGER: {
            if (len != sizeof(int64_t)) return Value();
            int64_t v = 0;
            std::memcpy(&v, payload, sizeof(v));
            return Value::fromInt(v);
        }
        case DataType::FLOAT: {
            if (len != sizeof(double)) return Value();
            double v = 0.0;
            std::memcpy(&v, payload, sizeof(v));
            return Value::fromFloat(v);
        }
        case DataType::BOOLEAN: {
            if (len != 1) return Value();
            return Value::fromBool(payload[0] != 0);
        }
        default:
            return Value();
    }
}

bool Value::operator==(const Value& other) const {
    return (type_ == other.type_) && (data_ == other.data_);
}

}  // namespace kvstore
