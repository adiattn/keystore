#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "engine/engine.hpp"
#include "network/server.hpp"
#include "network/socket.hpp"

namespace {

struct BenchResult {
    double total_time_sec{0.0};
    double ops_per_sec{0.0};
    double avg_latency_us{0.0};
    size_t total_ops{0};
};

template <typename Func>
BenchResult runBenchmark(size_t num_threads, size_t total_ops, Func&& op) {
    size_t ops_per_thread = total_ops / num_threads;
    std::vector<std::thread> threads;
    std::atomic<bool> start_flag{false};

    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            while (!start_flag.load()) {
                std::this_thread::yield();
            }
            for (size_t i = 0; i < ops_per_thread; ++i) {
                op(t, i);
            }
        });
    }

    auto start_time = std::chrono::high_resolution_clock::now();
    start_flag.store(true);

    for (auto& th : threads) {
        th.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end_time - start_time;

    BenchResult res;
    res.total_ops = ops_per_thread * num_threads;
    res.total_time_sec = diff.count();
    res.ops_per_sec = static_cast<double>(res.total_ops) / res.total_time_sec;
    res.avg_latency_us = (res.total_time_sec * 1e6) / static_cast<double>(res.total_ops);
    return res;
}

}  // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    const std::vector<size_t> thread_counts = {1, 2, 4, 8, 16, 32};
    const size_t total_ops_get = 200000;
    const size_t total_ops_set = 100000;

    std::cout << "########################################################################################\n"
              << "              KVStore Comprehensive Stress Test & Benchmark Suite                       \n"
              << "########################################################################################\n\n";

    std::map<std::string, std::map<size_t, BenchResult>> all_results;

    // ##### SECTION 1: Multi-Thread Scalability (1, 2, 4, 8, 16, 32 Threads) #####
    std::cout << "[1/4] Running Multi-Thread Concurrency Scalability Tests...\n";
    for (size_t threads : thread_counts) {
        std::string bench_dir = "./bench_data_" + std::to_string(threads);
        ::system(("rm -rf " + bench_dir).c_str());

        kvstore::KVEngine engine(bench_dir, false);
        if (!engine.open()) return 1;

        for (size_t i = 0; i < 50000; ++i) {
            engine.set("preload_key_" + std::to_string(i),
                       kvstore::Value::fromString("payload_data_string_" + std::to_string(i)));
        }

        // 1. GET (In-Memory Concurrent Reads)
        all_results["GET"][threads] = runBenchmark(threads, total_ops_get, [&](size_t t, size_t i) {
            std::string key = "preload_key_" + std::to_string(((t * 10000) + i) % 50000);
            (void)engine.get(key);
        });

        // 2. SET (WAL Writes + Checksumming)
        all_results["SET"][threads] = runBenchmark(threads, total_ops_set, [&](size_t t, size_t i) {
            std::string key = "set_th_" + std::to_string(t) + "_k_" + std::to_string(i);
            engine.set(key, kvstore::Value::fromString("sample_val_12345"));
        });

        // 3. INCR (Atomic Shared Counter)
        engine.set("atomic_counter", kvstore::Value::fromInt(0));
        all_results["INCR"][threads] = runBenchmark(
            threads, total_ops_set, [&](size_t /*t*/, size_t /*i*/) { engine.incrByInt("atomic_counter", 1); });

        // 4. 80/20 Mixed
        all_results["80/20 Mixed"][threads] = runBenchmark(threads, total_ops_get, [&](size_t t, size_t i) {
            std::string key = "preload_key_" + std::to_string(((t * 10000) + i) % 50000);
            if (i % 5 == 0) {
                engine.set(key, kvstore::Value::fromString("updated_mixed_val"));
            } else {
                (void)engine.get(key);
            }
        });

        engine.close();
        ::system(("rm -rf " + bench_dir).c_str());
    }

    std::cout << "\n########################################################################################\n"
              << "                             BENCHMARK RESULTS SUMMARY TABLE                            \n"
              << "########################################################################################\n";
    std::cout << std::left << std::setw(16) << "Workload" << std::setw(10) << "Threads" << std::setw(18)
              << "Throughput (ops/s)" << std::setw(18) << "Avg Latency (us)" << std::setw(14) << "Speedup (vs 1T)"
              << "\n";
    std::cout << "########################################################################################\n";

    for (const auto& workload : {"GET", "SET", "INCR", "80/20 Mixed"}) {
        double base_ops = all_results[workload][1].ops_per_sec;
        for (size_t threads : thread_counts) {
            const auto& res = all_results[workload][threads];
            std::cout << std::left << std::setw(16) << workload << std::setw(10) << threads << std::setw(18)
                      << std::fixed << std::setprecision(0) << res.ops_per_sec << std::setw(18) << std::fixed
                      << std::setprecision(2) << res.avg_latency_us << std::setw(14) << std::fixed
                      << std::setprecision(2) << (res.ops_per_sec / base_ops) << "\n";
        }
        std::cout << "----------------------------------------------------------------------------------------\n";
    }

    // --- SECTION 2: Payload Size Scaling (64B to 16KB) ---
    std::cout << "\n[2/4] Running Variable Payload Size Stress Test (16 Threads)...\n";
    std::cout << std::left << std::setw(16) << "Payload Size" << std::setw(20) << "SET Throughput" << std::setw(20)
              << "GET Throughput" << "\n";
    std::cout << "----------------------------------------------------------------------------------------\n";
    {
        std::string bench_dir = "./bench_payload_data";
        ::system(("rm -rf " + bench_dir).c_str());
        kvstore::KVEngine engine(bench_dir, false);
        engine.open();

        for (size_t size_bytes : {64, 512, 2048, 8192, 16384}) {
            std::string payload(size_bytes, 'A');
            auto set_p = runBenchmark(16, 50000, [&](size_t t, size_t i) {
                engine.set("pkey_" + std::to_string(t) + "_" + std::to_string(i), kvstore::Value::fromString(payload));
            });
            auto get_p = runBenchmark(16, 50000, [&](size_t t, size_t i) {
                (void)engine.get("pkey_" + std::to_string(t) + "_" + std::to_string(i));
            });
            std::cout << std::left << std::setw(16) << (std::to_string(size_bytes) + " Bytes") << std::setw(20)
                      << (std::to_string(static_cast<int>(set_p.ops_per_sec)) + " ops/s") << std::setw(20)
                      << (std::to_string(static_cast<int>(get_p.ops_per_sec)) + " ops/s") << "\n";
        }
        engine.close();
        ::system(("rm -rf " + bench_dir).c_str());
    }

    // --- SECTION 3: Copy-On-Write (BGSAVE) Under Concurrent Write Load ---
    std::cout << "\n[3/4] Running BGSAVE Fork Copy-On-Write (COW) Stress Test...\n";
    {
        std::string bench_dir = "./bench_cow_data";
        ::system(("rm -rf " + bench_dir).c_str());
        kvstore::KVEngine engine(bench_dir, false);
        engine.open();

        // Populate 20,000 keys
        for (int i = 0; i < 20000; ++i) {
            engine.set("cow_k_" + std::to_string(i), kvstore::Value::fromString(std::string(256, 'X')));
        }

        // Trigger background snapshot fork
        auto snap_start = std::chrono::high_resolution_clock::now();
        bool bgsave_ok = engine.bgSave();

        // Concurrently mutate memory pages during BGSAVE to test OS page duplication
        auto write_res = runBenchmark(8, 40000, [&](size_t t, size_t i) {
            engine.set("cow_k_" + std::to_string((t * 1000) + (i % 20000)),
                       kvstore::Value::fromString(std::string(256, 'Y')));
        });

        int exit_code = 0;
        while (engine.checkBgSave(exit_code)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        auto snap_end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> snap_diff = snap_end - snap_start;

        std::cout << "  BGSAVE Fork Triggered:  " << (bgsave_ok ? "SUCCESS" : "FAILED") << "\n"
                  << "  Concurrent Mutations:   40,000 writes across 8 threads\n"
                  << "  Concurrent Write Speed: " << static_cast<int>(write_res.ops_per_sec)
                  << " ops/s (Latency: " << std::fixed << std::setprecision(2) << write_res.avg_latency_us << " us)\n"
                  << "  BGSAVE Duration:        " << std::fixed << std::setprecision(3) << snap_diff.count()
                  << " seconds\n";

        engine.close();
        ::system(("rm -rf " + bench_dir).c_str());
    }

    // --- SECTION 4: End-to-End TCP Client/Server Socket Network Stress Test ---
    std::cout << "\n[4/4] Running End-to-End TCP Network Socket Stress Test (Loopback Sockets + Epoll)...\n";
    {
        std::string bench_dir = "./bench_net_data";
        ::system(("rm -rf " + bench_dir).c_str());
        kvstore::KVEngine engine(bench_dir, false);
        engine.open();

        int port = 6398;
        kvstore::KVServer server(engine, port, "127.0.0.1", 8);
        std::thread server_thread([&server]() { server.start(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        constexpr size_t NUM_CLIENTS = 16;
        constexpr size_t OPS_PER_CLIENT = 5000;
        std::vector<std::thread> client_threads;
        std::atomic<size_t> completed_net_ops{0};

        auto net_start = std::chrono::high_resolution_clock::now();

        for (size_t c = 0; c < NUM_CLIENTS; ++c) {
            client_threads.emplace_back([port, c, &completed_net_ops]() {
                int sock = kvstore::SocketUtil::createTcpClient("127.0.0.1", port);
                if (sock < 0) return;

                char recv_buf[1024];
                for (size_t i = 0; i < OPS_PER_CLIENT; ++i) {
                    std::string req =
                        "SET net_" + std::to_string(c) + "_" + std::to_string(i) + " " + std::to_string(i) + "\r\n";
                    kvstore::SocketUtil::sendAll(sock, req.data(), req.size());
                    ssize_t n = ::recv(sock, recv_buf, sizeof(recv_buf), 0);
                    if (n > 0) completed_net_ops++;
                }
                ::close(sock);
            });
        }

        for (auto& th : client_threads) th.join();
        auto net_end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> net_diff = net_end - net_start;

        double net_ops_per_sec = completed_net_ops.load() / net_diff.count();
        double net_latency_us = (net_diff.count() * 1e6) / completed_net_ops.load();

        std::cout << "  TCP Clients Connected:  " << NUM_CLIENTS << " concurrent connections\n"
                  << "  Total Network Requests: " << completed_net_ops.load() << " TCP round-trips\n"
                  << "  Network Throughput:     " << static_cast<int>(net_ops_per_sec) << " req/sec\n"
                  << "  Avg Network Latency:    " << std::fixed << std::setprecision(2) << net_latency_us << " us\n";

        server.stop();
        if (server_thread.joinable()) server_thread.join();
        engine.close();
        ::system(("rm -rf " + bench_dir).c_str());
    }

    std::cout << "\n########################################################################################\n"
              << " Stress Test & Benchmark Suite Completed Successfully!\n"
              << "########################################################################################\n";
    return 0;
}
