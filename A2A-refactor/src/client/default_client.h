/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_DEFAULT_CLIENT
#define A2A_DEFAULT_CLIENT

#include <memory>
#include <vector>
#include <future>

#include "client/client.h"
#include "client_transport.h"
#include "client_task_manager.h"

namespace A2A::Client {

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
    DefaultClient(const A2A::AgentCard& card, const ClientConfig& config, std::shared_ptr<ClientTransport> transport,
        const std::vector<Consumer>& consumers = {},
        const std::vector<std::shared_ptr<ClientCallInterceptor>>& middleware = {});

    /**
     * @brief destructor
     */
    ~DefaultClient();

    /**
     * @brief send message to server and get return via consumers
     * For streaming, this will synchronously call consumers per update; for non-streaming, a single event.
     * message/send
     *
     * @param[in] msg message to be sent
     * @param[in] context client call context
     * @param[in] handler response handler
     */
    void SendMessage(const Message& msg, const ClientCallContext* context, ResponseHandler handler) override;

    /**
     * @brief retrive a task by query params
     *
     * @param[in] params params of the task to fetch
     * @param[in] context client call context, defaults to nullptr
     * @return a Task object containing details of the requested task
     */
    std::future<Task> GetTask(const TaskQueryParams& params, const ClientCallContext* context = nullptr) override;

    /**
     * @brief cancel a task by task id
     *
     * @param[in] params task id param
     * @param[in] context client call context, defaults to nullptr
     * @return a Task object representing the canceled task state
     */
    std::future<Task> CancelTask(const TaskIdParams& params, const ClientCallContext* context = nullptr) override;

    /**
     * @brief set or update the push notification callback configuration for tasks
     *
     * @param[in] config configuration object specifying callback details
     * @param[in] context client call context, defaults to nullptr
     * @return TaskPushNotificationConfig
     */
    std::future<TaskPushNotificationConfig> SetTaskPushNotificationConfig(const TaskPushNotificationConfig& config,
        const ClientCallContext* context = nullptr) override;

    /**
     * @brief retrive the push notification callback configuration for a specific task
     *
     * @param[in] params task param
     * @param[in] context client call context, defaults to nullptr
     * @return the push notification callback configuration for a specific task
     */
    std::future<TaskPushNotificationConfig> GetTaskPushNotificationConfig(
        const GetTaskPushNotificationConfigParams& params, const ClientCallContext* context = nullptr) override;

    /**
     * @brief retrive the list of push notification callback configuration for a specific task
     * tasks/pushNotificationConfig/list
     *
     * @param[in] params task param specifying task information
     * @param[in] context client call context, defaults to nullptr
     * @return the list of push notification callback configuration for a specific task
     */
    std::future<std::vector<TaskPushNotificationConfig>> ListTaskPushNotificationConfigs(
        const ListTaskPushNotificationConfigParams& params, const ClientCallContext* context = nullptr) override;

    /**
     * @brief delete the list of push notification callback configuration for a specific task
     * tasks/pushNotificationConfig/delete
     *
     * @param[in] params task param specifying task information
     * @param[in] context client call context, defaults to nullptr
     */
    std::future<void> DeleteTaskPushNotificationConfig(const DeleteTaskPushNotificationConfigParams& params,
        const ClientCallContext* context = nullptr) override;

    /**
     * @brief rescribe to task events for a given task id
     *
     * @param[in] params task id params
     * @param[in] context client call context, defaults to nullptr
     * @param[in] handler response handler
     */
    void Resubscribe(const TaskIdParams& params, const ClientCallContext* context,
        ResponseHandler handler) override;

    /**
     * @brief retrive card information of agent
     *
     * @param[in] context client call context, defaults to nullptr
     * @return AgentCard information
     */
    std::future<AgentCard> GetCard(const ClientCallContext* context = nullptr) override;

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
    void AddRequestMiddleware(std::shared_ptr<ClientCallInterceptor> middleware) override;

    /**
     * @brief close client connection and release associated resources
     *
     * @note after calling this, no further operations should be performed
     */
    void Close() override;

private:
    void Consume(const ClientEvent& ev);
    void TransportEventCb(const std::string& requestId, const TransportEvent& event);

    using PromisePtrVariant = std::variant<
        std::shared_ptr<std::promise<Task>>,
        std::shared_ptr<std::promise<TaskPushNotificationConfig>>,
        std::shared_ptr<std::promise<std::vector<TaskPushNotificationConfig>>>,
        std::shared_ptr<std::promise<void>>,
        std::shared_ptr<std::promise<A2A::AgentCard>>
    >;

    struct CallbackInfo {
        std::string requestId;
        std::string method;
        PromisePtrVariant promise;
        ResponseHandler handler;
        std::shared_ptr<ClientTaskManager> mgr;
    };

    A2A::AgentCard card_;
    ClientConfig config_;
    std::shared_ptr<ClientTransport> transport_;
    std::vector<Consumer> consumers_;
    std::vector<std::shared_ptr<ClientCallInterceptor>> middleware_;
    std::unordered_map<std::string, std::shared_ptr<CallbackInfo>> callbackInfo_;
};

} // namespace A2A::Client

#endif
