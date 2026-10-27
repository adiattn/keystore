#include <arpa/inet.h>
#include <unistd.h>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "network/socket.hpp"

namespace {

const char* COLOR_RESET = "\033[0m";
const char* COLOR_GREEN = "\033[1;32m";
const char* COLOR_CYAN = "\033[1;36m";
const char* COLOR_YELLOW = "\033[1;33m";
const char* COLOR_MAGENTA = "\033[1;35m";
const char* COLOR_RED = "\033[1;31m";
const char* COLOR_GRAY = "\033[0;90m";

bool readLineFromSocket(int fd, std::string& line) {
    line.clear();
    char c = 0;
    while (true) {
        ssize_t n = ::recv(fd, &c, 1, 0);
        if (n <= 0) return false;
        if (c == '\n') break;
        if (c != '\r') line.push_back(c);
    }
    return true;
}

std::string formatResp(int fd, const std::string& first_line) {
    if (first_line.empty()) return "";

    char type_marker = first_line[0];
    std::string payload = first_line.substr(1);

    if (type_marker == '+') {
        // Simple String
        return std::string(COLOR_GREEN) + payload + COLOR_RESET;
    } else if (type_marker == '-') {
        // Error
        return std::string(COLOR_RED) + "(error) " + payload + COLOR_RESET;
    } else if (type_marker == ':') {
        // Integer
        return std::string(COLOR_YELLOW) + "(integer) " + payload + COLOR_RESET;
    } else if (type_marker == ',') {
        // Float
        return std::string(COLOR_MAGENTA) + "(float) " + payload + COLOR_RESET;
    } else if (type_marker == '#') {
        // Boolean
        bool val = (payload == "t" || payload == "true");
        return std::string(COLOR_GREEN) + "(boolean) " + (val ? "true" : "false") + COLOR_RESET;
    } else if (type_marker == '$') {
        // Bulk String
        int len = std::stoi(payload);
        if (len < 0) {
            return std::string(COLOR_GRAY) + "(nil)" + COLOR_RESET;
        }
        std::vector<char> buf(len);
        if (len > 0) {
            kvstore::SocketUtil::recvAll(fd, buf.data(), len);
        }
        // Consume trailing \r\n
        std::string dummy;
        readLineFromSocket(fd, dummy);
        return std::string(COLOR_CYAN) + "\"" + std::string(buf.data(), len) + "\"" + COLOR_RESET;
    } else if (type_marker == '*') {
        // Array
        int count = std::stoi(payload);
        if (count < 0) return std::string(COLOR_GRAY) + "(nil)" + COLOR_RESET;
        if (count == 0) return std::string(COLOR_GRAY) + "(empty array)" + COLOR_RESET;

        std::string out;
        for (int i = 0; i < count; ++i) {
            std::string sub_line;
            if (!readLineFromSocket(fd, sub_line)) break;
            if (i > 0) out += "\n";
            out += std::to_string(i + 1) + ") " + formatResp(fd, sub_line);
        }
        return out;
    }

    return first_line;
}

std::string executeCliCommand(int fd, const std::string& cmd) {
    std::string formatted = cmd + "\r\n";
    if (kvstore::SocketUtil::sendAll(fd, formatted.data(), formatted.size()) < 0) {
        return std::string(COLOR_RED) + "Error: Failed to send command to server" + COLOR_RESET;
    }

    std::string resp_header;
    if (!readLineFromSocket(fd, resp_header)) {
        return std::string(COLOR_RED) + "Error: Server disconnected" + COLOR_RESET;
    }

    return formatResp(fd, resp_header);
}

}  // namespace

void printCliUsage(const char* prog) {
    std::cout << "Usage: " << prog << " [options] [command [command ...]]\n"
              << "Options:\n"
              << "  -h <hostname>    Server hostname (default: 127.0.0.1)\n"
              << "  -p <port>        Server port (default: 6379)\n"
              << "  --help           Show this help message\n\n"
              << "Examples:\n"
              << "  " << prog << "\n"
              << "  " << prog << " -p 6379 SET user:1 \"Alice\"\n"
              << "  " << prog << " GET user:1\n";
}

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    int port = 6379;
    std::vector<std::string> inline_cmds;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" && i + 1 < argc) {
            host = argv[++i];
        } else if (arg == "-p" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--help") {
            printCliUsage(argv[0]);
            return 0;
        } else {
            inline_cmds.push_back(arg);
        }
    }

    int fd = kvstore::SocketUtil::createTcpClient(host, port);
    if (fd < 0) {
        std::cerr << COLOR_RED << "Could not connect to KVStore at " << host << ":" << port << ": Connection refused"
                  << COLOR_RESET << "\n";
        return 1;
    }

    // If one-shot commands passed on command line
    if (!inline_cmds.empty()) {
        std::string full_cmd;
        for (size_t i = 0; i < inline_cmds.size(); ++i) {
            if (i > 0) full_cmd += " ";
            full_cmd += inline_cmds[i];
        }
        std::cout << executeCliCommand(fd, full_cmd) << "\n";
        ::close(fd);
        return 0;
    }

    // Interactive REPL Mode
    std::cout << COLOR_GREEN << "Connected to KVStore at " << host << ":" << port << COLOR_RESET << "\n"
              << "Type " << COLOR_CYAN << "HELP" << COLOR_RESET << " for list of commands, or " << COLOR_CYAN << "QUIT"
              << COLOR_RESET << " to exit.\n\n";

    std::string line;
    while (true) {
        std::cout << COLOR_CYAN << host << ":" << port << "> " << COLOR_RESET;
        if (!std::getline(std::cin, line)) {
            std::cout << "\n";
            break;
        }

        // Trim leading and trailing whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);

        if (line.empty()) continue;

        if (line == "exit" || line == "quit" || line == "QUIT") {
            executeCliCommand(fd, "QUIT");
            break;
        }

        std::string result = executeCliCommand(fd, line);
        std::cout << result << "\n";
    }

    ::close(fd);
    return 0;
}
