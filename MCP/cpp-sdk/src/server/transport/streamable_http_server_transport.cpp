/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "streamable_http_server_transport.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <optional>

#include "mcp_log.h"
#include "shared/jsonrpc.h"

namespace Mcp {

using Http::HttpResponse;

// Header names
constexpr const char* LAST_EVENT_ID_HEADER = "last-event-id";

// Special key for the standalone GET stream
constexpr const char* GET_STREAM_KEY = "_GET_stream";

// Session ID validation pattern (visible ASCII characters ranging from 0x21 to 0x7E)
static const std::regex SESSION_ID_PATTERN("^[\\x21-\\x7E]+$");

StreamableHttpServerTransport::StreamableHttpServerTransport(const std::string& mcpSessionId,
    bool isJsonResponseEnabled, bool stateless)
    : mcpSessionId_(mcpSessionId),
      isJsonResponseEnabled_(isJsonResponseEnabled),
      stateless_(stateless),
      getStreamRequestContext_(std::nullopt),
      isTerminated_(false),
      callback_(nullptr)
{
    if (!mcpSessionId_.empty() && !std::regex_match(mcpSessionId_, SESSION_ID_PATTERN)) {
        throw std::invalid_argument("Session ID must only contain visible ASCII characters (0x21-0x7E)");
    }
}

void StreamableHttpServerTransport::SetCallback(std::shared_ptr<TransportCallback> callback)
{
    callback_ = std::move(callback);
}

std::string StreamableHttpServerTransport::GetSessionId(const HttpRequest& request) const
{
    // Extract the session ID from request headers
    auto it = request.headers.find(Http::MCP_SESSION_ID_HEADER);
    if (it != request.headers.end()) {
        return it->second;
    }
    return "";
}

bool StreamableHttpServerTransport::ValidateProtocolVersion(RequestContext& ctx, const HttpRequest& request)
{
    if (ctx.httpSendFunc == nullptr) {
        throw std::runtime_error("HTTP callback not set");
    }

    // Get the protocol version from the request headers
    std::string protocolVersion{};
    auto versionIt = request.headers.find(Http::MCP_PROTOCOL_VERSION_HEADER);
    if (versionIt != request.headers.end()) {
        protocolVersion = versionIt->second;
    } else {
        // If no protocol version provided, assume default version
        protocolVersion = DEFAULT_PROTOCOL_VERSION;
    }

    // Check if the protocol version is supported
    auto it = std::find(SUPPORTED_PROTOCOL_VERSIONS.begin(), SUPPORTED_PROTOCOL_VERSIONS.end(), protocolVersion);
    if (it != SUPPORTED_PROTOCOL_VERSIONS.end()) {
        return true;
    }

    std::string errorMessage = "Bad Request: Unsupported protocol version: " + protocolVersion +
                               ". Supported versions: " + SUPPORTED_PROTOCOL_VERSIONS_STRING;
    HttpResponse response = CreateErrorResponse(errorMessage, Http::HTTP_STATUS_BAD_REQUEST,
                                                static_cast<int>(JsonRpcErrorCode::INVALID_REQUEST));
    ctx.httpSendFunc(response, ctx);
    return false;
}

HttpResponse StreamableHttpServerTransport::CreateErrorResponse(
    const std::string& errorMessage, int statusCode, int errorCode,
    const std::unordered_map<std::string, std::string>& headers)
{
    HttpResponse response{};
    response.statusCode = statusCode;
    response.headers = headers;
    response.headers[Http::CONTENT_TYPE_HEADER] = Http::CONTENT_TYPE_JSON;

    // Stateless transport instances are constructed with an empty session id.
    if (!mcpSessionId_.empty()) {
        response.headers[Http::MCP_SESSION_ID_HEADER] = mcpSessionId_;
    }

    nlohmann::json errorJson;
    errorJson["code"] = errorCode;
    errorJson["message"] = errorMessage;

    nlohmann::json errorResponse;
    errorResponse["jsonrpc"] = JSONRPC_VERSION;
    errorResponse["id"] = "server-error";
    errorResponse["error"] = errorJson;

    response.body = errorResponse.dump();
    return response;
}

HttpResponse StreamableHttpServerTransport::CreateJsonResponse(
    const std::optional<JSONRPCMessage>& message, int statusCode,
    const std::unordered_map<std::string, std::string>& headers, const RequestContext& ctx)
{
    HttpResponse response{};
    response.statusCode = statusCode;
    response.headers = headers;
    response.headers[Http::CONTENT_TYPE_HEADER] = Http::CONTENT_TYPE_JSON;

    // Stateless transport instances are constructed with an empty session id.
    if (!mcpSessionId_.empty()) {
        response.headers[Http::MCP_SESSION_ID_HEADER] = mcpSessionId_;
    }

    // Serialize the JSON-RPC message if provided
    if (message.has_value()) {
        response.body = SerializeJSONRPCMessage(message.value(), ctx.method);
    }

    return response;
}

std::string StreamableHttpServerTransport::CreateEventData(const EventMessage& eventMessage, const RequestContext& ctx)
{
    std::ostringstream oss{};

    // SSE format: event: message\ndata: {...}\nid: xxx\n\n
    oss << "event: message\n";
    // Serialize the message
    if (!eventMessage.eventId.empty()) {
        oss << "id: " << eventMessage.eventId << "\n";
    }
    std::string messageData = SerializeJSONRPCMessage(eventMessage.message, ctx.method);
    // Each SSE event must end with a blank line
    oss << "data: " << messageData << "\n\n";
    return oss.str();
}

void StreamableHttpServerTransport::HandleRequest(const HttpRequest& request, RequestContext& ctx)
{
    const std::string requestSessionId = GetSessionId(request);
    MCP_LOG(MCP_LOG_LEVEL_DEBUG, std::string("Handle request for session ") + ctx.sessionId +
            ", request.sessionid is " + requestSessionId);
    if (ctx.httpSendFunc == nullptr) {
        throw std::runtime_error("HTTP callback not set");
    }

    // Stateless mode is POST-only (each request is handled independently).
    if (stateless_ && request.method != "POST") {
        std::unordered_map<std::string, std::string> headers{};
        headers["Allow"] = "POST";
        HttpResponse response = CreateErrorResponse("Method Not Allowed", Http::HTTP_STATUS_METHOD_NOT_ALLOWED,
                                                    static_cast<int>(JsonRpcErrorCode::INVALID_REQUEST), headers);
        ctx.httpSendFunc(response, ctx);
        return;
    }

    // Check if session has been terminated
    if (isTerminated_) {
        HttpResponse response =
            CreateErrorResponse("Not Found: Session has been terminated", Http::HTTP_STATUS_NOT_FOUND,
                                static_cast<int>(JsonRpcErrorCode::INVALID_REQUEST));
        ctx.httpSendFunc(response, ctx);
        return;
    }

    if (request.method == "POST") {
        HandlePostRequest(ctx, request);
    } else if (request.method == "GET") {
        HandleGetRequest(ctx, request);
    } else if (request.method == "DELETE") {
        HandleDeleteRequest(ctx, request);
    } else {
        HandleUnsupportedRequest(ctx, request);
    }
}

bool StreamableHttpServerTransport::ValidatePostRequestHeaders(RequestContext& ctx, const HttpRequest& request)
{
    auto acceptIt = request.headers.find(Http::ACCEPT_HEADER);
    std::string acceptHeader{};
    if (acceptIt != request.headers.end()) {
        acceptHeader = acceptIt->second;
    }
    bool hasJson = acceptHeader.find(Http::CONTENT_TYPE_JSON) != std::string::npos;
    bool hasSse = acceptHeader.find(Http::CONTENT_TYPE_SSE) != std::string::npos;
    if (isJsonResponseEnabled_) {
        if (!hasJson) {
            HttpResponse response = CreateErrorResponse(
                "Not Acceptable: Client must accept application/json",
                Http::HTTP_STATUS_NOT_ACCEPTABLE, static_cast<int>(JsonRpcErrorCode::INVALID_REQUEST));
            ctx.httpSendFunc(response, ctx);
            return false;
        }
    } else {
        if (!hasJson || !hasSse) {
            // print headers
            for (const auto& header : request.headers) {
                MCP_LOG(MCP_LOG_LEVEL_DEBUG, std::string("Header: ") + header.first + ": " + header.second);
            }
            HttpResponse response = CreateErrorResponse(
                "Not Acceptable: Client must accept both application/json and text/event-stream",
                Http::HTTP_STATUS_NOT_ACCEPTABLE, static_cast<int>(JsonRpcErrorCode::INVALID_REQUEST));
            ctx.httpSendFunc(response, ctx);
            return false;
        }
    }
    auto contentTypeIt = request.headers.find(Http::CONTENT_TYPE_HEADER);
    if (contentTypeIt == request.headers.end() ||
        contentTypeIt->second.find(Http::CONTENT_TYPE_JSON) == std::string::npos) {
        HttpResponse response = CreateErrorResponse("Unsupported Media Type: Content-Type must be application/json",
                                                    Http::HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE,
                                                    static_cast<int>(JsonRpcErrorCode::INVALID_REQUEST));
        ctx.httpSendFunc(response, ctx);
        return false;
    }
    return true;
}

bool StreamableHttpServerTransport::ValidateSessionId(RequestContext& ctx, const HttpRequest& request,
                                                      bool isInitializationRequest)
{
    if (isInitializationRequest || mcpSessionId_.empty()) {
        return true;
    }
    std::string requestSessionId = GetSessionId(request);
    if (requestSessionId.empty()) {
        HttpResponse response = CreateErrorResponse("Bad Request: Missing session ID", Http::HTTP_STATUS_BAD_REQUEST,
                                                    static_cast<int>(JsonRpcErrorCode::INVALID_REQUEST));
        ctx.httpSendFunc(response, ctx);
        return false;
    }
    if (requestSessionId != mcpSessionId_) {
        MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Invalid session ID: " + requestSessionId + ", expected: " + mcpSessionId_);
        HttpResponse response = CreateErrorResponse("Not Found: Invalid or expired session ID",
            Http::HTTP_STATUS_NOT_FOUND, static_cast<int>(JsonRpcErrorCode::INVALID_REQUEST));
        ctx.httpSendFunc(response, ctx);
        return false;
    }
    return true;
}

bool StreamableHttpServerTransport::ParseJsonBody(RequestContext& ctx, const HttpRequest& request,
                                                  nlohmann::json& messageJson)
{
    try {
        messageJson = nlohmann::json::parse(request.body);
        return true;
    } catch (const nlohmann::json::parse_error& e) {
        HttpResponse response =
            CreateErrorResponse(std::string("Parse error: ") + e.what(), Http::HTTP_STATUS_BAD_REQUEST,
                                static_cast<int>(JsonRpcErrorCode::PARSE_ERROR));
        ctx.httpSendFunc(response, ctx);
        return false;
    }
}

void StreamableHttpServerTransport::HandleNonRequestMessage(RequestContext& ctx, const JSONRPCMessage& message)
{
    MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Handle non request message for session " + ctx.sessionId);
    HttpResponse response = CreateJsonResponse(std::nullopt, Http::HTTP_STATUS_ACCEPTED, {}, ctx);
    ctx.httpSendFunc(response, ctx);
    if (callback_ != nullptr) {
        callback_->OnMessageReceived(message, ctx);
    }
}

void StreamableHttpServerTransport::HandlePostRequest(RequestContext& ctx, const HttpRequest& request)
{
    if (callback_ == nullptr || ctx.httpSendFunc == nullptr) {
        throw std::runtime_error("Callbacks not set");
    }
    if (!ValidatePostRequestHeaders(ctx, request)) {
        return;
    }
    nlohmann::json messageJson{};
    if (!ParseJsonBody(ctx, request, messageJson)) {
        return;
    }

    bool isInitializationRequest = messageJson.contains("method") && messageJson["method"] == "initialize";
    MCP_LOG(MCP_LOG_LEVEL_DEBUG, std::string("isInitializationRequest ") +
            (isInitializationRequest ? "true" : "false"));

    if (!ValidateSessionId(ctx, request, isInitializationRequest)) {
        return;
    }
    if (!ValidateProtocolVersion(ctx, request)) {
        return;
    }

    std::string deserializeMethod{};
    // For responses, recover the original request method via request-id.
    const auto pendingMethod = TryTakePendingMethodForJsonRpcResponse(messageJson, pendingMethods_);
    if (pendingMethod.has_value()) {
        deserializeMethod = *pendingMethod;
    }

    JSONRPCMessage message = DeserializeJSONRPCMessage(request.body, deserializeMethod);
    if (std::holds_alternative<JSONRPCError>(message) && messageJson.contains("method")) {
        const int statusCode =
            isJsonResponseEnabled_ ? Http::HTTP_STATUS_OK : Http::HTTP_STATUS_BAD_REQUEST;
        HttpResponse response =
            CreateJsonResponse(std::optional<JSONRPCMessage>(std::move(message)), statusCode, {}, ctx);
        ctx.httpSendFunc(response, ctx);
        return;
    }

    bool isRequest = std::holds_alternative<JSONRPCRequest>(message);
    if (!isRequest) {
        HandleNonRequestMessage(ctx, message);
        return;
    }

    if (!isJsonResponseEnabled_) {
        HttpResponse response{};
        response.statusCode = Http::HTTP_STATUS_OK;
        response.type = HttpSendType::HTTPRESPONSESTART;
        response.headers[Http::CACHE_CONTROL_HEADER] = Http::CACHE_CONTROL_NO_CACHE_NO_TRANSFORM;
        response.headers[Http::CONNECTION_HEADER] = Http::CONNECTION_KEEP_ALIVE;
        response.headers[Http::CONTENT_TYPE_HEADER] = Http::CONTENT_TYPE_SSE;
        response.headers[Http::TRANSFER_ENCODING_HEADER] = Http::TRANSFER_ENCODING_CHUNKED;
        response.headers[Http::X_ACCEL_BUFFERING_HEADER] = "no";

        // Stateless transport instances are constructed with an empty session id.
        if (!mcpSessionId_.empty()) {
            response.headers[Http::MCP_SESSION_ID_HEADER] = mcpSessionId_;
        }

        response.body.clear();
        ctx.httpSendFunc(response, ctx);
    }

    if (callback_ != nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_DEBUG, "handle request " + ctx.method);
        callback_->OnMessageReceived(message, ctx);
    }
}

