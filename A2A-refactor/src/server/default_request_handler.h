/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_DEFAULT_REQUEST_HANDLER
#define A2A_DEFAULT_REQUEST_HANDLER

#include <memory>
#include <variant>

#include "base_push_notification_sender.h"
#include "inmemory_push_notification_config_store.h"
#include "inmemory_task_store.h"
#include "queue_manager.h"
#include "request_context_builder.h"
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
          task_store_(std::make_shared<InMemoryTaskStore>()),
          queue_manager_(std::move(queueManager)),
          push_config_store_(std::make_shared<InMemoryPushNotificationConfigStore>()),
          push_sender_(std::make_shared<BasePushNotificationSender>(push_config_store_)),
          context_builder_(std::make_shared<RequestContextBuilder>(false, task_store_))
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
    std::variant<Task, Message> OnSendMessage(const MessageSendParams& params, const ServerCallContext* ctx) override;

    /**
     * @brief retrieve a task by query params
     *
     * @param[in] params param containing the query information
     * @param[in] ctx server call context
     * @return task
     */
    Task OnGetTask(const TaskQueryParams& params, const ServerCallContext* ctx) override;

    /**
     * @brief cancel a task by task id params
     *
     * @param[in] params param containing task id
     * @param[in] ctx server call context
     * @return task canceled
     */
    Task OnCancelTask(const TaskIdParams& params, const ServerCallContext* ctx) override;

    /**
     * @brief set a task push notification configuration
     *
     * @param[in] cfg push notification config
     * @param[in] ctx server call context
     * @return TaskPushNotificationConfig
     */
    TaskPushNotificationConfig OnSetTaskPushNotificationConfig(const TaskPushNotificationConfig& cfg,
                                                               const ServerCallContext* ctx) override;

    /**
     * @brief retrieve a task push notification configuration of a task
     *
     * @param[in] params param containing the request information
     * @param[in] ctx server call context
     * @return TaskPushNotificationConfig
     */
    TaskPushNotificationConfig OnGetTaskPushNotificationConfig(const GetTaskPushNotificationConfigParams& params,
                                                               const ServerCallContext* ctx) override;

    /**
     * @brief retrieve all task push notification configurations of a task
     *
     * @param[in] params param containing the id information
     * @param[in] ctx server call context
     * @return vector of TaskPushNotificationConfig
     */
    std::vector<TaskPushNotificationConfig> OnListTaskPushNotificationConfigs(
        const ListTaskPushNotificationConfigParams& params, const ServerCallContext* ctx) override;

    /**
     * @brief delete a task push notification configuration of a task
     *
     * @param[in] params param containing the id information
     * @param[in] ctx server call context
     */
    void OnDeleteTaskPushNotificationConfig(const DeleteTaskPushNotificationConfigParams& params,
                                            const ServerCallContext* ctx) override;

    /**
     * @brief retrieve agent card
     *
     * @param[in] ctx server call context
     * @return AgentCard
     */
    AgentCard OnGetCard(const ServerCallContext* ctx) override;

    /**
     * @brief handle the stream request reveiced
     *
     * @param[in] params param containing the request information
     * @param[in] emit emiter to call to update message status
     * @param[in] ctx server call context
     */
    void OnSendMessageStreaming(const MessageSendParams& params, const StreamEmitter& emit,
                                const ServerCallContext* ctx) override;

    /**
     * @brief handle resubscribe to task request
     *
     * @param[in] params param containing the task id information
     * @param[in] emit emiter to call to update message status
     * @param[in] ctx server call context
     */
    void OnResubscribeToTask(const TaskIdParams& params, const StreamEmitter& emit,
                             const ServerCallContext* ctx) override;

private:
    std::shared_ptr<AgentExecutor> executor_;
    std::shared_ptr<AgentCard> agentCard_;
    std::shared_ptr<TaskStore> task_store_;
    std::shared_ptr<QueueManager> queue_manager_;
    std::shared_ptr<PushNotificationConfigStore> push_config_store_;
    std::shared_ptr<PushNotificationSender> push_sender_;
    std::shared_ptr<RequestContextBuilder> context_builder_;

    // Helper methods
    static bool is_final_state(TaskState state);
    static std::string generate_task_id();
    void UpdatePushNotificationConfig(const MessageSendParams &params, const std::string& task_id) const;
    std::optional<Task> GetOrCreateTask(const MessageSendParams &params, const ServerCallContext *ctx,
                                        std::string &task_id,
                                        std::shared_ptr<TaskManager> &task_manager);
};

} // namespace A2A::Server

#endif
