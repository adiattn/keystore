#include "network/socket.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

namespace kvstore {

bool SocketUtil::setNonBlocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

bool SocketUtil::setReuseAddr(int fd) {
    int opt = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        return false;
    }
#ifdef SO_REUSEPORT
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif
    return true;
}

bool SocketUtil::setTcpNoDelay(int fd) {
    int opt = 1;
    return ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) >= 0;
}

int SocketUtil::createTcpServer(int port, const std::string& host, int backlog) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    if (!setReuseAddr(fd) || !setNonBlocking(fd) || !setTcpNoDelay(fd)) {
        ::close(fd);
        return -1;
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (host == "0.0.0.0" || host.empty()) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
            ::close(fd);
            return -1;
        }
    }

    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return -1;
    }

    if (::listen(fd, backlog) < 0) {
        ::close(fd);
        return -1;
    }

    return fd;
}

int SocketUtil::createTcpClient(const std::string& host, int port) {
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    std::string port_str = std::to_string(port);
    if (::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0) {
        return -1;
    }

    int fd = -1;
    for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;

        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
            break;  // Connected successfully
        }

        ::close(fd);
        fd = -1;
    }

    ::freeaddrinfo(res);
    if (fd >= 0) {
        setTcpNoDelay(fd);
    }
    return fd;
}

ssize_t SocketUtil::sendAll(int fd, const void* data, size_t length) {
    const char* ptr = static_cast<const char*>(data);
    size_t remaining = length;

    while (remaining > 0) {
        ssize_t sent = ::send(fd, ptr, remaining, MSG_NOSIGNAL);
        if (sent <= 0) {
            if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
                continue;
            }
            return -1;
        }
        ptr += sent;
        remaining -= sent;
    }
    return static_cast<ssize_t>(length);
}

ssize_t SocketUtil::recvAll(int fd, void* data, size_t length) {
    char* ptr = static_cast<char*>(data);
    size_t remaining = length;

    while (remaining > 0) {
        ssize_t bytes = ::recv(fd, ptr, remaining, 0);
        if (bytes <= 0) {
            if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
                continue;
            }
            return -1;
        }
        ptr += bytes;
        remaining -= bytes;
    }
    return static_cast<ssize_t>(length);
}

Epoll::Epoll(int /*max_events*/) {
    epoll_fd_ = ::epoll_create1(0);
}

Epoll::~Epoll() {
    if (epoll_fd_ >= 0) {
        ::close(epoll_fd_);
    }
}

bool Epoll::add(int fd, uint32_t events) {
    struct epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    return ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == 0;
}

bool Epoll::modify(int fd, uint32_t events) {
    struct epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    return ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) == 0;
}

bool Epoll::remove(int fd) {
    return ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) == 0;
}

int Epoll::wait(struct epoll_event* events, int max_events, int timeout_ms) {
    return ::epoll_wait(epoll_fd_, events, max_events, timeout_ms);
}

}  // namespace kvstore
