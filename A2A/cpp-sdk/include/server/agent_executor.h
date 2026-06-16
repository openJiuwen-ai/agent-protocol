/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_AGENT_EXECUTOR
#define A2A_AGENT_EXECUTOR

#include <memory>

#include "task_updater.h"
#include "request_context.h"

namespace A2A::Server {

class AgentExecutor {
public:
    /**
    * @brief destructor
    */
    virtual ~AgentExecutor() = default;

    /**
    * @brief execute a task in given request context
    *
    * @param[in] context request context containing task information
    * @param[in] taskUpdater updater used to update events
    */
    virtual void Execute(std::shared_ptr<RequestContext> context, std::shared_ptr<TaskUpdater> taskUpdater) = 0;

    /**
    * @brief execute a task in given request context and customized jsonrpc method
    * (other than SendMessage and SendMessageStreaming)
    *
    * @param[in] context request context
    * @param[in] taskUpdater updater used to update events
    * @param[in] method Customizable jsonrpc method
    */
    virtual void Execute(std::shared_ptr<RequestContext> context, std::shared_ptr<TaskUpdater> taskUpdater,
        [[maybe_unused]] const std::string& method)
    {
        Execute(std::move(context), std::move(taskUpdater));
    }

    /**
    * @brief cancel a task in given request context
    *
    * @param[in] context request context cintaining task information
    * @param[in] taskUpdater updater used to update events
    */
    virtual void Cancel(std::shared_ptr<RequestContext> context, std::shared_ptr<TaskUpdater> taskUpdater) = 0;
};

} // namespace A2A::Server

#endif