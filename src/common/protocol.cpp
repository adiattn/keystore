#include "common/protocol.hpp"

#include <cctype>
#include <iomanip>
#include <sstream>

namespace kvstore {

Response Response::fromValue(const Value& val) {
    switch (val.getType()) {
        case DataType::STRING:
            return Response::bulkString(val.asString());
        case DataType::INTEGER:
            return Response::integer(val.asInt());
        case DataType::FLOAT:
            return Response::floating(val.asFloat());
        case DataType::BOOLEAN:
            return Response::boolean(val.asBool());
        default:
            return Response::nil();
    }
}

std::string Response::toRespString() const {
    switch (type) {
        case ResponseType::SIMPLE_STRING:
            return "+" + str_val + "\r\n";
        case ResponseType::ERROR:
            return "-ERR " + str_val + "\r\n";
        case ResponseType::INTEGER:
            return ":" + std::to_string(int_val) + "\r\n";
        case ResponseType::FLOAT: {
            std::ostringstream oss;
            oss << "," << std::setprecision(15) << float_val << "\r\n";
            return oss.str();
        }
        case ResponseType::BOOLEAN:
            return bool_val ? "#t\r\n" : "#f\r\n";
        case ResponseType::BULK_STRING:
            return "$" + std::to_string(str_val.size()) + "\r\n" + str_val + "\r\n";
        case ResponseType::NIL:
            return "$-1\r\n";
        case ResponseType::ARRAY: {
            std::string out = "*" + std::to_string(array_val.size()) + "\r\n";
            for (const auto& item : array_val) out += item.toRespString();
            return out;
        }
    }
    return "-ERR unknown response\r\n";
}

std::string Response::toCliString() const {
    switch (type) {
        case ResponseType::SIMPLE_STRING:
            return str_val;
        case ResponseType::ERROR:
            return "(error) " + str_val;
        case ResponseType::INTEGER:
            return "(integer) " + std::to_string(int_val);
        case ResponseType::FLOAT: {
            std::ostringstream oss;
            oss << "(float) " << std::setprecision(15) << float_val;
            return oss.str();
        }
        case ResponseType::BOOLEAN:
            return "(boolean) " + std::string(bool_val ? "true" : "false");
        case ResponseType::BULK_STRING:
            return "\"" + str_val + "\"";
        case ResponseType::NIL:
            return "(nil)";
        case ResponseType::ARRAY: {
            if (array_val.empty()) return "(empty list or set)";
            std::string out;
            for (size_t i = 0; i < array_val.size(); ++i) {
                if (i > 0) out += "\n";
                out += std::to_string(i + 1) + ") " + array_val[i].toCliString();
            }
            return out;
        }
    }
    return "(unknown)";
}

std::vector<std::string> Protocol::parseCommandLine(const std::string& line) {
    std::vector<std::string> tokens;
    std::string current;
    bool in_quotes = false, escape = false;
    char quote_char = '\0';

    for (char c : line) {
        if (escape) {
            current.push_back(c);
            escape = false;
            continue;
        }
        if (c == '\\') {
            escape = true;
            continue;
        }

        if (in_quotes) {
            if (c == quote_char)
                in_quotes = false;
            else
                current.push_back(c);
        } else {
            if (c == '\'' || c == '"') {
                in_quotes = true;
                quote_char = c;
            } else if (std::isspace(static_cast<unsigned char>(c))) {
                if (!current.empty()) {
                    tokens.push_back(std::move(current));
                    current.clear();
                }
            } else
                current.push_back(c);
        }
    }
    if (!current.empty()) tokens.push_back(std::move(current));
    return tokens;
}

bool Protocol::extractCommandArgs(std::string& buffer, std::vector<std::string>& args) {
    args.clear();
    if (buffer.empty()) return false;

    // 1. Check if Redis RESP Multi-Bulk Array (*<count>\r\n$<len>\r\n<data>\r\n...)
    if (buffer[0] == '*') {
        size_t line_end = buffer.find("\r\n");
        if (line_end == std::string::npos) return false;

        int num_args = 0;
        try {
            num_args = std::stoi(buffer.substr(1, line_end - 1));
        } catch (...) {
            buffer.erase(0, line_end + 2);
            return false;
        }

        if (num_args <= 0) {
            buffer.erase(0, line_end + 2);
            return false;
        }

        size_t offset = line_end + 2;
        std::vector<std::string> parsed_args;
        parsed_args.reserve(num_args);

        for (int i = 0; i < num_args; ++i) {
            if (offset >= buffer.size() || buffer[offset] != '$') return false;
            size_t len_end = buffer.find("\r\n", offset);
            if (len_end == std::string::npos) return false;

            int arg_len = 0;
            try {
                arg_len = std::stoi(buffer.substr(offset + 1, len_end - offset - 1));
            } catch (...) {
                return false;
            }

            if (arg_len < 0) return false;

            offset = len_end + 2;
            if (offset + arg_len + 2 > buffer.size()) return false;  // Incomplete payload

            parsed_args.emplace_back(buffer.substr(offset, arg_len));
            offset += arg_len + 2;  // Skip payload and \r\n
        }

        buffer.erase(0, offset);
        args = std::move(parsed_args);
        return true;
    }

    // 2. Otherwise parse plain text line (SET foo bar\n)
    size_t pos = buffer.find('\n');
    if (pos == std::string::npos) return false;

    std::string line = buffer.substr(0, pos);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    buffer.erase(0, pos + 1);

    if (line.empty()) return false;
    args = parseCommandLine(line);
    return !args.empty();
}

}  // namespace kvstore
