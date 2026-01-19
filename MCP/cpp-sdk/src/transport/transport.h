/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef MCP_TRANSPORT_INCLUDE_H_
#define MCP_TRANSPORT_INCLUDE_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "mcp_type.h"
#include "shared/common_type.h"
#include "shared/http_common.h"
#include "shared/jsonrpc.h"

namespace Mcp {

using HttpRequest = Http::HttpRequest;

/**
 * @brief Base callback interface for MCP transports.
 */
class TransportCallback {
public:
    virtual ~TransportCallback() = default;

    /**
     * @brief Invoked when a JSON-RPC message is received.
     */
    virtual void OnMessageReceived(const JSONRPCMessage& message, RequestContext& ctx) = 0;

    /**
     * @brief Invoked when the transport is disconnected.
     */
    virtual void OnDisconnected(const std::string& reason) = 0;
};

/**
 * @brief Common base class for MCP transports.
 */
class ClientTransport {
public:
    virtual ~ClientTransport() = default;

    /**
     * @brief Establish resources or connections required for communication.
     */
    virtual void Connect() = 0;

    /**
     * @brief Terminate the transport and release resources.
     */
    virtual void Terminate() = 0;

    /**
     * @brief Send a JSON-RPC message through the transport.
     * @param message JSON-RPC message to send.
     */
    virtual void SendMessage(const JSONRPCMessage& message) = 0;

    /**
     * @brief Set callback interface for handling transport events.
     */
    virtual void SetCallback(std::shared_ptr<TransportCallback> callback) = 0;
};

/**
 * @brief Common base class for MCP server transports.
 */
class ServerTransport {
public:
    virtual ~ServerTransport() = default;

    /**
     * @brief Establish resources or connections required for communication.
     */
    virtual void Listen() = 0;

    /**
     * @brief Terminate the transport and release resources.
     */
    virtual void Terminate() = 0;

    /**
     * @brief Send a JSON-RPC message through the transport.
     * @param message JSON-RPC message to send.
     */
    virtual void SendMessage(const JSONRPCMessage& message, RequestContext& ctx) = 0;

    /**
     * @brief Set callback interface for handling transport events.
     */
    virtual void SetCallback(std::shared_ptr<TransportCallback> callback) = 0;

    /**
     * @brief Handle an HTTP request from the server.
     * @param request The HTTP request.
     * @param ctx Request context.
     */
    virtual void HandleRequest(const Http::HttpRequest& request, RequestContext& ctx) = 0;
};

} // namespace Mcp

#endif // MCP_TRANSPORT_INCLUDE_H_
