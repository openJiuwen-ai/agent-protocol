/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <stdexcept>
#include <variant>

#include "event_consumer.h"

a2a::server::EventConsumer::EventConsumer(std::shared_ptr<EventQueue> q) : q_(std::move(q))
{
}

a2a::server::Event a2a::server::EventConsumer::ConsumeOne()
{
    try {
        auto ev = q_->Dequeue(true); // no_wait
        q_->TaskDone();
        return ev;
    } catch (...) {
        throw std::runtime_error("Agent did not return any response");
    }
}

void a2a::server::EventConsumer::ConsumeAll(const std::function<void(const Event&)>& on_event)
{
    while (true) {
        try {
            auto ev = q_->Dequeue(false);
            q_->TaskDone();
            bool is_final = std::visit(
                [](auto&& x) -> bool {
                    using T = std::decay_t<decltype(x)>;
                    if constexpr (std::is_same_v<T, a2a::Task>) {
                        auto st = x.status.state;
                        return st == a2a::TaskState::COMPLETED || st == a2a::TaskState::CANCELED ||
                               st == a2a::TaskState::FAILED || st == a2a::TaskState::REJECTED ||
                               st == a2a::TaskState::UNKNOWN || st == a2a::TaskState::INPUT_REQUIRED;
                    } else if constexpr (std::is_same_v<T, a2a::Message>) {
                        return true; // message regarded as final in Python helper
                    } else if constexpr (std::is_same_v<T, a2a::TaskStatusUpdateEvent>) {
                        return x.final;
                    } else { // TaskArtifactUpdateEvent
                        return false;
                    }
                },
                ev);
            on_event(ev);
            if (is_final) {
                q_->Close(true);
                break;
            }
        } catch (...) {
            if (q_->IsClosed())
                break;
            else
                continue;
        }
    }
}
