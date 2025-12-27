/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_EVENT_CONSUMER
#define A2A_EVENT_CONSUMER

#include <functional>
#include <memory>

#include "server/event_queue.h"

namespace a2a::server {

class EventConsumer {
public:
    explicit EventConsumer(std::shared_ptr<EventQueue> q);

    Event ConsumeOne();

    // Consume all until final event; call on_event for each
    void ConsumeAll(const std::function<void(const Event&)>& onEvent);

private:
    std::shared_ptr<EventQueue> q_;
};

} // namespace a2a::server

#endif
