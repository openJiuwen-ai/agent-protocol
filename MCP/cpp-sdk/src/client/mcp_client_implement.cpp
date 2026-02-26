/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */
#include "mcp_client_implement.h"

#include "mcp_log.h"

namespace Mcp {

McpClientImplement::McpClientImplement(const ClientConfig config, std::shared_ptr<ClientTransport> transport)
    : transport_(std::move(transport)), config_(config)
{
}

McpClientImplement::~McpClientImplement()
{
    if (transport_) {
        transport_->Terminate();
        transport_.reset();
    }
    if (session_) {
        session_.reset();
    }
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

std::future<std::shared_ptr<ListToolsResult>> McpClientImplement::ListTools(
    const std::optional<std::string>& cursor)
{
    CheckInitialized();
    if (cursor.has_value()) {
        return session_->ListTools(cursor);
    }
    return session_->ListTools();
}

std::future<std::shared_ptr<CallToolResult>> McpClientImplement::CallTool(const std::string& name,
                                                                          const std::optional<std::string>& arguments,
                                                                          int timeout)
{
    CheckInitialized();
    // Convert string to JsonValue for internal session usage
    std::optional<JsonValue> argsJson = std::nullopt;
    if (arguments.has_value()) {
        argsJson = JsonValue::parse(arguments.value());
    }
    return session_->CallTool(name, argsJson, timeout);
}

std::future<std::shared_ptr<ListResourcesResult>> McpClientImplement::ListResources(
    const std::optional<std::string>& cursor)
{
    CheckInitialized();
    if (cursor.has_value()) {
        return session_->ListResources(cursor);
    }
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
                                                                            const std::optional<std::string>& arguments)
{
    CheckInitialized();
    // Convert string to JsonValue for internal session usage
    std::optional<JsonValue> argsJson = std::nullopt;
    if (arguments.has_value()) {
        argsJson = JsonValue::parse(arguments.value());
    }
    return session_->GetPrompt(name, argsJson);
}

void McpClientImplement::SendRootsListChanged()
{
    CheckInitialized();
    session_->SendRootsListChanged();
}

std::future<std::shared_ptr<EmptyResult>> McpClientImplement::SendPing()
{
    CheckInitialized();
    return session_->SendPing();
}

void McpClientImplement::CloseGracefully()
{
    if (!initialized_) {
        return;
    }
    try {
        // First terminate session, then transport to avoid callbacks to destroyed session
        if (transport_) {
            transport_->TerminateSession();
            transport_->Terminate();
            transport_.reset();
        }
        if (session_) {
            session_.reset();
        }
        initialized_ = false;
        MCP_LOG(MCP_LOG_LEVEL_INFO, "Client closed gracefully.");
    } catch (const std::exception& e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("CloseGracefully failed: ") + e.what());
        transport_.reset();
        session_.reset();
        initialized_ = false;
    } catch (...) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "CloseGracefully failed: unknown exception");
        transport_.reset();
        session_.reset();
        initialized_ = false;
    }
}

void McpClientImplement::SetClientCapabilities(const McpClientCapabilities& caps)
{
    CheckInitialized();
}

ServerCapabilities McpClientImplement::GetServerCapabilities()
{
    CheckInitialized();
    throw std::runtime_error("GetServerCapabilities is not implemented.");
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
