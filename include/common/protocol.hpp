#pragma once

#include <string>
#include <vector>

#include "common/types.hpp"

namespace kvstore {

/**
 * @brief Wire protocol response types (compatible with Redis RESP2 / RESP3).
 */
enum class ResponseType {
    SIMPLE_STRING,  // +<str>\r\n (e.g. +OK\r\n)
    ERROR,          // -ERR <msg>\r\n
    INTEGER,        // :<number>\r\n
    FLOAT,          // ,<float>\r\n
    BOOLEAN,        // #t\r\n or #f\r\n
    BULK_STRING,    // $<len>\r\n<data>\r\n
    NIL,            // $-1\r\n
    ARRAY           // *<count>\r\n...
};

/**
 * @brief Typed server response sent across the network or displayed in CLI.
 */
struct Response {
    ResponseType type{ResponseType::NIL};
    std::string str_val;
    int64_t int_val{0};
    double float_val{0.0};
    bool bool_val{false};
    std::vector<Response> array_val;

    static Response ok() { return Response{ResponseType::SIMPLE_STRING, "OK", 0, 0.0, false, {}}; }
    static Response simpleString(std::string msg) {
        return Response{ResponseType::SIMPLE_STRING, std::move(msg), 0, 0.0, false, {}};
    }
    static Response error(std::string err) { return Response{ResponseType::ERROR, std::move(err), 0, 0.0, false, {}}; }
    static Response integer(int64_t val) { return Response{ResponseType::INTEGER, "", val, 0.0, false, {}}; }
    static Response floating(double val) { return Response{ResponseType::FLOAT, "", 0, val, false, {}}; }
    static Response boolean(bool val) { return Response{ResponseType::BOOLEAN, "", 0, 0.0, val, {}}; }
    static Response bulkString(std::string data) {
        return Response{ResponseType::BULK_STRING, std::move(data), 0, 0.0, false, {}};
    }
    static Response nil() { return Response{ResponseType::NIL, "", 0, 0.0, false, {}}; }
    static Response fromValue(const Value& val);
    static Response array(std::vector<Response> items) {
        return Response{ResponseType::ARRAY, "", 0, 0.0, false, std::move(items)};
    }

    /// Formats response into Redis RESP wire format
    std::string toRespString() const;

    /// Formats response for human-readable CLI terminal display
    std::string toCliString() const;
};

/**
 * @brief Network protocol parser.
 * Extracts arguments from both Redis RESP Multi-Bulk arrays (*3\r\n...) and inline text lines.
 */
class Protocol {
public:
    /// Parses an inline string command (e.g. "SET user:1 Alice") into arguments.
    static std::vector<std::string> parseCommandLine(const std::string& line);

    /// Extracts complete command arguments from incoming network buffer.
    static bool extractCommandArgs(std::string& buffer, std::vector<std::string>& args);
};

}  // namespace kvstore
