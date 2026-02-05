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
#include "server/prompt_manager.h"
#include "resource_manager.h"
#include "server_manager.h"
#include "shared/common_type.h"
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

private:
    void ReceiveIncomingMessages(const RequestId& requestId, const Request& request, RequestContext& ctx);
    void HandleToolsList(const RequestId& requestId, const Request& request, RequestContext& ctx);
    void HandleToolsCall(const RequestId& requestId, const Request& request, RequestContext& ctx);
    void HandlePromptsList(const RequestId& requestId, const Request& request, RequestContext& ctx);
    void HandlePromptsGet(const RequestId& requestId, const Request& request, RequestContext& ctx);
    void HandlePing(const RequestId& requestId, const Request& request, RequestContext& ctx);
    void HandleResourcesList(const RequestId& requestId, const Request& request, RequestContext& ctx);
    void HandleResourcesRead(const RequestId& requestId, const Request& request, RequestContext& ctx);
    void HandleResourcesSubscribe(const RequestId& requestId, const Request& request, RequestContext& ctx);
    void HandleResourcesUnsubscribe(const RequestId& requestId, const Request& request, RequestContext& ctx);
    void HandleResourcesTemplatesList(const RequestId& requestId, const Request& request, RequestContext& ctx);

    void SendErrorResponse(const RequestId& requestId, JsonRpcErrorCode code, const std::string& message,
                           RequestContext& ctx);

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
};

} // namespace Mcp

#endif // MCP_SERVER_IMPLEMENT_INCLUDE_H_
