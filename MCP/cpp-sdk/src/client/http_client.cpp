/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "client/http_client.h"

#include <sstream>

#include "mcp_log.h"
#include "shared/http_common.h"

namespace Mcp::Http {

namespace {

std::string BuildRawHttpRequest(const HttpRequest& request, const std::string& host)
{
    std::ostringstream output;

    std::string path = request.url.empty() ? "/" : request.url;
    output << request.method << " " << path << " HTTP/1.1\r\n";

    // Host header
    if (request.headers.find(HOST_HEADER) == request.headers.end()) {
        output << HOST_HEADER << ": " << host << "\r\n";
    }

    // Content-Length
    if (!request.body.empty() && request.headers.find(CONTENT_LENGTH_HEADER) == request.headers.end()) {
        output << CONTENT_LENGTH_HEADER << ": " << request.body.size() << "\r\n";
    }

    // Other headers
    for (const auto& header : request.headers) {
        output << header.first << ": " << header.second << "\r\n";
    }

    // Default Connection: close for simple synchronous client
    output << CONNECTION_HEADER << ": " << CONNECTION_CLOSE << "\r\n";

    output << "\r\n";

    if (!request.body.empty()) {
        output << request.body;
    }

    return output.str();
}

bool ParseHttpResponse(const std::string& buffer, HttpResponse& outResponse, std::size_t& consumedBytes)
{
    consumedBytes = 0;

    std::size_t headerEnd = buffer.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        return false;
    }

    std::string headerPart = buffer.substr(0, headerEnd);
    std::istringstream headerStream(headerPart);

    std::string statusLine;
    if (!std::getline(headerStream, statusLine)) {
        return false;
    }

    if (!statusLine.empty() && statusLine.back() == '\r') {
        statusLine.pop_back();
    }

    std::istringstream lineStream(statusLine);
    std::string version;
    lineStream >> version >> outResponse.statusCode;
    std::getline(lineStream, outResponse.statusText);
    if (!outResponse.statusText.empty() && outResponse.statusText.front() == ' ') {
        outResponse.statusText.erase(0, 1);
    }

    // Use shared helper to parse headers and body.
    return ParseHeadersAndBody(buffer, headerEnd, outResponse.headers, outResponse.body, consumedBytes);
}

} // namespace

HttpClient::HttpClient() = default;

std::optional<HttpResponse> HttpClient::SendRequest(const std::string& host, uint16_t port, const HttpRequest& request,
                                                    int timeoutMs)
{
    Mcp::EventSystem eventSystem;
    if (!eventSystem.Init()) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "[http_client] EventSystem init failed");
        return std::nullopt;
    }

    Mcp::Net::TcpSocketOptions options;
    options.tcpNoDelay = true;

    auto connection = Mcp::Net::TcpSocket::Connect(eventSystem, host, port, timeoutMs, options);
    if (!connection) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "[http_client] connect() returned null");
        return std::nullopt;
    }

    std::string receiveBuffer;
    bool done = false;
    bool error = false;
    HttpResponse response;

    connection->OnError([&](const Mcp::Net::SocketPtr& socket, int errorCode, const std::string& message) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "[http_client] error fd=%d err=%d msg=%s", (socket ? socket->Fd() : -1), errorCode,
                message.c_str());
        error = true;
        done = true;
        eventSystem.Stop();
    });

    connection->OnClose([&](const Mcp::Net::SocketPtr& /* socket */) {
        if (!done) {
            // Connection closed before a complete response was parsed
            done = true;
        }
        eventSystem.Stop();
    });

    connection->OnRead([&](const Mcp::Net::SocketPtr& /* socket */) {
        Mcp::Net::Buffer& buffer = connection->InputBuffer();
        if (buffer.ReadableBytes() == 0) {
            return;
        }
        receiveBuffer += buffer.RetrieveAllAsString();

        std::size_t consumed = 0;
        if (ParseHttpResponse(receiveBuffer, response, consumed)) {
            done = true;
            eventSystem.Stop();
            connection->Close();
        }
    });

    std::string rawRequest = BuildRawHttpRequest(request, host);
    if (!connection->Send(rawRequest)) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "[http_client] send failed");
        return std::nullopt;
    }

    int timerId = -1;
    if (timeoutMs > 0) {
        timerId = eventSystem.AddTimer(
            timeoutMs,
            [&](int /* fileDescriptor */, short /* events */, void* /* argument */) {
                if (!done) {
                    MCP_LOG(MCP_LOG_LEVEL_ERROR, "[http_client] request timeout after %d ms", timeoutMs);
                    error = true;
                    done = true;
                    if (connection) {
                        connection->Close();
                    }
                    eventSystem.Stop();
                }
            },
            nullptr, false);
    }

    eventSystem.Start(false);

    if (timerId > 0) {
        eventSystem.RemoveEvent(timerId);
    }

    if (error || !done) {
        return std::nullopt;
    }

    return response;
}

} // namespace Mcp::Http
