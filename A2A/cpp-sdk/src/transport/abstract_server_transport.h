/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_ABSTRACT_SERVER_TRANSPORT
#define A2A_ABSTRACT_SERVER_TRANSPORT

#include <httplib.h>

#include <map>
#include <optional>
#include <string>

namespace a2a::transport {

class AbstractServerTransportEmiter {
public:
    virtual ~AbstractServerTransportEmiter() = default;
    virtual void WriteData(const std::string& data) = 0;
    virtual void WriteDone() = 0;
};

using ServerTransportEventHandler = std::function<void(const std::string&, std::string&)>;
using ServerTransportStreamEventHandler = std::function<void(const std::string&, AbstractServerTransportEmiter&)>;

class AbstractServerTransport {
public:
    /**
     * @brief constructor
     */
    AbstractServerTransport() = default;

    /**
     * @brief destructor
     */
    virtual ~AbstractServerTransport() = default;

    /**
     * @brief start to listen
     *
     * @param[in] ipAddr ip address
     * @param[in] port port
     * @return 0 on succeed
     */
    virtual int Start(const std::string& ipAddr, uint16_t port) = 0;

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
     * @brief set event handler
     * handler will be called when server receive non-streaming data
     *
     * @param[in] handler event handler
     */
    virtual void SetEventHandler(ServerTransportEventHandler handler) = 0;

    /**
     * @brief set stream event handler
     * handler will be called when server receive streaming data
     *
     * @param[in] handler event handler
     */
    virtual void SetStreamEventHandler(ServerTransportStreamEventHandler handler) = 0;
};

} // namespace a2a::transport

#endif
