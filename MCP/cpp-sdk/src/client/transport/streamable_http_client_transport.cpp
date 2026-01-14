/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "transport/streamable_http_client_transport.h"

#include <cstring>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

#include "http_client_service.h"
#include "mcp_log.h"

namespace Mcp {

// SSE field prefix lengths
constexpr size_t SSE_EVENT_PREFIX_LEN = std::strlen("event:"); // "event:"
constexpr size_t SSE_ID_PREFIX_LEN = std::strlen("id:"); // "id:"
constexpr size_t SSE_DATA_PREFIX_LEN = std::strlen("data:"); // "data:"
constexpr std::chrono::milliseconds MAX_TIMEOUT_MS{30 * 60 * 1000}; // 30 minutes in milliseconds

StreamableHttpClientTransport::StreamableHttpClientTransport(std::string url,
                                                             std::unordered_map<std::string, std::string> headers,
                                                             std::chrono::milliseconds timeout,
                                                             std::chrono::milliseconds sseReadTimeout,
                                                             const Mcp::TlsConfig& tlsConfig)
    : url_(std::move(url)),
      timeout_(timeout),
      sseReadTimeout_(sseReadTimeout),
      sessionId_(""),
      protocolVersion_(""),
      sseConnectionId_(0),
      httpClientService_(nullptr),
      callback_(nullptr)
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
    requestHeaders_[CONTENT_TYPE_HEADER] = Http::CONTENT_TYPE_JSON;
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

void StreamableHttpClientTransport::SendSessionTerminatedError(const HttpResponse& response)
{
    if (callback_ == nullptr) {
        return;
    }
    // Construct JSON-RPC error per MCP spec: code -32600, message "Session terminated"
    JSONRPCError error;
    error.jsonrpc_ = JSONRPC_VERSION;
    error.id_ = static_cast<int>(response.userData.requestId);
    error.code_ = static_cast<int>(JsonRpcErrorCode::INVALID_REQUEST); // -32600
    error.message_ = "Session terminated";
    error.data_ = std::nullopt;
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

void StreamableHttpClientTransport::SendMessage(const JSONRPCMessage& message)
{
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
    userData.requestId = 0;
    std::visit(
        [&userData](const auto& msg) {
            using T = std::decay_t<decltype(msg)>;
            if constexpr (std::is_same_v<T, JSONRPCRequest>) {
                if (msg.id_ > 0) {
                    userData.requestId = static_cast<uint64_t>(msg.id_);
                }
                userData.method = msg.method_;
            }
        },
        message);

    // Bind response callback to HandleResponse
    HttpCallback callback = [this](const HttpResponse& response) { HandleResponse(response); };

    try {
        httpClientService_->Send(httpRequest, &userData, static_cast<int>(sseReadTimeout_.count()), callback);
    } catch (const std::exception& e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("Failed to enqueue HTTP request: ") + e.what());
        throw std::runtime_error(std::string("Failed to enqueue HTTP request: ") + e.what());
    }
}

void StreamableHttpClientTransport::HandleResponse(const HttpResponse& response)
{
    MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Received HTTP status " + std::to_string(response.statusCode));

    // Check low-level HTTP client success flag first
    if (!response.success) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "HTTP request failed: " + response.errorMessage);
        if (callback_ != nullptr) {
            std::string reason = "HTTP request failed";
            if (!response.errorMessage.empty()) {
                reason += ": " + response.errorMessage;
            }
            callback_->OnDisconnected(reason);
        }
        return;
    }

    // Check status code
    if (response.statusCode == Http::HTTP_STATUS_ACCEPTED) {
        MCP_LOG(MCP_LOG_LEVEL_INFO, "Received " + std::to_string(Http::HTTP_STATUS_ACCEPTED) + " Accepted");
        return;
    }

    if (response.statusCode == Http::HTTP_STATUS_NOT_FOUND) {
        MCP_LOG(MCP_LOG_LEVEL_WARN, "Session not found or expired (" +
                std::to_string(Http::HTTP_STATUS_NOT_FOUND) + ")");
        // 404 Not Found - Session not found or expired
        SendSessionTerminatedError(response);
        return;
    }

    if (response.statusCode != Http::HTTP_STATUS_OK) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "HTTP error status " + std::to_string(response.statusCode));
        // Handle other error responses
        return;
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

    // Determine content type
    auto contentTypeIter = response.headers.find(CONTENT_TYPE_HEADER);
    if (contentTypeIter == response.headers.end()) {
        HandleUnexpectedContentType(response);
        return;
    }

    const std::string& contentType = contentTypeIter->second;

    // Handle based on content type
    if (contentType.find(Http::CONTENT_TYPE_JSON) != std::string::npos) {
        HandleJsonResponse(response, isInitialization);
    } else if (contentType.find(Http::CONTENT_TYPE_SSE) != std::string::npos) {
        HandleSseResponse(response, isInitialization);
    } else {
        HandleUnexpectedContentType(response);
    }
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
    sseConnectionId_ = 0;
    callback_ = nullptr;
}

