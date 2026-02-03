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

// LATEST_PROTOCOL_VERSION is the latest protocol version supported by the SDK
const char* const LATEST_PROTOCOL_VERSION = "2025-06-18";

// Default protocol version for Initialize request/response
const char* const DEFAULT_PROTOCOL_VERSION = "2025-03-26";

// Protocol version constants
const std::vector<std::string> SUPPORTED_PROTOCOL_VERSIONS = {"2025-03-26", LATEST_PROTOCOL_VERSION};
const std::string SUPPORTED_PROTOCOL_VERSIONS_STRING = std::string("2025-03-26, ") + LATEST_PROTOCOL_VERSION;

constexpr unsigned int MAX_THREAD_NUM = 64;

enum class LoggingLevel { Debug = 0, Info, Notice, Warning, Error, Critical, Alert, Emergency };

struct SetLevelRequestParams {
    LoggingLevel level;
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

    bool isGetStream = false;

    HttpSendFunc httpSendFunc;
};

} // namespace Mcp

#endif // MCP_COMMON_TYPE_INCLUDE_H_
