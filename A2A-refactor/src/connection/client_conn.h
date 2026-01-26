/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_CLIENT_CONN
#define A2A_CLIENT_CONN

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "shared/common_types.h"

namespace A2A {

struct ConnEventData {
    int errCode;
    bool isStream;
    bool isStreamFin;
    std::string data;
};

/**
 * @brief Base callback interface for A2A connections.
 */
class ConnCallback {
public:
    virtual ~ConnCallback() = default;

    /**
     * @brief Invoked when a message is received.
     */
    virtual void OnMessageReceived(const ConnEventData& message, const UserData* userData) = 0;

    /**
     * @brief Invoked when the connection is disconnected.
     */
    virtual void OnDisconnected(const std::string& reason) = 0;
};

/**
 * @brief Common base class for A2A connections.
 */
class ClientConn {
public:
    virtual ~ClientConn() = default;

    /**
     * @brief Establish resources or connections required for communication.
     */
    virtual void Connect() = 0;

    /**
     * @brief Terminate the connection and release resources.
     */
    virtual void Terminate() = 0;

    /**
     * @brief Send a message through the connection.
     *
     * @param[in] message message to send.
     * @param[in] headers headers of connection
     * @param[in] userData user data
     */
    virtual void SendMessage(const std::string& message, const std::map<std::string, std::string>& headers,
        UserData* userData) = 0;

    /**
     * @brief Set callback interface for handling connection events.
     */
    virtual void SetCallback(std::shared_ptr<ConnCallback> callback) = 0;
};

} // namespace A2A

#endif // A2A_ABSTRACT_CLIENT_CONN
