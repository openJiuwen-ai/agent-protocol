/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "mcp_server_implement.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <iostream>
#include <algorithm>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "mcp_log.h"
#include "mcp_type.h"
#include "server/server_session.h"
#include "server/tool_manager.h"
#include "shared/common_type.h"
#include "shared/jsonrpc.h"

namespace Mcp {

static const char* g_serverStateStrings[] = {
    "INIT",
    "RUNNING",
    "STOPPED"
};

constexpr size_t MAX_HOSTNAME_LENGTH = 253;

void McpServerImplement::ReceiveIncomingMessages(const RequestId& requestId, const Request& request,
                                                 ServerRequestContext& ctx)
{
    const std::string& method = request.method_;
    auto session = ctx.activeSession.lock();

    {
        std::lock_guard<std::mutex> lock(session->reqMtx);
        session->sessionRequests[requestId] = ctx;
    }

    try {
        if (method == "tools/list") {
            HandleToolsList(requestId, request, ctx);
            return;
        }
        if (method == "tools/call") {
            HandleToolsCall(requestId, request, ctx);
            return;
        }
        if (method == "prompts/list") {
            HandlePromptsList(requestId, request, ctx);
            return;
        }
        if (method == "prompts/get") {
            HandlePromptsGet(requestId, request, ctx);
            return;
        }
        if (method == "resources/list") {
            HandleResourcesList(requestId, request, ctx);
            return;
        }
        if (method == "resources/read") {
            HandleResourcesRead(requestId, request, ctx);
            return;
        }
        if (method == "resources/subscribe") {
            HandleResourcesSubscribe(requestId, request, ctx);
            return;
        }
        if (method == "resources/unsubscribe") {
            HandleResourcesUnsubscribe(requestId, request, ctx);
            return;
        }
        if (method == "resources/templates/list") {
            HandleResourcesTemplatesList(requestId, request, ctx);
            return;
        }
        if (method == "logging/setLevel") {
            HandleSetLoggingLevel(requestId, request, ctx);
            return;
        }
        if (method == "ping") {
            HandlePing(requestId, request, ctx);
            return;
        }
        if (method == "completion/complete") {
            HandleComplete(requestId, request, ctx);
            return;
        }

        SendErrorResponse(requestId, JsonRpcErrorCode::METHOD_NOT_FOUND, "Method not found: " + method, ctx);
    } catch (const std::exception& e) {
        SendErrorResponse(requestId, JsonRpcErrorCode::SERVER_ERROR,
                          std::string("Server error handling ") + method + ": " + e.what(), ctx);
    }
}

void McpServerImplement::SendErrorResponse(const RequestId& requestId,
                                           JsonRpcErrorCode code,
                                           const std::string& message,
                                           ServerRequestContext& ctx)
{
    auto session = ctx.activeSession.lock();
    if (session == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Session not found for error response: " + ctx.sessionId);
        return;
    }

    JSONRPCError error;
    error.id_ = requestId;
    error.code_ = static_cast<int>(code);
    error.message_ = message;
    session->SendResponse(requestId, error, ctx);
}

void McpServerImplement::HandleToolsList(const RequestId& requestId, const Request& request, ServerRequestContext& ctx)
{
    auto session = ctx.activeSession.lock();
    if (session == nullptr) {
        throw std::runtime_error("Session not found: " + ctx.sessionId);
    }

    std::optional<std::string> cursor;
    if (request.params_) {
        cursor = request.params_->cursor;
    }

    auto result = std::make_shared<ListToolsResult>(toolManager_.ListTools(cursor));
    session->SendResponse(requestId, result, ctx);
}

void McpServerImplement::HandleToolsCall(const RequestId& requestId, const Request& request, ServerRequestContext& ctx)
{
    auto session = ctx.activeSession.lock();
    if (session == nullptr) {
        throw std::runtime_error("Session not found: " + ctx.sessionId);
    }

    auto params = dynamic_cast<CallToolParams*>(request.params_.get());
    if (params == nullptr) {
        SendErrorResponse(requestId, JsonRpcErrorCode::INVALID_PARAMS, "Invalid params for tools/call", ctx);
        return;
    }

    // Create unified response callback for async tools to send responses from user threads
    ResponseCallback responseCallback = [this, requestId, ctx, session](const Result& result) {
        const auto& toolResult = static_cast<const CallToolResult&>(result);
        auto resultPtr = std::make_shared<CallToolResult>(toolResult);
        serverManager_->DispatchResponse(requestId, resultPtr, ctx, session);
    };

    ServerContext serverCtx = {session, responseCallback, params->_meta};
    std::string args = params->arguments.has_value() ? params->arguments->dump() : "{}";
    try {
        auto optionalResult = toolManager_.CallTool(serverCtx, params->name, args);
        // Only send response when there is a return value (synchronous function)
        if (optionalResult.has_value()) {
            auto result = std::make_shared<CallToolResult>(std::move(optionalResult.value()));
            session->SendResponse(requestId, result, ctx);
        }
        // Asynchronous function has no return value, response will be sent via callback
    } catch (const std::exception& e) {
        MCP_LOG(MCP_LOG_LEVEL_INFO, "CallTool: caught exception: " + std::string(e.what()));
        SendErrorResponse(requestId, JsonRpcErrorCode::INVALID_PARAMS, e.what(), ctx);
    }
}

void McpServerImplement::HandlePromptsList(
    const RequestId& requestId, const Request& request, ServerRequestContext& ctx)
{
    (void)request;
    auto session = ctx.activeSession.lock();
    if (session == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Session not found: " + ctx.sessionId);
        return;
    }

    auto result = std::make_shared<ListPromptsResult>(promptManager_.ListPrompts());
    session->SendResponse(requestId, result, ctx);
}

void McpServerImplement::HandlePing(const RequestId& requestId, const Request& request, ServerRequestContext& ctx)
{
    (void)request;
    auto session = ctx.activeSession.lock();
    if (session == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Session not found: " + ctx.sessionId);
        return;
    }

    auto result = std::make_shared<EmptyResult>();
    session->SendResponse(requestId, result, ctx);
}

void McpServerImplement::HandlePromptsGet(const RequestId& requestId, const Request& request, ServerRequestContext& ctx)
{
    auto session = ctx.activeSession.lock();
    if (session == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Session not found: " + ctx.sessionId);
        return;
    }

    auto params = dynamic_cast<GetPromptParams*>(request.params_.get());
    if (params == nullptr) {
        SendErrorResponse(requestId, JsonRpcErrorCode::INVALID_PARAMS, "Invalid params for prompts/get", ctx);
        return;
    }

    try {
        // Create unified response callback for async prompts to send responses from user threads
        ResponseCallback responseCallback = [this, requestId, ctx, session](const Result& result) {
            const auto& promptResult = static_cast<const GetPromptResult&>(result);
            auto resultPtr = std::make_shared<GetPromptResult>(promptResult);
            serverManager_->DispatchResponse(requestId, resultPtr, ctx, session);
        };

        ServerContext serverCtx = {session, responseCallback, params->_meta};
        auto optionalResult = promptManager_.GetPrompt(serverCtx, params->name, params->arguments);
        // Only send response when there is a return value (synchronous function)
        if (optionalResult.has_value()) {
            auto result = std::make_shared<GetPromptResult>(std::move(optionalResult.value()));
            session->SendResponse(requestId, result, ctx);
        }
        // Async: response will be sent via callback
    } catch (const std::exception& e) {
        SendErrorResponse(requestId, JsonRpcErrorCode::INVALID_PARAMS, e.what(), ctx);
    }
}

void McpServerImplement::HandleResourcesList(
    const RequestId& requestId, const Request& request, ServerRequestContext& ctx)
{
    auto session = ctx.activeSession.lock();
    if (session == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Session not found: " + ctx.sessionId);
        return;
    }

    try {
        std::optional<std::string> cursor;
        if (request.params_) {
            cursor = request.params_->cursor;
        }

        auto result = std::make_shared<ListResourcesResult>(resourceManager_.ListResources(cursor));
        session->SendResponse(requestId, result, ctx);
    } catch (const std::exception& e) {
        SendErrorResponse(requestId, JsonRpcErrorCode::SERVER_ERROR, e.what(), ctx);
    }
}

void McpServerImplement::HandleResourcesRead(
    const RequestId& requestId, const Request& request, ServerRequestContext& ctx)
{
    auto session = ctx.activeSession.lock();
    if (session == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Session not found: " + ctx.sessionId);
        return;
    }

    auto params = dynamic_cast<ReadResourceRequestParams*>(request.params_.get());
    if (params == nullptr) {
        SendErrorResponse(requestId, JsonRpcErrorCode::INVALID_PARAMS, "Invalid params for resources/read", ctx);
        return;
    }

    if (params->uri_.empty()) {
        SendErrorResponse(requestId, JsonRpcErrorCode::INVALID_PARAMS, "URI cannot be empty", ctx);
        return;
    }

    try {
        // Create unified response callback for async resources to send responses from user threads
        ResponseCallback responseCallback = [this, requestId, ctx, session](const Result& result) {
            const auto& resourceResult = static_cast<const ReadResourceResult&>(result);
            auto resultPtr = std::make_shared<ReadResourceResult>(resourceResult);
            serverManager_->DispatchResponse(requestId, resultPtr, ctx, session);
        };

        ServerContext serverCtx = {session, responseCallback, params->_meta};
        auto optionalResult = resourceManager_.ReadResource(serverCtx, params->uri_);
        if (optionalResult.has_value()) {
            auto result = std::make_shared<ReadResourceResult>(std::move(optionalResult.value()));
            session->SendResponse(requestId, result, ctx);
        }
        // Async: response will be sent via callback
    } catch (const std::exception& e) {
        SendErrorResponse(requestId, JsonRpcErrorCode::INVALID_PARAMS, e.what(), ctx);
    }
}

void McpServerImplement::HandleResourcesSubscribe(const RequestId& requestId, const Request& request,
                                                  ServerRequestContext& ctx)
{
    auto session = ctx.activeSession.lock();
    if (session == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Session not found: " + ctx.sessionId);
        return;
    }

    auto params = dynamic_cast<SubscribeRequestParams*>(request.params_.get());
    if (params == nullptr) {
        SendErrorResponse(requestId, JsonRpcErrorCode::INVALID_PARAMS, "Invalid params for resources/subscribe", ctx);
        return;
    }

    if (params->uri_.empty()) {
        SendErrorResponse(requestId, JsonRpcErrorCode::INVALID_PARAMS, "URI cannot be empty", ctx);
        return;
    }

    try {
        resourceManager_.SubscribeResource(params->uri_);
        // For subscription, we just acknowledge with an empty result
        EmptyResult result;
        session->SendResponse(requestId, std::make_shared<EmptyResult>(result), ctx);
    } catch (const std::exception& e) {
        SendErrorResponse(requestId, JsonRpcErrorCode::SERVER_ERROR, e.what(), ctx);
    }
}

void McpServerImplement::HandleResourcesUnsubscribe(const RequestId& requestId, const Request& request,
                                                    ServerRequestContext& ctx)
{
    auto session = ctx.activeSession.lock();
    if (session == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Session not found: " + ctx.sessionId);
        return;
    }

    auto params = dynamic_cast<UnsubscribeRequestParams*>(request.params_.get());
    if (params == nullptr) {
        SendErrorResponse(requestId, JsonRpcErrorCode::INVALID_PARAMS, "Invalid params for resources/unsubscribe", ctx);
        return;
    }

    if (params->uri_.empty()) {
        SendErrorResponse(requestId, JsonRpcErrorCode::INVALID_PARAMS, "URI cannot be empty", ctx);
        return;
    }

    try {
        resourceManager_.UnsubscribeResource(params->uri_);
        // For unsubscription, we just acknowledge with an empty result
        EmptyResult result;
        session->SendResponse(requestId, std::make_shared<EmptyResult>(result), ctx);
    } catch (const std::exception& e) {
        SendErrorResponse(requestId, JsonRpcErrorCode::SERVER_ERROR, e.what(), ctx);
    }
}

void McpServerImplement::HandleResourcesTemplatesList(const RequestId& requestId, const Request& request,
                                                      ServerRequestContext& ctx)
{
    (void)request;
    auto session = ctx.activeSession.lock();
    if (session == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Session not found: " + ctx.sessionId);
        return;
    }

    try {
        auto result = std::make_shared<ListResourceTemplatesResult>(resourceManager_.ListResourceTemplates());
        session->SendResponse(requestId, result, ctx);
    } catch (const std::exception& e) {
        SendErrorResponse(requestId, JsonRpcErrorCode::SERVER_ERROR, e.what(), ctx);
    }
}

void McpServerImplement::HandleSetLoggingLevel(const RequestId& requestId, const Request& request,
                                               ServerRequestContext& ctx)
{
    auto session = ctx.activeSession.lock();
    if (session == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Session not found: " + ctx.sessionId);
        return;
    }

    auto params = dynamic_cast<SetLoggingLevelParams*>(request.params_.get());
    if (params == nullptr) {
        SendErrorResponse(requestId, JsonRpcErrorCode::INVALID_PARAMS, "Invalid params for logging/setLevel", ctx);
        return;
    }

    try {
        if (setLevelHandler_ == nullptr) {
            SendErrorResponse(requestId, JsonRpcErrorCode::INVALID_PARAMS, "not set LoggingLevelHandler", ctx);
        } else {
            setLevelHandler_(params->level);
            auto result = std::make_shared<EmptyResult>();
            session->SendResponse(requestId, result, ctx);
        }
    } catch (const std::exception& e) {
        SendErrorResponse(requestId, JsonRpcErrorCode::SERVER_ERROR, e.what(), ctx);
    }
}

McpServerImplement::McpServerImplement(const ServerConfig& config)
{
    if (!ValidateConfig(config)) {
        throw std::invalid_argument("Invalid server configuration");
    }

    config_ = config;
    toolManager_.SetPageSize(config.toolsPageSize);
    resourceManager_.SetPageSize(config.resourcesPageSize);
}

McpServerImplement::McpServerImplement(const ServerConfig& config, const StreamableHttpServerConfig& transportConfig)
{
    isStdio_ = false;
    if (!ValidateConfig(config)) {
        throw std::invalid_argument("Invalid server configuration");
    }

    if (!ValidateStreamableHttpConfig(transportConfig)) {
        throw std::invalid_argument("Invalid Streamable HTTP transport configuration");
    }

    config_ = config;
    streamableConfig_ = transportConfig;
    toolManager_.SetPageSize(config.toolsPageSize);
    resourceManager_.SetPageSize(config.resourcesPageSize);
}

McpServerImplement::~McpServerImplement()
{
    if (state_.load() == ServerState::RUNNING) {
        McpServerImplement::Stop();
    }
}

bool McpServerImplement::Run()
{
    ServerState currentState = state_.load();
    if (currentState != ServerState::INIT) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "run failed. Server is not in init state, current state: " +
                std::string(g_serverStateStrings[static_cast<int>(currentState)]));
        return false;
    }

    if (!InitializeServerManager()) {
        return false;
    }

    state_ = ServerState::RUNNING;
    MCP_LOG(MCP_LOG_LEVEL_INFO, "MCP Server started successfully");
    return true;
}

