/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef MCP_STREAMABLE_HTTP_CLIENT_TRANSPORT_INCLUDE_H_
#define MCP_STREAMABLE_HTTP_CLIENT_TRANSPORT_INCLUDE_H_

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "http_client_service.h"
#include "mcp_auth.h"
#include "mcp_error.h"
#include "mcp_type.h"
#include "shared/http_common.h"
#include "shared/jsonrpc.h"
#include "transport/transport.h"

namespace Mcp {

using UserData = Http::UserData;
using HttpRequest = Http::HttpRequest;
using HttpResponse = Http::HttpResponse;
using HttpCallback = Http::HttpCallback;


// HTTP Timeout constants (in milliseconds)
constexpr int DEFAULT_HTTP_TIMEOUT_MS = 30000; // 30 seconds
constexpr int DEFAULT_SSE_READ_TIMEOUT_MS = 60000; // 60 seconds

class StreamableHttpClientTransport : public ClientTransport {
public:
    StreamableHttpClientTransport(
        std::string url, std::unordered_map<std::string, std::string> headers = {},
        std::chrono::milliseconds timeout = std::chrono::milliseconds{DEFAULT_HTTP_TIMEOUT_MS},
        std::chrono::milliseconds sseReadTimeout = std::chrono::milliseconds{DEFAULT_SSE_READ_TIMEOUT_MS},
        const Mcp::TlsConfig& tlsConfig = Mcp::TlsConfig{},
        std::shared_ptr<AuthProvider> authProvider = nullptr);
    virtual ~StreamableHttpClientTransport();

    // Send message to server
    void SendMessage(const JSONRPCMessage& message) override;

    void Connect() override;

    void Terminate() override;

    void TerminateSession(std::chrono::milliseconds timeout = std::chrono::milliseconds{1000}) override;

    void SetCallback(std::shared_ptr<TransportCallback> callback) override;

private:
    void PrepareRequestHeaders(HttpRequest& httpRequest);
    bool IsInitializationRequest(const JSONRPCMessage& message) const;
    bool IsInitializedNotification(const JSONRPCMessage& message) const;
    void HandleJsonResponse(const HttpResponse& response, bool isInitialization);
    bool HandleSseResponse(const HttpResponse& response, bool isInitialization);
    bool HandleSseEvent(const Http::ServerSentEvent& sse, const std::string& method = "",
        bool isInitialization = false);
    void HandleUnexpectedContentType(const HttpResponse& response);
    void MayExtractSessionIdFromResponse(const HttpResponse& response);
    void MayExtractProtocolVersionFromMessage(const JSONRPCMessage& message);
    void ReportError(const RequestId& requestId, JsonRpcErrorCode errorCode, const std::string& message,
                     const std::optional<nlohmann::json>& data = std::nullopt);

    // Handle response from server (called internally when response received, triggers session callback)
    bool HandleResponseHeader(const HttpResponse& response);
    bool HandleResponseBody(const HttpResponse& response);

    // SSE stream handling methods
    void StartGetStream();
    bool HandleGetStreamHeader(const HttpResponse& response);
    bool HandleGetStreamBody(const HttpResponse& response);

    std::string url_;
    std::unordered_map<std::string, std::string> requestHeaders_;
    std::chrono::milliseconds timeout_;
    std::chrono::milliseconds sseReadTimeout_;

    std::string sessionId_;
    std::string protocolVersion_;

    std::unique_ptr<Http::HttpClientService> httpClientService_;
    std::shared_ptr<TransportCallback> callback_;
    std::shared_ptr<AuthProvider> authProvider_;
    RequestContext ctx_;
};

} // namespace Mcp

#endif // MCP_STREAMABLE_HTTP_CLIENT_TRANSPORT_INCLUDE_H_
