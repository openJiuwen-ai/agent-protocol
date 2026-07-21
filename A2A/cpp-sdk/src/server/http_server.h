/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_HTTP_SERVER_INCLUDE_H_
#define A2A_HTTP_SERVER_INCLUDE_H_

#include <openssl/ssl.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "event/event_system.h"
#include "net/tcp_listener.h"
#include "net/tcp_socket.h"
#include "shared/http_common.h"
#include "shared/message_queue/mpsc_notify_queue.h"

namespace A2A::Server {

using RouteMap = std::unordered_map<std::string, Http::HttpHandler>;

struct TlsConfig {
    bool enabled{false};

    std::string caFile;
    std::string certFile;
    std::string keyFile;

    std::string serverName;
    bool verifyPeer{true};
};

class HttpServer {
public:
    HttpServer();
    explicit HttpServer(const std::string& host, uint16_t port, const TlsConfig& tlsConfig, RouteMap& routes,
        size_t ioThreadIndex = 0);

    ~HttpServer();

    void Run();

    void Stop();

    void SendResponseAsync(const Http::HttpResponse& response, const Http::HttpRequestContext& ctx);

private:
    bool SendResponse(int connectionFd, const Http::HttpResponse& response);
    struct ConnectionContext {
        std::string requestBuffer;
        Http::HttpRequest currentRequest;
        std::chrono::steady_clock::time_point lastActivity;
        TcpSocketPtr connection;

        // TLS-related state; only used when tlsConfig_.enabled is true.
        SSL* ssl{nullptr};
        BIO* rbio{nullptr};
        BIO* wbio{nullptr};
        bool handshaked{false};
        bool sseChunked{false};
    };

    void HandleNewConnection(const TcpSocketPtr& connection);
    void HandleRead(const TcpSocketPtr& connection);
    void HandleClose(const SocketPtr& socket);
    void HandleError(const SocketPtr& socket, int errorCode, const std::string& message);

    void HandleRequest(int fileDescriptor, ConnectionContext& context);
    std::string BuildHttpResponse(const Http::HttpResponse& response) const;
    std::string BuildchunkedResponse(const Http::HttpResponse& response, bool& chunkedEnabled) const;
    bool SendRawResponse(int fileDescriptor, const std::string& response);
    void CleanupConnection(int fileDescriptor);

    int ParseRequest(const std::string& buffer, Http::HttpRequest& outRequest, std::size_t& consumedBytes) const;

    void OnRead(int connectionFd, const std::string& data);

    void InitializeSslContext();

    EventSystem eventSystem_;
    std::unique_ptr<MPSCNotifyQueue<std::function<void()>>> taskQueue_;
    std::unique_ptr<TcpListener> listener_;
    std::thread eventThread_;

    std::unordered_map<std::string, Http::HttpHandler> routes_;
    Http::HttpHandler onRecv_{nullptr};
    std::unordered_map<int, ConnectionContext> connections_;
    std::atomic<bool> running_{false};

    std::string host_;
    uint16_t port_{0};
    TlsConfig tlsConfig_;
    SSL_CTX* sslContext_{nullptr};
    size_t ioThreadIndex_{0};  // Index for naming IO threads
};

} // namespace A2A::Http

#endif // A2A_HTTP_SERVER_INCLUDE_H_