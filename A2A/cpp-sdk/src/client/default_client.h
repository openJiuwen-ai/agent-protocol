/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_DEFAULT_CLIENT
#define A2A_DEFAULT_CLIENT

#include <functional>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include "client/client.h"
#include "client_transport.h"

namespace a2a::client {

class DefaultClient : public Client {
public:
    /**
     * @brief constructor
     *
     * @param[in] card agent card related to current client
     * @param[in] config client config
     * @param[in] transport transport layer object
     * @param[in] consumers consumers
     * @param[in] middleware interceptor of current client
     */
    DefaultClient(const a2a::AgentCard& card, ClientConfig config, std::shared_ptr<ClientTransport> transport,
                  std::vector<Consumer> consumers = {}, std::vector<ClientCallInterceptor*> middleware = {});

    /**
     * @brief destructor
     */
    ~DefaultClient();

    /**
     * @brief send message to server and get return via consumer
     * For streaming, this will synchronously call consumer per update; for non-streaming, a single event.
     *
     * @param[in] msg message to be sent
     * @param[in] context client call context
     * @param[in] metadata metadata of message
     * @param[in] consumer client event handler
     */
    void SendMessage(const Message& msg, const ClientCallContext* context, const Consumer& consumer) override;

    /**
     * @brief send message to server and get return via consumers
     * For streaming, this will synchronously call consumers per update; for non-streaming, a single event.
     * message/send
     *
     * @param[in] msg message to be sent
     * @param[in] context client call context
     * @param[in] consumers client consumers
     */
    void SendMessage(const Message& msg, const ClientCallContext* context,
                     const std::vector<Consumer>& consumers) override;

    /**
     * @brief retrive a task by query params
     *
     * @param[in] params params of the task to fetch
     * @param[in] context client call context, defaults to nullptr
     * @return a Task object containing details of the requested task
     */
    Task GetTask(const TaskQueryParams& params, const ClientCallContext* context = nullptr) override;

    /**
     * @brief cancel a task by task id
     *
     * @param[in] params task id param
     * @param[in] context client call context, defaults to nullptr
     * @return a Task object representing the canceled task state
     */
    Task CancelTask(const TaskIdParams& params, const ClientCallContext* context = nullptr) override;

    /**
     * @brief set or update the push notification callback configuration for tasks
     *
     * @param[in] config configuration object specifying callback details
     * @param[in] context client call context, defaults to nullptr
     * @return TaskPushNotificationConfig
     */
    TaskPushNotificationConfig SetTaskPushNotificationConfig(const TaskPushNotificationConfig& config,
                                                             const ClientCallContext* context = nullptr) override;

    /**
     * @brief retrive the push notification callback configuration for a specific task
     *
     * @param[in] params task param
     * @param[in] context client call context, defaults to nullptr
     * @return the push notification callback configuration for a specific task
     */
    TaskPushNotificationConfig GetTaskPushNotificationConfig(const GetTaskPushNotificationConfigParams& params,
                                                             const ClientCallContext* context = nullptr) override;

    /**
     * @brief retrive the list of push notification callback configuration for a specific task
     * tasks/pushNotificationConfig/list
     *
     * @param[in] params task param specifying task information
     * @param[in] context client call context, defaults to nullptr
     * @return the list of push notification callback configuration for a specific task
     */
    std::vector<TaskPushNotificationConfig> ListTaskPushNotificationConfigs(
        const ListTaskPushNotificationConfigParams& params, const ClientCallContext* context = nullptr) override;

    /**
     * @brief delete the list of push notification callback configuration for a specific task
     * tasks/pushNotificationConfig/delete
     *
     * @param[in] params task param specifying task information
     * @param[in] context client call context, defaults to nullptr
     */
    void DeleteTaskPushNotificationConfig(const DeleteTaskPushNotificationConfigParams& params,
                                          const ClientCallContext* context = nullptr) override;

    /**
     * @brief rescribe to task events for a given task id
     *
     * @param[in] params task id params
     * @param[in] context client call context, defaults to nullptr
     * @param[in] consumer callback function to be called when a ClientEvent is received
     */
    void Resubscribe(const TaskIdParams& params, const ClientCallContext* context, const Consumer& consumer) override;

    /**
     * @brief retrive card information of agent
     *
     * @param[in] context client call context, defaults to nullptr
     * @return AgentCard information
     */
    a2a::AgentCard GetCard(const ClientCallContext* context = nullptr) override;

    /**
     * @brief register a consumer to receive client events
     *
     * @param[in] c consumer object
     */
    void AddEventConsumer(Consumer c) override;

    /**
     * @brief add request middleware
     *
     * @param[in] middleware interceptor object
     */
    void AddRequestMiddleware(ClientCallInterceptor* middleware) override;

    /**
     * @brief close client connection and release associated resources
     *
     * @note after calling this, no further operations should be performed
     */
    void Close() override;

private:
    void Consume(const ClientEvent& ev);

    a2a::AgentCard card_;
    ClientConfig config_;
    std::shared_ptr<ClientTransport> transport_;
    std::vector<Consumer> consumers_;
    std::vector<ClientCallInterceptor*> middleware_;
};

} // namespace a2a::client

#endif
