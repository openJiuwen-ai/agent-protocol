/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "inmemory_task_store.h"
#include "task_store.h"

namespace a2a::server {

void InMemoryTaskStore::Save(const Task& task, const ServerCallContext* context)
{
    std::lock_guard<std::mutex> lock(m_);
    tasks_[task.id] = task;
}

std::optional<Task> InMemoryTaskStore::Get(const std::string& taskId, const ServerCallContext* context)
{
    std::lock_guard<std::mutex> lock(m_);
    auto it = tasks_.find(taskId);
    if (it != tasks_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void InMemoryTaskStore::Delete(const std::string& taskId, const ServerCallContext* context)
{
    std::lock_guard<std::mutex> lock(m_);
    tasks_.erase(taskId);
}

} // namespace a2a::server