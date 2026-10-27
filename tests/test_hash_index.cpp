#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

#include "engine/hash_index.hpp"

int main() {
    std::cout << "[Test] Running HashIndex tests...\n";

    kvstore::HashIndex index(16);

    // 1. Basic SET and GET
    index.set("key1", kvstore::Value::fromString("val1"));
    index.set("key2", kvstore::Value::fromInt(100));
    index.set("key3", kvstore::Value::fromFloat(3.14));
    index.set("key4", kvstore::Value::fromBool(true));

    assert(index.size() == 4);
    assert(index.exists("key1"));
    assert(index.exists("key2"));
    assert(!index.exists("nonexistent"));

    auto g1 = index.get("key1");
    assert(g1.has_value() && g1->asString() == "val1");
    assert(index.type("key1") == kvstore::DataType::STRING);

    auto g2 = index.get("key2");
    assert(g2.has_value() && g2->asInt() == 100);
    assert(index.type("key2") == kvstore::DataType::INTEGER);

    // 2. DEL
    assert(index.del("key1") == true);
    assert(index.del("key1") == false);
    assert(!index.exists("key1"));
    assert(index.size() == 3);

    // 3. INCR operations
    auto incr_res = index.incrByInt("key2", 25);
    assert(incr_res.has_value() && incr_res.value() == 125);
    assert(index.get("key2")->asInt() == 125);

    auto incr_float_res = index.incrByFloat("key3", 1.5);
    assert(incr_float_res.has_value());
    assert(std::abs(incr_float_res.value() - 4.64) < 1e-5);

    // 4. CAS
    assert(!index.compareAndSwap("key2", kvstore::Value::fromInt(999),
                                 kvstore::Value::fromInt(200)));  // Expected did not match
    assert(index.compareAndSwap("key2", kvstore::Value::fromInt(125), kvstore::Value::fromInt(200)));  // Success
    assert(index.get("key2")->asInt() == 200);

    // 5. Pattern matching
    index.set("user:101", kvstore::Value::fromString("Alice"));
    index.set("user:102", kvstore::Value::fromString("Bob"));
    index.set("item:50", kvstore::Value::fromString("Book"));

    auto user_keys = index.getKeys("user:*");
    assert(user_keys.size() == 2);

    auto all_keys = index.getKeys("*");
    assert(all_keys.size() == index.size());

    // 6. Multi-threaded Concurrent Access
    std::cout << "[Test] Running multi-threaded HashIndex stress test...\n";
    constexpr int NUM_THREADS = 8;
    constexpr int OPS_PER_THREAD = 1000;
    std::vector<std::thread> threads;

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&index, t]() {
            for (int i = 0; i < OPS_PER_THREAD; ++i) {
                std::string k = "tkey_" + std::to_string(t) + "_" + std::to_string(i);
                index.set(k, kvstore::Value::fromInt(i));
                auto v = index.get(k);
                assert(v.has_value() && v->asInt() == i);
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    std::cout << "[Test] HashIndex tests PASSED!\n";
    return 0;
}
