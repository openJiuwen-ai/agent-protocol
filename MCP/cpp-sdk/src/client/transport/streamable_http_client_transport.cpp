/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "transport/streamable_http_client_transport.h"

#include <cstring>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

#include "http_client_service.h"
#include "mcp_auth.h"
#include "mcp_error.h"
#include "mcp_log.h"

namespace Mcp {

constexpr std::chrono::milliseconds MAX_TIMEOUT_MS{30 * 60 * 1000}; // 30 minutes in milliseconds

StreamableHttpClientTransport::StreamableHttpClientTransport(std::string url,
                                                             std::unordered_map<std::string, std::string> headers,
                                                             std::chrono::milliseconds timeout,
                                                             std::chrono::milliseconds sseReadTimeout,
                                                             const Mcp::TlsConfig& tlsConfig,
                                                             std::shared_ptr<AuthProvider> authProvider)
    : url_(std::move(url)),
      timeout_(timeout),
      sseReadTimeout_(sseReadTimeout),
      sessionId_(""),
      protocolVersion_(""),
      httpClientService_(nullptr),
      callback_(nullptr),
      authProvider_(std::move(authProvider))
{
    if (timeout_.count() <= 0 || timeout_.count() > MAX_TIMEOUT_MS.count()) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR,
                std::string("Invalid timeout value: ") + std::to_string(timeout_.count()) +
                    " milliseconds, max is " + std::to_string(MAX_TIMEOUT_MS.count()) + " milliseconds");
        throw std::invalid_argument("Invalid timeout value");
    }

    if (sseReadTimeout_.count() <= 0 || sseReadTimeout_.count() > MAX_TIMEOUT_MS.count()) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR,
                std::string("Invalid SSE read timeout value: ") + std::to_string(sseReadTimeout_.count()) +
                    " milliseconds, max is " + std::to_string(MAX_TIMEOUT_MS.count()) + " milliseconds");
        throw std::invalid_argument("Invalid SSE read timeout value");
    }
    // Create HttpClientServiceConfig with timeout settings
    Http::HttpClientServiceConfig config{};
    config.connectionTimeoutMs = static_cast<int>(timeout.count());
    config.requestTimeoutMs    = static_cast<int>(sseReadTimeout.count());
    config.tlsVerifyPeer       = tlsConfig.verifyPeer;
    config.tlsVerifyHost       = tlsConfig.verifyPeer;
    config.tlsCaFile           = tlsConfig.caFile;
    config.tlsClientCertFile   = tlsConfig.certFile;
    config.tlsClientKeyFile    = tlsConfig.keyFile;
    httpClientService_ = Http::HttpClientServiceFactory::Create(config);
    // Initialize request headers with Accept and Content-Type, then merge user headers
    requestHeaders_[Http::ACCEPT_HEADER] = std::string(Http::CONTENT_TYPE_JSON) + ", " + Http::CONTENT_TYPE_SSE;
    requestHeaders_[Http::CONTENT_TYPE_HEADER] = Http::CONTENT_TYPE_JSON;
    for (const auto& [key, value] : headers) {
        requestHeaders_[key] = value;
    }

    // Initialize HTTP client service
    if (httpClientService_ == nullptr) {
        throw std::runtime_error("HttpClientService creation failed");
    }
    if (!httpClientService_->Start()) {
        throw std::runtime_error("Failed to start HttpClientService");
    }
}


void StreamableHttpClientTransport::ReportError(const RequestId& requestId, JsonRpcErrorCode errorCode,
                                                const std::string& message, const std::optional<nlohmann::json>& data)
{
    if (callback_ == nullptr) {
        return;
    }

    // Check if requestId is valid (not default int64_t(0))
    if (std::holds_alternative<int64_t>(requestId) && std::get<int64_t>(requestId) == 0) {
        return;
    }

    JSONRPCError error;
    error.jsonrpc_ = JSONRPC_VERSION;
    error.id_ = requestId;
    error.code_ = static_cast<int>(errorCode);
    error.message_ = message;
    error.data_ = data;

    // Send error message via callback
    callback_->OnMessageReceived(error, ctx_);
}


StreamableHttpClientTransport::~StreamableHttpClientTransport()
{
    Terminate();
}

