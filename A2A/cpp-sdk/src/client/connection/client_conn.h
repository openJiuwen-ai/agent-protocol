/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_CLIENT_CONN
#define A2A_CLIENT_CONN

#include <memory>
#include <string>
#include <map>

#include "common_types.h"

namespace A2A {

struct ConnEventData {
    int errCode;
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
    virtual void OnMessageReceived(const ConnEventData& message, const UserData& userData) = 0;

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
    virtual int SendMessage(const std::string& message, const std::map<std::string, std::string>& headers,
        std::shared_ptr<UserData> userData, int timeout) = 0;

    /**
    * @brief Set callback interface for handling connection events.
    */
    virtual void SetCallback(std::shared_ptr<ConnCallback> callback) = 0;

    virtual void FinishRequest(int timerId) = 0;

    virtual void RefreshRequest(int timerId, int timeout) = 0;
};

} // namespace A2A

#endif // A2A_CLIENT_CONN