void StreamableHttpServerTransport::HandleGetRequest(RequestContext& ctx, const HttpRequest& request)
{
    if (ctx.httpSendFunc == nullptr) {
        throw std::runtime_error("HTTP callback not set");
    }

    // Check Accept header
    auto acceptIt = request.headers.find("accept");
    std::string acceptHeader{};
    if (acceptIt != request.headers.end()) {
        acceptHeader = acceptIt->second;
    }

    bool hasSse = acceptHeader.find(Http::CONTENT_TYPE_SSE) != std::string::npos;
    if (!hasSse) {
        HttpResponse response =
            CreateErrorResponse("Not Acceptable: Client must accept text/event-stream",
                                Http::HTTP_STATUS_NOT_ACCEPTABLE, static_cast<int>(JsonRpcErrorCode::INVALID_REQUEST),
                                {});
        ctx.httpSendFunc(response, ctx);
        return;
    }

    // Validate session ID
    if (!mcpSessionId_.empty()) {
        std::string requestSessionId = GetSessionId(request);
        if (requestSessionId.empty() || requestSessionId != mcpSessionId_) {
            MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Invalid session ID: " + requestSessionId + ", expected: " + mcpSessionId_);
            HttpResponse response = CreateErrorResponse("Bad Request: Invalid session ID",
                Http::HTTP_STATUS_BAD_REQUEST,
                static_cast<int>(JsonRpcErrorCode::INVALID_REQUEST));
            ctx.httpSendFunc(response, ctx);
            return;
        }
    }

    // Validate protocol version
    if (!ValidateProtocolVersion(ctx, request)) {
        return;
    }

    // Check if we already have an active GET stream
    if (getStreamRequestContext_.has_value()) {
        HttpResponse response =
            CreateErrorResponse("Conflict: Only one SSE stream is allowed per session", Http::HTTP_STATUS_CONFLICT,
                                static_cast<int>(JsonRpcErrorCode::INVALID_REQUEST));
        ctx.httpSendFunc(response, ctx);
        return;
    }

    // Store the GET stream request context
    ctx.isGetStream = true;
    getStreamRequestContext_ = ctx;

    // Create SSE response
    HttpResponse response{};
    response.statusCode = Http::HTTP_STATUS_OK;
    response.type = HttpSendType::HTTPRESPONSESTART;
    response.headers[Http::CONTENT_TYPE_HEADER] = Http::CONTENT_TYPE_SSE;
    response.headers[Http::CACHE_CONTROL_HEADER] = Http::CACHE_CONTROL_NO_CACHE_NO_TRANSFORM;
    response.headers[Http::CONNECTION_HEADER] = Http::CONNECTION_KEEP_ALIVE;
    response.headers[Http::TRANSFER_ENCODING_HEADER] = Http::TRANSFER_ENCODING_CHUNKED;

    // Stateless transport instances are constructed with an empty session id.
    if (!mcpSessionId_.empty()) {
        response.headers[Http::MCP_SESSION_ID_HEADER] = mcpSessionId_;
    }

    ctx.httpSendFunc(response, ctx);
}