void StreamableHttpClientTransport::SetCallback(std::shared_ptr<TransportCallback> callback)
{
    callback_ = std::move(callback);
}

void StreamableHttpClientTransport::Connect()
{
    MCP_LOG(MCP_LOG_LEVEL_INFO, "HTTP client transport initialized");
    // For HTTP client transport, no persistent connection is required
    // The connection is established on each request
}

void StreamableHttpClientTransport::StartGetStream()
{
    HttpRequest httpRequest{};
    httpRequest.method = "GET";
    httpRequest.url = url_;
    PrepareRequestHeaders(httpRequest);
    httpRequest.body = "";

    if (sessionId_.empty()) {
        MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Session ID is empty while starting get stream");
        return;
    }

    // Bind response callback to HandleResponseHeader
    HttpCallback getStreamHeaderCallback = [this](const HttpResponse& response) {
        return HandleGetStreamHeader(response); // Return bool from HandleGetStreamHeader
    };
    HttpCallback getStreamBodyCallback = [this](const HttpResponse& response) {
        return HandleGetStreamBody(response); // Return bool from HandleGetStreamBody
    };

    UserData userData;
    try {
        httpClientService_->Send(httpRequest, userData, static_cast<int>(sseReadTimeout_.count()),
            getStreamHeaderCallback, getStreamBodyCallback);
    } catch (const std::exception& e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("Failed to enqueue HTTP request: ") + e.what());
        throw std::runtime_error(std::string("Failed to enqueue HTTP request: ") + e.what());
    }
}

bool StreamableHttpClientTransport::HandleGetStreamHeader(const HttpResponse& response)
{
    if (!response.success) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "SSE header request failed: " + response.errorMessage);
        return true; // Close connection on failure
    }

    // Check if status code is 2xx (success) : 100 is used to get the first digit, and the result is compared with 2
    if (response.statusCode / 100 != 2) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "HTTP error status " + std::to_string(response.statusCode));
        return true; // Close connection on status code error
    }

    MCP_LOG(MCP_LOG_LEVEL_DEBUG, "GET SSE connection established");
    return false; // Continue processing
}

bool StreamableHttpClientTransport::HandleGetStreamBody(const HttpResponse& response)
{
    if (!response.success) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "SSE body request failed: " + response.errorMessage);
        return true; // Close connection on failure
    }

    HandleSseEvent(response.sseEvent);
    return false; // Continue processing
}

void StreamableHttpClientTransport::SendMessage(const JSONRPCMessage& message)
{
    if (IsInitializedNotification(message)) {
        StartGetStream();
    }

    // Serialize the JSON-RPC message
    std::string messageBody = SerializeJSONRPCMessage(message);

    // Prepare HTTP request for HttpClientService
    HttpRequest httpRequest{};
    httpRequest.method = "POST";
    httpRequest.url = url_;
    httpRequest.body = std::move(messageBody);
    PrepareRequestHeaders(httpRequest);

    if (httpClientService_ == nullptr || !httpClientService_->IsRunning()) {
        throw std::runtime_error("HttpClientService is not running");
    }

    // Build user data from JSON-RPC message (only requests have id/method)
    UserData userData{};
    userData.requestId = int64_t(0);
    std::visit(
        [&userData](const auto& msg) {
            using T = std::decay_t<decltype(msg)>;
            if constexpr (std::is_same_v<T, JSONRPCRequest>) {
                // Directly use the RequestId from JSON-RPC message
                userData.requestId = msg.id_;
                userData.method = msg.method_;
            }
        },
        message);

    // Bind response callback to HandleResponseHeader
    HttpCallback responseHeaderCallback = [this](const HttpResponse& response) {
        return HandleResponseHeader(response); // Return bool from HandleResponseHeader
    };
    HttpCallback responseBodyCallback = [this](const HttpResponse& response) {
        return HandleResponseBody(response); // Return bool from HandleResponseBody
    };

    try {
        httpClientService_->Send(httpRequest, userData, static_cast<int>(sseReadTimeout_.count()),
            responseHeaderCallback, responseBodyCallback);
    } catch (const std::exception& e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("Failed to enqueue HTTP request: ") + e.what());
        throw std::runtime_error(std::string("Failed to enqueue HTTP request: ") + e.what());
    }
}

