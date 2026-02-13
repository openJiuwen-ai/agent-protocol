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

    std::future<std::shared_ptr<ListToolsResult>> ListTools() override;
    std::future<std::shared_ptr<CallToolResult>> CallTool(const std::string& name,
                                                          const std::optional<JsonValue>& arguments = std::nullopt,
                                                          int timeout = 0) override;

    std::future<std::shared_ptr<ListResourcesResult>> ListResources() override;
    std::future<std::shared_ptr<ReadResourceResult>> ReadResource(const std::string& uri) override;
    std::future<std::shared_ptr<EmptyResult>> SubscribeResource(const std::string& uri) override;
    std::future<std::shared_ptr<EmptyResult>> UnsubscribeResource(const std::string& uri) override;
    std::future<std::shared_ptr<EmptyResult>> SetLoggingLevel(const LoggingLevel level) override;
    std::future<std::shared_ptr<ListResourceTemplatesResult>> ListResourcesTemplates() override;

    std::future<std::shared_ptr<ListPromptsResult>> ListPrompts() override;
    std::future<std::shared_ptr<GetPromptResult>> GetPrompt(
        const std::string& name, const std::optional<JsonValue>& arguments = std::nullopt) override;

    void SendRootsListChanged() override;
    std::future<EmptyResult> SendPing() override;
    ServerCapabilities GetServerCapabilities() override;

    // Not yet developed, temporarily commented out
    std::future<void> SendProgressNotification(std::string progressToken, float progress, float total,
                                               std::string message) override;

    std::future<std::shared_ptr<CompleteResult>> Complete(
        const CompleteReference& ref, const CompletionArgument& argument,
        const std::optional<CompletionContext>& context = std::nullopt) override;

    void SetListRootsCallback(ListRootsCallback cb) override;

    void SetElicitCallback(ElicitCallback cb) override;

    void SetElicitUrlCallback(ElicitUrlCallback cb) override;

    void SetLoggingCallback(LoggingCallback cb) override;

    void SetSamplingCreateMessageCallback(SamplingCreateMessageCallback cb,
        SamplingCapability capability = SamplingCapability{}) override;
private:
    void CheckInitialized();

    std::shared_ptr<ClientSession> session_;
    std::shared_ptr<ClientTransport> transport_;
    std::shared_ptr<AuthProvider> authProvider_;

    // Configuration
    ClientConfig config_;
    bool initialized_ = false;

    // Allows SetListRootsCallback() to be called before Initialize() creates session_.
    // Once session_ is created, this callback is applied to the session and cleared.
    std::optional<ListRootsCallback> listRootsCallback_{std::nullopt};

    // Allows SetSamplingCreateMessageCallback() to be called before Initialize() creates session_.
    // Once session_ is created, this callback is applied to the session and cleared.
    struct PreInitSamplingHandler {
        SamplingCreateMessageCallback cb;
        SamplingCapability capability;
    };
    std::optional<PreInitSamplingHandler> samplingCreateMessageHandler_{std::nullopt};
};

} // namespace Mcp

#endif // MCP_CLIENT_IMPLEMENT_INCLUDE_H_
