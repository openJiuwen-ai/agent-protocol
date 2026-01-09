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

// Initialize implementation
std::future<std::shared_ptr<InitializeResult>> ClientSession::Initialize()
{
    auto request = std::make_unique<InitializeRequest>(clientConfig_.name, clientConfig_.version);
    auto promise = std::make_shared<std::promise<std::shared_ptr<InitializeResult>>>();
    auto future = promise->get_future();

    auto completion = [this, promise](std::shared_ptr<Result> resultPtr) {
        try {
            auto initPtr = std::dynamic_pointer_cast<InitializeResult>(resultPtr);
            if (!initPtr) {
                throw std::runtime_error("Result type mismatch: cannot cast to InitializeResult");
            }
            initialized_ = true;
            promise->set_value(initPtr);
            SendInitializedNotification();
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    };

    SendRequest(std::move(request), completion);

    return future;
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

    // relatedRequestId is currently unused in wire format; kept for API parity

    if (clientTransport_ != nullptr) {
        clientTransport_->SendMessage(message);
    }
}

// Handle incoming request (example: log the method)
void ClientSession::HandleRequest(const Mcp::JSONRPCRequest& request)
{
}

// OnMessageReceived implementation
void ClientSession::OnMessageReceived(const std::string& messageJson)
{
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

} // namespace Mcp
