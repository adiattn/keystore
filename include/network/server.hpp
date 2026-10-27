#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "common/thread_pool.hpp"
#include "engine/engine.hpp"
#include "network/socket.hpp"

namespace kvstore {

/**
 * @brief Per-client session state with thread-safe send serialization.
 */
struct ClientSession {
    int fd{-1};             ///< Client socket descriptor
    std::string in_buffer;  ///< Stream accumulator for partial commands
    std::mutex send_mutex;  ///< Serializes concurrent writes to client socket
};

/**
 * @brief Multithreaded Event-Driven TCP Server.
 *
 * OS & Architecture:
 * - Reactor Pattern: epoll event loop accepts connections and reads requests non-blockingly.
 * - ThreadPool Offloading: Dispatches command execution to worker pool without blocking the reactor.
 * - Self-Pipe Trick: POSIX pipe wakes up epoll immediately on shutdown signal.
 */
class KVServer {
public:
    KVServer(KVEngine& engine, int port = 6379, std::string host = "0.0.0.0", size_t num_workers = 8);
    ~KVServer();

    /// Starts epoll event loop (blocks until stop() is invoked).
    bool start();

    /// Signals server to stop and wakes up epoll via self-pipe.
    void stop();

    bool isRunning() const { return running_.load(); }
    int getPort() const { return port_; }

private:
    void eventLoop();
    void handleAccept();
    void handleClientRead(int client_fd);
    void closeClient(int client_fd);

    KVEngine& engine_;
    int port_;
    std::string host_;
    size_t num_workers_;

    int listen_fd_{-1};
    int stop_pipe_[2]{-1, -1};
    std::atomic<bool> running_{false};

    std::unique_ptr<Epoll> epoll_;
    ThreadPool thread_pool_;

    std::mutex clients_mutex_;
    std::unordered_map<int, std::shared_ptr<ClientSession>> clients_;
};

}  // namespace kvstore
