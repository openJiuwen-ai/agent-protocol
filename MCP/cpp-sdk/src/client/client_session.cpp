/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "client_session.h"

#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>

#include "mcp_log.h"
#include "shared/jsonrpc.h"

namespace {

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
} // anonymous namespace

namespace Mcp {

ClientSession::ClientSession(std::shared_ptr<ClientTransport> transport, const ClientConfig& clientConfig,
                             std::string sessionId)
    : BaseSession(std::move(transport), std::nullopt, sessionId), clientConfig_(clientConfig)
{
}

ClientCapabilities ClientSession::BuildClientCapabilities() const
{
    ClientCapabilities caps;
    // Only advertise `roots` when the client can handle roots/list.
    if (listRootsCallback_) {
        // Change the value to true after ListRoot is fully implemented.
        caps.roots = RootsCapability{.listChanged = false};
    }
    return caps;
}

void ClientSession::SetListRootsCallback(ListRootsCallback cb)
{
    listRootsCallback_ = std::move(cb);
}

void ClientSession::SetLoggingCallback(LoggingCallback cb)
{
    loggingCallback_ = std::move(cb);
}

// Initialize implementation
std::future<std::shared_ptr<InitializeResult>> ClientSession::Initialize()
{
    // Advertise client capabilities based on which callbacks are actually configured.
    // Only advertise `roots` when the client can handle roots/list.
    auto request = std::make_unique<InitializeRequest>(clientConfig_.name, clientConfig_.version,
                                                       BuildClientCapabilities());

    auto promise = std::make_shared<std::promise<std::shared_ptr<InitializeResult>>>();
    auto future = promise->get_future();

    auto completion = [this, promise](std::shared_ptr<Result> resultPtr) {
        try {
            auto initPtr = std::dynamic_pointer_cast<InitializeResult>(resultPtr);
            if (!initPtr) {
                throw std::runtime_error("Result type mismatch: cannot cast to InitializeResult");
            }

            // Persist server capabilities for users to query later.
            serverCapabilities_ = initPtr->capabilities;
            SendInitializedNotification();
            initialized_ = true;
            promise->set_value(initPtr);
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    };

    SendRequest(std::move(request), completion);

    return future;
}

ServerCapabilities ClientSession::GetServerCapabilities() const
{
    if (serverCapabilities_.has_value()) {
        return serverCapabilities_.value();
    }
    return ServerCapabilities{};
}

std::future<std::shared_ptr<CallToolResult>> ClientSession::CallTool(const std::string& name,
                                                                     const std::optional<JsonValue>& arguments,
                                                                     int timeout)
{
    auto promise = std::make_shared<std::promise<std::shared_ptr<CallToolResult>>>();
    auto future = promise->get_future();
    auto params = std::make_unique<CallToolParams>(name, arguments);
    auto req = std::make_unique<CallToolRequest>();
    req->params_ = std::move(params);
    SendRequest(std::move(req), MakeTypedCompletion<CallToolResult>(promise, "CallTool"));

    return future;
}

std::future<std::shared_ptr<ListToolsResult>> ClientSession::ListTools()
{
    auto promise = std::make_shared<std::promise<std::shared_ptr<ListToolsResult>>>();
    auto future = promise->get_future();
    auto req = std::make_unique<ListToolsRequest>();
    SendRequest(std::move(req), MakeTypedCompletion<ListToolsResult>(promise, "ListTools"));

    return future;
}

// Send notifications/initialized implementation
void ClientSession::SendInitializedNotification()
{
    auto notification = std::make_unique<InitializedNotification>();
    notification->method_ = "notifications/initialized";

    // Delegate to BaseSession notification sending. Related request id is
    // not associated for this simple notification.
    SendNotification(std::move(notification), std::nullopt);
}

// send_notification implementation
void ClientSession::SendNotification(std::unique_ptr<Notification> notification,
                                     std::optional<int64_t> relatedRequestId [[maybe_unused]])
{
    // Build a JSONRPCMessage variant holding a JSONRPCNotification instance
    // and move the Notification instance so params_ can be serialized.
    JSONRPCMessage message{std::in_place_type<JSONRPCNotification>};
    auto& notif = std::get<JSONRPCNotification>(message);
    notif.jsonrpc_ = JSONRPC_VERSION;
    notif.method_ = notification->method_;
    notif.notification_ = std::move(notification);

    if (clientTransport_ != nullptr) {
        clientTransport_->SendMessage(message);
    }
}

void ClientSession::SendJsonRpcError(int64_t requestId, JsonRpcErrorCode code, const std::string& message,
                                     RequestContext& ctx)
{
    JSONRPCError error;
    error.jsonrpc_ = JSONRPC_VERSION;
    error.id_ = requestId;
    error.code_ = static_cast<int>(code);
    error.message_ = message;
    SendResponse(requestId, std::move(error), ctx);
}

void ClientSession::SendMethodNotFound(int64_t requestId, const std::string& method, RequestContext& ctx)
{
    SendJsonRpcError(requestId, JsonRpcErrorCode::METHOD_NOT_FOUND, "Method not found: " + method, ctx);
}

void ClientSession::SendInternalError(int64_t requestId, const std::string& message, RequestContext& ctx)
{
    SendJsonRpcError(requestId, JsonRpcErrorCode::INTERNAL_ERROR, message, ctx);
}

void ClientSession::HandleRootsListRequest(int64_t requestId, const Request& request, RequestContext& ctx)
{
    if (!listRootsCallback_) {
        SendMethodNotFound(requestId, request.method_, ctx);
        return;
    }

    try {
        ListRootsResult result = listRootsCallback_();
        auto resultPtr = std::make_unique<ListRootsResult>(std::move(result));
        SendResponse(requestId, std::move(resultPtr), ctx);
    } catch (const std::exception& e) {
        SendInternalError(requestId, e.what(), ctx);
    }
}

void ClientSession::ReceivedRequest(int64_t requestId, const Request& request, RequestContext& ctx)
{
    if (clientTransport_ == nullptr) {
        return;
    }

    const std::string& method = request.method_;
    if (method == "roots/list") {
        HandleRootsListRequest(requestId, request, ctx);
        return;
    }

    // Default: explicit JSON-RPC error so the server doesn't hang waiting for a response.
    SendMethodNotFound(requestId, method, ctx);
}

std::future<std::shared_ptr<ListPromptsResult>> ClientSession::ListPrompts()
{
    auto promise = std::make_shared<std::promise<std::shared_ptr<ListPromptsResult>>>();
    auto future = promise->get_future();
    auto listPromptsRequest = std::make_unique<ListPromptsRequest>();
    SendRequest(std::move(listPromptsRequest), MakeTypedCompletion<ListPromptsResult>(promise, "ListPrompts"));

    return future;
}

std::future<std::shared_ptr<GetPromptResult>> ClientSession::GetPrompt(const std::string& name,
                                                                       const std::optional<JsonValue>& arguments)
{
    auto promise = std::make_shared<std::promise<std::shared_ptr<GetPromptResult>>>();
    auto future = promise->get_future();
    auto params = std::make_unique<GetPromptParams>(name, arguments);
    auto getPromptRequest = std::make_unique<GetPromptRequest>();
    getPromptRequest->params_ = std::move(params);
    SendRequest(std::move(getPromptRequest), MakeTypedCompletion<GetPromptResult>(promise, "GetPrompt"));

    return future;
}

std::future<EmptyResult> ClientSession::SendPing()
{
    // Empty implementation
    std::promise<EmptyResult> promise;
    promise.set_value(EmptyResult{});
    return promise.get_future();
}

void ClientSession::SendRootsListChanged()
{
    // Empty implementation
}

std::future<std::shared_ptr<ListResourcesResult>> ClientSession::ListResources()
{
    auto promise = std::make_shared<std::promise<std::shared_ptr<ListResourcesResult>>>();
    auto future = promise->get_future();
    auto req = std::make_unique<ListResourcesRequest>();
    SendRequest(std::move(req), MakeTypedCompletion<ListResourcesResult>(promise, "ListResources"));

    return future;
}

std::future<std::shared_ptr<ListResourceTemplatesResult>> ClientSession::ListResourcesTemplates()
{
    auto promise = std::make_shared<std::promise<std::shared_ptr<ListResourceTemplatesResult>>>();
    auto future = promise->get_future();
    auto req = std::make_unique<ListResourceTemplatesRequest>();
    SendRequest(std::move(req), MakeTypedCompletion<ListResourceTemplatesResult>(promise, "ListResourcesTemplates"));

    return future;
}

std::future<std::shared_ptr<ReadResourceResult>> ClientSession::ReadResource(const std::string& uri)
{
    auto promise = std::make_shared<std::promise<std::shared_ptr<ReadResourceResult>>>();
    auto future = promise->get_future();
    auto req = std::make_unique<ReadResourceRequest>();
    req->params_ = std::make_unique<ReadResourceRequestParams>(uri);
    SendRequest(std::move(req), MakeTypedCompletion<ReadResourceResult>(promise, "ReadResource"));

    return future;
}

std::future<std::shared_ptr<EmptyResult>> ClientSession::SubscribeResource(const std::string& uri)
{
    auto promise = std::make_shared<std::promise<std::shared_ptr<EmptyResult>>>();
    auto future = promise->get_future();
    auto req = std::make_unique<SubscribeRequest>();
    req->params_ = std::make_unique<SubscribeRequestParams>(uri);
    SendRequest(std::move(req), MakeTypedCompletion<EmptyResult>(promise, "SubscribeResource"));

    return future;
}

std::future<std::shared_ptr<EmptyResult>> ClientSession::UnsubscribeResource(const std::string& uri)
{
    auto promise = std::make_shared<std::promise<std::shared_ptr<EmptyResult>>>();
    auto future = promise->get_future();
    auto req = std::make_unique<UnsubscribeRequest>();
    req->params_ = std::make_unique<UnsubscribeRequestParams>(uri);
    SendRequest(std::move(req), MakeTypedCompletion<EmptyResult>(promise, "UnsubscribeResource"));

    return future;
}
std::future<std::shared_ptr<CompleteResult>> ClientSession::Complete(
    const CompleteReference& ref, const CompletionArgument& argument,
    const std::optional<CompletionContext>& context)
{
    auto promise = std::make_shared<std::promise<std::shared_ptr<CompleteResult>>>();
    auto future = promise->get_future();
    auto params = std::make_unique<CompleteRequestParams>(ref, argument, context);
    auto req = std::make_unique<CompleteRequest>();
    req->params_ = std::move(params);
    SendRequest(std::move(req), MakeTypedCompletion<CompleteResult>(promise, "Complete"));

    return future;
}


std::future<std::shared_ptr<EmptyResult>> ClientSession::SetLoggingLevel(LoggingLevel level)
{
    auto promise = std::make_shared<std::promise<std::shared_ptr<EmptyResult>>>();
    auto future = promise->get_future();
    auto req = std::make_unique<SetLoggingLevelRequest>();
    req->params_ = std::make_unique<SetLoggingLevelParams>(Mcp::ToString(level));
    SendRequest(std::move(req), MakeTypedCompletion<EmptyResult>(promise, "SetLoggingLevel"));

    return future;
}

void ClientSession::ReceivedNotification(const Notification& notification)
{
    // The "initialized" notification completes the initialization handshake.
    if (notification.method_ == "notifications/message") {
        auto params = dynamic_cast<LoggingMessageNotificationParams*>(notification.params_.get());
        if (params == nullptr) {
            throw std::invalid_argument("notifications/message get no params");
        }
        if (loggingCallback_ == nullptr) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "Received Server logging. level is %s, data is %s, logger is %s",
                params->level.c_str(), params->data.c_str(), params->logger.c_str());
            return;
        }
        loggingCallback_(params->level, params->data, params->logger);
    } else {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "undefined notification: %s", notification.method_);
    }
}

} // namespace Mcp
