/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#ifndef MCP_HTTP_SERVER_INCLUDE_H_
#define MCP_HTTP_SERVER_INCLUDE_H_

#include <openssl/ssl.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "event/event_system.h"
#include "mcp_type.h"
#include "net/tcp_listener.h"
#include "net/tcp_socket.h"
#include "shared/common_type.h"
#include "shared/http_common.h"
#include "shared/message_queue/mpsc_notify_queue.h"

namespace Mcp::Http {

using RouteMap = std::unordered_map<std::string, HttpHandler>;

class HttpServer {
public:
    HttpServer();
    explicit HttpServer(const std::string& host, uint16_t port, const TlsConfig& tlsConfig, RouteMap& routes,
        size_t ioThreadIndex = 0);

    void Run();

    void Stop();

    void SendResponseAsync(const HttpResponse& response, const RequestContext& ctx);

private:
    bool SendResponse(int connectionFd, const HttpResponse& response);
    struct ConnectionContext {
        std::string requestBuffer;
        HttpRequest currentRequest;
        std::chrono::steady_clock::time_point lastActivity;
        Mcp::Net::TcpSocketPtr connection;

        // TLS-related state; only used when tlsConfig_.enabled is true.
        SSL* ssl{nullptr};
        BIO* rbio{nullptr};
        BIO* wbio{nullptr};
        bool handshaked{false};
        bool sseChunked{false};
    };

    void HandleNewConnection(const Mcp::Net::TcpSocketPtr& connection);
    void HandleRead(const Mcp::Net::TcpSocketPtr& connection);
    void HandleClose(const Mcp::Net::SocketPtr& socket);
    void HandleError(const Mcp::Net::SocketPtr& socket, int errorCode, const std::string& message);

    void HandleRequest(int fileDescriptor, ConnectionContext& context);
    std::string BuildHttpResponse(const HttpResponse& response) const;
    std::string BuildchunkedResponse(const HttpResponse& response, bool& chunkedEnabled) const;
    bool SendRawResponse(int fileDescriptor, const std::string& response);
    void CleanupConnection(int fileDescriptor);

    int ParseRequest(const std::string& buffer, HttpRequest& outRequest, std::size_t& consumedBytes);

    void OnRead(int connectionFd, const std::string& data);

    void InitializeSslContext();

    Mcp::EventSystem eventSystem_;
    std::unique_ptr<Mcp::MPSCNotifyQueue<std::function<void()>>> taskQueue_;
    std::unique_ptr<Mcp::Net::TcpListener> listener_;
    std::thread eventThread_;

    std::unordered_map<std::string, HttpHandler> routes_;
    HttpHandler onRecv_{nullptr};
    std::unordered_map<int, ConnectionContext> connections_;
    std::atomic<bool> running_{false};

    std::string host_;
    uint16_t port_{0};
    TlsConfig tlsConfig_;
    SSL_CTX* sslContext_{nullptr};
    size_t ioThreadIndex_{0};  // Index for naming IO threads
};

} // namespace Mcp::Http

#endif // MCP_HTTP_SERVER_INCLUDE_H_
