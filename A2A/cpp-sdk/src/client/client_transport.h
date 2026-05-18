/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_CLIENT_TRANSPORT
#define A2A_CLIENT_TRANSPORT

#include <functional>
#include <vector>
#include <future>

#include "client/client.h"
#include "types.h"
#include "common_types.h"
#include "client_conn.h"

namespace A2A::Client {

struct TransportError {
    int errorCode;
    std::string errInfo;
};

using TransportEvent = std::variant<TransportError, Message, Task, TaskStatusUpdateEvent, TaskArtifactUpdateEvent,
    TaskPushNotificationConfig, std::vector<TaskPushNotificationConfig>, std::monostate, AgentCard>;
using TransportEventCallback = std::function<void(const std::string&, const TransportEvent&)>;

class ClientTransport {
public:
    ~ClientTransport() = default;

    /**
     * @brief send message to server and get response
     *
     * @param[in] requestId requestId
     * @param[in] request request data info
     * @param[in] context client call context
     */
    virtual void SendMessage(const std::string& requestId, const MessageSendParams& request,
        const ClientCallContext* context) = 0;

    /**
     * @brief send stream message to server and get response with callback
     *
     * @param[in] requestId requestId
     * @param[in] request request data info
     * @param[in] context client call context
     */
    virtual void SendMessageStreaming(const std::string& requestId, const MessageSendParams& request,
        const ClientCallContext* context) = 0;

    /**
     * @brief retrieve task information from server
     *
     * @param[in] requestId requestId
     * @param[in] params query params
     * @param[in] context client call context
     */
    virtual void GetTask(const std::string& requestId, const TaskQueryParams& params,
        const ClientCallContext* context) = 0;

    /**
     * @brief call server to cancel task
     *
     * @param[in] requestId requestId
     * @param[in] params task id params
     * @param[in] context client call context
     */
    virtual void CancelTask(const std::string& requestId, const TaskIdParams& params,
        const ClientCallContext* context) = 0;

    /**
     * @brief set push notification config for a specific task
     *
     * @param[in] requestId requestId
     * @param[in] config push notification config
     * @param[in] context client call context
     */
    virtual void SetTaskPushNotificationConfig(const std::string& requestId, const TaskPushNotificationConfig& config,
        const ClientCallContext* context) = 0;

    /**
     * @brief retrieve push notification config for a specific task
     *
     * @param[in] requestId requestId
     * @param[in] params task id and metadata information
     * @param[in] context client call context
     */
    virtual void GetTaskPushNotificationConfig(const std::string& requestId,
        const GetTaskPushNotificationConfigParams& params, const ClientCallContext* context) = 0;

    /**
     * @brief retrieve the list of push notification config for a specific task
     *
     * @param[in] requestId requestId
     * @param[in] params task id and metadata information
     * @param[in] context client call context
     */
    virtual void ListTaskPushNotificationConfigs(const std::string& requestId,
        const ListTaskPushNotificationConfigParams& params, const ClientCallContext* context) = 0;

    /**
     * @brief delete the list of push notification config for a specific task
     *
     * @param[in] requestId requestId
     * @param[in] params task id and metadata information
     * @param[in] context client call context
     */
    virtual void DeleteTaskPushNotificationConfig(const std::string& requestId,
        const DeleteTaskPushNotificationConfigParams& params, const ClientCallContext* context) = 0;

    /**
     * @brief resubscribe to server
     *
     * @param[in] requestId requestId
     * @param[in] params task id params
     * @param[in] context client call context
     */
    virtual void Resubscribe(const std::string& requestId, const TaskIdParams& params,
        const ClientCallContext* context) = 0;

    /**
     * @brief retrieve agent card from server
     *
     * @param[in] requestId requestId
     * @param[in] context client call context
     */
    virtual void GetCard(const std::string& requestId, const ClientCallContext* context) = 0;

    /**
     * @brief set transport callback
     *
     * @param[in] callback callback function triggered when receive response data
     */
    virtual void SetTransportCallback(TransportEventCallback callback) = 0;

    /**
     * @brief transport callback fuction
     *
     * @param[in] message message payload
     * @param[in] userData user data
     */
    virtual void OnTransportMessage(const ConnEventData& message, const UserData* userData) = 0;

    /**
     * @brief close client connection and release associated resources
     *
     * @note after calling this, no further operations should be performed
     */
    virtual void Close() = 0;
};

} // namespace A2A::Client

#endif
