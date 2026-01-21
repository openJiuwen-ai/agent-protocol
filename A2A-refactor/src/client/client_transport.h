/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_CLIENT_TRANSPORT
#define A2A_CLIENT_TRANSPORT

#include <functional>
#include <vector>
#include <future>

#include "client/client.h"
#include "utils/types.h"

namespace A2A::Client {

using TrasnportEvent = std::variant<ClientEvent, Task, TaskPushNotificationConfig,
    std::vector<TaskPushNotificationConfig>, std::monostate, AgentCard>;
using TransportEventCallback = std::function<void(const std::string&, const TrasnportEvent&)>;

class ClientTransport {
public:
    ~ClientTransport() = default;

    /**
     * @brief send message to server and get response
     *
     * @param[in] requestId requestId
     * @param[in] request request data info
     * @param[in] context client call context
     * @return 0 on success, errno on fail
     */
    virtual int SendMessage(const std::string& requestId, const MessageSendParams& request,
        const ClientCallContext* context) = 0;

    /**
     * @brief send stream message to server and get response with callback
     *
     * @param[in] requestId requestId
     * @param[in] request request data info
     * @param[in] context client call context
     * @return 0 on success, errno on fail
     */
    virtual int SendMessageStreaming(const std::string& requestId, const MessageSendParams& request,
        const ClientCallContext* context) = 0;

    /**
     * @brief retrive task information from server
     *
     * @param[in] requestId requestId
     * @param[in] params query params
     * @param[in] context client call context
     * @return 0 on success, errno on fail
     */
    virtual int GetTask(const std::string& requestId, const TaskQueryParams& params,
        const ClientCallContext* context) = 0;

    /**
     * @brief call server to cancel task
     *
     * @param[in] requestId requestId
     * @param[in] params task id params
     * @param[in] context client call context
     * @return 0 on success, errno on fail
     */
    virtual int CancelTask(const std::string& requestId, const TaskIdParams& params,
        const ClientCallContext* context) = 0;

    /**
     * @brief set push notification config for a specific task
     *
     * @param[in] requestId requestId
     * @param[in] config push notification config
     * @param[in] context client call context
     * @return 0 on success, errno on fail
     */
    virtual int SetTaskPushNotificationConfig(const std::string& requestId, const TaskPushNotificationConfig& config,
        const ClientCallContext* context) = 0;

    /**
     * @brief retrive push notification config for a specific task
     *
     * @param[in] requestId requestId
     * @param[in] params task id and metadata information
     * @param[in] context client call context
     * @return 0 on success, errno on fail
     */
    virtual int GetTaskPushNotificationConfig(const std::string& requestId,
        const GetTaskPushNotificationConfigParams& params, const ClientCallContext* context) = 0;

    /**
     * @brief retrive the list of push notification config for a specific task
     *
     * @param[in] requestId requestId
     * @param[in] params task id and metadata information
     * @param[in] context client call context
     * @return 0 on success, errno on fail
     */
    virtual int ListTaskPushNotificationConfigs(const std::string& requestId,
        const ListTaskPushNotificationConfigParams& params, const ClientCallContext* context) = 0;

    /**
     * @brief delete the list of push notification config for a specific task
     *
     * @param[in] requestId requestId
     * @param[in] params task id and metadata information
     * @param[in] context client call context
     * @return 0 on success, errno on fail
     */
    virtual int DeleteTaskPushNotificationConfig(const std::string& requestId,
        const DeleteTaskPushNotificationConfigParams& params, const ClientCallContext* context) = 0;

    /**
     * @brief resubscribe to server
     *
     * @param[in] requestId requestId
     * @param[in] params task id params
     * @param[in] context client call context
     * @return 0 on success, errno on fail
     */
    virtual int Resubscribe(const std::string& requestId, const TaskIdParams& params,
        const ClientCallContext* context) = 0;

    /**
     * @brief retrive agent card from server
     *
     * @param[in] requestId requestId
     * @param[in] context client call context
     * @return 0 on success, errno on fail
     */
    virtual int GetCard(const std::string& requestId, const ClientCallContext* context) = 0;

    /**
     * @brief set transport callback
     *
     * @param[in] callback callback function triggered when receive response data
     */
    virtual void SetTransportCallback(TransportEventCallback callback) = 0;

    /**
     * @brief close client connection and release associated resources
     *
     * @note after calling this, no further operations should be performed
     */
    virtual void Close() = 0;
};

} // namespace A2A::Client

#endif
