/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef MCP_CLIENT_IMPLEMENT_INCLUDE_H_
#define MCP_CLIENT_IMPLEMENT_INCLUDE_H_

#include <future>
#include <optional>
#include <string>

#include "mcp_auth.h"
#include "client_session.h"
#include "mcp_client.h"
#include "mcp_type.h"
#include "shared/common_type.h"

namespace Mcp {

class McpClientImplement : public McpClient {
public:
    explicit McpClientImplement(const ClientConfig config, std::shared_ptr<ClientTransport> transport,
        std::shared_ptr<AuthProvider> authProvider = nullptr);
    ~McpClientImplement() = default;

    std::future<std::shared_ptr<InitializeResult>> Initialize() override;

    std::future<std::shared_ptr<ListToolsResult>> ListTools(
        const std::optional<std::string>& cursor = std::nullopt) override;
    std::future<std::shared_ptr<CallToolResult>> CallTool(const std::string& name,
                                                          const std::optional<std::string>& arguments = std::nullopt,
                                                          int timeout = 0) override;

    std::future<std::shared_ptr<ListResourcesResult>> ListResources(
        const std::optional<std::string>& cursor = std::nullopt) override;
    std::future<std::shared_ptr<ReadResourceResult>> ReadResource(const std::string& uri) override;
    std::future<std::shared_ptr<EmptyResult>> SubscribeResource(const std::string& uri) override;
    std::future<std::shared_ptr<EmptyResult>> UnsubscribeResource(const std::string& uri) override;
    std::future<std::shared_ptr<ListResourceTemplatesResult>> ListResourcesTemplates() override;

    std::future<std::shared_ptr<ListPromptsResult>> ListPrompts() override;
    std::future<std::shared_ptr<GetPromptResult>> GetPrompt(
        const std::string& name, const std::optional<std::string>& arguments = std::nullopt) override;

    void SendRootsListChanged() override;
    std::future<std::shared_ptr<EmptyResult>> SendPing() override;

    void SetClientCapabilities(const McpClientCapabilities& caps) override;
    ServerCapabilities GetServerCapabilities() override;

    // Not yet developed, temporarily commented out
    std::future<void> SendProgressNotification(std::string progressToken, float progress, float total,
                                               std::string message) override;

    std::future<Result> Complete(std::string type, std::string uri,
                                 std::unordered_map<std::string, std::string> extras) override;

private:
    void CheckInitialized();

    std::shared_ptr<ClientSession> session_;
    std::shared_ptr<ClientTransport> transport_;
    std::shared_ptr<AuthProvider> authProvider_;

    // Configuration
    ClientConfig config_;
    McpClientCapabilities capabilities_;

    bool initialized_ = false;
};

} // namespace Mcp

#endif // MCP_CLIENT_IMPLEMENT_INCLUDE_H_
