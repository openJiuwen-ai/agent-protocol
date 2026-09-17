/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_HTTP_SERVER_TRANSPORT
#define A2A_HTTP_SERVER_TRANSPORT

#include <map>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <memory>
#include <mutex>

#include "http_common.h"
#include "http_server.h"
#include "http_server_manager.h"
#include "server/http_server_builder.h"
#include "server_transport.h"

namespace A2A::Transport {

constexpr const char* AGENT_CARD_ENDPOINT = "/.well-known/agent-card.json";

class HttpServerTransport : public ServerTransport {
public:
    /**
    * @brief constructor
    */
    explicit HttpServerTransport(const Server::HttpConfig& config)
        : config_(config), jsonrpc_endpoint_(config.endpoint) {}

    ~HttpServerTransport() override;

    // Configure static headers and auth
    void SetHeader(const std::string& key, const std::string& value);
    void SetBearerToken(const std::string& token);
    void SetTimeoutMs(long connectMs, long readMs);

    /**
    * @brief start to listen
    *
    * @return 0 on succeed
    */
    int Start() override;

    /**
    * @brief stop listen
    */
    void Stop() override;

    /**
    * @brief send data to specific url
    *
    * @param[in] url url to send data to
    * @param[in] data payload
    * @return response data
    */
    int SendData(const std::string& url, const std::string& data) const override;

    /**
    * @brief set RPC handler
    * handler will be called when server receive data (both streaming and non-streaming)
    *
    * @param[in] handler event handler
    */
    void SetRpcHandler(ServerTransportRpcHandler handler) override;

    void SetCardHandler(ServerTransportCardHandler handler) override;

    static void SetCommonHeaders(Http::HttpResponse& response);

private:
    std::map<std::string, std::string> headers_;
    std::optional<std::string> bearerToken_;
    long connectTimeoutMs_ = 10000; // default 10000ms connect
    long readTimeoutMs_ = 60000; // default 60000ms total

    std::unique_ptr<Server::HttpServerManager> httpServerMgr_{};
    Server::HttpConfig config_;
    std::thread listenThread_;
    std::vector<std::thread> workerThreads_;
    mutable std::mutex workerThreadsMutex_;

    ServerTransportRpcHandler handler_;
    ServerTransportCardHandler handlerCard_;

    std::string jsonrpc_endpoint_;

    // Private helper methods for route handlers
    void SetupJsonRpcEndpoint(Server::RouteMap& routeMap);
    void SetupCardEndpoint(Server::RouteMap& routeMap);
    void HandleJsonRpcRequest(const Http::HttpRequest& req, const Http::HttpRequestContext& ctx);
    void HandleStreamingRequest(const std::string& reqBody, const Http::HttpRequestContext& ctx,
        const std::map<std::string, std::string>& headersCopy);
    void HandleNonStreamingRequest(const std::string& reqBody, const Http::HttpRequestContext& ctx,
        const std::map<std::string, std::string>& headersCopy);
    bool IsStreamingMethod(const std::string& reqBody) const;
};

} // namespace A2A::Transport

#endif