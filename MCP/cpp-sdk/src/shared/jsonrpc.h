/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef MCP_JSONRPC_INCLUDE_H_
#define MCP_JSONRPC_INCLUDE_H_

#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "common_type.h"
#include "mcp_type.h"

namespace Mcp {

/** Optional _meta that may exist on any request params (e.g. progressToken for MCP progress). */
struct RequestParamsMeta {
    std::optional<ProgressToken> progressToken;
};

// Base class for pure parameter data
struct RequestParams {
    virtual ~RequestParams() = default;
    /** Optional _meta, serialized as params._meta when present. */
    std::optional<RequestParamsMeta> _meta;
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

// Best-effort helpers for transports.
// JSON-RPC responses/errors carry "id" but do not carry "method".
// Our internal message envelopes use int64_t ids.
std::optional<int64_t> TryGetJsonRpcResponseId(const nlohmann::json& j);

// Consume (move + erase) the pending request method for a JSON-RPC response/error.
// Returns nullopt if the message is not a response/error, id is missing/invalid, or id is not found.
std::optional<std::string> TryTakePendingMethodForJsonRpcResponse(const nlohmann::json& messageJson,
    std::unordered_map<int64_t, std::string>& pendingMethods);

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

struct ElicitRequestFormParams : public RequestParams {
    std::string mode; // "form"
    std::string message;
    MetaMap requestedSchema;
};

struct ElicitRequestUrlParams : public RequestParams {
    std::string mode; // "url"
    std::string message;
    std::string url;
    std::string elicitationId;
};

struct ElicitRequest : public Request {
    ElicitRequest();
};

struct CancelledNotificationParams : public NotificationParams {
    int64_t requestId;
    std::string reason;

    explicit CancelledNotificationParams(const int64_t requestId,
        const std::string& reason) : requestId(requestId), reason(reason) {}
};

struct CancelledNotification : public Notification {
    CancelledNotification();
};

// resources/templates/list
struct ListResourceTemplatesRequest : public Request {
    ListResourceTemplatesRequest();
};

// roots/list
struct ListRootsRequest : public Request {
    ListRootsRequest();
};

// sampling/createMessage
struct CreateMessageRequestParams : public RequestParams, public CreateMessageParams {
    CreateMessageRequestParams() = default;
    explicit CreateMessageRequestParams(const CreateMessageParams& params)
    {
        static_cast<CreateMessageParams&>(*this) = params;
    }
};

struct CreateMessageRequest : public Request {
    CreateMessageRequest();
};

struct ResourceUpdatedNotificationParams : public NotificationParams {
    std::string uri;

    explicit ResourceUpdatedNotificationParams(const std::string &resourceUri) : uri(resourceUri) {}
};

struct ResourceUpdatedNotification : public Notification {
    ResourceUpdatedNotification();
};

struct LoggingMessageNotificationParams : public NotificationParams {
    std::string level;
    std::string data;
    std::string logger;

    explicit LoggingMessageNotificationParams(const std::string &level,
            const std::string &data, const std::string &logger) : level(level), data(data), logger(logger)  {}
};

struct LoggingMessageNotification : public Notification {
    LoggingMessageNotification();
};

/** Params for notifications/progress (MCP progress tracking). */
struct ProgressNotificationParams : public NotificationParams {
    ProgressToken progressToken;
    double progress{0.0};
    std::optional<double> total;
    std::optional<std::string> message;
};

struct ProgressNotification : public Notification {
    ProgressNotification();
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

struct RootsListChangedNotification : public Notification {
    RootsListChangedNotification();
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
