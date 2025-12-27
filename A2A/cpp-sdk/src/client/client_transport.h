/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_CLIENT_TRANSPORT
#define A2A_CLIENT_TRANSPORT

#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <vector>

#include "client/client.h"
#include "utils/types.h"

namespace a2a::client {

using TransportEventCallback = std::function<void(const std::string&)>;

class ClientTransport {
public:
    virtual ~ClientTransport() = default;

    /**
     * @brief send message to server and get response
     *
     * @param[in] request request data info
     * @param[in] context client call context
     * @return task or message
     */
    virtual std::variant<Task, Message> SendMessage(const MessageSendParams& request,
                                                    const ClientCallContext* context = nullptr) = 0;

    /**
     * @brief send stream message to server and get response with callback
     *
     * @param[in] request request data info
     * @param[in] onEvent callback function to receive response
     * @param[in] context client call context
     */
    virtual void SendMessageStreaming(const MessageSendParams& request, const TransportEventCallback& onEvent,
                                      const ClientCallContext* context = nullptr) = 0;

    /**
     * @brief retrive task information from server
     *
     * @param[in] params query params
     * @param[in] context client call context
     * @return Task get
     */
    virtual Task GetTask(const TaskQueryParams& params, const ClientCallContext* context = nullptr) = 0;

    /**
     * @brief call server to cancel task
     *
     * @param[in] params task id params
     * @param[in] context client call context
     * @return Task canceled
     */
    virtual Task CancelTask(const TaskIdParams& params, const ClientCallContext* context = nullptr) = 0;

    /**
     * @brief set push notification config for a specific task
     *
     * @param[in] config push notification config
     * @param[in] context client call context
     * @return TaskPushNotificationConfig
     */
    virtual TaskPushNotificationConfig SetTaskPushNotificationConfig(const TaskPushNotificationConfig& config,
                                                                     const ClientCallContext* context = nullptr) = 0;

    /**
     * @brief retrive push notification config for a specific task
     *
     * @param[in] params task id and metadata information
     * @param[in] context client call context
     * @return TaskPushNotificationConfig
     */
    virtual TaskPushNotificationConfig GetTaskPushNotificationConfig(const GetTaskPushNotificationConfigParams& params,
                                                                     const ClientCallContext* context = nullptr) = 0;

    /**
     * @brief retrive the list of push notification config for a specific task
     *
     * @param[in] params task id and metadata information
     * @param[in] context client call context
     * @return vector of TaskPushNotificationConfig
     */
    virtual std::vector<TaskPushNotificationConfig> ListTaskPushNotificationConfigs(
        const ListTaskPushNotificationConfigParams& params, const ClientCallContext* context = nullptr) = 0;

    /**
     * @brief delete the list of push notification config for a specific task
     *
     * @param[in] params task id and metadata information
     * @param[in] context client call context
     */
    virtual void DeleteTaskPushNotificationConfig(const DeleteTaskPushNotificationConfigParams& params,
                                                  const ClientCallContext* context = nullptr) = 0;

    /**
     * @brief resubscribe to server
     *
     * @param[in] params task id params
     * @param[in] onEvent callback function to receive response
     * @param[in] context client call context
     */
    virtual void Resubscribe(const TaskIdParams& params, const TransportEventCallback& onEvent,
                             const ClientCallContext* context = nullptr) = 0;

    /**
     * @brief retrive agent card from server
     *
     * @param[in] context client call context
     * @return AgentCard
     */
    virtual a2a::AgentCard GetCard(const ClientCallContext* context = nullptr) = 0;

    /**
     * @brief close client connection and release associated resources
     *
     * @note after calling this, no further operations should be performed
     */
    virtual void Close() = 0;
};

} // namespace a2a::client

#endif
