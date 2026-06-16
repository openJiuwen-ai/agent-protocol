/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_AGENT_EXECUTOR
#define A2A_AGENT_EXECUTOR

#include <memory>

#include "task_updater.h"
#include "request_context.h"

namespace A2A::Server {

/**
 * @brief User-implemented agent logic for task execution and cancellation.
 * @note 实现此接口以处理 message/send 与 tasks/cancel 请求。
 */
class AgentExecutor {
public:
    /** @brief Virtual destructor. */
    virtual ~AgentExecutor() = default;

    /**
     * @brief Execute a task in the given request context.
     * @param[in] context     Request context with message and task info.
     * @param[in] taskUpdater Updater for publishing status / artifact events.
     * @throws A2AServerError on unrecoverable execution errors.
     */
    virtual void Execute(std::shared_ptr<RequestContext> context, std::shared_ptr<TaskUpdater> taskUpdater) = 0;

    /**
     * @brief Execute a task for a custom JSON-RPC method (non SendMessage variants).
     * @param[in] context     Request context.
     * @param[in] taskUpdater Updater for publishing events.
     * @param[in] method      Custom JSON-RPC method name.
     * @note Default implementation delegates to Execute(context, taskUpdater).
     */
    virtual void Execute(std::shared_ptr<RequestContext> context, std::shared_ptr<TaskUpdater> taskUpdater,
        [[maybe_unused]] const std::string& method)
    {
        Execute(std::move(context), std::move(taskUpdater));
    }

    /**
     * @brief Cancel a running task.
     * @param[in] context     Request context containing task information.
     * @param[in] taskUpdater Updater for publishing cancellation status.
     * @throws A2AServerError if the task cannot be canceled.
     */
    virtual void Cancel(std::shared_ptr<RequestContext> context, std::shared_ptr<TaskUpdater> taskUpdater) = 0;
};

} // namespace A2A::Server

#endif
