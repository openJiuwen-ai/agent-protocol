/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_REQUEST_HANDLER
#define A2A_REQUEST_HANDLER

#include <functional>
#include <optional>
#include <string>
#include <variant>

#include "server/server_call_context.h"
#include "utils/types.h"

namespace a2a::server {

// Minimal in-process server handler interface
class RequestHandler {
public:
    /**
     * @brief destructor
     *
     * @param[in] service credential service
     */
    virtual ~RequestHandler() = default;

    // Streaming hooks: provide an event emitter callback for SSE-like streaming
    using StreamEvent = std::variant<Task, Message, TaskArtifactUpdateEvent, TaskStatusUpdateEvent>;
    using StreamEmitter = std::function<void(const StreamEvent&)>;

    /**
     * @brief handle the request reveiced
     *
     * @param[in] params param containing the request information
     * @param[in] ctx server call context
     * @return task or message
     */
    virtual std::variant<Task, Message> OnSendMessage(const MessageSendParams& params,
                                                      const ServerCallContext* context = nullptr) = 0;

    /**
     * @brief retrive a task by query params
     *
     * @param[in] params param containing the query information
     * @param[in] ctx server call context
     * @return task
     */
    virtual Task OnGetTask(const TaskQueryParams& params, const ServerCallContext* context = nullptr) = 0;

    /**
     * @brief cancel a task by task id params
     *
     * @param[in] params param containing task id
     * @param[in] ctx server call context
     * @return task canceled
     */
    virtual Task OnCancelTask(const TaskIdParams& params, const ServerCallContext* context = nullptr) = 0;

    /**
     * @brief set a task push notification configuration
     *
     * @param[in] cfg push notification config
     * @param[in] ctx server call context
     * @return TaskPushNotificationConfig
     */
    virtual TaskPushNotificationConfig OnSetTaskPushNotificationConfig(const TaskPushNotificationConfig& cfg,
                                                                       const ServerCallContext* context = nullptr) = 0;

    /**
     * @brief retrive a task push notification configuration of a task
     *
     * @param[in] params param containing the request information
     * @param[in] ctx server call context
     * @return TaskPushNotificationConfig
     */
    virtual TaskPushNotificationConfig OnGetTaskPushNotificationConfig(
        const GetTaskPushNotificationConfigParams& params, const ServerCallContext* context = nullptr) = 0;

    /**
     * @brief retrive all task push notification configurations of a task
     *
     * @param[in] params param containing the id information
     * @param[in] ctx server call context
     * @return vector of TaskPushNotificationConfig
     */
    virtual std::vector<TaskPushNotificationConfig> OnListTaskPushNotificationConfigs(
        const ListTaskPushNotificationConfigParams& params, const ServerCallContext* context = nullptr) = 0;

    /**
     * @brief delete a task push notification configuration of a task
     *
     * @param[in] params param containing the id information
     * @param[in] ctx server call context
     */
    virtual void OnDeleteTaskPushNotificationConfig(const DeleteTaskPushNotificationConfigParams& params,
                                                    const ServerCallContext* context = nullptr) = 0;

    /**
     * @brief handle the stream request reveiced
     *
     * @param[in] params param containing the request information
     * @param[in] emit emiter to call to update message status
     * @param[in] ctx server call context
     */
    virtual void OnSendMessageStreaming(const MessageSendParams& params, const StreamEmitter& emit,
                                        const ServerCallContext* context = nullptr) = 0;

    /**
     * @brief handle resubscribe to task request
     *
     * @param[in] params param containing the task id information
     * @param[in] emit emiter to call to update message status
     * @param[in] ctx server call context
     */
    virtual void OnResubscribeToTask(const TaskIdParams& params, const StreamEmitter& emit,
                                     const ServerCallContext* context = nullptr) = 0;

    /**
     * @brief retrive agent card
     *
     * @param[in] context server call context
     * @return AgentCard
     */
    virtual AgentCard OnGetCard(const ServerCallContext* context = nullptr) = 0;
};

} // namespace a2a::server

#endif