void McpServerImplement::Stop()
{
    ServerState currentState = state_.load();
    if (currentState != ServerState::RUNNING) {
        MCP_LOG(MCP_LOG_LEVEL_WARN, "Server is not running");
        return;
    }

    state_ = ServerState::STOPPED;
    serverManager_->Stop();

    MCP_LOG(MCP_LOG_LEVEL_INFO, "MCP Server stopped");
}

void McpServerImplement::CheckServerState() const
{
    if (state_.load() == ServerState::STOPPED) {
        throw std::runtime_error("Cannot perform operation: server has been stopped");
    }
}

void McpServerImplement::AddTool(const std::string& name, ToolFunc fn, AddToolOptionalParams params)
{
    CheckServerState();

    if (fn == nullptr) {
        throw std::invalid_argument("Tool function implementation cannot be null");
    }
    if (name.empty()) {
        throw std::invalid_argument("Tool name cannot be empty");
    }
    ServerTool tool(name, fn, params.title, params.description, params.inputSchema, params.outputSchema,
                    params.structuredOutput, params.annotations, params.icons);
    toolManager_.AddTool(tool);
}

void McpServerImplement::RemoveTool(const std::string& name)
{
    CheckServerState();

    toolManager_.RemoveTool(name);
}

void McpServerImplement::AddPrompt(const std::string& name, RenderPromptFunc handler, AddPromptOptionalParams params)
{
    CheckServerState();

    PromptInfo prompt;
    prompt.name = name;
    if (params.description.has_value()) {
        prompt.description = params.description->get();
    }
    if (params.title.has_value()) {
        prompt.title = params.title->get();
    }
    if (params.icons.has_value()) {
        prompt.icons = params.icons->get();
    }
    if (params.arguments.has_value()) {
        prompt.arguments = params.arguments->get();
    }

    promptManager_.AddPrompt(prompt, handler);
}

