/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef MCP_SERVER_SESSION_INCLUDE_H_
#define MCP_SERVER_SESSION_INCLUDE_H_

#include "mcp_type.h"
#include "shared/base_session.h"
#include "shared/jsonrpc.h"

namespace Mcp {

// Callback type invoked when the session receives a request from the client.
using IncomingRequestCallback = std::function<void(int64_t requestId, const Request& request, RequestContext& ctx)>;

// Callback type invoked when the session receives a notification from the client.
using IncomingNotificationCallback = std::function<void(const Notification& notification)>;

/**
 * @brief Server-side session.
 *
 * This class owns the server-side session state built on top of `BaseSession`.
 * It receives requests/notifications from the client via the underlying `Transport`,
 * handles MCP initialization, and then forwards requests and notifications to the callback.
 */
class ServerSession final : public BaseSession, public std::enable_shared_from_this<ServerSession> {
public:
    explicit ServerSession(std::shared_ptr<ServerTransport> transport,
                           const ServerConfig& serverConfig = ServerConfig(), std::string sessionId = "")
        : BaseSession(std::move(transport), sessionId), serverConfig_(serverConfig)
    {
    }
    ~ServerSession() override;

    /**
     * @brief Set the capabilities that will be returned during initialization.
     *
     * Typically called before the client sends an Initialize request.
     */
    void SetServerCapabilities(const ServerCapabilities& capabilities);

    /**
     * @brief Set the callback invoked for incoming requests.
     */
    void SetIncomingRequestCallback(IncomingRequestCallback callback);

    /**
     * @brief Set the callback invoked for incoming notifications.
     */
    void SetIncomingNotificationCallback(IncomingNotificationCallback callback);

    /**
     * @brief Handle an incoming JSON-RPC request from the client.
     * This is called by `ServerManager` when a request is received.
     */
    void HandleRequest(const HttpRequest& request, RequestContext& context);

protected:
    /**
    * @brief Handle an incoming JSON-RPC request from the client.
    *
    * This is called by `BaseSession` when a request message is parsed.
     */
    void ReceivedRequest(int64_t requestId, const Request& request, RequestContext& ctx) override;

    /**
    * @brief Handle an incoming JSON-RPC notification from the client.
    *
    * This is called by `BaseSession` when a notification message is parsed.
     */
    void ReceivedNotification(const Notification& notification) override;

    /**
     * @brief Send a notification to the client.
     */
    void SendNotification(const Notification& notification, std::optional<int64_t> relatedRequestId) override;

    /**
     * @brief Send a progress notification to the client.
     */
    void SendProgressNotification(int64_t progressToken, double progress, std::optional<double> total,
                                  const std::optional<std::string>& message) override;

private:
    /**
     * @brief Handle the Initialize request from the client.
     *
     * Validates initialization parameters and replies with an InitializeResult.
     */
    void HandleInitializeRequest(int64_t requestId, const InitializeRequestParams& requestParams, RequestContext& ctx);

    /**
     * @brief Send an InitializeResult response to the client.
     */
    void SendInitializeResponse(int64_t requestId, const ServerCapabilities& capabilities,
                                const std::string& requestedProtocolVersion, RequestContext& ctx);

    /**
     * @brief Handle the "initialized" notification from the client.
     *
     * After this point the session is considered initialized and can accept
     * regular MCP requests.
     */
    void HandleInitializeNotification(const Notification& notification);

    // Server configuration used to guide session behavior.
    ServerConfig serverConfig_{};

    // Capabilities that the server advertises to the client.
    ServerCapabilities capabilities_{};

    // Whether the MCP initialization handshake has completed.
    bool isInitialized_{false};

    // request callback.
    IncomingRequestCallback incomingRequestCallback_{nullptr};

    // notification callback.
    IncomingNotificationCallback incomingNotificationCallback_{nullptr};
};

} // namespace Mcp

#endif // MCP_SERVER_SESSION_INCLUDE_H_
