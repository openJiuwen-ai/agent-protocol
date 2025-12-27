/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_IN_MEMORY_QUEUE_MANAGER
#define A2A_IN_MEMORY_QUEUE_MANAGER

#include <mutex>
#include <unordered_map>

#include "server/event_queue.h"
#include "server/queue_manager.h"

namespace a2a::server {

class InMemoryQueueManager : public QueueManager {
public:
    void Add(const std::string& taskId, std::shared_ptr<EventQueue> q) override;

    std::shared_ptr<EventQueue> Get(const std::string& taskId) override;

    std::shared_ptr<EventQueue> Tap(const std::string& taskId) override;

    void Close(const std::string& taskId) override;

    std::shared_ptr<EventQueue> CreateOrTap(const std::string& taskId) override;

private:
    std::mutex m_;
    std::unordered_map<std::string, std::shared_ptr<EventQueue>> queues_;
};

} // namespace a2a::server

#endif