bool StreamableHttpClientTransport::HandleResponseHeader(const HttpResponse& response)
{
    MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Received HTTP status " + std::to_string(response.statusCode));

    // Check low-level HTTP client success flag first
    if (!response.success) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "HTTP request failed: " + response.errorMessage);
        std::string reason = "HTTP request failed";
        if (!response.errorMessage.empty()) {
            reason += ": " + response.errorMessage;
        }
        ReportError(response.userData.requestId, JsonRpcErrorCode::INTERNAL_ERROR, reason);
        return true; // Request failed, close connection
    }

    // Check status code
    if (response.statusCode == Http::HTTP_STATUS_ACCEPTED) {
        MCP_LOG(MCP_LOG_LEVEL_INFO, "Received " + std::to_string(Http::HTTP_STATUS_ACCEPTED) + " Accepted");
        return false; // Continue processing
    }

    // Handle 404 Not Found response (session not found or expired)
    if (response.statusCode == Http::HTTP_STATUS_NOT_FOUND) {
        MCP_LOG(MCP_LOG_LEVEL_WARN, "Session not found or expired (" +
                std::to_string(Http::HTTP_STATUS_NOT_FOUND) + ")");
        ReportError(response.userData.requestId, JsonRpcErrorCode::INVALID_REQUEST, "Session terminated");
        return true; // Session terminated, close connection
    }

    // Check if status code is 2xx (success): 100 is used to get the first digit, and the result is compared with 2
    if (response.statusCode / 100 != 2) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "HTTP error status " + std::to_string(response.statusCode));
        ReportError(response.userData.requestId, JsonRpcErrorCode::INVALID_REQUEST,
                    "HTTP error: " + std::to_string(response.statusCode));
        return true; // Close connection on status code error
    }

    // Determine if this response corresponds to an initialization request
    bool isInitialization = (response.userData.method == "initialize");
    // Extract session ID from response headers if this is initialization response
    if (isInitialization) {
        MayExtractSessionIdFromResponse(response);
        if (!sessionId_.empty()) {
            MCP_LOG(MCP_LOG_LEVEL_INFO, "Extracted session ID: " + sessionId_);
        }
    }

    return false; // Success, continue processing
}

bool StreamableHttpClientTransport::HandleResponseBody(const HttpResponse& response)
{
    if (!response.success) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "HTTP request failed");
        ReportError(response.userData.requestId, JsonRpcErrorCode::INTERNAL_ERROR, "HTTP request failed");
        return true; // Request failed, close connection
    }

    // handle based on contentType
    const std::string& contentType = getContentType(response);

    bool isInitialization = (response.userData.method == "initialize");
    if (contentType.find(Http::CONTENT_TYPE_JSON) != std::string::npos) {
        HandleJsonResponse(response, isInitialization);
    } else if (contentType.find(Http::CONTENT_TYPE_SSE) != std::string::npos) {
        return HandleSseResponse(response, isInitialization);
    } else {
        HandleUnexpectedContentType(response);
    }

    return false; // Continue processing
}

void StreamableHttpClientTransport::Terminate()
{
    MCP_LOG(MCP_LOG_LEVEL_INFO, "Clearing session state");

    // Stop HTTP client service if running
    if (httpClientService_ != nullptr) {
        httpClientService_->Stop();
    }

    // Clear session state
    sessionId_.clear();
    protocolVersion_.clear();
    callback_ = nullptr;
}

void StreamableHttpClientTransport::PrepareRequestHeaders(HttpRequest& httpRequest)
{
    httpRequest.headers = requestHeaders_;

    // Apply authentication provider (e.g., bearer token) if available
    if (authProvider_ != nullptr) {
        authProvider_->Apply(httpRequest.headers);
    }

    // Add MCP session ID if available
    if (!sessionId_.empty()) {
        httpRequest.headers[Http::MCP_SESSION_ID_HEADER] = sessionId_;
    }

    // Add MCP protocol version if available
    if (!protocolVersion_.empty()) {
        httpRequest.headers[Http::MCP_PROTOCOL_VERSION_HEADER] = protocolVersion_;
    }
}

