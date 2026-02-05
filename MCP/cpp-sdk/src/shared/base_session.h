/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef MCP_BASE_SESSION_INCLUDE_H_
#define MCP_BASE_SESSION_INCLUDE_H_

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>

#include "common_type.h"
#include "jsonrpc.h"
#include "transport/transport.h"

namespace Mcp {

using ProgressCallback =
    std::function<void(double progress, std::optional<double> total, const std::optional<std::string>& message)>;

// Forward declaration so it can be used in SessionTransportCallback
class BaseSession;

class SessionTransportCallback : public TransportCallback {
public:
    explicit SessionTransportCallback(BaseSession* session) : session_(session)
    {
    }

    void OnMessageReceived(const JSONRPCMessage& message, RequestContext& ctx) override;

    void OnDisconnected(const std::string& reason) override
    {
        (void)reason;
    }

private:
    BaseSession* session_;
};

class BaseSession;

/**
 * Base class for MCP sessions implementing request/response handling,
 * notifications, and progress tracking.
 */
class BaseSession {
public:
    // Constructor accepting ServerTransport
    explicit BaseSession(std::shared_ptr<ServerTransport> transport, std::string sessionId = "")
        : serverTransport_(std::move(transport)), sessionId_(sessionId), requestId_(1)
    {
        if (serverTransport_) {
            serverTransport_->SetCallback(std::make_shared<SessionTransportCallback>(this));
        }
    }

    // Constructor accepting ClientTransport
    explicit BaseSession(std::shared_ptr<ClientTransport> transport,
                         std::optional<std::chrono::seconds> readTimeout = std::nullopt, std::string sessionId = "")
        : clientTransport_(std::move(transport)), sessionId_(sessionId), requestId_(1), sessionReadTimeout_(readTimeout)
    {
        if (clientTransport_) {
            clientTransport_->SetCallback(std::make_shared<SessionTransportCallback>(this));
        }
    }

    virtual ~BaseSession()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        progressCallbacks_.clear();
        completionCallbacks_.clear();
    }

    /**
     * Send a request with an optional completion callback for handling responses.
     *
     * - The caller supplies an optional completion callback invoked when the response arrives.
     * - On success, the callback receives the result; on error or missing result, it receives nullptr.
     * - Progress updates (if provided) invoke `progressCallback`.
     *
     * @param request Request payload to send.
     * @param completion Optional callback invoked with the response result (or nullptr on error).
     * @param requestTimeout Optional timeout for this request (defaults to nullopt).
     * @param progressCallback Optional callback for progress notifications (defaults to nullopt).
     */
    void SendRequest(std::unique_ptr<Request> request, std::function<void(std::shared_ptr<Result>)> completion,
                     std::optional<std::chrono::seconds> requestTimeout,
                     std::optional<ProgressCallback> progressCallback);

    // Convenience overload: only completion callback
    void SendRequest(std::unique_ptr<Request> request, std::function<void(std::shared_ptr<Result>)> completion)
    {
        SendRequest(std::move(request), std::move(completion), std::nullopt, std::nullopt);
    }

    /**
     * Send a notification (one-way message with no response expected).
     *
     * @param notification The notification to send
     * @param relatedRequestId Optional ID of the request this notification relates to
     */
    virtual void SendNotification(std::unique_ptr<Notification> notification,
                                  std::optional<int64_t> relatedRequestId = std::nullopt) = 0;

    /**
     * Send a progress notification for a request being processed.
     *
     * @param progress_token Token identifying the request
     * @param progress Current progress value
     * @param total Optional total progress value
     * @param message Optional progress message
     */
    virtual void SendProgressNotification(const std::string& progressToken, double progress,
                                          std::optional<double> total = std::nullopt,
                                          const std::optional<std::string>& message = std::nullopt);

    /**
     * Send a response to a request.
     */
    void SendResponse(int64_t requestId, std::unique_ptr<Result> result, RequestContext& ctx);
    void SendResponse(int64_t requestId, JSONRPCError error, RequestContext& ctx);

    void OnTransportMessage(const JSONRPCMessage& message, RequestContext& ctx);

    std::string GetSessionId() const
    {
        return sessionId_;
    }

public:
    // record requests
    std::unordered_map<int64_t, RequestContext> sessionRequests;

    std::mutex reqMtx;

protected:
    /**
     * Called when a request is received.
     */
    virtual void ReceivedRequest(int64_t requestId, const Request& request, RequestContext& ctx)
    {
    }

    /**
     * Called when a notification is received. Can be overridden to handle
     * notifications without using the message stream.
     */
    virtual void ReceivedNotification(const Notification& notification)
    {
    }

    /**
     * Handle an incoming response or error.
     * Routes to appropriate response stream or progress callback.
     */
    void HandleResponse(const JSONRPCResponse& response);
    void HandleResponse(const JSONRPCError& error);

    void ProcessIncomingRequest(const JSONRPCRequest& request, RequestContext& ctx);

    /**
     * Process an incoming notification.
     */
    void ProcessIncomingNotification(const JSONRPCNotification& notification);

    std::shared_ptr<ClientTransport> clientTransport_;
    std::shared_ptr<ServerTransport> serverTransport_;

    // Unique session identifier
    std::string sessionId_;

    // Request ID counter
    std::atomic<int64_t> requestId_;

    // Session-wide read timeout
    std::optional<std::chrono::seconds> sessionReadTimeout_;

    // Mutex for thread-safe access to shared state
    std::mutex mutex_;

    // Progress callbacks for requests
    std::unordered_map<int64_t, ProgressCallback> progressCallbacks_;

    // Completion callbacks for requests
    std::unordered_map<int64_t, std::function<void(std::shared_ptr<Result>)>> completionCallbacks_;
};

inline void SessionTransportCallback::OnMessageReceived(const JSONRPCMessage& message, RequestContext& ctx)
{
    if (session_ != nullptr) {
        session_->OnTransportMessage(message, ctx);
    }
}

} // namespace Mcp

#endif // MCP_BASE_SESSION_INCLUDE_H_
