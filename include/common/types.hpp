#pragma once

#include <cstdint>
#include <iostream>
#include <string>
#include <variant>
#include <vector>

namespace kvstore {

/**
 * @brief Supported data types in KVStore.
 * Stored as a 1-byte opcode in serialization format.
 */
enum class DataType : uint8_t { NONE = 0, STRING = 1, INTEGER = 2, FLOAT = 3, BOOLEAN = 4 };

std::string dataTypeToString(DataType type);
DataType stringToDataType(const std::string& str);

/**
 * @brief Represents a strongly-typed value in the database.
 * Supports STRING, INTEGER (int64_t), FLOAT (double), and BOOLEAN (bool).
 */
class Value {
public:
    using VariantType = std::variant<std::monostate, std::string, int64_t, double, bool>;

    Value();
    explicit Value(std::string str);
    explicit Value(const char* str);
    explicit Value(int64_t val);
    explicit Value(int val);
    explicit Value(double val);
    explicit Value(bool val);

    // Factory methods for creating typed values
    static Value fromString(std::string str);
    static Value fromInt(int64_t val);
    static Value fromFloat(double val);
    static Value fromBool(bool val);

    /**
     * @brief Automatically infers and parses string into bool, int, float, or string.
     */
    static Value parseAuto(const std::string& str);

    // Type queries
    DataType getType() const { return type_; }
    bool isNull() const { return type_ == DataType::NONE; }
    bool isString() const { return type_ == DataType::STRING; }
    bool isInt() const { return type_ == DataType::INTEGER; }
    bool isFloat() const { return type_ == DataType::FLOAT; }
    bool isBool() const { return type_ == DataType::BOOLEAN; }

    // Type getters (throws runtime_error on mismatch)
    const std::string& asString() const;
    int64_t asInt() const;
    double asFloat() const;
    bool asBool() const;

    // Display & representation helpers
    std::string toString() const;
    std::string typeName() const;

    /**
     * @brief Encodes value into binary format: [type: 1B] [len: 4B] [data bytes...]
     */
    void serialize(std::vector<uint8_t>& buffer) const;

    /**
     * @brief Decodes a serialized Value from raw memory bytes.
     */
    static Value deserialize(const uint8_t* data, size_t size, size_t& bytes_read);

    bool operator==(const Value& other) const;
    bool operator!=(const Value& other) const { return !(*this == other); }

private:
    DataType type_{DataType::NONE};
    VariantType data_{std::monostate{}};
};

}  // namespace kvstore
