/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_TASK_STORE
#define A2A_TASK_STORE

#include <string>
#include <memory>

#include "server_call_context.h"
#include "types.h"

namespace A2A::Server {

/**
 * @brief Persistence interface for A2A tasks.
 * @note 任务存储抽象；默认实现为内存存储。
 */
struct TaskStore {
    /** @brief Virtual destructor. */
    virtual ~TaskStore() = default;

    /**
     * @brief Save or update a task.
     * @param[in] task    Task to persist.
     * @param[in] context Server call context.
     */
    virtual void Save(const A2A::Task& task, std::shared_ptr<ServerCallContext> context) = 0;

    /**
     * @brief Retrieve a task by ID.
     * @param[in] taskId  Task identifier.
     * @param[in] context Server call context.
     * @return Shared pointer to the task, or nullptr if not found.
     */
    virtual std::shared_ptr<Task> Get(const std::string& taskId, std::shared_ptr<ServerCallContext> context) = 0;

    /**
     * @brief Delete a task by ID.
     * @param[in] taskId  Task identifier.
     * @param[in] context Server call context.
     */
    virtual void Delete(const std::string& taskId, std::shared_ptr<ServerCallContext> context) = 0;
};

} // namespace A2A::Server

#endif
