/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_REQUEST_HANDLER
#define A2A_REQUEST_HANDLER

#include <functional>

#include "server/server_call_context.h"
#include "utils_helpers.h"
#include "types.h"

namespace A2A::Server {

// Minimal in-process server handler interface
class RequestHandler {
public:
    /**
    * @brief destructor
    *
    */
    virtual ~RequestHandler() = default;

    /**
    * @brief handle the request reveiced
    *
    * @param[in] params param containing the request information
    * @param[in] context server call context
    * @param[in] emit response callback
    */
    virtual void OnSendMessage(const MessageSendParams& params,
                                const std::shared_ptr<ServerCallContext> context,
                                StreamEmitter emit,
                                const std::string& method) = 0;

    /**
    * @brief retrive a task by query params
    *
    * @param[in] params param containing the query information
    * @param[in] ctx server call context
    * @return task
    */
    virtual Task OnGetTask(const TaskQueryParams& params, std::shared_ptr<ServerCallContext> context = nullptr) = 0;

    /**
    * @brief cancel a task by task id params
    *
    * @param[in] params param containing task id
    * @param[in] ctx server call context
    * @return task canceled
    */
    virtual Task OnCancelTask(const TaskIdParams& params, std::shared_ptr<ServerCallContext> context = nullptr) = 0;

    /**
    * @brief set a task push notification configuration
    *
    * @param[in] cfg push notification config
    * @param[in] ctx server call context
    * @return TaskPushNotificationConfig
    */
    virtual void OnSetTaskPushNotificationConfig(
        const TaskPushNotificationConfig& cfg, std::shared_ptr<ServerCallContext> context = nullptr) = 0;

    /**
    * @brief retrive a task push notification configuration of a task
    *
    * @param[in] params param containing the request information
    * @param[in] ctx server call context
    * @return TaskPushNotificationConfig
    */
    virtual TaskPushNotificationConfig OnGetTaskPushNotificationConfig(
        const GetTaskPushNotificationConfigParams& params, std::shared_ptr<ServerCallContext> context = nullptr) = 0;

    /**
    * @brief retrive all task push notification configurations of a task
    *
    * @param[in] params param containing the id information
    * @param[in] ctx server call context
    * @return vector of TaskPushNotificationConfig
    */
    virtual std::vector<TaskPushNotificationConfig> OnListTaskPushNotificationConfigs(
        const ListTaskPushNotificationConfigParams& params, std::shared_ptr<ServerCallContext> context = nullptr) = 0;

    /**
    * @brief delete a task push notification configuration of a task
    *
    * @param[in] params param containing the id information
    * @param[in] ctx server call context
    */
    virtual void OnDeleteTaskPushNotificationConfig(const DeleteTaskPushNotificationConfigParams& params,
                                                    std::shared_ptr<ServerCallContext> context = nullptr) = 0;

    /**
    * @brief handle the stream request reveiced
    *
    * @param[in] params param containing the request information
    * @param[in] emit emiter to call to update message status
    * @param[in] ctx server call context
    */
    virtual void OnSendMessageStreaming(const MessageSendParams& params, StreamEmitter emit,
                                        std::shared_ptr<ServerCallContext> context = nullptr) = 0;

    /**
    * @brief handle resubscribe to task request
    *
    * @param[in] params param containing the task id information
    * @param[in] emit emiter to call to update message status
    * @param[in] ctx server call context
    */
    virtual void OnResubscribeToTask(const TaskIdParams& params, StreamEmitter emit,
                                    std::shared_ptr<ServerCallContext> context = nullptr) = 0;

    /**
    * @brief retrive agent card
    *
    * @param[in] context server call context
    * @return AgentCard
    */
    virtual AgentCard OnGetCard(std::shared_ptr<ServerCallContext> context = nullptr) = 0;
};

} // namespace A2A::Server

#endif