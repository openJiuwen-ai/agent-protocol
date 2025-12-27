/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_ABSTRACT_CLIENT_TRANSPORT
#define A2A_ABSTRACT_CLIENT_TRANSPORT

#include <functional>
#include <map>
#include <string>

namespace a2a::transport {

using ClientTransportEventHandler = std::function<void(const std::string&)>;

class AbstractClientTransport {
public:
    /**
     * @brief destructor
     */
    virtual ~AbstractClientTransport() = default;

    /**
     * @brief send data
     *
     * @param[in] data payload
     * @return response data
     */
    virtual std::string SendData(const std::string& data) const = 0;

    /**
     * @brief send data in async mode
     *
     * @param[in] data payload
     * @param[in] onEvent event handler called when receive response data
     */
    virtual void SendDataStreaming(const std::string& data, const ClientTransportEventHandler& onEvent) const = 0;
};

} // namespace a2a::transport

#endif
