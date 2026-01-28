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

#include "events/event_system.h"
#include "net/tcp_listener.h"
#include "net/tcp_socket.h"
#include "http_common.h"
#include "lock_free_queue.h"

namespace A2A::Server {

using RouteMap = std::unordered_map<std::string,
    std::function<void(const HttpRequest&, const HttpRequestContext&)>>;

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
    explicit HttpServer(const std::string& host, uint16_t port, const TlsConfig& tlsConfig, RouteMap& routes);

    ~HttpServer();

    void Run();

    void Stop();

    void SendResponseAsync(const HttpResponse& response, const HttpRequestContext& ctx);

private:
    bool SendResponse(int connectionFd, const HttpResponse& response);
    struct ConnectionContext {
        std::string requestBuffer;
        HttpRequest currentRequest;
        std::chrono::steady_clock::time_point lastActivity;
        TcpSocketPtr connection;

        // TLS-related state; only used when tlsConfig_.enabled is true.
        SSL* ssl{nullptr};
        BIO* rbio{nullptr};
        BIO* wbio{nullptr};
        bool handshaked{false};
    };

    void HandleNewConnection(const TcpSocketPtr& connection);
    void HandleRead(const TcpSocketPtr& connection);
    void HandleClose(const SocketPtr& socket);
    void HandleError(const SocketPtr& socket, int errorCode, const std::string& message);

    void HandleRequest(int fileDescriptor, ConnectionContext& context);
    std::string BuildHttpResponse(const HttpResponse& response) const;
    bool SendRawResponse(int fileDescriptor, const std::string& response);
    void CleanupConnection(int fileDescriptor);

    int ParseRequest(const std::string& buffer, HttpRequest& outRequest, std::size_t& consumedBytes);

    void OnRead(int connectionFd, const std::string& data);

    void InitializeSslContext();

    EventSystem eventSystem_;
    std::unique_ptr<MPSCQueue<std::function<void()>>> taskQueue_;  // Changed from MPSCNotifyQueue to MPSCQueue
    std::unique_ptr<TcpListener> listener_;
    std::thread eventThread_;

    std::unordered_map<std::string, std::function<void(const HttpRequest&,
        const HttpRequestContext&)>> routes_;
    std::function<void(const HttpRequest&, const HttpRequestContext&)> onRecv_{nullptr};
    std::unordered_map<int, ConnectionContext> connections_;
    std::atomic<bool> running_{false};

    std::string host_;
    uint16_t port_{0};
    TlsConfig tlsConfig_;
    SSL_CTX* sslContext_{nullptr};
};

} // namespace A2A::Http

#endif // A2A_HTTP_SERVER_INCLUDE_H_