/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_DEFAULT_REQUEST_HANDLER
#define A2A_DEFAULT_REQUEST_HANDLER

#include <memory>

#include "inmemory_push_notification_config_store.h"
#include "inmemory_task_store.h"
#include "tasks/task_manager.h"
#include "server/agent_executor.h"
#include "tasks/push_notification_config_store.h"
#include "tasks/push_notification_sender.h"
#include "server/task_store.h"
#include "types.h"
#include "request_handler.h"

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
    DefaultRequestHandler(std::shared_ptr<AgentExecutor> executor,
        std::shared_ptr<AgentCard> agentCard,
        std::shared_ptr<TaskStore> taskStore = nullptr)
        : executor_(std::move(executor)), agentCard_(std::move(agentCard)),
        taskStore_(taskStore == nullptr ? std::make_shared<InMemoryTaskStore>() : taskStore),
        taskManager_(std::make_shared<TaskManager>(taskStore_)),
        pushConfigStore_(std::make_shared<InMemoryPushNotificationConfigStore>())
    {
    }
    
    ~DefaultRequestHandler() override = default;

    /**
    * @brief handle the request reveiced
    *
    * @param[in] params param containing the request information
    * @param[in] ctx server call context
    */
    void OnSendMessage(const MessageSendParams& params, const std::shared_ptr<ServerCallContext> ctx,
        StreamEmitter emit, const std::string& method) override;

    /**
    * @brief retrive a task by query params
    *
    * @param[in] params param containing the query information
    * @param[in] ctx server call context
    * @return task
    */
    Task OnGetTask(const TaskQueryParams& params, const std::shared_ptr<ServerCallContext> ctx) override;

    /**
    * @brief cancel a task by task id params
    *
    * @param[in] params param containing task id
    * @param[in] ctx server call context
    * @return task canceled
    */
    Task OnCancelTask(const TaskIdParams& params, const std::shared_ptr<ServerCallContext> ctx) override;

    /**
    * @brief set a task push notification configuration
    *
    * @param[in] cfg push notification config
    * @param[in] ctx server call context
    */
    void OnSetTaskPushNotificationConfig(const TaskPushNotificationConfig& cfg,
                                        const std::shared_ptr<ServerCallContext> ctx) override;

    /**
    * @brief retrive a task push notification configuration of a task
    *
    * @param[in] params param containing the request information
    * @param[in] ctx server call context
    * @return TaskPushNotificationConfig
    */
    TaskPushNotificationConfig OnGetTaskPushNotificationConfig(const GetTaskPushNotificationConfigParams& params,
                                                                const std::shared_ptr<ServerCallContext> ctx) override;

    /**
    * @brief retrive all task push notification configurations of a task
    *
    * @param[in] params param containing the id information
    * @param[in] ctx server call context
    * @return vector of TaskPushNotificationConfig
    */
    std::vector<TaskPushNotificationConfig> OnListTaskPushNotificationConfigs(
        const ListTaskPushNotificationConfigParams& params, const std::shared_ptr<ServerCallContext> ctx) override;

    /**
    * @brief delete a task push notification configuration of a task
    *
    * @param[in] params param containing the id information
    * @param[in] ctx server call context
    */
    void OnDeleteTaskPushNotificationConfig(const DeleteTaskPushNotificationConfigParams& params,
                                            const std::shared_ptr<ServerCallContext> ctx) override;

    /**
    * @brief retrive agent card
    *
    * @param[in] ctx server call context
    * @return AgentCard
    */
    AgentCard OnGetCard(const std::shared_ptr<ServerCallContext> ctx) override;

    /**
    * @brief handle the stream request reveiced
    *
    * @param[in] params param containing the request information
    * @param[in] emit emiter to call to update message status
    * @param[in] ctx server call context
    */
    void OnSendMessageStreaming(const MessageSendParams& params, StreamEmitter emit,
                                const std::shared_ptr<ServerCallContext> ctx) override;

    /**
    * @brief handle resubscribe to task request
    *
    * @param[in] params param containing the task id information
    * @param[in] emit emiter to call to update message status
    * @param[in] ctx server call context
    */
    void OnResubscribeToTask(const TaskIdParams& params, StreamEmitter emit,
                            const std::shared_ptr<ServerCallContext> ctx) override;

private:
    std::shared_ptr<AgentExecutor> executor_;
    std::shared_ptr<AgentCard> agentCard_;
    std::shared_ptr<TaskStore> taskStore_;
    std::shared_ptr<TaskManager> taskManager_;
    // 只缓存流式请求的taskUpdater，用于resubscribe请求
    std::unordered_map<std::string, std::shared_ptr<TaskUpdater>> taskUpdaterMap_;
    std::shared_ptr<PushNotificationConfigStore> pushConfigStore_;
    std::shared_ptr<PushNotificationSender> pushSender_;

    std::vector<Task> GetRelatedTasksFromReferenceTaskIds(const MessageSendParams& params,
        const std::shared_ptr<ServerCallContext>& ctx);

    static std::string GenerateTaskId();

    void UpdatePushNotificationConfig(const MessageSendParams &params, const std::string& taskId) const;

    // Private methods extracted from OnSendMessage
    std::string DetermineTaskId(const MessageSendParams& params, const std::shared_ptr<ServerCallContext> ctx,
        std::shared_ptr<Task>& existingTask) const;

    void CreateNewTask(const MessageSendParams& params, const std::string& taskId, const std::string& contextId,
        const std::shared_ptr<ServerCallContext> ctx) const;

    void ExecuteAgentAndGetResult(const MessageSendParams& params,
        const std::string& taskId,
        std::shared_ptr<RequestContext> requestContext,
        StreamEmitter emit,
        const std::string& method) const;

    void SendPushNotificationIfNeeded(const std::string& taskId, const std::shared_ptr<ServerCallContext> ctx) const;

    // Private methods extracted from OnSendMessageStreaming
    void InitializeStreamingTask(const MessageSendParams& params,
        const std::shared_ptr<ServerCallContext> ctx,
        std::shared_ptr<Task>& existingTask,
        std::string& taskId) const;

    void SetupAndExecuteStreamingAgent(
        const std::string& taskId,
        std::shared_ptr<RequestContext> requestContext,
        const StreamEmitter& emit,
        const bool resubscribe);
};

} // namespace A2A::Server

#endif