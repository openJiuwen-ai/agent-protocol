/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <functional>
#include <future>
#include <thread>

#include "events/event_consumer.h"
#include "result_aggregator.h"
#include "utils/types.h"

namespace a2a::server {

std::optional<std::variant<a2a::Task, a2a::Message>> ResultAggregator::CurrentResult()
{
    if (message_) {
        return *message_;
    }
    auto t = taskManager_->GetTask();
    if (t) {
        return *t;
    }
    return std::nullopt;
}

void ResultAggregator::ConsumeAndEmit(a2a::server::EventConsumer& consumer,
                                      const std::function<void(const a2a::server::RequestHandler::StreamEvent&)>& emit)
{
    consumer.ConsumeAll([&](const a2a::server::RequestHandler::StreamEvent& ev) {
        // process and re-emit
        std::visit(
            [&](auto&& x) {
                using T = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<T, a2a::Message>) {
                    message_ = x;
                } else {
                    taskManager_->Process(ev);
                }
            },
            ev);
        emit(ev);
    });
}

std::optional<std::variant<a2a::Task, a2a::Message>> ResultAggregator::ConsumeAll(a2a::server::EventConsumer& consumer)
{
    std::optional<std::variant<a2a::Task, a2a::Message>> result;
    consumer.ConsumeAll([&](const a2a::server::RequestHandler::StreamEvent& ev) {
        std::visit(
            [&](auto&& x) {
                using T = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<T, a2a::Message>) {
                    message_ = x;
                    result = x;
                } else {
                    taskManager_->Process(ev);
                }
            },
            ev);
    });
    if (result) {
        return result;
    }
    auto t = taskManager_->GetTask();
    if (t) {
        return *t;
    }
    return std::nullopt;
}

std::tuple<bool, std::future<void>> ResultAggregator::ConsumeAndBreakOnInterrupt(a2a::server::EventConsumer& consumer,
                                                                                 const bool blocking)
{
    // We'll use a simple approach that mimics the Python behavior:
    // - Process events until we hit a terminal state or interrupt condition
    // - Return result and whether it was interrupted

    std::optional<std::variant<a2a::Task, a2a::Message>> result;
    bool interrupted = false;

    // Process events one by one
    while (!interrupted) {
        try {
            auto event = consumer.ConsumeOne();
            std::visit(
                [&](auto&& x) {
                    using T = std::decay_t<decltype(x)>;
                    if constexpr (std::is_same_v<T, a2a::Message>) {
                        message_ = x;
                        result = x;
                    } else {
                        // Process the event with the task manager
                        taskManager_->Process(event);

                        // Check if we need to interrupt
                        if (std::holds_alternative<a2a::TaskStatusUpdateEvent>(event)) {
                            auto& status_update = std::get<a2a::TaskStatusUpdateEvent>(event);
                            if (status_update.status.state == a2a::TaskState::AUTH_REQUIRED) {
                                // Interrupt on auth_required state
                                interrupted = true;
                            }
                        } else if (std::holds_alternative<a2a::Task>(event)) {
                            auto& task = std::get<a2a::Task>(event);
                            if (task.status.state == a2a::TaskState::AUTH_REQUIRED) {
                                // Interrupt on auth_required state
                                interrupted = true;
                            }
                        }
                    }
                },
                event);

            // For non-blocking, return immediately when we have a result
            if (!blocking && result.has_value()) {
                interrupted = true;
            }

            // If we've hit a terminal state, stop processing
            if (result.has_value() && std::holds_alternative<a2a::Task>(result.value())) {
                // Check if it's a terminal state that should stop processing
                if (std::holds_alternative<a2a::Task>(result.value())) {
                    auto& task = std::get<a2a::Task>(result.value());
                    if (task.status.state == a2a::TaskState::COMPLETED ||
                        task.status.state == a2a::TaskState::CANCELED || task.status.state == a2a::TaskState::FAILED ||
                        task.status.state == a2a::TaskState::REJECTED) {
                        interrupted = true;
                    }
                }
            }
        } catch (const std::runtime_error&) {
            // Queue is empty and closed, break the loop
            break;
        }
    }

    if (!interrupted) {
        return std::make_tuple(false, std::future<void>());
    }

    // If interrupted, continue processing in background
    // Create a future that will handle the background processing
    std::future<void> background_future = std::async(std::launch::async, [this, &consumer]() {
        try {
            // This is a simplified version - in reality we would need to keep
            // a reference to the event stream, which isn't directly available
            // from our current interface
            while (true) {
                try {
                    auto event = consumer.ConsumeOne();
                    taskManager_->Process(event);
                } catch (const std::runtime_error&) {
                    // Queue is empty and closed, break the loop
                    break;
                }
            }
        } catch (...) {
            // Handle any exceptions
        }
    });
    return std::make_tuple(true, std::move(background_future));
}

} // namespace a2a::server