void McpServerImplement::RemovePrompt(const std::string& name)
{
    CheckServerState();

    promptManager_.RemovePrompt(name);
}

void McpServerImplement::AddResource(const std::string& uri, const std::string& name, ReadResourceFunc readFunc,
    AddResourceOptionalParams params)
{
    CheckServerState();

    ResourceInfo resource;
    resource.uri = uri;
    resource.name = name;
    if (params.title.has_value()) {
        resource.title = params.title->get();
    }
    if (params.description.has_value()) {
        resource.description = params.description->get();
    }
    if (params.mimeType.has_value()) {
        resource.mimeType = params.mimeType->get();
    }
    if (params.size.has_value()) {
        resource.size = params.size.value();
    }
    if (params.icons.has_value()) {
        resource.icons = params.icons->get();
    }
    if (params.annotations.has_value()) {
        resource.annotations = params.annotations->get();
    }

    resourceManager_.AddResource(resource, readFunc);
}

void McpServerImplement::RemoveResource(const std::string& uri)
{
    CheckServerState();

    resourceManager_.RemoveResource(uri);
}

void McpServerImplement::AddResourceTemplate(const std::string& uriTemplate, const std::string& name,
    AddResourceTemplateOptionalParams params)
{
    CheckServerState();

    ResourceTemplate resourceTemplate;
    resourceTemplate.uriTemplate = uriTemplate;
    resourceTemplate.name = name;
    if (params.title.has_value()) {
        resourceTemplate.title = params.title->get();
    }
    if (params.description.has_value()) {
        resourceTemplate.description = params.description->get();
    }
    if (params.mimeType.has_value()) {
        resourceTemplate.mimeType = params.mimeType->get();
    }
    if (params.icons.has_value()) {
        resourceTemplate.icons = params.icons->get();
    }
    if (params.annotations.has_value()) {
        resourceTemplate.annotations = params.annotations->get();
    }

    resourceManager_.AddResourceTemplate(resourceTemplate);
}

