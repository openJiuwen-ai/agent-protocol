/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include "inmemory_task_store.h"
#include "server/task_store.h"

namespace A2A::Server {

void InMemoryTaskStore::Save(const Task& task, [[maybe_unused]] const std::shared_ptr<ServerCallContext> context)
{
    std::lock_guard<std::mutex> lock(mutex_);
    tasks_[task.id] = std::make_shared<Task>(task);
}

std::shared_ptr<Task> InMemoryTaskStore::Get(const std::string& taskId,
    [[maybe_unused]] std::shared_ptr<ServerCallContext> context)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(taskId);
    if (it != tasks_.end()) {
        return it->second;
    }
    return nullptr;
}

void InMemoryTaskStore::Delete(const std::string& taskId,
    [[maybe_unused]] const std::shared_ptr<ServerCallContext> context)
{
    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.erase(taskId);
}

} // namespace A2A::Server