/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <stdexcept>
#include <variant>

#include "event_consumer.h"

A2A::Server::EventConsumer::EventConsumer(std::shared_ptr<EventQueue> q) : queue_(std::move(q))
{
}

A2A::Server::Event A2A::Server::EventConsumer::ConsumeOne()
{
    try {
        auto ev = queue_->Dequeue(true); // no_wait
        queue_->TaskDone();
        return ev;
    } catch (...) {
        throw std::runtime_error("Agent did not return any response");
    }
}

void A2A::Server::EventConsumer::ConsumeAll(const std::function<void(const Event&)>& onEvent)
{
    bool isFinal = false;
    while (!isFinal) {
        try {
            auto ev = queue_->Dequeue(false);
            queue_->TaskDone();
            is_final = std::visit(
                [](auto&& x) -> bool {
                    using T = std::decay_t<decltype(x)>;
                    if constexpr (std::is_same_v<T, A2A::Task>) {
                        auto st = x.status.state;
                        return st == A2A::TaskState::COMPLETED || st == A2A::TaskState::CANCELED ||
                               st == A2A::TaskState::FAILED || st == A2A::TaskState::REJECTED ||
                               st == A2A::TaskState::UNKNOWN || st == A2A::TaskState::INPUT_REQUIRED;
                    } else if constexpr (std::is_same_v<T, A2A::Message>) {
                        return true; // message regarded as final in Python helper
                    } else if constexpr (std::is_same_v<T, A2A::TaskStatusUpdateEvent>) {
                        return x.final.has_value() ? x.final.value() : false;
                    } else { // TaskArtifactUpdateEvent
                        return false;
                    }
                },
                ev);
            onEvent(ev);
            if (is_final) {
                queue_->Close(true);
                break;
            }
        } catch (...) {
            if (queue_->IsClosed()) {
                break;
            }
        }
    }
}

bool A2A::Server::EventConsumer::IsEmpty() const
{
    return queue_->IsEmpty();
}
