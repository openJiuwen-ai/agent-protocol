/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef MCP_CLIENT_SESSION_INCLUDE_H_
#define MCP_CLIENT_SESSION_INCLUDE_H_

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "mcp_type.h"
#include "shared/base_session.h"
#include "shared/jsonrpc.h"

namespace Mcp {
// Client session class for handling requests and notifications
class ClientSession : public BaseSession {
public:
    using BaseSession::SendNotification;
    using BaseSession::SendRequest;

    // Constructor with configurable client configuration (defaults provided)
    explicit ClientSession(std::shared_ptr<ClientTransport> transport,
                           const ClientConfig& clientConfig = ClientConfig(), std::string sessionId = "");

    // Initialize session with the server (send initialize request and wait response)
    std::future<std::shared_ptr<InitializeResult>> Initialize();

    // Send a ping request and wait for a response
    std::future<EmptyResult> SendPing();

    // Send a roots list changed notification
    void SendRootsListChanged();

    // Set the callback for handling roots/list requests from the server
    void SetListRootsCallback(ListRootsCallback cb);

    // Set the logging level
    std::future<std::shared_ptr<EmptyResult>> SetLoggingLevel(LoggingLevel level);

    // Call a tool on the server
    std::future<std::shared_ptr<CallToolResult>> CallTool(const std::string& name,
                                                          const std::optional<JsonValue>& arguments = std::nullopt,
                                                          int timeout = 0);

    // List available tools
    std::future<std::shared_ptr<ListToolsResult>> ListTools();

    // List prompts
    std::future<std::shared_ptr<ListPromptsResult>> ListPrompts();

    // --- Resources ---
    std::future<std::shared_ptr<ListResourcesResult>> ListResources();
    std::future<std::shared_ptr<ListResourceTemplatesResult>> ListResourcesTemplates();
    std::future<std::shared_ptr<ReadResourceResult>> ReadResource(const std::string& uri);
    std::future<std::shared_ptr<EmptyResult>> SubscribeResource(const std::string& uri);
    std::future<std::shared_ptr<EmptyResult>> UnsubscribeResource(const std::string& uri);

    // Get prompt from server
    std::future<std::shared_ptr<GetPromptResult>> GetPrompt(const std::string& name,
                                                            const std::optional<JsonValue>& arguments = std::nullopt);

    // Request completion options
    std::future<std::shared_ptr<CompleteResult>> Complete(const CompleteReference& ref,
                                                          const CompletionArgument& argument,
                                                          const std::optional<CompletionContext>& context
                                                            = std::nullopt);

    // Override BaseSession notification sending
    void SendNotification(std::unique_ptr<Notification> notification,
                          std::optional<int64_t> relatedRequestId [[maybe_unused]] = std::nullopt) override;

    // Check if initialize has completed successfully
    bool IsInitialized() const
    {
        return initialized_;
    }

    // Get the capabilities advertised by the connected server.
    // Populated when the client receives the InitializeResult.
    ServerCapabilities GetServerCapabilities() const;

    // Send notifications/initialized after successful initialize handshake
    void SendInitializedNotification();

protected:
    void ReceivedRequest(int64_t requestId, const Request& request, RequestContext& ctx) override;

private:
    // Incremental request ID for tracking (thread-safe)
    std::atomic<int64_t> _request_id{1};

    // Pending requests map: id -> raw-response callback
    std::mutex pending_mutex_;
    std::unordered_map<int64_t, std::function<void(const std::string&)>> pending_callbacks_;

    // Transport callback that sends raw JSON strings. Must be set by caller
    // (or via constructor) before making requests.
    std::function<bool(const std::string&)> send_raw_;

    // Build client capabilities for initialize request.
    ClientCapabilities BuildClientCapabilities() const;

    // Handle roots/list request. This is used when the server asks the client to
    // provide its roots. The request is only supported when listRootsCallback_ is set.
    void HandleRootsListRequest(int64_t requestId, const Request& request, RequestContext& ctx);

    // Utility helpers for replying to server-initiated requests with JSON-RPC errors.
    // These helpers centralize error formatting and ensure consistent responses.
    void SendJsonRpcError(int64_t requestId, JsonRpcErrorCode code, const std::string& message, RequestContext& ctx);
    void SendMethodNotFound(int64_t requestId, const std::string& method, RequestContext& ctx);
    void SendInternalError(int64_t requestId, const std::string& message, RequestContext& ctx);

    // Indicates whether initialize has succeeded
    bool initialized_{false};

    // Capabilities advertised by the server during the MCP handshake.
    std::optional<ServerCapabilities> serverCapabilities_{std::nullopt};

    // If set, the client supports roots/list. Used to decide whether to advertise
    // ClientCapabilities.roots in the initialize request.
    ListRootsCallback listRootsCallback_{nullptr};

    // Client configuration used to build initialize.clientInfo
    ClientConfig clientConfig_;
};

} // namespace Mcp

#endif // MCP_CLIENT_SESSION_INCLUDE_H_