bool StreamableHttpClientTransport::IsInitializationRequest(const JSONRPCMessage& message) const
{
    return std::visit(
        [](const auto& msg) -> bool {
            if constexpr (std::is_same_v<std::decay_t<decltype(msg)>, JSONRPCRequest>) {
                return msg.method_ == "initialize";
            }
            return false;
        },
        message);
}

bool StreamableHttpClientTransport::IsInitializedNotification(const JSONRPCMessage& message) const
{
    return std::visit(
        [](const auto& msg) -> bool {
            if constexpr (std::is_same_v<std::decay_t<decltype(msg)>, JSONRPCNotification>) {
                return msg.method_ == "notifications/initialized";
            }
            return false;
        },
        message);
}

void StreamableHttpClientTransport::HandleJsonResponse(const HttpResponse& response, bool isInitialization)
{
    // Deserialize JSON-RPC message
    JSONRPCMessage message = DeserializeJSONRPCMessage(response.body, response.userData.method);

    // Extract protocol version from initialization response if needed
    if (isInitialization) {
        MayExtractProtocolVersionFromMessage(message);
    }

    // Invoke transport callback
    if (callback_ != nullptr) {
        callback_->OnMessageReceived(message, ctx_);
    }
}

bool StreamableHttpClientTransport::HandleSseResponse(const HttpResponse& response, bool isInitialization)
{
    if (response.statusCode != Http::HTTP_STATUS_OK) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "SSE response error status " + std::to_string(response.statusCode));
        // Handle error response
        return true;
    }

    bool iscomplete = HandleSseEvent(response.sseEvent, response.userData.method, isInitialization);
    MCP_LOG(MCP_LOG_LEVEL_DEBUG, "response finish: " + std::to_string(iscomplete));
    return iscomplete;
}

bool StreamableHttpClientTransport::HandleSseEvent(const Http::ServerSentEvent& sse, const std::string& method,
    bool isInitialization)
{
    // Per MCP spec: Only process "message" events
    if (sse.event != "message") {
        MCP_LOG(MCP_LOG_LEVEL_WARN, "Unknown SSE event: " + sse.event);
        return false;
    }

    JSONRPCMessage message = JSONRPCRequest();
    try {
        message = DeserializeJSONRPCMessage(sse.data, method);

        // Extract protocol version from initialization response if applicable
        if (isInitialization) {
            MayExtractProtocolVersionFromMessage(message);
            if (!protocolVersion_.empty()) {
                MCP_LOG(MCP_LOG_LEVEL_INFO, "Extracted protocol version: " + protocolVersion_);
            }
        }
    } catch (const std::exception& e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("Error parsing SSE message: ") + e.what());
        return false;
    }

    // Forward the message to the transport callback
    if (callback_ != nullptr) {
        callback_->OnMessageReceived(message, ctx_);
    }

    return std::holds_alternative<JSONRPCResponse>(message) || std::holds_alternative<JSONRPCError>(message);
}

void StreamableHttpClientTransport::HandleUnexpectedContentType(const HttpResponse& response)
{
    auto contentTypeIter = response.headers.find(Http::CONTENT_TYPE_HEADER);
    std::string contentType = contentTypeIter != response.headers.end() ? contentTypeIter->second : "<missing>";
    MCP_LOG(MCP_LOG_LEVEL_ERROR, "Unexpected content type: " + contentType);
}

void StreamableHttpClientTransport::MayExtractSessionIdFromResponse(const HttpResponse& response)
{
    auto sessionIdIter = response.headers.find(Http::MCP_SESSION_ID_HEADER);
    if (sessionIdIter != response.headers.end() && !sessionIdIter->second.empty()) {
        sessionId_ = sessionIdIter->second;
    }
}

void StreamableHttpClientTransport::MayExtractProtocolVersionFromMessage(const JSONRPCMessage& message)
{
    // Extract protocol version from initialization response
    // Per MCP spec: protocolVersion is in the result field of the initialize response
    std::visit(
        [this](const auto& msg) {
            using T = std::decay_t<decltype(msg)>;
            if constexpr (std::is_same_v<T, JSONRPCResponse>) {
                if (msg.result_ != nullptr) {
                    auto* initResult = dynamic_cast<InitializeResult*>(msg.result_.get());
                    if (initResult != nullptr) {
                        protocolVersion_ = initResult->protocolVersion;
                    }
                }
            }
        },
        message);
}

} // namespace Mcp