void McpServerImplement::RemoveResourceTemplate(const std::string& uriTemplate)
{
    CheckServerState();

    resourceManager_.RemoveResourceTemplate(uriTemplate);
}

bool McpServerImplement::ValidateStreamableHttpConfig(const StreamableHttpServerConfig& config)
{
    if (config.endpoint.empty() || config.endpoint.find(' ') != std::string::npos) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Invalid Streamable HTTP endpoint");
        return false;
    }

    if (config.ioThreads == 0 || config.ioThreads > MAX_THREAD_NUM) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR,
                std::string("IO threads count (") + std::to_string(config.ioThreads) +
                    ") should be in (0, " + std::to_string(MAX_THREAD_NUM) + ")");
        return false;
    }

    if (!ValidateTlsConfig(config.tlsConfig)) {
        return false;
    }
    return true;
}

bool McpServerImplement::ValidateConfig(const ServerConfig& config)
{
    if (config.name.empty() || config.version.empty() || config.name.find(' ') != std::string::npos ||
        config.version.find('.') == std::string::npos) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Invalid Server name or version");
        return false;
    }

    if (isStdio_) {
        return true;
    }

    if (config_.workerThreads == 0 || config_.workerThreads > MAX_THREAD_NUM) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR,
                std::string("Worker threads count (") + std::to_string(config.workerThreads) +
                    ") should be in (0, " + std::to_string(MAX_THREAD_NUM) + ")");
        return false;
    }

    return true;
}

