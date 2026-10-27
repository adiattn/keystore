#include "network/server.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <iostream>

#include "common/protocol.hpp"

namespace kvstore {

KVServer::KVServer(KVEngine& engine, int port, std::string host, size_t num_workers)
    : engine_(engine), port_(port), host_(std::move(host)), num_workers_(num_workers), thread_pool_(num_workers) {
    if (::pipe(stop_pipe_) == 0) {
        SocketUtil::setNonBlocking(stop_pipe_[0]);
        SocketUtil::setNonBlocking(stop_pipe_[1]);
    }
}

KVServer::~KVServer() {
    stop();
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (stop_pipe_[0] >= 0) ::close(stop_pipe_[0]);
    if (stop_pipe_[1] >= 0) ::close(stop_pipe_[1]);
}

bool KVServer::start() {
    listen_fd_ = SocketUtil::createTcpServer(port_, host_);
    if (listen_fd_ < 0) {
        std::cerr << "[KVServer] Error: Failed to bind/listen on " << host_ << ":" << port_ << "\n";
        return false;
    }

    epoll_ = std::make_unique<Epoll>(1024);
    if (!epoll_->add(listen_fd_, EPOLLIN)) {
        std::cerr << "[KVServer] Error: Failed to add listen socket to epoll\n";
        return false;
    }

    if (stop_pipe_[0] >= 0) {
        epoll_->add(stop_pipe_[0], EPOLLIN);
    }

    running_.store(true);
    std::cout << "[KVServer] KVStore Server started on " << host_ << ":" << port_ << " (" << num_workers_
              << " worker threads)\n";

    eventLoop();
    return true;
}

void KVServer::stop() {
    if (running_.exchange(false)) {
        if (stop_pipe_[1] >= 0) {
            char val = 1;
            (void)::write(stop_pipe_[1], &val, 1);
        }
    }
}

void KVServer::eventLoop() {
    constexpr int MAX_EVENTS = 64;
    struct epoll_event events[MAX_EVENTS];

    while (running_.load()) {
        int nfds = epoll_->wait(events, MAX_EVENTS, 1000);  // 1s timeout

        // Periodic background task check (BGSAVE process)
        int bgsave_exit = 0;
        if (!engine_.checkBgSave(bgsave_exit)) {
            // No background save or just completed
        }

        if (nfds < 0) {
            if (errno == EINTR) continue;
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            if (fd == stop_pipe_[0]) {
                // Wake up for shutdown
                break;
            }

            if (fd == listen_fd_) {
                handleAccept();
            } else {
                if (ev & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                    closeClient(fd);
                } else if (ev & EPOLLIN) {
                    handleClientRead(fd);
                }
            }
        }
    }

    // Close all remaining client connections on shutdown
    std::vector<int> client_fds;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (const auto& [fd, _] : clients_) {
            client_fds.push_back(fd);
        }
    }
    for (int fd : client_fds) {
        closeClient(fd);
    }
}

void KVServer::handleAccept() {
    while (true) {
        struct sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = ::accept(listen_fd_, reinterpret_cast<struct sockaddr*>(&client_addr), &addr_len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  // Handled all pending incoming connections
            }
            break;
        }

        SocketUtil::setNonBlocking(client_fd);
        SocketUtil::setTcpNoDelay(client_fd);

        auto session = std::make_shared<ClientSession>();
        session->fd = client_fd;

        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            clients_[client_fd] = session;
        }

        epoll_->add(client_fd, EPOLLIN);
    }
}

void KVServer::handleClientRead(int client_fd) {
    std::shared_ptr<ClientSession> session;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        auto it = clients_.find(client_fd);
        if (it == clients_.end()) return;
        session = it->second;
    }

    char buf[4096];
    while (true) {
        ssize_t n = ::recv(client_fd, buf, sizeof(buf), 0);
        if (n > 0) {
            session->in_buffer.append(buf, n);
        } else if (n == 0) {
            // Client disconnected (EOF)
            closeClient(client_fd);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  // Finished reading available socket data
            }
            closeClient(client_fd);
            return;
        }
    }

    // Extract complete commands from buffer (supports both RESP arrays and inline lines)
    std::vector<std::vector<std::string>> pending_commands;
    std::vector<std::string> args;
    while (Protocol::extractCommandArgs(session->in_buffer, args)) {
        if (!args.empty()) {
            pending_commands.push_back(std::move(args));
        }
    }

    // Dispatch extracted commands to thread pool
    for (auto& cmd_args : pending_commands) {
        thread_pool_.enqueue([this, session, args = std::move(cmd_args)]() {
            if (args.empty()) return;

            bool is_quit = false;
            std::string upper_cmd = args[0];
            std::transform(upper_cmd.begin(), upper_cmd.end(), upper_cmd.begin(), ::toupper);
            if (upper_cmd == "QUIT") {
                is_quit = true;
            }

            Response resp = engine_.executeCommand(args);
            std::string wire_resp = resp.toRespString();

            {
                std::lock_guard<std::mutex> lock(session->send_mutex);
                SocketUtil::sendAll(session->fd, wire_resp.data(), wire_resp.size());
            }

            if (is_quit) {
                closeClient(session->fd);
            }
        });
    }
}

void KVServer::closeClient(int client_fd) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    auto it = clients_.find(client_fd);
    if (it != clients_.end()) {
        epoll_->remove(client_fd);
        ::close(client_fd);
        clients_.erase(it);
    }
}

}  // namespace kvstore
