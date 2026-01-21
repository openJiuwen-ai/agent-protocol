/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */
#include "mcp_client_implement.h"

#include "mcp_log.h"

namespace Mcp {

McpClientImplement::McpClientImplement(const ClientConfig config, std::shared_ptr<ClientTransport> transport,
    std::shared_ptr<AuthProvider> authProvider)
    : transport_(std::move(transport)), authProvider_(std::move(authProvider)), config_(config)
{
}

void McpClientImplement::CheckInitialized()
{
    if (!initialized_) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "client is not initialized.");
        throw std::runtime_error("client is not initialized.");
    }
}

std::future<std::shared_ptr<InitializeResult>> McpClientImplement::Initialize()
{
    if (initialized_) {
        throw std::runtime_error("client is initialized.");
    }
    transport_->Connect();
    session_ = std::make_shared<ClientSession>(transport_, config_);
    if (session_ == nullptr) {
        throw std::runtime_error("Failed to create session layer.");
    }
    initialized_ = true;
    return session_->Initialize();
}

std::future<std::shared_ptr<ListToolsResult>> McpClientImplement::ListTools()
{
    CheckInitialized();
    return session_->ListTools();
}

std::future<std::shared_ptr<CallToolResult>> McpClientImplement::CallTool(const std::string& name,
                                                                          const std::optional<JsonValue>& arguments,
                                                                          int timeout)
{
    CheckInitialized();
    return session_->CallTool(name, arguments, timeout);
}

std::future<std::shared_ptr<ListResourcesResult>> McpClientImplement::ListResources()
{
    CheckInitialized();
    return session_->ListResources();
}

std::future<std::shared_ptr<ReadResourceResult>> McpClientImplement::ReadResource(const std::string& uri)
{
    CheckInitialized();
    return session_->ReadResource(uri);
}

std::future<std::shared_ptr<EmptyResult>> McpClientImplement::SubscribeResource(const std::string& uri)
{
    CheckInitialized();
    return session_->SubscribeResource(uri);
}

std::future<std::shared_ptr<EmptyResult>> McpClientImplement::UnsubscribeResource(const std::string& uri)
{
    CheckInitialized();
    return session_->UnsubscribeResource(uri);
}

std::future<std::shared_ptr<EmptyResult>> McpClientImplement::SetLoggingLevel(const LoggingLevel level)
{
    CheckInitialized();
    return session_->SetLoggingLevel(level);
}

std::future<std::shared_ptr<ListResourceTemplatesResult>> McpClientImplement::ListResourcesTemplates()
{
    CheckInitialized();
    return session_->ListResourcesTemplates();
}

std::future<std::shared_ptr<ListPromptsResult>> McpClientImplement::ListPrompts()
{
    CheckInitialized();
    return session_->ListPrompts();
}

std::future<std::shared_ptr<GetPromptResult>> McpClientImplement::GetPrompt(const std::string& name,
                                                                            const std::optional<JsonValue>& arguments)
{
    CheckInitialized();
    return session_->GetPrompt(name, arguments);
}

void McpClientImplement::SendRootsListChanged()
{
    CheckInitialized();
    session_->SendRootsListChanged();
}

std::future<EmptyResult> McpClientImplement::SendPing()
{
    CheckInitialized();
    return session_->SendPing();
}

ServerCapabilities McpClientImplement::GetServerCapabilities()
{
    CheckInitialized();
    if (session_ == nullptr) {
        throw std::runtime_error("client session is not created.");
    }
    return session_->GetServerCapabilities();
}

std::future<void> McpClientImplement::SendProgressNotification(std::string progressToken, float progress, float total,
                                                               std::string message)
{
    CheckInitialized();
    std::promise<void> promise;
    promise.set_exception(std::make_exception_ptr(
        std::runtime_error("SendProgressNotification is not implemented.")));
    return promise.get_future();
}

std::future<Result> McpClientImplement::Complete(std::string type, std::string uri,
                                                 std::unordered_map<std::string, std::string> extras)
{
    CheckInitialized();
    std::promise<Result> promise;
    promise.set_exception(std::make_exception_ptr(std::runtime_error("Complete is not implemented.")));
    return promise.get_future();
}

} // namespace Mcp
