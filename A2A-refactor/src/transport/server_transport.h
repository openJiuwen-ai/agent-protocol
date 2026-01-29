/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_ABSTRACT_SERVER_TRANSPORT
#define A2A_ABSTRACT_SERVER_TRANSPORT

#include <httplib.h>
#include <string>

#include "transport_emitter.h"

namespace A2A::Transport {

using ServerTransportRpcHandler = std::function<void(const std::string& req_body, std::string& resp_body,
    TransportEmitter& emitter)>;
using ServerTransportCardHandler = std::function<void(const std::string& req_body, std::string& resp_body)>;

class ServerTransport {
public:
    /**
     * @brief constructor
     */
    ServerTransport() = default;

    /**
     * @brief destructor
     */
    virtual ~ServerTransport() = default;

    /**
     * @brief start to listen
     *
     * @return 0 on succeed
     */
    virtual int Start() = 0;

    /**
     * @brief stop listen
     */
    virtual void Stop() = 0;

    /**
     * @brief send data to specific url
     *
     * @param[in] url url to send data to
     * @param[in] data payload
     * @return response data
     */
    virtual int SendData(const std::string& url, const std::string& data) const = 0;

    /**
     * @brief set RPC handler
     * handler will be called when server receive data (both streaming and non-streaming)
     *
     * @param[in] handler event handler
     */
    virtual void SetRpcHandler(ServerTransportRpcHandler handler) = 0;

    virtual void SetCardHandler(ServerTransportCardHandler handler) = 0;
};

} // namespace A2A::Transport

#endif
