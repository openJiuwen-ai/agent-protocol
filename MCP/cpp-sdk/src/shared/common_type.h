/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef MCP_COMMON_TYPE_INCLUDE_H_
#define MCP_COMMON_TYPE_INCLUDE_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

#include "shared/http_common.h"

namespace Mcp {

// JSON-RPC version constant
const char* const JSONRPC_VERSION = "2.0";

// JSON-RPC request ID type (can be int64_t or string)
using RequestId = std::variant<int64_t, std::string>;

using ConnectionId = int;

// Default protocol version for Initialize request/response
const char* const DEFAULT_PROTOCOL_VERSION = "2025-03-26";

// Protocol version constants
static const std::vector<std::string> SUPPORTED_PROTOCOL_VERSIONS = {"2024-11-05", "2025-03-26", "2025-06-18"};
static const std::string SUPPORTED_PROTOCOL_VERSIONS_STRING = "2024-11-05, 2025-03-26, 2025-06-18";

// JSON-RPC 2.0 error codes
enum class JsonRpcErrorCode : int {
    PARSE_ERROR = -32700,
    INVALID_REQUEST = -32600,
    METHOD_NOT_FOUND = -32601,
    INVALID_PARAMS = -32602,
    INTERNAL_ERROR = -32603,
    SERVER_ERROR = -32000 // -32000 to -32099 reserved for server errors
};

// JSON-RPC error data structure
struct ErrorData {
    int code;
    std::string message;
    std::optional<nlohmann::json> data; // Optional additional error information

    ErrorData()
        : code(static_cast<int>(JsonRpcErrorCode::INTERNAL_ERROR)), message("Internal error"), data(std::nullopt)
    {
    }
    ErrorData(int c, const std::string& msg) : code(c), message(msg), data(std::nullopt)
    {
    }
    ErrorData(JsonRpcErrorCode c, const std::string& msg) : code(static_cast<int>(c)), message(msg), data(std::nullopt)
    {
    }
};

// JSON-RPC error response structure
struct JsonRpcError {
    std::string jsonrpc; // Fixed to "2.0"
    RequestId id; // Request ID that caused the error
    ErrorData error; // Error information

    JsonRpcError() : jsonrpc(JSONRPC_VERSION), id(0), error()
    {
    }
    JsonRpcError(const ErrorData& err, const RequestId& id) : jsonrpc(JSONRPC_VERSION), id(id), error(err)
    {
    }
};

// JSON-RPC request structure (with id)
struct JsonRpcRequest {
    std::string jsonrpc; // Fixed to "2.0"
    RequestId id; // Request ID (int64_t or string)
    std::string method;
    std::optional<nlohmann::json> params; // Can be object or array

    JsonRpcRequest() : jsonrpc(JSONRPC_VERSION), id(0), method(""), params(std::nullopt)
    {
    }
    JsonRpcRequest(const std::string& method, const nlohmann::json& params, const RequestId& id)
        : jsonrpc(JSONRPC_VERSION), id(id), method(method), params(params)
    {
    }
};

// JSON-RPC notification structure (without id)
struct JsonRpcNotification {
    std::string jsonrpc; // Fixed to "2.0"
    std::string method;
    std::optional<nlohmann::json> params; // Can be object or array

    JsonRpcNotification() : jsonrpc(JSONRPC_VERSION), method(""), params(std::nullopt)
    {
    }
    JsonRpcNotification(const std::string& method, const nlohmann::json& params)
        : jsonrpc(JSONRPC_VERSION), method(method), params(params)
    {
    }
};

// JSON-RPC response structure (success only)
struct JsonRpcResponse {
    std::string jsonrpc; // Fixed to "2.0"
    RequestId id; // Must match the request id
    nlohmann::json result;

    JsonRpcResponse() : jsonrpc(JSONRPC_VERSION), id(0), result(nlohmann::json::object())
    {
    }
    JsonRpcResponse(const nlohmann::json& result, const RequestId& id)
        : jsonrpc(JSONRPC_VERSION), id(id), result(result)
    {
    }
};

// forward declaration
struct RequestContext;

// HTTP send function type
using HttpSendFunc = std::function<void(const Http::HttpResponse& response, const RequestContext& ctx)>;

using HttpHandler = std::function<void(const Http::HttpRequest& request, RequestContext& ctx)>;

// forward declaration
struct RequestContext {
    ConnectionId connectionId;
    std::string sessionId;
    std::string method;

    HttpSendFunc httpSendFunc;
};

} // namespace Mcp

#endif // MCP_COMMON_TYPE_INCLUDE_H_
