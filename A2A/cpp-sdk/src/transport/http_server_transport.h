/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_HTTP_SERVER_TRANSPORT
#define A2A_HTTP_SERVER_TRANSPORT

#include <map>
#include <optional>
#include <string>
#include <thread>

#include "abstract_server_transport.h"
#include "utils/types.h"

namespace a2a::transport {

class HttpServerTransport : public AbstractServerTransport {
public:
    /**
     * @brief constructor
     */
    explicit HttpServerTransport(std::shared_ptr<AgentCard> agentCard);

    ~HttpServerTransport() override;

    // Configure static headers and auth
    HttpServerTransport& SetHeader(std::string key, std::string value);
    HttpServerTransport& SetBearerToken(std::string token);
    HttpServerTransport& SetTimeoutMs(long connectMs, long readMs);

    /**
     * @brief start to listen
     *
     * @param[in] ipAddr ip address
     * @param[in] port port
     * @return 0 on succeed
     */
    virtual int Start(const std::string& ipAddr, uint16_t port) override;

    /**
     * @brief stop listen
     */
    virtual void Stop() override;

    /**
     * @brief send data to specific url
     *
     * @param[in] url url to send data to
     * @param[in] data payload
     * @return response data
     */
    virtual int SendData(const std::string& url, const std::string& data) const override;

    /**
     * @brief set event handler
     * handler will be called when server receive non-streaming data
     *
     * @param[in] handler event handler
     */
    virtual void SetEventHandler(ServerTransportEventHandler handler) override;

    /**
     * @brief set stream event handler
     * handler will be called when server receive streaming data
     *
     * @param[in] handler event handler
     */
    virtual void SetStreamEventHandler(ServerTransportStreamEventHandler handler) override;

private:
    std::map<std::string, std::string> headers_;
    std::optional<std::string> bearerToken_;
    long connectTimeoutMs_ = 10000; // default 10s connect
    long readTimeoutMs_ = 60000; // default 60s total

    httplib::Server server_;
    std::string ipAddr_;
    uint16_t port_;
    std::thread listen_thread_;

    ServerTransportEventHandler handler_;
    ServerTransportStreamEventHandler streamHandler_;
    std::shared_ptr<AgentCard> agentCard_;
};

} // namespace a2a::transport

#endif
