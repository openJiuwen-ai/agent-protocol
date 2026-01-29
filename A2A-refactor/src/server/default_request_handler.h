/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_DEFAULT_REQUEST_HANDLER
#define A2A_DEFAULT_REQUEST_HANDLER

#include <memory>
#include <variant>

#include "inmemory_push_notification_config_store.h"
#include "inmemory_task_store.h"
#include "queue_manager.h"
#include "result_aggregator.h"
#include "server/agent_executor.h"
#include "tasks/push_notification_config_store.h"
#include "tasks/push_notification_sender.h"
#include "server/task_store.h"
#include "types.h"

namespace A2A::Server {

// A simple default request handler that forwards to an inner Handler or throws if not set.
class DefaultRequestHandler : public RequestHandler {
public:
    /**
     * @brief constructor
     *
     * @param[in] executor executor of request
     * @param[in] agentCard agent card object
     */
    DefaultRequestHandler(std::shared_ptr<AgentExecutor> executor, std::shared_ptr<AgentCard> agentCard,
                          std::shared_ptr<QueueManager> queueManager = nullptr)
        : executor_(std::move(executor)),
          agentCard_(std::move(agentCard)),
          taskStore_(std::make_shared<InMemoryTaskStore>()),
          queueManager_(std::move(queueManager)),
          pushConfigStore_(std::make_shared<InMemoryPushNotificationConfigStore>())
    {
    }
    
    ~DefaultRequestHandler() override = default;

    /**
     * @brief handle the request reveiced
     *
     * @param[in] params param containing the request information
     * @param[in] ctx server call context
     * @return task or message
     */
    std::variant<Task, Message> OnSendMessage(
        const MessageSendParams& params, std::shared_ptr<ServerCallContext> ctx) override;

    /**
     * @brief retrieve a task by query params
     *
     * @param[in] params param containing the query information
     * @param[in] ctx server call context
     * @return task
     */
    Task OnGetTask(const TaskQueryParams& params, std::shared_ptr<ServerCallContext> ctx) override;

    /**
     * @brief cancel a task by task id params
     *
     * @param[in] params param containing task id
     * @param[in] ctx server call context
     * @return task canceled
     */
    Task OnCancelTask(const TaskIdParams& params, std::shared_ptr<ServerCallContext> ctx) override;

    /**
     * @brief set a task push notification configuration
     *
     * @param[in] cfg push notification config
     * @param[in] ctx server call context
     * @return TaskPushNotificationConfig
     */
    TaskPushNotificationConfig OnSetTaskPushNotificationConfig(const TaskPushNotificationConfig& cfg,
                                                               std::shared_ptr<ServerCallContext> ctx) override;

    /**
     * @brief retrieve a task push notification configuration of a task
     *
     * @param[in] params param containing the request information
     * @param[in] ctx server call context
     * @return TaskPushNotificationConfig
     */
    TaskPushNotificationConfig OnGetTaskPushNotificationConfig(const GetTaskPushNotificationConfigParams& params,
                                                               std::shared_ptr<ServerCallContext> ctx) override;

    /**
     * @brief retrieve all task push notification configurations of a task
     *
     * @param[in] params param containing the id information
     * @param[in] ctx server call context
     * @return vector of TaskPushNotificationConfig
     */
    std::vector<TaskPushNotificationConfig> OnListTaskPushNotificationConfigs(
        const ListTaskPushNotificationConfigParams& params, std::shared_ptr<ServerCallContext> ctx) override;

    /**
     * @brief delete a task push notification configuration of a task
     *
     * @param[in] params param containing the id information
     * @param[in] ctx server call context
     */
    void OnDeleteTaskPushNotificationConfig(const DeleteTaskPushNotificationConfigParams& params,
                                            std::shared_ptr<ServerCallContext> ctx) override;

    /**
     * @brief retrieve agent card
     *
     * @param[in] ctx server call context
     * @return AgentCard
     */
    AgentCard OnGetCard(std::shared_ptr<ServerCallContext> ctx) override;

    /**
     * @brief handle the stream request reveiced
     *
     * @param[in] params param containing the request information
     * @param[in] emit emiter to call to update message status
     * @param[in] ctx server call context
     */
    void OnSendMessageStreaming(const MessageSendParams& params, const StreamEmitter& emit,
                                std::shared_ptr<ServerCallContext> ctx) override;

    /**
     * @brief handle resubscribe to task request
     *
     * @param[in] params param containing the task id information
     * @param[in] emit emiter to call to update message status
     * @param[in] ctx server call context
     */
    void OnResubscribeToTask(const TaskIdParams& params, const StreamEmitter& emit,
                             std::shared_ptr<ServerCallContext> ctx) override;

private:
    std::shared_ptr<AgentExecutor> executor_;
    std::shared_ptr<AgentCard> agentCard_;
    std::shared_ptr<TaskStore> taskStore_;
    std::shared_ptr<QueueManager> queueManager_;
    std::shared_ptr<PushNotificationConfigStore> pushConfigStore_;
    std::shared_ptr<PushNotificationSender> pushSender_;

    // Helper methods
    static bool IsFinalState(TaskState state);

    static std::string GenerateTaskId();

    void UpdatePushNotificationConfig(const MessageSendParams &params, const std::string& taskId) const;

    // Private method to create RequestContext without using RequestContextBuilder
    static RequestContext CreateRequestContext(std::optional<MessageSendParams> params,
        std::string taskId,
        std::optional<std::string> contextId,
        std::optional<Task> existingTask,
        const std::shared_ptr<ServerCallContext>& ctx);

    // Private methods extracted from OnSendMessage
    std::string DetermineTaskId(const MessageSendParams& params, std::shared_ptr<ServerCallContext> ctx,
        std::optional<Task>& existing_task) const;

    std::shared_ptr<TaskManager> CreateTaskManager(const std::string& taskId, const std::string& contextId,
        const Message& message, std::shared_ptr<ServerCallContext> ctx) const;

    void UpdateOrCreateTask(const MessageSendParams& params, const std::string& taskId,
        const std::shared_ptr<TaskManager>& taskManager, const std::optional<Task>& existing_task,
        std::shared_ptr<ServerCallContext> ctx) const;

    std::optional<std::variant<Task, Message>> ExecuteAgentAndGetResult(const MessageSendParams& params,
        const std::string& taskId,
        const std::shared_ptr<TaskManager>& taskManager,
        const RequestContext& request_context,
        std::shared_ptr<ServerCallContext> ctx) const;

    void SendPushNotificationIfNeeded(const std::string& taskId, std::shared_ptr<ServerCallContext> ctx) const;

    // Private methods extracted from OnSendMessageStreaming
    void InitializeStreamingTask(const MessageSendParams& params,
        std::shared_ptr<ServerCallContext> context,
        std::optional<Task>& existingTask,
        std::string& taskId,
        std::shared_ptr<TaskManager>& taskManager) const;

    void SetupAndExecuteStreamingAgent(const MessageSendParams& params,
        const std::string& taskId,
        const RequestContext& requestContext,
        const std::shared_ptr<TaskManager>& taskManager,
        const StreamEmitter& emit) const;
};

} // namespace A2A::Server

#endif
