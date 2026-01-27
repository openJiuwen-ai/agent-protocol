/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_TASK_STORE
#define A2A_TASK_STORE

#include <optional>
#include <string>
#include <memory>

#include "server/server_call_context.h"
#include "types.h"

namespace A2A::Server {

struct TaskStore {
    /**
     * @brief constructor
     */
    virtual ~TaskStore() = default;

    /**
     * @brief saves or updates a task in the store
     *
     * @param[in] task transport type
     * @param[in] context server call context
     */
    virtual void Save(const A2A::Task& task, std::shared_ptr<ServerCallContext> context) = 0;

    /**
     * @brief retrives a task from the store by ID
     *
     * @param[in] taskId task id
     * @param[in] context server call context
     */
    virtual std::optional<A2A::Task> Get(const std::string& taskId, std::shared_ptr<ServerCallContext> context) = 0;

    /**
     * @brief delete a task from the store by ID
     *
     * @param[in] taskId task id
     * @param[in] context server call context
     */
    virtual void Delete(const std::string& taskId, std::shared_ptr<ServerCallContext> context) = 0;
};

} // namespace A2A::Server

#endif
