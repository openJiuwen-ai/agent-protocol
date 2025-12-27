/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_AGENT_EXECUTOR
#define A2A_AGENT_EXECUTOR

#include "server/event_queue.h"
#include "server/request_context.h"

namespace a2a::server {

class AgentExecutor {
public:
    /**
     * @brief destructor
     */
    virtual ~AgentExecutor() = default;

    /**
     * @brief execute a task in given request context
     *
     * @param[in] context request context cintaining task information
     * @param[in] eventQueue event queue used to publish events
     */
    virtual void Execute(RequestContext& context, EventQueue& eventQueue) = 0;

    /**
     * @brief cancel a task in given request context
     *
     * @param[in] context request context cintaining task information
     * @param[in] eventQueue event queue used to publish events
     */
    virtual void Cancel(RequestContext& context, EventQueue& eventQueue) = 0;
};

} // namespace a2a::server

#endif