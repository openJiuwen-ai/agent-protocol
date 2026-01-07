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

class McpServerImplement : public McpServer {
public:
    explicit McpServerImplement(const ServerConfig& config);
    explicit McpServerImplement(const ServerConfig& config, const StreamableHttpServerConfig& transportConfig);
    ~McpServerImplement() override;

    bool Run() override;
    void Stop() override;
    bool IsRunning() const override
    {
        return running_;
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
    void ReceiveIncomingMessages(int64_t requestId, const Request& request, RequestContext& ctx);
    void HandleToolsList(int64_t requestId, const Request& request, RequestContext& ctx);
    void HandleToolsCall(int64_t requestId, const Request& request, RequestContext& ctx);
    void HandlePromptsList(int64_t requestId, const Request& request, RequestContext& ctx);
    void HandlePromptsGet(int64_t requestId, const Request& request, RequestContext& ctx);
    void HandleResourcesList(int64_t requestId, const Request& request, RequestContext& ctx);
    void HandleResourcesRead(int64_t requestId, const Request& request, RequestContext& ctx);
    void HandleResourcesSubscribe(int64_t requestId, const Request& request, RequestContext& ctx);
    void HandleResourcesUnsubscribe(int64_t requestId, const Request& request, RequestContext& ctx);
    void HandleResourcesTemplatesList(int64_t requestId, const Request& request, RequestContext& ctx);

    void SendErrorResponse(int64_t requestId, JsonRpcErrorCode code, const std::string& message, RequestContext& ctx);

    bool ValidateConfig(const ServerConfig& config);
    bool ValidateStreamableHttpConfig(const StreamableHttpServerConfig& config);
    bool InitializeServerManager();
    bool ValidateTlsConfig(const TlsConfig& config);

    ServerConfig config_;
    StreamableHttpServerConfig streamableConfig_;
    bool isStdio_{true};
    std::atomic<bool> running_{false};
    std::shared_ptr<ServerManager> serverManager_{nullptr};
    std::mutex sessionMutex_;
    ToolManager toolManager_;
    PromptManager promptManager_;
    ResourceManager resourceManager_;
};

} // namespace Mcp

#endif // MCP_SERVER_IMPLEMENT_INCLUDE_H_
