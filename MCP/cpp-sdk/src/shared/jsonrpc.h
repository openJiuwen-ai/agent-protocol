/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef MCP_JSONRPC_INCLUDE_H_
#define MCP_JSONRPC_INCLUDE_H_

#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "common_type.h"
#include "mcp_type.h"

namespace Mcp {

// Base class for pure parameter data
struct RequestParams {
    virtual ~RequestParams() = default;
};

// Base for JSON-RPC envelopes
struct JSONRPC {
    virtual ~JSONRPC() = default;

    virtual std::string Serialize(const std::string& method) const = 0;
    virtual int Deserialize(const std::string& jsonStr, const std::string& method) = 0;
};

struct Request : public MCPBaseType {
    std::string method_;
    std::unique_ptr<RequestParams> params_;

    Request() = default;
    virtual ~Request() = default;

    Request(const Request&) = delete;
    Request& operator=(const Request&) = delete;

    Request(Request&&) noexcept = default;
    Request& operator=(Request&&) noexcept = default;
};

class JSONRPCRequest : public JSONRPC {
public:
    std::string jsonrpc_;
    int64_t id_;
    std::string method_;
    std::unique_ptr<Request> request_;

    JSONRPCRequest();

    std::string Serialize(const std::string& method) const override;
    int Deserialize(const std::string& jsonStr, const std::string& method) override;
};

class JSONRPCResponse : public JSONRPC {
public:
    std::string jsonrpc_;
    int64_t id_;
    std::shared_ptr<Result> result_;

    JSONRPCResponse();

    std::string Serialize(const std::string& method) const override;
    int Deserialize(const std::string& jsonStr, const std::string& method) override;
};

// Base class for notification parameter data
struct NotificationParams {
    virtual ~NotificationParams() = default;
};

// Notification base class
struct Notification : public MCPBaseType {
    std::string method_;
    std::unique_ptr<NotificationParams> params_;

    Notification() = default;
    virtual ~Notification() = default;

    Notification(const Notification&) = delete;
    Notification& operator=(const Notification&) = delete;
    Notification(Notification&&) noexcept = default;
    Notification& operator=(Notification&&) noexcept = default;
};

class JSONRPCNotification : public JSONRPC {
public:
    std::string jsonrpc_;
    std::string method_;
    std::unique_ptr<Notification> notification_;

    JSONRPCNotification();

    std::string Serialize(const std::string& method) const override;
    int Deserialize(const std::string& jsonStr, const std::string& method) override;
};

// JSON-RPC error response structure
struct JSONRPCError {
    std::string jsonrpc_;
    int64_t id_;
    int code_;
    std::string message_;
    std::optional<nlohmann::json> data_;

    JSONRPCError() : jsonrpc_(JSONRPC_VERSION), id_(0), code_(-1), message_("Internal error"), data_(std::nullopt)
    {
    }

    std::string Serialize() const;
    int Deserialize(const std::string& jsonStr);
};

using JSONRPCMessage = std::variant<JSONRPCRequest, JSONRPCResponse, JSONRPCNotification, JSONRPCError>;

JSONRPCMessage DeserializeJSONRPCMessage(const std::string& jsonStr, const std::string& method);
std::string SerializeJSONRPCMessage(const JSONRPCMessage& message, std::optional<std::string> method = std::nullopt);

struct InitializeRequestParams : public RequestParams {
    std::string protocolVersion_;
    ClientCapabilities capabilities_;
    Implementation clientInfo_;

    InitializeRequestParams(std::string protocolVersion, ClientCapabilities capabilities, Implementation clientInfo);
};

struct InitializeRequest : public Request {
    InitializeRequest();
    InitializeRequest(const std::string& clientName, const std::string& clientVersion,
                      ClientCapabilities capabilities = ClientCapabilities{});
};

struct ListPromptsRequest : public Request {
    ListPromptsRequest();
};

struct ListToolsParams : public RequestParams {
    std::optional<std::string> cursor;

