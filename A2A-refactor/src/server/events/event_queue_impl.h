/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_EVENT_QUEUE_IMPL
#define A2A_EVENT_QUEUE_IMPL

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>

#include "server/event_queue.h"

namespace A2A::Server {

class EventQueueImpl {
public:
    explicit EventQueueImpl(std::size_t maxSize = 1024);

    ~EventQueueImpl() = default;

    // Enqueue event to this queue and all children (blocking if full)
    void Enqueue(const Event& ev);

    // Dequeue one event; if no_wait, throw std::runtime_error on empty
    Event Dequeue(bool noWait = false);

    void TaskDone();

    // Create a child queue which will receive subsequent events
    std::shared_ptr<EventQueue> Tap();

    // Close queue; if immediate, clear remaining events
    void Close(bool immediate = false);

    bool IsClosed() const
    {
        return closed_;
    }

    // Clear pending events (optionally children)
    void Clear(bool clearChildren = true);

private:
    std::size_t maxSize_;
    mutable std::mutex m_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    std::queue<Event> q_;
    bool closed_ = false;

    mutable std::mutex cm_;
    std::vector<std::shared_ptr<EventQueue>> children_;
};

} // namespace A2A::Server
#endif