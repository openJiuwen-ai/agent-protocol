/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef EVENT_QUEUE_MANAGER
#define EVENT_QUEUE_MANAGER

#include <memory>
#include <string>

#include "server/event_queue.h"

namespace a2a::server {

class QueueManager {
public:
    virtual ~QueueManager() = default;

    virtual void Add(const std::string& taskId, std::shared_ptr<EventQueue> q) = 0;

    virtual std::shared_ptr<EventQueue> Get(const std::string& taskId) = 0;

    virtual std::shared_ptr<EventQueue> Tap(const std::string& taskId) = 0;

    virtual void Close(const std::string& taskId) = 0;

    virtual std::shared_ptr<EventQueue> CreateOrTap(const std::string& taskId) = 0;
};

} // namespace a2a::server

#endif
