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
                                                                          int timeout,
                                                                          std::optional<McpClient::ProgressCallback>
                                                                              progressCallback)
{
    CheckInitialized();
    std::optional<ProgressCallback> internalCb;
    if (progressCallback.has_value()) {
        internalCb = std::move(*progressCallback);
    }
    return session_->CallTool(name, arguments, timeout, internalCb);
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

void McpClientImplement::SetClientCapabilities(const ClientCapabilities& caps)
{
    CheckInitialized();
    (void)caps;
}

ServerCapabilities McpClientImplement::GetServerCapabilities()
{
    CheckInitialized();
    if (session_ == nullptr) {
        throw std::runtime_error("client session is not created.");
    }
    return session_->GetServerCapabilities();
}

std::future<void> McpClientImplement::SendProgressNotification(ProgressToken progressToken, double progress,
                                                               std::optional<double> total,
                                                               std::optional<std::string> message)
{
    CheckInitialized();
    session_->SendProgressNotification(std::move(progressToken), progress, total, message);
    std::promise<void> promise;
    promise.set_value();
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