bool McpServerImplement::InitializeServerManager()
{
    try {
        if (isStdio_) {
            serverManager_ = std::make_shared<ServerManager>(config_);
        } else {
            serverManager_ = std::make_shared<ServerManager>(config_, streamableConfig_);
        }
        if (serverManager_ == nullptr) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "Failed to create ServerManager");
            return false;
        }
        serverManager_->SetIncomingRequestCallback(
            [this](const RequestId& requestId, const Request& request, ServerRequestContext& ctx) {
            this->ReceiveIncomingMessages(requestId, request, ctx);
            });
        MCP_LOG(MCP_LOG_LEVEL_DEBUG, "ServerManager initialized successfully");
        serverManager_->Start();
        MCP_LOG(MCP_LOG_LEVEL_DEBUG, "ServerManager start successfully");
        return true;
    } catch (const std::exception& e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("Exception while creating ServerManager: ") + e.what());
        return false;
    }
}

static bool ValidateFilePathWithRealpath(const std::string& path, const char* what)
{
    if (path.empty()) {
        return true;
    }

    char* canonical = realpath(path.c_str(), nullptr);
    if (canonical == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string(what) + " realpath failed: " + path +
                " (errno=" + std::to_string(errno) + ", " + std::strerror(errno) + ")");
        return false;
    }

    MCP_LOG(MCP_LOG_LEVEL_DEBUG, std::string(what) + " validated: " + std::string(canonical));
    free(canonical);
    return true;
}