void StreamableHttpServerTransport::HandleDeleteRequest(RequestContext& ctx, const HttpRequest& request)
{
    if (ctx.httpSendFunc == nullptr) {
        throw std::runtime_error("HTTP callback not set");
    }

    // Validate session ID
    if (mcpSessionId_.empty()) {
        HttpResponse response = CreateErrorResponse("Method Not Allowed: Session termination not supported",
                                                    Http::HTTP_STATUS_METHOD_NOT_ALLOWED,
                                                    static_cast<int>(JsonRpcErrorCode::INVALID_REQUEST));
        ctx.httpSendFunc(response, ctx);
        return;
    }

    std::string requestSessionId = GetSessionId(request);
    if (requestSessionId.empty() || requestSessionId != mcpSessionId_) {
        HttpResponse response = CreateErrorResponse("Bad Request: Invalid session ID", Http::HTTP_STATUS_BAD_REQUEST,
                                                    static_cast<int>(JsonRpcErrorCode::INVALID_REQUEST));
        ctx.httpSendFunc(response, ctx);
        return;
    }

    // Terminate the session
    Terminate();

    // Send success response
    HttpResponse response = CreateJsonResponse(std::nullopt, Http::HTTP_STATUS_OK, {}, ctx);
    ctx.httpSendFunc(response, ctx);
}

