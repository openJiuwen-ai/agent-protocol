/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "server/server_session.h"

#include <future>
#include <memory>
#include <set>

#include "mcp_log.h"

namespace Mcp {

namespace {

std::vector<Mcp::SamplingMessageContentBlock> ContentAsList(const Mcp::SamplingMessage& message)
{
    if (std::holds_alternative<Mcp::SamplingMessageContentBlock>(message.content)) {
        return {std::get<Mcp::SamplingMessageContentBlock>(message.content)};
    }
    return std::get<std::vector<Mcp::SamplingMessageContentBlock>>(message.content);
}

void ValidateToolResultPreconditions(const std::vector<Mcp::SamplingMessageContentBlock>& lastContent,
    bool hasPrevious, bool hasPreviousToolUse)
{
    for (const auto& c : lastContent) {
        if (!std::holds_alternative<Mcp::ToolResultContent>(c)) {
            throw std::runtime_error(
                "The last message must contain only tool_result content if any is present");
        }
    }
    if (!hasPrevious) {
        throw std::runtime_error("tool_result requires a previous message containing tool_use");
    }
    if (!hasPreviousToolUse) {
        throw std::runtime_error("tool_result blocks do not match any tool_use in the previous message");
    }
}

// Validate tool_use/tool_result message structure.
// 1) Messages with tool_result content contain ONLY tool_result content
// 2) tool_result messages are preceded by a message with tool_use
// 3) tool_result IDs match tool_use IDs from the previous message
void ValidateToolUseResultMessages(const std::vector<Mcp::SamplingMessage>& messages)
{
    if (messages.empty()) {
        return;
    }

    const auto lastContent = ContentAsList(messages.back());
    bool hasToolResults = false;
    for (const auto& c : lastContent) {
        if (std::holds_alternative<Mcp::ToolResultContent>(c)) {
            hasToolResults = true;
            break;
        }
    }

    const bool hasPrevious = messages.size() >= 2;
    const std::vector<Mcp::SamplingMessageContentBlock> previousContent =
        hasPrevious ? ContentAsList(messages[messages.size() - 2]) : std::vector<Mcp::SamplingMessageContentBlock>{};

    bool hasPreviousToolUse = false;
    for (const auto& c : previousContent) {
        if (std::holds_alternative<Mcp::ToolUseContent>(c)) {
            hasPreviousToolUse = true;
            break;
        }
    }

    if (hasToolResults) {
        ValidateToolResultPreconditions(lastContent, hasPrevious, hasPreviousToolUse);
    }

    if (hasPreviousToolUse) {
        std::set<std::string> toolUseIds;
        for (const auto& c : previousContent) {
            if (std::holds_alternative<Mcp::ToolUseContent>(c)) {
                toolUseIds.insert(std::get<Mcp::ToolUseContent>(c).id);
            }
        }

        std::set<std::string> toolResultIds;
        for (const auto& c : lastContent) {
            if (std::holds_alternative<Mcp::ToolResultContent>(c)) {
                toolResultIds.insert(std::get<Mcp::ToolResultContent>(c).toolUseId);
            }
        }

        // tool_result IDs match tool_use IDs from the previous message
        if (toolUseIds != toolResultIds) {
            throw std::runtime_error(
                "ids of tool_result blocks and tool_use blocks from previous message do not match");
        }
    }
}

template <typename T>
std::function<void(std::shared_ptr<Mcp::Result>)> MakeTypedCompletion(
    std::shared_ptr<std::promise<std::shared_ptr<T>>> promise, const char* opName)
{
    return [promise, opName](std::shared_ptr<Mcp::Result> resultPtr) {
        try {
            if (!resultPtr) {
                throw std::runtime_error(std::string(opName) + " failed: null result");
            }

            auto typed = std::dynamic_pointer_cast<T>(resultPtr);
            if (!typed) {
                throw std::runtime_error(std::string(opName) + " failed: result type mismatch");
            }

            promise->set_value(typed);
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    };
}

} // namespace

ServerSession::~ServerSession() = default;

ClientCapabilities ServerSession::GetClientCapabilities() const
{
    if (clientCapabilities_.has_value()) {
        return clientCapabilities_.value();
    }
    return ClientCapabilities{};
}

void ServerSession::SetServerCapabilities(const ServerCapabilities& capabilities)
{
    // Stored and later returned in the InitializeResult during the MCP handshake.
    capabilities_ = capabilities;

    if (capabilities_.resources.has_value()) {
        // NOTE: The server currently does not support resource subscription.
        capabilities_.resources->subscribe = false;
    }
}

void ServerSession::SetIncomingRequestCallback(IncomingRequestCallback callback)
{
    // The callback is invoked for non-initialization requests.
    incomingRequestCallback_ = std::move(callback);
}

void ServerSession::SetIncomingNotificationCallback(IncomingNotificationCallback callback)
{
    // The callback is invoked for non-initialization notifications.
    incomingNotificationCallback_ = std::move(callback);
}

void ServerSession::HandleRequest(const HttpRequest& request, RequestContext& context)
{
    serverTransport_->HandleRequest(request, context);
}

std::future<std::shared_ptr<ListRootsResult>> ServerSession::ListRoots()
{
    if (serverTransport_ == nullptr) {
        throw std::runtime_error("Server transport is not available");
    }

    if (!isInitialized_) {
        throw std::runtime_error("Session is not initialized");
    }

    auto promise = std::make_shared<std::promise<std::shared_ptr<ListRootsResult>>>();
    auto future = promise->get_future();

    auto req = std::make_unique<ListRootsRequest>();
    SendRequest(std::move(req), MakeTypedCompletion<ListRootsResult>(promise, "ListRoots"));

    return future;
}

std::future<std::shared_ptr<CreateMessageResult>> ServerSession::SamplingCreateMessage(
    const CreateMessageRequestParams& params)
{
    if (!isInitialized_) {
        throw std::runtime_error("Session is not initialized");
    }

    const auto caps = GetClientCapabilities();
    const bool supported = caps.tasks.has_value() && caps.tasks->samplingCreateMessage;
    if (!supported) {
        throw std::runtime_error("Client does not support sampling/createMessage");
    }

    ValidateToolUseResultMessages(params.messages);

    auto promise = std::make_shared<std::promise<std::shared_ptr<CreateMessageResult>>>();
    auto future = promise->get_future();

    auto req = std::make_unique<CreateMessageRequest>();
    auto reqParams = std::make_unique<CreateMessageRequestParams>();
    *reqParams = params;
    req->params_ = std::move(reqParams);

    SendRequest(std::move(req), MakeTypedCompletion<CreateMessageResult>(promise, "SamplingCreateMessage"));
    return future;
}

void ServerSession::ReceivedRequest(int64_t requestId, const Request& request, RequestContext& ctx)
{
    // The Initialize request is handled internally because it wires up session
    // state and negotiates capabilities.
    if (request.method_ == "initialize") {
        const auto* initParams = dynamic_cast<const InitializeRequestParams*>(request.params_.get());
        if (initParams != nullptr) {
            HandleInitializeRequest(requestId, *initParams, ctx);
        }
    } else {
        if (!isInitialized_) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "Received request before initialization: %s", request.method_.c_str());
            return;
        }
        if (incomingRequestCallback_) {
            incomingRequestCallback_(requestId, request, ctx);
        }
    }
}

