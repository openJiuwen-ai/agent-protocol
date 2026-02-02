/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "client_session.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <nlohmann/json-schema.hpp>
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
            if (resultPtr == nullptr) {
                throw std::runtime_error(std::string(opName) + " failed: null result");
            }

            auto typed = std::dynamic_pointer_cast<T>(resultPtr);
            if (typed == nullptr) {
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
            if (initPtr == nullptr) {
                throw std::runtime_error("Result type mismatch: cannot cast to InitializeResult");
            }
            if (std::find(SUPPORTED_PROTOCOL_VERSIONS.begin(), SUPPORTED_PROTOCOL_VERSIONS.end(),
                          initPtr->protocolVersion) == SUPPORTED_PROTOCOL_VERSIONS.end()) {
                throw std::runtime_error("Unsupported protocol version from the server: " + initPtr->protocolVersion);
            }
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

std::future<std::shared_ptr<CallToolResult>> ClientSession::CallTool(const std::string& name,
                                                                     const std::optional<JsonValue>& arguments,
                                                                     int timeout)
{
    auto promise = std::make_shared<std::promise<std::shared_ptr<CallToolResult>>>();
    auto future = promise->get_future();
    auto params = std::make_unique<CallToolParams>(name, arguments);
    auto req = std::make_unique<CallToolRequest>();
    req->params_ = std::move(params);

    auto completion = [this, promise, name](std::shared_ptr<Result> resultPtr) {
        try {
            auto callToolPtr = std::dynamic_pointer_cast<CallToolResult>(resultPtr);
            if (callToolPtr == nullptr) {
                throw std::runtime_error("Result type mismatch: cannot cast to CallToolResult");
            }
            if (callToolPtr->isError == false && toolOutputSchemas_.find(name) != toolOutputSchemas_.end()) {
                ValidateToolResult(name, *callToolPtr);
            }
            promise->set_value(callToolPtr);
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    };

    SendRequest(std::move(req), completion);

    return future;
}

std::future<std::shared_ptr<ListToolsResult>> ClientSession::ListTools()
{
    return ListTools(std::nullopt);
}

std::future<std::shared_ptr<ListToolsResult>> ClientSession::ListTools(const std::optional<std::string>& cursor)
{
    auto promise = std::make_shared<std::promise<std::shared_ptr<ListToolsResult>>>();
    auto future = promise->get_future();
    auto req = std::make_unique<ListToolsRequest>();
    if (cursor.has_value()) {
        auto params = std::make_unique<RequestParams>();
        params->cursor = cursor;
        req->params_ = std::move(params);
    }

    auto completion = [this, promise](std::shared_ptr<Result> resultPtr) {
        try {
            auto listToolPtr = std::dynamic_pointer_cast<ListToolsResult>(resultPtr);
            if (listToolPtr == nullptr) {
                throw std::runtime_error("Result type mismatch: cannot cast to ListToolsResult");
            }
            CacheToolSchemas(*listToolPtr);
            promise->set_value(listToolPtr);
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    };

    SendRequest(std::move(req), completion);

    return future;
}

// Send notifications/initialized implementation
void ClientSession::SendInitializedNotification()
{
    InitializedNotification notification;
    notification.method_ = "notifications/initialized";

    // Delegate to BaseSession notification sending. Related request id is
    // not associated for this simple notification.
    SendNotification(notification, std::nullopt);
}

// send_notification implementation
void ClientSession::SendNotification(const Notification& notification, std::optional<int64_t> relatedRequestId)
{
    // Build a JSONRPCMessage variant holding a JSONRPCNotification instance
    // and populate only lightweight fields (strings). Do not copy any
    // underlying unique_ptr members from the input notification.
    JSONRPCMessage message{std::in_place_type<JSONRPCNotification>};
    auto& notif = std::get<JSONRPCNotification>(message);
    notif.jsonrpc_ = JSONRPC_VERSION;
    notif.method_ = notification.method_;

    // relatedRequestId is currently unused in wire format; kept for API parity
    (void)relatedRequestId;

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

void ClientSession::CacheToolSchemas(const ListToolsResult& r)
{
    for (const auto& tool : r.tools) {
        if (tool.outputSchema.has_value()) {
            auto schemaJson = nlohmann::json::parse(tool.outputSchema.value());
            toolOutputSchemas_[tool.name] = schemaJson;
        }
    }
}

void ClientSession::ValidateToolResult(const std::string& name, const CallToolResult& result)
{
    if (result.structuredContent.has_value() == false) {
        return;
    }
    auto it = toolOutputSchemas_.find(name);
    if (it == toolOutputSchemas_.end()) {
        return;
    }

    try {
        nlohmann::json data = nlohmann::json::parse(result.structuredContent.value());
        if (data.is_object() == false) {
            throw std::runtime_error("structuredContent must be a JSON object");
        }
        nlohmann::json_schema::json_validator validator;
        validator.set_root_schema(it->second);
        validator.validate(data);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Invalid structured content for tool ") + name + ": " + e.what());
    }
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

std::future<std::shared_ptr<EmptyResult>> ClientSession::SendPing()
{
    auto promise = std::make_shared<std::promise<std::shared_ptr<EmptyResult>>>();
    auto future = promise->get_future();
    auto req = std::make_unique<PingRequest>();
    SendRequest(std::move(req), MakeTypedCompletion<EmptyResult>(promise, "SendPing"));
    return future;
}

void ClientSession::SendRootsListChanged()
{
    // Empty implementation
}

std::future<std::shared_ptr<ListResourcesResult>> ClientSession::ListResources()
{
    return ListResources(std::nullopt);
}

std::future<std::shared_ptr<ListResourcesResult>> ClientSession::ListResources(
    const std::optional<std::string>& cursor)
{
    auto promise = std::make_shared<std::promise<std::shared_ptr<ListResourcesResult>>>();
    auto future = promise->get_future();
    auto req = std::make_unique<ListResourcesRequest>();

    if (cursor.has_value()) {
        auto params = std::make_unique<RequestParams>();
        params->cursor = cursor;
        req->params_ = std::move(params);
    }

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