void StreamableHttpServerTransport::HandleUnsupportedRequest(RequestContext& ctx, const HttpRequest& request)
{
    if (ctx.httpSendFunc == nullptr) {
        throw std::runtime_error("HTTP callback not set");
    }

    std::unordered_map<std::string, std::string> headers{};
    headers["Allow"] = "GET, POST, DELETE";

    HttpResponse response = CreateErrorResponse("Method Not Allowed", Http::HTTP_STATUS_METHOD_NOT_ALLOWED,
                                                static_cast<int>(JsonRpcErrorCode::INVALID_REQUEST), headers);

    ctx.httpSendFunc(response, ctx);
}

void StreamableHttpServerTransport::SendMessage(const JSONRPCMessage& message, RequestContext& ctx)
{
    if (std::holds_alternative<JSONRPCRequest>(message)) {
        const auto& req = std::get<JSONRPCRequest>(message);
        pendingMethods_[req.id_] = req.method_;
    }

    bool isGetStream = ctx.isGetStream || ctx.connectionId == 0;
    if (isGetStream) {
        if (!getStreamRequestContext_.has_value()) {
            throw std::runtime_error("SSE stream request context not set");
        }
        ctx = getStreamRequestContext_.value();
    }

    if (ctx.httpSendFunc == nullptr) {
        throw std::runtime_error("HTTP callback not set");
    }

    if (isJsonResponseEnabled_ && !isGetStream) {
        // JSON response mode: send message as JSON response
        // Only send response messages (not notifications/requests)
        bool isResponseOrError = std::holds_alternative<JSONRPCResponse>(message) ||
                                 std::holds_alternative<JSONRPCError>(message);

        // for getstream or response, send message
        if (isResponseOrError) {
            HttpResponse response{};
            response.statusCode = Http::HTTP_STATUS_OK;
            response.headers[Http::CONTENT_TYPE_HEADER] = Http::CONTENT_TYPE_JSON;
            // Stateless transport instances are constructed with an empty session id.
            if (!mcpSessionId_.empty()) {
                response.headers[Http::MCP_SESSION_ID_HEADER] = mcpSessionId_;
            }
            response.body = SerializeJSONRPCMessage(message, ctx.method);
            ctx.httpSendFunc(response, ctx);
        } else {
            MCP_LOG(MCP_LOG_LEVEL_DEBUG, "not getstream and not response or error, not send message");
        }
        // For notifications and requests in JSON mode, don't send HTTP response
    } else {
        // SSE response mode: send messages as SSE events
        EventMessage eventMessage{message, ""}; // Empty event ID
        std::string eventData = CreateEventData(eventMessage, ctx);

        bool isResponse = std::holds_alternative<JSONRPCResponse>(message);
        bool isError = std::holds_alternative<JSONRPCError>(message);

        HttpResponse response{};
        response.statusCode = Http::HTTP_STATUS_OK;
        response.type = HttpSendType::HTTPRESPONSEBODY;
        response.headers.clear();
        response.body = eventData;

        // Send to the specified connection
        ctx.httpSendFunc(response, ctx);

        if (isResponse || isError) {
            // for response, finish stream
            HttpResponse finishResponse{};
            finishResponse.type = HttpSendType::HTTPRESPONSEBODY;
            finishResponse.headers.clear();
            finishResponse.body = "";
            ctx.httpSendFunc(finishResponse, ctx);
        }
    }
}

void StreamableHttpServerTransport::Listen()
{
    // Initialize transport
    MCP_LOG(MCP_LOG_LEVEL_INFO, "Transport connected");
}

void StreamableHttpServerTransport::Terminate()
{
    MCP_LOG(MCP_LOG_LEVEL_INFO, "Terminating session: " + mcpSessionId_);
    isTerminated_ = true;
    getStreamRequestContext_.reset();
    mcpSessionId_.clear();
}

} // namespace Mcp
