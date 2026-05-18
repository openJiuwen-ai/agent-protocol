/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef MCP_SERVER_IMPLEMENT_INCLUDE_H_
#define MCP_SERVER_IMPLEMENT_INCLUDE_H_

#include <atomic>
#include <fstream>
#include <istream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "mcp_error.h"
#include "mcp_server.h"
#include "mcp_type.h"
#include "server/server_session.h"
#include "server/prompt_manager.h"
#include "resource_manager.h"
#include "server_manager.h"
#include "shared/jsonrpc.h"
#include "tool_manager.h"

namespace Mcp {
using Json = nlohmann::json;

enum class ServerState {
    INIT = 0,       // Server initialized but not started
    RUNNING,        // Server is running
    STOPPED         // Server stopped and cannot be restarted
};


class McpServerImplement : public McpServer {
public:
    explicit McpServerImplement(const ServerConfig& config);
    explicit McpServerImplement(const ServerConfig& config, const StreamableHttpServerConfig& transportConfig);
    ~McpServerImplement() override;

    bool Run() override;
    void Stop() override;
    bool IsRunning() const override
    {
        return state_ == ServerState::RUNNING;
    }

    void AddTool(const std::string& name, ToolFunc fn,
        AddToolOptionalParams params = {}) override;
    void RemoveTool(const std::string& name) override;

    void AddPrompt(const std::string& name, RenderPromptFunc handler,
        AddPromptOptionalParams params = {}) override;
    void RemovePrompt(const std::string& name) override;
    void AddResource(const std::string& uri, const std::string& name, ReadResourceFunc readFunc,
        AddResourceOptionalParams params = {}) override;
    void RemoveResource(const std::string& uri) override;
    void AddResourceTemplate(const std::string& uriTemplate, const std::string& name,
        AddResourceTemplateOptionalParams params = {}) override;
    void RemoveResourceTemplate(const std::string& uriTemplate) override;
    void AddCompletion(CompleteFunc handler) override;
    using SetLoggingLevelHandler = std::function<void(const std::string& level)>;
    void RegisterSetLoggingLevelHandler(SetLoggingLevelHandler h) override
    {
        setLevelHandler_ = std::move(h);
    }

private:
    void ReceiveIncomingMessages(const RequestId& requestId, const Request& request, ServerRequestContext& ctx);
    void HandleToolsList(const RequestId& requestId, const Request& request, ServerRequestContext& ctx);
    void HandleToolsCall(const RequestId& requestId, const Request& request, ServerRequestContext& ctx);
    void HandlePromptsList(const RequestId& requestId, const Request& request, ServerRequestContext& ctx);
    void HandlePromptsGet(const RequestId& requestId, const Request& request, ServerRequestContext& ctx);
    void HandlePing(const RequestId& requestId, const Request& request, ServerRequestContext& ctx);
    void HandleSetLoggingLevel(const RequestId& requestId, const Request& request, ServerRequestContext& ctx);
    void HandleResourcesList(const RequestId& requestId, const Request& request, ServerRequestContext& ctx);
    void HandleResourcesRead(const RequestId& requestId, const Request& request, ServerRequestContext& ctx);
    void HandleResourcesSubscribe(const RequestId& requestId, const Request& request, ServerRequestContext& ctx);
    void HandleResourcesUnsubscribe(const RequestId& requestId, const Request& request, ServerRequestContext& ctx);
    void HandleResourcesTemplatesList(const RequestId& requestId, const Request& request, ServerRequestContext& ctx);
    void HandleComplete(const RequestId& requestId, const Request& request, ServerRequestContext& ctx);

    void SendErrorResponse(const RequestId& requestId, JsonRpcErrorCode code, const std::string& message,
                           ServerRequestContext& ctx);

    bool ValidateConfig(const ServerConfig& config);
    bool ValidateStreamableHttpConfig(const StreamableHttpServerConfig& config);
    bool InitializeServerManager();
    bool ValidateTlsConfig(const TlsConfig& config);
    void CheckServerState() const;

    ServerConfig config_;
    StreamableHttpServerConfig streamableConfig_;
    bool isStdio_{true};
    std::atomic<ServerState> state_{ServerState::INIT};
    std::shared_ptr<ServerManager> serverManager_{nullptr};
    std::mutex sessionMutex_;
    ToolManager toolManager_;
    PromptManager promptManager_;
    ResourceManager resourceManager_;
    SetLoggingLevelHandler setLevelHandler_;
    CompleteFunc completeHandler_;
};

} // namespace Mcp

#endif // MCP_SERVER_IMPLEMENT_INCLUDE_H_
