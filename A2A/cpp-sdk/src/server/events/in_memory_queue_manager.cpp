/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include "in_memory_queue_manager.h"

namespace A2A::Server {

void InMemoryQueueManager::Add(const std::string& taskId, std::shared_ptr<EventQueue> q)
{
    std::lock_guard<std::mutex> lock(mutex_);
    queues_[taskId] = q;
}

std::shared_ptr<EventQueue> InMemoryQueueManager::Get(const std::string& taskId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = queues_.find(taskId);
    if (it != queues_.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<EventQueue> InMemoryQueueManager::Tap(const std::string& taskId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = queues_.find(taskId);
    if (it != queues_.end()) {
        return it->second;
    }
    return nullptr;
}

void InMemoryQueueManager::Close(const std::string& taskId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    queues_.erase(taskId);
}

std::shared_ptr<EventQueue> InMemoryQueueManager::CreateOrTap(const std::string& taskId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = queues_.find(taskId);
    if (it != queues_.end()) {
        return it->second;
    }
    // Create a new queue if one doesn't exist
    auto new_queue = std::make_shared<EventQueue>(1024);
    queues_[taskId] = new_queue;
    return new_queue;
}

} // namespace A2A::Server