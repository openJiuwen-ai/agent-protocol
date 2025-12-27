/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_EVENT_QUEUE
#define A2A_EVENT_QUEUE

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>

#include "server/request_handler.h"

namespace a2a::server {

using Event = a2a::server::RequestHandler::StreamEvent;

class EventQueueImpl;

class EventQueue {
public:
    /**
     * @brief constructor
     *
     * @param[in] service credential service
     */
    explicit EventQueue(std::size_t maxSize = 1024);

    /**
     * @brief Enqueue event to this queue and all children (blocking if full)
     *
     * @param[in] ev event to enqueue
     */
    void Enqueue(const Event& ev);

    /**
     * @brief Dequeue one event
     *
     * @param[in] noWait no wait and throw std::runtime_error on empty
     * @return Event
     */
    Event Dequeue(bool noWait = false);

    /**
     * @brief finish task in queue
     */
    void TaskDone();

    /**
     * @brief  Create a child queue which will receive subsequent events
     *
     * @return event queue created
     */
    std::shared_ptr<EventQueue> Tap();

    /**
     * @brief Close queue
     *
     * @param[in] immediate if immediate, clear remaining events
     */
    void Close(bool immediate = false);

    /**
     * @brief  check whether queue is closed
     *
     * @return whether queue is closed
     */
    bool IsClosed() const;

    /**
     * @brief Clear pending events
     *
     * @param[in] clearChildren if clearChildren, events in children queue is alse cleared
     */
    void Clear(bool clearChildren = true);

private:
    std::unique_ptr<EventQueueImpl> impl_;
};

} // namespace a2a::server

#endif
