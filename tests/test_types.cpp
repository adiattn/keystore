#include <cassert>
#include <cmath>
#include <iostream>

#include "common/types.hpp"

int main() {
    std::cout << "[Test] Running DataType and Value tests...\n";

    // 1. String Type
    kvstore::Value str_val("Hello World");
    assert(str_val.isString());
    assert(str_val.asString() == "Hello World");
    assert(str_val.typeName() == "STRING");

    // 2. Integer Type
    kvstore::Value int_val(static_cast<int64_t>(123456789));
    assert(int_val.isInt());
    assert(int_val.asInt() == 123456789);
    assert(int_val.typeName() == "INTEGER");

    // 3. Float Type
    kvstore::Value float_val(3.14159265);
    assert(float_val.isFloat());
    assert(std::abs(float_val.asFloat() - 3.14159265) < 1e-6);
    assert(float_val.typeName() == "FLOAT");

    // 4. Boolean Type
    kvstore::Value bool_val(true);
    assert(bool_val.isBool());
    assert(bool_val.asBool() == true);
    assert(bool_val.typeName() == "BOOLEAN");

    // 5. Auto Detection
    kvstore::Value auto_int = kvstore::Value::parseAuto("42");
    assert(auto_int.isInt() && auto_int.asInt() == 42);

    kvstore::Value auto_neg_int = kvstore::Value::parseAuto("-100");
    assert(auto_neg_int.isInt() && auto_neg_int.asInt() == -100);

    kvstore::Value auto_float = kvstore::Value::parseAuto("2.71828");
    assert(auto_float.isFloat());

    kvstore::Value auto_bool_true = kvstore::Value::parseAuto("true");
    assert(auto_bool_true.isBool() && auto_bool_true.asBool() == true);

    kvstore::Value auto_bool_false = kvstore::Value::parseAuto("FALSE");
    assert(auto_bool_false.isBool() && auto_bool_false.asBool() == false);

    kvstore::Value auto_str = kvstore::Value::parseAuto("Hello1234");
    assert(auto_str.isString() && auto_str.asString() == "Hello1234");

    // 6. Serialization & Deserialization round-trip
    std::vector<kvstore::Value> test_vals = {
        kvstore::Value::fromString("Test String with spaces and \n special chars!"),
        kvstore::Value::fromInt(-9876543210LL), kvstore::Value::fromFloat(1.23456789), kvstore::Value::fromBool(true),
        kvstore::Value::fromBool(false)};

    for (const auto& original : test_vals) {
        std::vector<uint8_t> buffer;
        original.serialize(buffer);

        size_t bytes_read = 0;
        kvstore::Value deserialized = kvstore::Value::deserialize(buffer.data(), buffer.size(), bytes_read);
        assert(bytes_read == buffer.size());
        assert(original == deserialized);
    }

    std::cout << "[Test] DataType and Value tests PASSED!\n";
    return 0;
}