    ListToolsParams(std::optional<std::string> c = std::nullopt) : cursor(std::move(c))
    {
    }
};

struct ListToolsRequest : public Request {
    ListToolsRequest();
};

struct CallToolParams : public RequestParams {
    std::string name;
    std::optional<JsonValue> arguments;

    CallToolParams(const std::string& tool_name, std::optional<JsonValue> args = std::nullopt);
};

struct CallToolRequest : public Request {
    CallToolRequest();
};

struct GetPromptParams : public RequestParams {
    std::string name;
    std::optional<JsonValue> arguments;

    GetPromptParams(const std::string& prompt_name, std::optional<JsonValue> args = std::nullopt);
};

struct GetPromptRequest : public Request {
    GetPromptRequest();
};

struct InitializedNotification : public Notification {
    InitializedNotification();
};

struct ReadResourceRequestParams : public RequestParams {
    std::string uri_;

    ReadResourceRequestParams(const std::string& uri) : uri_(uri)
    {
    }
};

struct ReadResourceRequest : public Request {
    ReadResourceRequest();
};

// resources/subscribe
struct SubscribeRequestParams : public RequestParams {
    std::string uri_;

    SubscribeRequestParams(const std::string& uri) : uri_(uri)
    {
    }
};

struct SubscribeRequest : public Request {
    SubscribeRequest();
};

// resources/unsubscribe
struct UnsubscribeRequestParams : public RequestParams {
    std::string uri_;

    UnsubscribeRequestParams(const std::string& uri) : uri_(uri)
    {
    }
};

struct UnsubscribeRequest : public Request {
    UnsubscribeRequest();
};

struct ListResourcesRequest : public Request {
    ListResourcesRequest();
};

struct SetLoggingLevelParams : public RequestParams {
    std::string level;

    explicit SetLoggingLevelParams(const std::string& level) : level(level) {}
};

struct SetLoggingLevelRequest : public Request {
    SetLoggingLevelRequest();
};

// resources/templates/list
struct ListResourceTemplatesRequest : public Request {
    ListResourceTemplatesRequest();
};

// roots/list
struct ListRootsRequest : public Request {
    ListRootsRequest();
};

struct ResourceUpdatedNotificationParams : public NotificationParams {
    std::string uri;

    explicit ResourceUpdatedNotificationParams(const std::string &resourceUri) : uri(resourceUri) {}
};

struct ResourceUpdatedNotification : public Notification {
    ResourceUpdatedNotification();
};

struct ToolListChangedNotification : public Notification {
    ToolListChangedNotification();
};

struct PromptListChangedNotification : public Notification {
    PromptListChangedNotification();
};

struct ResourceListChangedNotification : public Notification {
    ResourceListChangedNotification();
};

inline const char* ToString(LoggingLevel lvl)
{
    switch (lvl) {
        case LoggingLevel::Debug:    return "debug";
        case LoggingLevel::Info:     return "info";
        case LoggingLevel::Notice:   return "notice";
        case LoggingLevel::Warning:  return "warning";
        case LoggingLevel::Error:    return "error";
        case LoggingLevel::Critical: return "critical";
        case LoggingLevel::Alert:    return "alert";
        case LoggingLevel::Emergency:return "emergency";
    }
    return "unknown";
}

// completion/complete
struct CompleteRequestParams : public RequestParams {
    CompleteReference ref;
    CompletionArgument argument;
    std::optional<CompletionContext> context;

    CompleteRequestParams() = default;
    CompleteRequestParams(CompleteReference r, CompletionArgument arg,
        std::optional<CompletionContext> ctx = std::nullopt)
        : ref(std::move(r)), argument(std::move(arg)), context(std::move(ctx))
    {
    }
};

struct CompleteRequest : public Request {
    CompleteRequest();
};

} // namespace Mcp

#endif // MCP_JSONRPC_INCLUDE_H_
