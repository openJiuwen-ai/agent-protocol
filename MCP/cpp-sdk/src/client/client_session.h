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
    std::future<std::shared_ptr<EmptyResult>> SendPing();

    // Send a roots list changed notification
    void SendRootsListChanged();

    // Set the logging level
    std::future<EmptyResult> SetLoggingLevel(LoggingLevel level);

    // Call a tool on the server
    std::future<std::shared_ptr<CallToolResult>> CallTool(const std::string& name,
                                                          const std::optional<JsonValue>& arguments = std::nullopt,
                                                          int timeout = 0);

    // List available tools. When cursor is provided, the server will return
    // a page of tools starting from that cursor and may include nextCursor in
    // the result for subsequent pages.
    std::future<std::shared_ptr<ListToolsResult>> ListTools();
    std::future<std::shared_ptr<ListToolsResult>> ListTools(const std::optional<std::string>& cursor);

    // List prompts
    std::future<std::shared_ptr<ListPromptsResult>> ListPrompts();

    // --- Resources ---
    std::future<std::shared_ptr<ListResourcesResult>> ListResources();
    std::future<std::shared_ptr<ListResourcesResult>> ListResources(const std::optional<std::string>& cursor);
    std::future<std::shared_ptr<ListResourceTemplatesResult>> ListResourcesTemplates();
    std::future<std::shared_ptr<ReadResourceResult>> ReadResource(const std::string& uri);
    std::future<std::shared_ptr<EmptyResult>> SubscribeResource(const std::string& uri);
    std::future<std::shared_ptr<EmptyResult>> UnsubscribeResource(const std::string& uri);

    // Get prompt from server
    std::future<std::shared_ptr<GetPromptResult>> GetPrompt(const std::string& name,
                                                            const std::optional<JsonValue>& arguments = std::nullopt);

    // Override BaseSession notification sending
    void SendNotification(const Notification& notification,
                          std::optional<RequestId> relatedRequestId = std::nullopt) override;

    // Check if initialize has completed successfully
    bool IsInitialized() const
    {
        return initialized_;
    }

    // Send notifications/initialized after successful initialize handshake
    void SendInitializedNotification();

private:
    // Incremental request ID for tracking (thread-safe)
    std::atomic<int64_t> _request_id{1};

    // Pending requests map: id -> raw-response callback
    std::mutex pending_mutex_;
    std::unordered_map<int64_t, std::function<void(const std::string&)>> pending_callbacks_;

    // Transport callback that sends raw JSON strings. Must be set by caller
    // (or via constructor) before making requests.
    std::function<bool(const std::string&)> send_raw_;

    // Method to handle incoming requests (for demonstration)
    void HandleRequest(const Mcp::JSONRPCRequest& request);

    // Feed incoming raw JSON messages from the transport into the session
    void OnMessageReceived(const std::string& message_json);

    // Indicates whether initialize has succeeded
    bool initialized_{false};

    // Client configuration used to build initialize.clientInfo
    ClientConfig clientConfig_;

    // Cache for tool output schemas
    std::unordered_map<std::string, JsonValue> toolOutputSchemas_;

    // Cache schemas from ListTools result
    void CacheToolSchemas(const ListToolsResult& r);

    // Validate tool result against cached output schema
    void ValidateToolResult(const std::string& name, const CallToolResult& result);
};

} // namespace Mcp

#endif // MCP_CLIENT_SESSION_INCLUDE_H_
