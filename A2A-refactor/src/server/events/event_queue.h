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

#include "request_handler.h"

namespace A2A::Server {

using Event = RequestHandler::StreamEvent;

class EventQueue {
public:
    explicit EventQueue(std::size_t maxSize = 1024);

    ~EventQueue() = default;

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

    bool IsEmpty() const;

private:
    std::size_t maxSize_;
    mutable std::mutex mutex_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    std::queue<Event> queue_;
    bool closed_ = false;

    mutable std::mutex cmutex_;
    std::vector<std::shared_ptr<EventQueue>> children_;
};

} // namespace A2A::Server
#endif