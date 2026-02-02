/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "server/server_session.h"

#include <algorithm>
#include "mcp_log.h"

namespace Mcp {

ServerSession::~ServerSession() = default;

void ServerSession::SetServerCapabilities(const ServerCapabilities& capabilities)
{
    // Stored and later returned in the InitializeResult during the MCP handshake.
    capabilities_ = capabilities;
}

void ServerSession::SetIncomingRequestCallback(IncomingRequestCallback callback)
{
    // The callback is invoked for non-initialization requests.
    incomingRequestCallback_ = std::move(callback);
}

void ServerSession::SetIncomingNotificationCallback(IncomingNotificationCallback callback)
{
    // The callback is invoked for non-initialization notifications.
    incomingNotificationCallback_ = std::move(callback);
}

void ServerSession::HandleRequest(const HttpRequest& request, RequestContext& context)
{
    serverTransport_->HandleRequest(request, context);
}

void ServerSession::ReceivedRequest(int64_t requestId, const Request& request, RequestContext& ctx)
{
    // The Initialize request is handled internally because it wires up session
    // state and negotiates capabilities.
    if (request.method_ == "initialize") {
        const auto* initParams = dynamic_cast<const InitializeRequestParams*>(request.params_.get());
        if (initParams != nullptr) {
            HandleInitializeRequest(requestId, *initParams, ctx);
        }
    } else {
        if (!isInitialized_) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "Received request before initialization: " + request.method_);
            return;
        }
        if (incomingRequestCallback_) {
            incomingRequestCallback_(requestId, request, ctx);
        }
    }
}

void ServerSession::ReceivedNotification(const Notification& notification)
{
    // The "initialized" notification completes the initialization handshake.
    if (notification.method_ == "notifications/initialized") {
        HandleInitializeNotification(notification);
    } else {
        if (incomingNotificationCallback_) {
            incomingNotificationCallback_(notification);
        }
    }
}

void ServerSession::HandleInitializeRequest(int64_t requestId, const InitializeRequestParams& requestParams,
                                            RequestContext& ctx)
{
    // Reply to the client with server capabilities and implementation info.
    SendInitializeResponse(requestId, capabilities_, requestParams.protocolVersion_, ctx);
}

void ServerSession::SendInitializeResponse(int64_t requestId, const ServerCapabilities& capabilities,
                                           const std::string& requestedProtocolVersion, RequestContext& ctx)
{
    // Build MCP InitializeResult payload.
    Implementation serverInfo;
    serverInfo.name = serverConfig_.name;
    serverInfo.version = serverConfig_.version;
    auto protocolVersion_ = (std::find(SUPPORTED_PROTOCOL_VERSIONS.begin(), SUPPORTED_PROTOCOL_VERSIONS.end(),
                                       requestedProtocolVersion) != SUPPORTED_PROTOCOL_VERSIONS.end()) ?
                             requestedProtocolVersion : LATEST_PROTOCOL_VERSION;
    auto initResult = std::make_unique<InitializeResult>(protocolVersion_, capabilities, serverInfo);
    SendResponse(requestId, std::move(initResult), ctx);
    isInitialized_ = true;
}

void ServerSession::HandleInitializeNotification(const Notification& notification)
{
    (void)notification;
    // From here on the server treats the client as fully initialized.
    isInitialized_ = true;
}

void ServerSession::SendNotification(const Notification& notification, std::optional<int64_t> relatedRequestId)
{
}

void ServerSession::SendProgressNotification(int64_t progressToken, double progress, std::optional<double> total,
                                             const std::optional<std::string>& message)
{
}

} // namespace Mcp
