/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef MCP_STREAMABLE_HTTP_SERVER_TRANSPORT_INCLUDE_H_
#define MCP_STREAMABLE_HTTP_SERVER_TRANSPORT_INCLUDE_H_

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>

#include "shared/common_type.h"
#include "shared/http_common.h"
#include "shared/jsonrpc.h"
#include "transport/transport.h"

namespace Mcp {

using Http::HttpResponse;
using Http::HttpSendType;

// Event message structure containing serialized JSON-RPC message and event ID
struct EventMessage {
    const JSONRPCMessage& message;
    std::string eventId;

    EventMessage(const JSONRPCMessage& msg, const std::string& id) : message(msg), eventId(id)
    {
    }
};

class StreamableHttpServerTransport : public ServerTransport {
public:
    StreamableHttpServerTransport(const std::string& mcpSessionId, bool isJsonResponseEnabled = false);
    virtual ~StreamableHttpServerTransport() = default;

    // Set callback for handling transport events (used by ServerManager)
    void SetCallback(std::shared_ptr<TransportCallback> callback) override;

    void HandleRequest(const HttpRequest& request, RequestContext& ctx) override;

    void SendMessage(const JSONRPCMessage& message, RequestContext& ctx) override;

    void Listen() override;

    void Terminate() override;

private:
    std::string GetSessionId(const HttpRequest& request) const;
    bool ValidateProtocolVersion(RequestContext& ctx, const HttpRequest& request);
    void HandlePostRequest(RequestContext& ctx, const HttpRequest& request);
    void HandleGetRequest(RequestContext& ctx, const HttpRequest& request);
    void HandleDeleteRequest(RequestContext& ctx, const HttpRequest& request);
    void HandleUnsupportedRequest(RequestContext& ctx, const HttpRequest& request);
    HttpResponse CreateErrorResponse(const std::string& errorMessage, int statusCode,
                                     int errorCode = static_cast<int>(JsonRpcErrorCode::INVALID_REQUEST),
                                     const std::unordered_map<std::string, std::string>& headers = {});
    HttpResponse CreateJsonResponse(const std::optional<JSONRPCMessage>& message, int statusCode,
                                    const std::unordered_map<std::string, std::string>& headers,
                                    const RequestContext& ctx);
    std::string CreateEventData(const EventMessage& eventMessage, const RequestContext& ctx);
    bool ValidatePostRequestHeaders(RequestContext& ctx, const HttpRequest& request);
    bool ValidateSessionId(RequestContext& ctx, const HttpRequest& request, bool isInitializationRequest);
    bool ParseJsonBody(RequestContext& ctx, const HttpRequest& request, nlohmann::json& messageJson);
    void HandleNonRequestMessage(RequestContext& ctx, const JSONRPCMessage& message);

    std::string mcpSessionId_;
    bool isJsonResponseEnabled_;
    std::optional<RequestContext> getStreamRequestContext_;
    bool isTerminated_;
    std::shared_ptr<TransportCallback> callback_;
};
} // namespace Mcp

#endif // MCP_STREAMABLE_HTTP_SERVER_TRANSPORT_INCLUDE_H_