void ServerSession::ReceivedNotification(const Notification& notification)
{
    // The "initialized" notification completes the initialization handshake.
    if (notification.method_ == "notifications/initialized") {
        HandleInitializeNotification(notification);
    } else {
        if (incomingNotificationCallback_) {
            incomingNotificationCallback_(notification);
        }
    }
}

void ServerSession::HandleInitializeRequest(int64_t requestId, const InitializeRequestParams& requestParams,
                                            RequestContext& ctx)
{
    // Store client-advertised capabilities for later use by server handlers.
    clientCapabilities_ = requestParams.capabilities_;

    // Reply to the client with server capabilities and implementation info.
    SendInitializeResponse(requestId, capabilities_, ctx);
}

void ServerSession::SendInitializeResponse(int64_t requestId, const ServerCapabilities& capabilities,
                                           RequestContext& ctx)
{
    // Build MCP InitializeResult payload.
    Implementation serverInfo;
    serverInfo.name = serverConfig_.name;
    serverInfo.version = serverConfig_.version;
    auto initResult = std::make_unique<InitializeResult>(DEFAULT_PROTOCOL_VERSION, capabilities, serverInfo);
    SendResponse(requestId, std::move(initResult), ctx);
    isInitialized_ = true;
}

void ServerSession::HandleInitializeNotification(const Notification& notification)
{
    (void)notification;
    // From here on the server treats the client as fully initialized.
    isInitialized_ = true;
}

void ServerSession::SendNotification(std::unique_ptr<Notification> notification,
                                     std::optional<int64_t> relatedRequestId [[maybe_unused]])
{
    // Build a JSONRPCMessage variant holding a JSONRPCNotification instance
    // and populate only lightweight fields (strings). Do not copy any
    // underlying unique_ptr members from the input notification.
    JSONRPCMessage message{std::in_place_type<JSONRPCNotification>};
    auto& rpcNotif = std::get<JSONRPCNotification>(message);
    rpcNotif.jsonrpc_ = JSONRPC_VERSION;
    rpcNotif.method_ = notification->method_;
    rpcNotif.notification_ = std::move(notification);

    RequestContext dummy{};
    dummy.sessionId = GetSessionId();
    dummy.method = rpcNotif.method_;
    dummy.connectionId = 0;
    serverTransport_->SendMessage(message, dummy);
}

void ServerSession::SendProgressNotification(const std::string& progressToken, double progress,
    std::optional<double> total, const std::optional<std::string>& message)
{
}

void ServerSession::SendToolListChangedNotification()
{
    if (serverTransport_ == nullptr) {
        return;
    }

    auto notif = std::make_unique<ToolListChangedNotification>();
    SendNotification(std::move(notif), std::nullopt);
}

void ServerSession::SendPromptListChangedNotification()
{
    if (serverTransport_ == nullptr) {
        return;
    }

    auto notif = std::make_unique<PromptListChangedNotification>();
    SendNotification(std::move(notif), std::nullopt);
}

void ServerSession::SendResourceListChangedNotification()
{
    if (serverTransport_ == nullptr) {
        return;
    }

    auto notif = std::make_unique<ResourceListChangedNotification>();
    SendNotification(std::move(notif), std::nullopt);
}

} // namespace Mcp
