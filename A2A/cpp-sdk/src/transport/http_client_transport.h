/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_HTTP_CLIENT_TRANSPORT
#define A2A_HTTP_CLIENT_TRANSPORT

#include <map>
#include <optional>
#include <string>

#include "abstract_client_transport.h"

namespace a2a::transport {

class HttpClientTransport : public AbstractClientTransport {
public:
    /**
     * @brief constructor
     */
    explicit HttpClientTransport(std::string url);

    // Configure static headers and auth
    HttpClientTransport& SetHeader(std::string key, std::string value);
    HttpClientTransport& SetBearerToken(std::string token);
    HttpClientTransport& SetTimeoutMs(long connectMs, long readMs);

    /**
     * @brief send data
     *
     * @param[in] data payload
     * @return response data
     */
    virtual std::string SendData(const std::string& data) const override;

    /**
     * @brief send data in stream mode
     *
     * @param[in] data payload
     * @param[in] onEvent event handler called when receive response data
     */
    virtual void SendDataStreaming(const std::string& data, const ClientTransportEventHandler& onEvent) const override;

private:
    std::string url_;
    std::map<std::string, std::string> headers_;
    std::optional<std::string> bearerToken_;
    long connectTimeoutMs_ = 10000; // default 10s connect
    long readTimeoutMs_ = 60000; // default 60s total
};

} // namespace a2a::transport

#endif