void StreamableHttpClientTransport::PrepareRequestHeaders(HttpRequest& httpRequest)
{
    httpRequest.headers = requestHeaders_;

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

void StreamableHttpClientTransport::HandleSseResponse(const HttpResponse& response, bool isInitialization)
{
    if (response.statusCode != Http::HTTP_STATUS_OK) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "SSE response error status " + std::to_string(response.statusCode));
        // Handle error response
        return;
    }

    // Parse SSE events from response body
    std::istringstream stream(response.body);
    std::string line;
    EventData currentEvent;

    while (std::getline(stream, line)) {
        // Remove trailing \r if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // Empty line indicates end of event
        if (line.empty()) {
            if (!currentEvent.data.index() == 0 || !currentEvent.event.empty()) {
                HandleSseEvent(currentEvent, isInitialization);
            }
            currentEvent = EventData();
            continue;
        }

        // Parse SSE field
        if (line.find("event:") == 0) {
            currentEvent.event = line.substr(SSE_EVENT_PREFIX_LEN);
            // Trim leading whitespace
            size_t firstNonSpace = currentEvent.event.find_first_not_of(" \t");
            if (firstNonSpace != std::string::npos) {
                currentEvent.event = currentEvent.event.substr(firstNonSpace);
            }
        } else if (line.find("id:") == 0) {
            currentEvent.id = line.substr(SSE_ID_PREFIX_LEN);
            // Trim leading whitespace
            size_t firstNonSpace = currentEvent.id.find_first_not_of(" \t");
            if (firstNonSpace != std::string::npos) {
                currentEvent.id = currentEvent.id.substr(firstNonSpace);
            }
        } else if (line.find("data:") == 0) {
            std::string dataStr = line.substr(SSE_DATA_PREFIX_LEN);
            // Trim leading whitespace
            size_t firstNonSpace = dataStr.find_first_not_of(" \t");
            if (firstNonSpace != std::string::npos) {
                dataStr = dataStr.substr(firstNonSpace);
            }

            // Parse JSON data
            currentEvent.data = DeserializeJSONRPCMessage(dataStr, response.userData.method);
        }
    }

    // Handle last event if present
    if (!currentEvent.event.empty()) {
        HandleSseEvent(currentEvent, isInitialization);
    }
}

void StreamableHttpClientTransport::HandleSseEvent(const EventData& eventData, bool isInitialization)
{
    // Per MCP spec: Only process "message" events
    if (eventData.event != "message" && !eventData.event.empty()) {
        MCP_LOG(MCP_LOG_LEVEL_WARN, "Ignoring unknown SSE event type: " + eventData.event);
        return;
    }

    // Extract protocol version from initialization response if applicable
    if (isInitialization) {
        MayExtractProtocolVersionFromMessage(eventData.data);
        if (!protocolVersion_.empty()) {
            MCP_LOG(MCP_LOG_LEVEL_INFO, "Extracted protocol version: " + protocolVersion_);
        }
    }

    // Forward the message to the transport callback
    if (callback_ != nullptr) {
        callback_->OnMessageReceived(eventData.data, ctx_);
    }

    // Note: In async implementations, this would return bool to indicate if response is complete
    // (i.e., message is JsonRpcResponse or JsonRpcError), but in sync version we process all events
}

void StreamableHttpClientTransport::HandleUnexpectedContentType(const HttpResponse& response)
{
    auto contentTypeIter = response.headers.find(CONTENT_TYPE_HEADER);
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
