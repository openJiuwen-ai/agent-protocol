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

    // Apply any pre-configured roots/list callback before the Initialize request is built,
    // so the client advertises the `roots` capability correctly.
    if (listRootsCallback_.has_value()) {
        session_->SetListRootsCallback(std::move(*listRootsCallback_));
        listRootsCallback_.reset();
    }

    // Apply any pre-configured sampling/createMessage callback before the Initialize request is built,
    // so the client advertises the `sampling` capability correctly.
    if (samplingCreateMessageHandler_.has_value()) {
        session_->SetSamplingCreateMessageCallback(std::move(samplingCreateMessageHandler_->cb),
            samplingCreateMessageHandler_->capability);
        samplingCreateMessageHandler_.reset();
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

std::future<std::shared_ptr<CompleteResult>> McpClientImplement::Complete(
    const CompleteReference& ref, const CompletionArgument& argument,
    const std::optional<CompletionContext>& context)
{
    CheckInitialized();
    return session_->Complete(ref, argument, context);
}

void McpClientImplement::SetListRootsCallback(ListRootsCallback cb)
{
    if (session_) {
        session_->SetListRootsCallback(std::move(cb));
        return;
    }
    listRootsCallback_ = std::move(cb);
}

void McpClientImplement::SetLoggingCallback(LoggingCallback cb)
{
    session_->SetLoggingCallback(std::move(cb));
}

void McpClientImplement::SetElicitCallback(ElicitCallback cb)
{
    session_->SetElicitCallback(std::move(cb));
}

void McpClientImplement::SetElicitUrlCallback(ElicitUrlCallback cb)
{
    session_->SetElicitUrlCallback(std::move(cb));
}

void McpClientImplement::SetSamplingCreateMessageCallback(SamplingCreateMessageCallback cb,
    SamplingCapability capability)
{
    if (session_) {
        session_->SetSamplingCreateMessageCallback(std::move(cb), capability);
        return;
    }
    samplingCreateMessageHandler_ = PreInitSamplingHandler{std::move(cb), capability};
}

} // namespace Mcp
