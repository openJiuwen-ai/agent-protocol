/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_EVENT_CONSUMER
#define A2A_EVENT_CONSUMER

#include <functional>
#include <memory>

#include "event_queue.h"

namespace A2A::Server {

class EventConsumer {
public:
    explicit EventConsumer(std::shared_ptr<EventQueue> q);

    ~EventConsumer() = default;

    Event ConsumeOne();

    // Consume all until final event; call on_event for each
    void ConsumeAll(const std::function<void(const Event&)>& onEvent, bool skipOnEmpty = false);

    bool IsEmpty() const;

private:
    std::shared_ptr<EventQueue> queue_;
};

} // namespace A2A::Server

#endif
