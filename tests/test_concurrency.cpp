#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

#include "engine/engine.hpp"

int main() {
    std::cout << "[Test] Running High-Concurrency Engine stress test...\n";

    std::string test_dir = "./test_concurrency_data";
    ::system(("rm -rf " + test_dir).c_str());

    {
        kvstore::KVEngine engine(test_dir, false);  // Fast WAL for stress testing
        bool opened = engine.open();
        assert(opened);

        constexpr int NUM_THREADS = 16;
        constexpr int OPS_PER_THREAD = 2000;
        std::vector<std::thread> threads;

        // Initialize shared counter
        engine.set("counter", kvstore::Value::fromInt(0));

        for (int t = 0; t < NUM_THREADS; ++t) {
            threads.emplace_back([&engine, t]() {
                for (int i = 0; i < OPS_PER_THREAD; ++i) {
                    // 1. Thread-private key SET and GET
                    std::string key = "th_" + std::to_string(t) + "_k_" + std::to_string(i);
                    engine.set(key, kvstore::Value::fromInt(i * 10));
                    auto val = engine.get(key);
                    assert(val.has_value() && val->asInt() == i * 10);

                    // 2. Concurrent atomic increment on shared counter
                    if (i % 10 == 0) {
                        engine.incrByInt("counter", 1);
                    }
                }
            });
        }

        for (auto& th : threads) {
            th.join();
        }

        // Verify shared counter
        auto counter_val = engine.get("counter");
        assert(counter_val.has_value());
        assert(counter_val->asInt() == (NUM_THREADS * (OPS_PER_THREAD / 10)));

        std::cout << "[Test] Completed " << (NUM_THREADS * OPS_PER_THREAD) << " concurrent ops across " << NUM_THREADS
                  << " threads.\n";
        engine.close();
    }

    ::system(("rm -rf " + test_dir).c_str());
    std::cout << "[Test] Concurrency tests PASSED!\n";
    return 0;
}
