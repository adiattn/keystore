#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "engine/engine.hpp"
#include "network/server.hpp"

namespace {
std::unique_ptr<kvstore::KVServer> g_server;

void handleSignal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        std::cout << "\n[KVServer] Received signal " << sig << ", shutting down gracefully...\n";
        if (g_server) {
            g_server->stop();
        }
    }
}

void setupSignalHandlers() {
    struct sigaction sa{};
    sa.sa_handler = handleSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    ::sigaction(SIGINT, &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);

    // Ignore SIGPIPE to avoid process termination when writing to a closed socket
    struct sigaction sa_pipe{};
    sa_pipe.sa_handler = SIG_IGN;
    sigemptyset(&sa_pipe.sa_mask);
    sa_pipe.sa_flags = 0;
    ::sigaction(SIGPIPE, &sa_pipe, nullptr);
}
}  // namespace

void printUsage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  -p, --port <port>       Port to listen on (default: 6379)\n"
              << "  -h, --host <ip>         Host address to bind (default: 0.0.0.0)\n"
              << "  -d, --data-dir <dir>    Directory for WAL and snapshots (default: ./data)\n"
              << "  -t, --threads <num>     Worker thread pool size (default: " << std::thread::hardware_concurrency()
              << ")\n"
              << "  --no-sync-wal           Disable fsync on each WAL write (increases throughput)\n"
              << "  --help                  Show this help message\n";
}

int main(int argc, char* argv[]) {
    int port = 6379;
    std::string host = "0.0.0.0";
    std::string data_dir = "./data";
    size_t num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    bool sync_wal = true;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) port = std::stoi(argv[++i]);
        } else if (arg == "-h" || arg == "--host") {
            if (i + 1 < argc) host = argv[++i];
        } else if (arg == "-d" || arg == "--data-dir") {
            if (i + 1 < argc) data_dir = argv[++i];
        } else if (arg == "-t" || arg == "--threads") {
            if (i + 1 < argc) num_threads = std::stoul(argv[++i]);
        } else if (arg == "--no-sync-wal") {
            sync_wal = false;
        } else if (arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
    }

    std::cout << "#########################################################\n"
              << "      KVStore Server - Multithreaded & Persistent        \n"
              << "#########################################################\n"
              << " Port:        " << port << "\n"
              << " Host:        " << host << "\n"
              << " Data Dir:    " << data_dir << "\n"
              << " Workers:     " << num_threads << "\n"
              << " WAL Sync:    " << (sync_wal ? "Enabled (fsync)" : "Disabled") << "\n"
              << " Types:       STRING, INTEGER, FLOAT, BOOLEAN\n"
              << " Integrity:   CRC32 Checksums\n"
              << " Features:    Atomic ops, Crash Recovery, COW Checkpoints\n"
              << "#########################################################\n";

    setupSignalHandlers();

    kvstore::KVEngine engine(data_dir, sync_wal);
    if (!engine.open()) {
        std::cerr << "[KVServer] Fatal error: Failed to open KVEngine storage.\n";
        return 1;
    }

    g_server = std::make_unique<kvstore::KVServer>(engine, port, host, num_threads);
    if (!g_server->start()) {
        std::cerr << "[KVServer] Server failed to start.\n";
        return 1;
    }

    engine.close();
    std::cout << "[KVServer] Server shut down cleanly.\n";
    return 0;
}
