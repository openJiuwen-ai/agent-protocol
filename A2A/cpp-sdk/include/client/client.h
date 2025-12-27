/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_CLIENT
#define A2A_CLIENT

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "client/client_call_interceptor.h"
#include "utils/types.h"

namespace a2a::client {

struct ClientConfig {
    bool streaming = true; // Whether client supports streaming
    bool polling = false; // Prefer polling for message/send
    std::vector<std::string> supportedTransports; // ordered labels (e.g., "jsonrpc", "http-json")
    bool useClientPreference = false; // Prefer client list over server preferences
    std::vector<std::string> acceptedOutputModes; // accepted output modes
    std::vector<PushNotificationConfig> pushNotificationConfigs; // default push configs
};

// Event alias: (Task, optional update) or a Message
using UpdateEvent = std::variant<std::monostate, TaskStatusUpdateEvent, TaskArtifactUpdateEvent>;
using ClientEvent = std::variant<Message, std::pair<Task, UpdateEvent>>;

// Consumer: receives ClientEvent or Message with AgentCard
using Consumer = std::function<void(const ClientEvent&, const a2a::AgentCard&)>;

class Client {
public:
    /**
     * @brief destructor
     */
    virtual ~Client() = default;

    /**
     * @brief send message to server and get return via consumer
     * For streaming, this will synchronously call consumer per update; for non-streaming, a single event.
     * message/send
     *
     * @param[in] msg message to be sent
     * @param[in] context client call context
     * @param[in] consumer client consumer
     */
    virtual void SendMessage(const Message& msg, const ClientCallContext* context, const Consumer& consumer) = 0;

    /**
     * @brief send message to server and get return via consumers
     * For streaming, this will synchronously call consumer per update; for non-streaming, a single event.
     * message/send
     *
     * @param[in] msg message to be sent
     * @param[in] context client call context
     * @param[in] consumers client consumers
     */
    virtual void SendMessage(const Message& msg, const ClientCallContext* context,
                             const std::vector<Consumer>& consumers) = 0;

    /**
     * @brief retrive a task by query params
     * tasks/get
     *
     * @param[in] params params of the task to fetch
     * @param[in] context client call context, defaults to nullptr
     * @return a Task object containing details of the requested task
     */
    virtual Task GetTask(const TaskQueryParams& params, const ClientCallContext* context = nullptr) = 0;

    /**
     * @brief cancel a task by task id
     * tasks/cancel
     *
     * @param[in] params task id param
     * @param[in] context client call context, defaults to nullptr
     * @return a Task object representing the canceled task state
     */
    virtual Task CancelTask(const TaskIdParams& params, const ClientCallContext* context = nullptr) = 0;

    /**
     * @brief set or update the push notification callback configuration for tasks
     * tasks/pushNotificationConfig/set
     *
     * @param[in] config configuration object specifying callback details
     * @param[in] context client call context, defaults to nullptr
     * @return TaskPushNotificationConfig
     */
    virtual TaskPushNotificationConfig SetTaskPushNotificationConfig(const TaskPushNotificationConfig& cfg,
                                                                     const ClientCallContext* context = nullptr) = 0;

    /**
     * @brief retrive the push notification callback configuration for a specific task
     * tasks/pushNotificationConfig/get
     *
     * @param[in] params task param
     * @param[in] context client call context, defaults to nullptr
     * @return the push notification callback configuration for a specific task
     */
    virtual TaskPushNotificationConfig GetTaskPushNotificationConfig(const GetTaskPushNotificationConfigParams& params,
                                                                     const ClientCallContext* context = nullptr) = 0;

    /**
     * @brief retrive the list of push notification callback configuration for a specific task
     * tasks/pushNotificationConfig/list
     *
     * @param[in] params task param specifying task information
     * @param[in] context client call context, defaults to nullptr
     * @return the list of push notification callback configuration for a specific task
     */
    virtual std::vector<TaskPushNotificationConfig> ListTaskPushNotificationConfigs(
        const ListTaskPushNotificationConfigParams& params, const ClientCallContext* context = nullptr) = 0;

    /**
     * @brief delete the list of push notification callback configuration for a specific task
     * tasks/pushNotificationConfig/delete
     *
     * @param[in] params task param specifying task information
     * @param[in] context client call context, defaults to nullptr
     */
    virtual void DeleteTaskPushNotificationConfig(const DeleteTaskPushNotificationConfigParams& params,
                                                  const ClientCallContext* context = nullptr) = 0;

    /**
     * @brief rescribe to task events for a given task id
     *
     * @param[in] params task id params
     * @param[in] context client call context
     * @param[in] consumer callback function to be called when a ClientEvent is received
     */
    virtual void Resubscribe(const TaskIdParams& params, const ClientCallContext* context,
                             const Consumer& consumer) = 0;

    /**
     * @brief retrive card information of agent
     *
     * @param[in] context client call context, defaults to nullptr
     * @return AgentCard information
     */
    virtual a2a::AgentCard GetCard(const ClientCallContext* context = nullptr) = 0;

    /**
     * @brief register a consumer to receive client events
     *
     * @param[in] c consumer object
     */
    virtual void AddEventConsumer(Consumer c) = 0;

    /**
     * @brief add request middleware
     *
     * @param[in] middleware interceptor object
     */
    virtual void AddRequestMiddleware(ClientCallInterceptor* middleware) = 0;

    /**
     * @brief close client connection and release associated resources
     *
     * @note after calling this, no further operations should be performed
     */
    virtual void Close() = 0;
};

} // namespace a2a::client

#endif