bool McpServerImplement::ValidateTlsConfig(const TlsConfig& config)
{
    if (!config.enabled) {
        return true;
    }

    if (!config.serverName.empty()) {
        if (config.serverName.length() > MAX_HOSTNAME_LENGTH) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR,
                    std::string("TLS server name too long: ") +
                        std::to_string(config.serverName.length()) + " characters");
            return false;
        }

        if (config.serverName.find_first_of(" \t\r\n") != std::string::npos) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "TLS server name contains invalid whitespace characters");
            return false;
        }

        if (config.serverName.find("..") != std::string::npos) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "TLS server name contains invalid path traversal sequence");
            return false;
        }
    }

    if (!ValidateFilePathWithRealpath(config.certFile, "TLS certificate file")) {
        return false;
    }

    if (!ValidateFilePathWithRealpath(config.keyFile, "TLS key file")) {
        return false;
    }

    if (!ValidateFilePathWithRealpath(config.caFile, "TLS CA file")) {
        return false;
    }

    return true;
}

void McpServerImplement::AddCompletion(CompleteFunc handler)
{
    completeHandler_ = handler;
    MCP_LOG(MCP_LOG_LEVEL_INFO, "Completion handler registered");
}

void McpServerImplement::HandleComplete(const RequestId& requestId, const Request& request, ServerRequestContext& ctx)
{
    auto session = ctx.activeSession.lock();
    if (session == nullptr) {
        SendErrorResponse(requestId, JsonRpcErrorCode::SERVER_ERROR, "Session not found", ctx);
        return;
    }

    const auto* completeReq = dynamic_cast<const CompleteRequest*>(&request);
    if (completeReq == nullptr || completeReq->params_ == nullptr) {
        SendErrorResponse(requestId, JsonRpcErrorCode::INVALID_REQUEST, "Invalid complete request", ctx);
        return;
    }

    const auto* params = static_cast<const CompleteRequestParams*>(completeReq->params_.get());
    if (completeHandler_ == nullptr) {
        SendErrorResponse(requestId, JsonRpcErrorCode::SERVER_ERROR, "Completion handler not registered", ctx);
        return;
    }

    try {
        auto result = completeHandler_(params->ref, params->argument, params->context);
        auto completeResult = std::make_shared<CompleteResult>(result);
        session->SendResponse(requestId, completeResult, ctx);
    } catch (const std::exception& e) {
        SendErrorResponse(requestId, JsonRpcErrorCode::SERVER_ERROR,
                          std::string("Complete handler error: ") + e.what(), ctx);
    }
}

} // namespace Mcp
