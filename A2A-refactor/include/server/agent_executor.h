/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_AGENT_EXECUTOR
#define A2A_AGENT_EXECUTOR

#include "server/task_updater.h"
#include "server/request_context.h"

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
    virtual void Execute(const RequestContext& context, std::shared_ptr<TaskUpdater> taskUpdater) = 0;

    /**
     * @brief cancel a task in given request context
     *
     * @param[in] context request context cintaining task information
     * @param[in] taskUpdater updater used to update events
     */
    virtual void Cancel(const RequestContext& context, std::shared_ptr<TaskUpdater> taskUpdater) = 0;
};

} // namespace A2A::Server

#endif