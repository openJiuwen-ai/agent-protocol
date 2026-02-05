/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef MCP_HTTP_COMMON_INCLUDE_H_
#define MCP_HTTP_COMMON_INCLUDE_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <variant>

// Forward declaration for RequestId type
namespace Mcp {
using RequestId = std::variant<int64_t, std::string>;
}

namespace Mcp::Http {

// Use RequestId from parent Mcp namespace
using Mcp::RequestId;

// MCP Header constants
constexpr const char* MCP_SESSION_ID_HEADER = "mcp-session-id";
constexpr const char* MCP_PROTOCOL_VERSION_HEADER = "mcp-protocol-version";
constexpr const char* CONTENT_TYPE_HEADER = "content-type";
constexpr const char* ACCEPT_HEADER = "accept";

// Common HTTP Header names (lowercase for consistent processing)
constexpr const char* CACHE_CONTROL_HEADER = "cache-control";
constexpr const char* CONNECTION_HEADER = "connection";
constexpr const char* HOST_HEADER = "host";
constexpr const char* CONTENT_LENGTH_HEADER = "content-length";
constexpr const char* TRANSFER_ENCODING_HEADER = "transfer-encoding";
constexpr const char* X_ACCEL_BUFFERING_HEADER = "x-accel-buffering";
constexpr const char* AUTHORIZATION_HEADER = "authorization";

// Content-Type constants
constexpr const char* CONTENT_TYPE_JSON = "application/json";
constexpr const char* CONTENT_TYPE_SSE = "text/event-stream";
constexpr const char* CONTENT_TYPE_TEXT_PLAIN = "text/plain";

// Common header values
constexpr const char* CACHE_CONTROL_NO_CACHE_NO_TRANSFORM = "no-cache, no-transform";
constexpr const char* CONNECTION_KEEP_ALIVE = "keep-alive";
constexpr const char* CONNECTION_CLOSE = "close";
constexpr const char* TRANSFER_ENCODING_CHUNKED = "chunked";

// HTTP Status Code constants
constexpr int HTTP_STATUS_OK = 200;
constexpr int HTTP_STATUS_ACCEPTED = 202;
constexpr int HTTP_STATUS_BAD_REQUEST = 400;
constexpr int HTTP_STATUS_UNAUTHORIZED = 401;
constexpr int HTTP_STATUS_FORBIDDEN = 403;
constexpr int HTTP_STATUS_NOT_FOUND = 404;
constexpr int HTTP_STATUS_METHOD_NOT_ALLOWED = 405;
constexpr int HTTP_STATUS_NOT_ACCEPTABLE = 406;
constexpr int HTTP_STATUS_CONFLICT = 409;
constexpr int HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE = 415;
constexpr int HTTP_STATUS_INTERNAL_SERVER_ERROR = 500;
constexpr int HTTP_STATUS_SERVICE_UNAVAILABLE = 503;

// Internal parse status codes shared by HTTP server/client parsing routines
constexpr int HTTP_PARSE_OK = 0;
constexpr int HTTP_PARSE_NEED_MORE = 1;
constexpr int HTTP_PARSE_ERROR = -1;

// HTTP Client timeout constants (milliseconds)
constexpr int HTTP_CLIENT_DEFAULT_TIMEOUT_MS = 5000;

// Common helpers shared by HTTP client/server for parsing headers and body.

constexpr std::size_t HTTP_HEADER_BODY_SEPARATOR_LENGTH = 4; // length of "\r\n\r\n"

// SSE field prefix lengths
constexpr size_t SSE_EVENT_PREFIX_LEN = sizeof("event:") - 1; // "event:"
constexpr size_t SSE_ID_PREFIX_LEN = sizeof("id:") - 1; // "id:"
constexpr size_t SSE_DATA_PREFIX_LEN = sizeof("data:") - 1; // "data:"

// SSE event data structure
struct ServerSentEvent {
    std::string event;
    std::string id;
    std::string data;
    int retry;

    ServerSentEvent() : event("message"), id(""), data(""), retry(0)
    {
    }
    ServerSentEvent(const std::string& evt, const std::string& evtId, const std::string& evtData, int retry)
        : event(evt), id(evtId), data(evtData), retry(retry)
    {
    }
};

struct UserData {
    RequestId requestId = int64_t(0);
    std::string method;
};

enum class HttpSendType {
    HTTPRESPONSE = 0, // send total response
    HTTPRESPONSESTART, // send response start(header only)
    HTTPRESPONSEBODY, // send response body
};

// HTTP message types used by HttpServer/HttpClient

struct HttpRequest {
    std::string method;
    std::string url;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    std::unordered_map<std::string, std::string> queryParams;
    std::unordered_map<std::string, std::string> pathParams;
};

struct HttpResponse {
    bool success = false;
    UserData userData;
    int statusCode = 0;
    std::string errorMessage; // Error message (when success is false)
    std::string statusText;
    ServerSentEvent sseEvent;
    HttpSendType type = HttpSendType::HTTPRESPONSE;
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    HttpResponse() : statusCode(HTTP_STATUS_OK), statusText("OK")
    {
        headers[CONNECTION_HEADER] = CONNECTION_KEEP_ALIVE;
    }
};

// Unified callback type definition
using HttpCallback = std::function<bool(const HttpResponse& response)>;

// Common internal HTTP status codes used by both HTTP server and client
// for representing operation results (not wire-level HTTP status line).
enum class HttpServerStartResult {
    OK = 0,
    ALREADY_RUNNING,
    EVENT_SYSTEM_INIT_FAILED,
    SSL_CONTEXT_INIT_FAILED,
    LISTEN_FAILED,
    START_LISTENER_FAILED,
};

// Trim ASCII spaces and tabs from both ends of a string in place.
void TrimInPlace(std::string& value);

// Parse HTTP headers and body given a full buffer and the position of "\r\n\r\n".
// - buffer: entire HTTP message buffer.
// - headerEnd: index of the '\r' in "\r\n\r\n" (returned by find("\r\n\r\n")).
// - headers: target map to fill with parsed header key/value pairs.
// - body: target string to fill with body content.
// - consumedBytes: on success, total bytes consumed (headers + separator + body).
// Returns true if a complete message (including body) is present; false if incomplete.
bool ParseHeadersAndBody(const std::string& buffer, std::size_t headerEnd,
                         std::unordered_map<std::string, std::string>& headers, std::string& body,
                         std::size_t& consumedBytes);


std::string getContentType(const HttpResponse& response);

bool parseSseLine(const std::string& line, ServerSentEvent& sseEvent);

} // namespace Mcp::Http

#endif // MCP_HTTP_COMMON_INCLUDE_H_
