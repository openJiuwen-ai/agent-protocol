/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_HTTP_SERVER_INCLUDE_H_
#define A2A_HTTP_SERVER_INCLUDE_H_

#include <openssl/ssl.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "event/event_system.h"
#include "net/tcp_listener.h"
#include "net/tcp_socket.h"
#include "shared/common_types.h"
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

/**
 * @brief Connection reclamation settings.
 *
 * A periodic sweep closes connections that have made no progress for too long, so that idle
 * or hung clients cannot leak file descriptors. Which budget applies depends on whether a
 * request is in flight on the connection:
 * - timeoutSeconds: no request in flight. The server is merely waiting for the client, so
 *   silence means the client is idle or gone. 0 disables this budget.
 * - requestTimeoutSeconds: a handler is still producing the response. Silence on the wire is
 *   expected here (an agent may run for minutes before the first byte), so the budget is much
 *   larger and is measured from the last progress, i.e. the request being read or the last
 *   chunk written. 0 disables this budget, which lets a stuck handler pin the connection.
 */
struct ConnectionTimeoutConfig {
    unsigned int timeoutSeconds{DEFAULT_CONNECTION_TIMEOUT_SECONDS};
    unsigned int sweepIntervalSeconds{DEFAULT_CONNECTION_SWEEP_INTERVAL_SECONDS};
    unsigned int requestTimeoutSeconds{DEFAULT_REQUEST_TIMEOUT_SECONDS};
};

class HttpServer {
public:
    HttpServer();
    explicit HttpServer(const std::string& host, uint16_t port, const TlsConfig& tlsConfig, RouteMap& routes,
        size_t ioThreadIndex = 0, const ConnectionTimeoutConfig& timeoutConfig = {});

    ~HttpServer();

    void Run();

    void Stop();

    void SendResponseAsync(const Http::HttpResponse& response, const Http::HttpRequestContext& ctx);

private:
    bool SendResponse(int connectionFd, const Http::HttpResponse& response);
    bool SendResponse(int connectionFd, uint64_t connectionSequence, const Http::HttpResponse& response);
    struct ConnectionContext {
        std::string requestBuffer;
        Http::HttpRequest currentRequest;
        std::chrono::steady_clock::time_point lastActivity;
        TcpSocketPtr connection;

        // Identifies this connection across fd reuse: the OS may hand the same fd number to a
        // new connection once this one is closed, so responses carry the sequence they were
        // produced for and are dropped when it no longer matches.
        uint64_t sequence{0};
        // True from the moment a request has been dispatched to a handler until the response is
        // complete. While set, the connection is held to requestTimeoutSeconds instead of the
        // idle timeout, because a handler legitimately produces no traffic while working.
        bool inFlight{false};

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
    // Sends a response the server produced itself (404 / 500 / 400) rather than a handler.
    // Routes around repeating the status line when a chunked response is already in progress.
    void SendGeneratedResponse(int fileDescriptor, const Http::HttpResponse& response);
    std::string BuildHttpResponse(const Http::HttpResponse& response) const;
    std::string BuildchunkedResponse(const Http::HttpResponse& response, bool& chunkedEnabled) const;
    bool SendRawResponse(int fileDescriptor, const std::string& response);
    void CleanupConnection(int fileDescriptor);

    int ParseRequest(const std::string& buffer, Http::HttpRequest& outRequest, std::size_t& consumedBytes) const;

    void OnRead(int connectionFd, const std::string& data);

    void InitializeSslContext();

    // Connection reclamation. RegisterIdleConnectionSweep arms a periodic timer on the event
    // loop; CloseIdleConnections closes every connection whose budget (see IsConnectionExpired)
    // has run out. Both run on the IO event thread, which is the only thread that touches
    // connections_ while the server is running; Stop() joins that thread before tearing
    // connections_ down.
    void RegisterIdleConnectionSweep();
    void CloseIdleConnections(std::chrono::steady_clock::time_point now);
    bool IsConnectionExpired(const ConnectionContext& context, std::chrono::steady_clock::time_point now) const;
    static long ResolveSweepIntervalMs(const ConnectionTimeoutConfig& config);
    // Whether a written response leaves nothing more to send, so the request is no longer in
    // flight: a one-shot full response, or the empty body that ends a stream.
    static bool CompletesResponse(const Http::HttpResponse& response);

    EventSystem eventSystem_;
    std::unique_ptr<MPSCNotifyQueue<std::function<void()>>> taskQueue_;
    std::unique_ptr<TcpListener> listener_;
    std::thread eventThread_;

    std::unordered_map<std::string, Http::HttpHandler> routes_;
    Http::HttpHandler onRecv_{nullptr};
    std::unordered_map<int, ConnectionContext> connections_;
    std::atomic<bool> running_{false};

    ConnectionTimeoutConfig timeoutConfig_;
    int sweepTimerId_{-1}; // Event id of the periodic connection sweep; -1 when not armed.
    uint64_t nextConnectionSequence_{1}; // 0 is reserved for "no connection", see ConnectionContext.

    std::string host_;
    uint16_t port_{0};
    TlsConfig tlsConfig_;
    SSL_CTX* sslContext_{nullptr};
    size_t ioThreadIndex_{0};  // Index for naming IO threads
};

} // namespace A2A::Http

#endif // A2A_HTTP_SERVER_INCLUDE_H_