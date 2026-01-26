/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_HTTP_SERVER_MANAGER_H_
#define A2A_HTTP_SERVER_MANAGER_H_

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "http_server.h"

namespace A2A::Server {

/**
 * @brief Configuration for HttpServerManager.
 */
struct HttpServerManagerConfig {
    std::string host; // Host address.
    uint16_t port{0}; // Port number.
    size_t ioThreadNum{0}; // Number of IO threads.
    TlsConfig tlsConfig_{};
    RouteMap routeMap;
};

/**
 * @brief HTTP server side entry used to interact with ServerManager.
 *
 * Responsibilities (high level):
 * - Start HTTP server(s) and register route
 * - On request, invoke OnRecvCallback(request, ctx) in the IO thread.
 * - Provide Send(response, ctx) for ServerManager to send responses via ctx.send.
 *
 * Note: All method implementations are intentionally left empty for now.
 */
class HttpServerManager {
public:
    explicit HttpServerManager(const HttpServerManagerConfig& config);
    ~HttpServerManager();

    /**
     * @brief Start the HTTP server(s) and enter event loop(s).
     * @return OK on successful start, otherwise an error code.
     */
    void Start();

    /**
     * @brief Stop the HTTP server(s) and release resources.
     */
    void Stop();

private:
    HttpServerManagerConfig config_; // Configuration.

    std::vector<std::unique_ptr<HttpServer>> servers_;
    std::atomic<bool> running_{false};
};

} // namespace A2A::Server

#endif // A2A_HTTP_SERVER_MANAGER_H_