#pragma once

#include <sys/epoll.h>

#include <string>
#include <vector>

namespace kvstore {

/**
 * @brief POSIX Socket utilities for configuring non-blocking TCP connections.
 * Configures O_NONBLOCK, SO_REUSEADDR, and TCP_NODELAY via fcntl(2) and setsockopt(2).
 */
class SocketUtil {
public:
    /// Sets non-blocking I/O mode via fcntl(O_NONBLOCK).
    static bool setNonBlocking(int fd);

    /// Enables address and port reuse for fast server restarts.
    static bool setReuseAddr(int fd);

    /// Disables Nagle's algorithm (TCP_NODELAY) to minimize transmission latency.
    static bool setTcpNoDelay(int fd);

    /// Binds and listens on non-blocking server socket.
    static int createTcpServer(int port, const std::string& host = "0.0.0.0", int backlog = 1024);

    /// Connects a client socket to remote host and port.
    static int createTcpClient(const std::string& host, int port);

    /// Guarantees all bytes are sent across socket before returning.
    static ssize_t sendAll(int fd, const void* data, size_t length);

    /// Reads exact number of requested bytes from socket.
    static ssize_t recvAll(int fd, void* data, size_t length);
};

/**
 * @brief Linux epoll event multiplexer wrapper.
 * Provides O(1) kernel I/O readiness notifications (epoll_create1, epoll_ctl, epoll_wait).
 */
class Epoll {
public:
    explicit Epoll(int max_events = 1024);
    ~Epoll();

    /// Adds file descriptor to epoll interest list.
    bool add(int fd, uint32_t events);

    /// Modifies events monitored for a file descriptor.
    bool modify(int fd, uint32_t events);

    /// Removes file descriptor from epoll.
    bool remove(int fd);

    /// Waits for ready I/O events (blocks up to timeout_ms).
    int wait(struct epoll_event* events, int max_events, int timeout_ms = -1);

private:
    int epoll_fd_{-1};
};

}  // namespace kvstore
