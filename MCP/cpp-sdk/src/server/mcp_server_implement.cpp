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
#include "shared/common_type.h"
#include "shared/jsonrpc.h"

namespace Mcp {

const uint32_t g_MaxThreads = 64;
const uint16_t g_DefaultHttpPort = 80;
const uint16_t g_DefaultHttpsPort = 443;
const uint16_t g_MaxPort = 65535;
const uint16_t g_InvalidPort = 0; // Represents invalid or unspecified port
const size_t g_MaxHostnameLength = 253;
const size_t g_UrlProtocolOffset = 3; // Length of "://"

void McpServerImplement::ReceiveIncomingMessages(int64_t requestId, const Request& request, RequestContext& ctx)
{
    const std::string& method = request.method_;
    auto session = serverManager_->GetSession(ctx.sessionId);

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

void McpServerImplement::SendErrorResponse(int64_t requestId, JsonRpcErrorCode code, const std::string& message,
                                           RequestContext& ctx)
{
    auto session = serverManager_ ? serverManager_->GetSession(ctx.sessionId) : nullptr;
    if (session == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Session not found for error response: %s", ctx.sessionId.c_str());
        return;
    }

    JSONRPCError error;
    error.id_ = requestId;
    error.code_ = static_cast<int>(code);
    error.message_ = message;
    session->SendResponse(requestId, error, ctx);
}

void McpServerImplement::HandleToolsList(int64_t requestId, const Request& request, RequestContext& ctx)
{
    auto session = serverManager_->GetSession(ctx.sessionId);
    if (session == nullptr) {
        throw std::runtime_error("Session not found: " + ctx.sessionId);
    }

    auto result = std::make_shared<ListToolsResult>(toolManager_.ListTools());
    session->SendResponse(requestId, result, ctx);
}

void McpServerImplement::HandleToolsCall(int64_t requestId, const Request& request, RequestContext& ctx)
{
    auto session = serverManager_->GetSession(ctx.sessionId);
    if (session == nullptr) {
        throw std::runtime_error("Session not found: " + ctx.sessionId);
    }

    auto params = dynamic_cast<CallToolParams*>(request.params_.get());
    if (params == nullptr) {
        SendErrorResponse(requestId, JsonRpcErrorCode::INVALID_PARAMS, "Invalid params for tools/call", ctx);
        return;
    }

    // Create unified response callback for async tools to send responses from user threads
    ResponseCallback responseCallback = [this, requestId, ctx](const Result& result) {
        // Cast to CallToolResult and create shared_ptr
        const auto& toolResult = static_cast<const CallToolResult&>(result);
        auto resultPtr = std::make_shared<CallToolResult>(toolResult);
        serverManager_->DispatchResponse(requestId, resultPtr, ctx);
    };

    ServerContext serverCtx = {session, responseCallback, params->_meta};
    std::string args = params->arguments.has_value() ? params->arguments->dump() : "{}";
    auto optionalResult = toolManager_.CallTool(serverCtx, params->name, args);
    // Only send response when there is a return value (synchronous function)
    if (optionalResult.has_value()) {
        auto result = std::make_shared<CallToolResult>(std::move(optionalResult.value()));
        session->SendResponse(requestId, result, ctx);
    }
    // Asynchronous function has no return value, response will be sent via callback
}

void McpServerImplement::HandlePromptsList(int64_t requestId, const Request& request, RequestContext& ctx)
{
    (void)request;
    auto session = serverManager_->GetSession(ctx.sessionId);
    if (session == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Session not found: %s", ctx.sessionId.c_str());
        return;
    }

    auto result = std::make_shared<ListPromptsResult>(promptManager_.ListPrompts());
    session->SendResponse(requestId, result, ctx);
}

void McpServerImplement::HandlePromptsGet(int64_t requestId, const Request& request, RequestContext& ctx)
{
    auto session = serverManager_->GetSession(ctx.sessionId);
    if (session == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Session not found: %s", ctx.sessionId.c_str());
        return;
    }

    auto params = dynamic_cast<GetPromptParams*>(request.params_.get());
    if (params == nullptr) {
        SendErrorResponse(requestId, JsonRpcErrorCode::INVALID_PARAMS, "Invalid params for prompts/get", ctx);
        return;
    }

    try {
        // Create unified response callback for async prompts to send responses from user threads
        ResponseCallback responseCallback = [this, requestId, ctx](const Result& result) {
            // Cast to GetPromptResult and create shared_ptr
            const auto& promptResult = static_cast<const GetPromptResult&>(result);
            auto resultPtr = std::make_shared<GetPromptResult>(promptResult);
            serverManager_->DispatchResponse(requestId, resultPtr, ctx);
        };

        ServerContext serverCtx = {session, responseCallback, params->_meta};
        auto optionalResult = promptManager_.GetPrompt(serverCtx, params->name, params->arguments);
        // Only send response when there is a return value (synchronous function)
        if (optionalResult.has_value()) {
            auto result = std::make_shared<GetPromptResult>(std::move(optionalResult.value()));
            session->SendResponse(requestId, result, ctx);
        }
        // Asynchronous function has no return value, response will be sent via callback
    } catch (const std::exception& e) {
        SendErrorResponse(requestId, JsonRpcErrorCode::SERVER_ERROR, e.what(), ctx);
    }
}

void McpServerImplement::HandleResourcesList(int64_t requestId, const Request& request, RequestContext& ctx)
{
    (void)request;
    auto session = serverManager_->GetSession(ctx.sessionId);
    if (session == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Session not found: %s", ctx.sessionId.c_str());
        return;
    }

    try {
        auto result = std::make_shared<ListResourcesResult>(resourceManager_.ListResources());
        session->SendResponse(requestId, result, ctx);
    } catch (const std::exception& e) {
        SendErrorResponse(requestId, JsonRpcErrorCode::SERVER_ERROR, e.what(), ctx);
    }
}

void McpServerImplement::HandleResourcesRead(int64_t requestId, const Request& request, RequestContext& ctx)
{
    auto session = serverManager_->GetSession(ctx.sessionId);
    if (session == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Session not found: %s", ctx.sessionId.c_str());
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
        ResponseCallback responseCallback = [this, requestId, ctx](const Result& result) {
            // Cast to ReadResourceResult and create shared_ptr
            const auto& resourceResult = static_cast<const ReadResourceResult&>(result);
            auto resultPtr = std::make_shared<ReadResourceResult>(resourceResult);
            serverManager_->DispatchResponse(requestId, resultPtr, ctx);
        };

        ServerContext serverCtx = {session, responseCallback, params->_meta};
        auto optionalResult = resourceManager_.ReadResource(serverCtx, params->uri_);
        if (optionalResult.has_value()) {
            // Synchronous execution - send response immediately
            auto result = std::make_shared<ReadResourceResult>(optionalResult.value());
            session->SendResponse(requestId, result, ctx);
        }
        // For async execution, the response will be sent via the callback
    } catch (const std::exception& e) {
        SendErrorResponse(requestId, JsonRpcErrorCode::SERVER_ERROR, e.what(), ctx);
    }
}

void McpServerImplement::HandleResourcesSubscribe(int64_t requestId, const Request& request, RequestContext& ctx)
{
    auto session = serverManager_->GetSession(ctx.sessionId);
    if (session == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Session not found: %s", ctx.sessionId.c_str());
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

void McpServerImplement::HandleResourcesUnsubscribe(int64_t requestId, const Request& request, RequestContext& ctx)
{
    auto session = serverManager_->GetSession(ctx.sessionId);
    if (session == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Session not found: %s", ctx.sessionId.c_str());
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

void McpServerImplement::HandleResourcesTemplatesList(int64_t requestId, const Request& request, RequestContext& ctx)
{
    (void)request;
    auto session = serverManager_->GetSession(ctx.sessionId);
    if (session == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Session not found: %s", ctx.sessionId.c_str());
        return;
    }

    try {
        auto result = std::make_shared<ListResourceTemplatesResult>(resourceManager_.ListResourceTemplates());
        session->SendResponse(requestId, result, ctx);
    } catch (const std::exception& e) {
        SendErrorResponse(requestId, JsonRpcErrorCode::SERVER_ERROR, e.what(), ctx);
    }
}

void McpServerImplement::HandleSetLoggingLevel(int64_t requestId, const Request& request, RequestContext& ctx)
{
    auto session = serverManager_->GetSession(ctx.sessionId);
    if (session == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Session not found: %s", ctx.sessionId.c_str());
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
    // 2. Initialize server manager
    if (!InitializeServerManager()) {
        throw std::runtime_error("Failed to initialize ServerManager");
    }
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
    // 2. Initialize server manager
    if (!InitializeServerManager()) {
        throw std::runtime_error("Failed to initialize ServerManager");
    }
}

McpServerImplement::~McpServerImplement()
{
    if (running_) {
        Stop();
    }

    if (serverManager_) {
        serverManager_.reset();
    }
}

bool McpServerImplement::Run()
{
    if (running_) {
        MCP_LOG(MCP_LOG_LEVEL_WARN, "Server is already running");
        return false;
    }

    try {
        serverManager_->Start();
        MCP_LOG(MCP_LOG_LEVEL_DEBUG, "ServerManager start successfully");
    } catch (const std::exception& e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Exception while starting ServerManager: %s", e.what());
        return false;
    }

    running_ = true;
    MCP_LOG(MCP_LOG_LEVEL_INFO, "MCP Server started successfully");
    return true;
}

void McpServerImplement::Stop()
{
    if (!running_) {
        MCP_LOG(MCP_LOG_LEVEL_WARN, "Server is not running");
        return;
    }

    running_ = false;
    serverManager_->Stop();

    MCP_LOG(MCP_LOG_LEVEL_INFO, "MCP Server stopped");
}

void McpServerImplement::AddTool(const ToolInfo& tool)
{
    toolManager_.AddTool(tool);
}

void McpServerImplement::RemoveTool(const std::string& name)
{
    toolManager_.RemoveTool(name);
}

void McpServerImplement::AddPrompt(const PromptInfo& prompt, RenderPromptFunc handler)
{
    promptManager_.AddPrompt(prompt, handler);
}

void McpServerImplement::RemovePrompt(const std::string& name)
{
    promptManager_.RemovePrompt(name);
}

void McpServerImplement::AddResource(const ResourceInfo& resource, ReadResourceFunc readFunc)
{
    resourceManager_.AddResource(resource, readFunc);
}

void McpServerImplement::RemoveResource(const std::string& uri)
{
    resourceManager_.RemoveResource(uri);
}

void McpServerImplement::AddResourceTemplate(const ResourceTemplate& resourceTemplate)
{
    resourceManager_.AddResourceTemplate(resourceTemplate);
}

void McpServerImplement::RemoveResourceTemplate(const std::string& uriTemplate)
{
    resourceManager_.RemoveResourceTemplate(uriTemplate);
}

bool McpServerImplement::ValidateStreamableHttpConfig(const StreamableHttpServerConfig& config)
{
    if (config.endpoint.empty() || config.endpoint.find(' ') != std::string::npos) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Invalid Streamable HTTP endpoint");
        return false;
    }

    if (config.ioThreads == 0 || config.ioThreads > g_MaxThreads) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "IO threads count (%u) should be in (0, %u)", config.ioThreads,
                g_MaxThreads);
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

    if (config_.workerThreads == 0 || config_.workerThreads > g_MaxThreads) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Worker threads count (%u) should be in (0, %u)",
            config.workerThreads, g_MaxThreads);
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
            [this](int64_t requestId, const Request& request, RequestContext& ctx) {
            this->ReceiveIncomingMessages(requestId, request, ctx);
            });
        MCP_LOG(MCP_LOG_LEVEL_DEBUG, "ServerManager initialized successfully");
        return true;
    } catch (const std::exception& e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Exception while creating ServerManager: %s", e.what());
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
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "%s realpath failed: %s (errno=%d, %s)",
            what, path.c_str(), errno, std::strerror(errno));
        return false;
    }

    MCP_LOG(MCP_LOG_LEVEL_DEBUG, "%s validated: %s", what, canonical);
    free(canonical);

    return true;
}

bool McpServerImplement::ValidateTlsConfig(const TlsConfig& config)
{
    if (!config.enabled) {
        return true;
    }

    if (!config.serverName.empty()) {
        if (config.serverName.length() > g_MaxHostnameLength) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "TLS server name too long: %zu characters", config.serverName.length());
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

void McpServerImplement::HandleComplete(int64_t requestId, const Request& request, RequestContext& ctx)
{
    auto session = serverManager_ ? serverManager_->GetSession(ctx.sessionId) : nullptr;
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
