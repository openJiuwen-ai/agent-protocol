/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <functional>
#include <future>
#include <memory>

#include "events/event_consumer.h"
#include "types.h"
#include "result_aggregator.h"

namespace A2A::Server {

std::optional<std::variant<Task, Message>> ResultAggregator::CurrentResult()
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

void ResultAggregator::ConsumeAndEmit(std::shared_ptr<EventConsumer> consumer,
                                      const std::function<void(const RequestHandler::StreamEvent&)>& emit)
{
    consumer->ConsumeAll([&](const RequestHandler::StreamEvent& ev) {
        // process and re-emit
        std::visit(
            [&](auto&& x) {
                using T = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<T, Message>) {
                    message_ = x;
                } else {
                    taskManager_->Process(ev);
                }
            },
            ev);
        emit(ev);
    });
}

std::optional<std::variant<Task, Message>> ResultAggregator::ConsumeAll(std::shared_ptr<EventConsumer> consumer)
{
    std::optional<std::variant<Task, Message>> result;
    consumer->ConsumeAll([&](const RequestHandler::StreamEvent& ev) {
        std::visit(
            [&](auto&& x) {
                using T = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<T, Message>) {
                    message_ = x;
                    result = x;
                } else {
                    taskManager_->Process(ev);
                }
            },
            ev);
    },
        true);
    if (result) {
        return result;
    }
    auto t = taskManager_->GetTask();
    if (t) {
        return *t;
    }
    return std::nullopt;
}

bool ResultAggregator::ShouldInterruptOnTerminalState(const std::optional<std::variant<Task, Message>>& result)
{
    if (!result.has_value()) {
        return false;
    }

    if (std::holds_alternative<Task>(result.value())) {
        auto& task = std::get<Task>(result.value());
        return task.status.state == TaskState::COMPLETED ||
               task.status.state == TaskState::CANCELED ||
               task.status.state == TaskState::FAILED ||
               task.status.state == TaskState::REJECTED;
    }

    return false;
}

void ResultAggregator::ProcessEvent(const RequestHandler::StreamEvent& event,
                                    std::optional<std::variant<Task, Message>>& result,
                                    bool& interrupted)
{
    std::visit(
        [&](auto&& x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, Message>) {
                message_ = x;
                result = x;
                return;
            }
            // Process the event with the task manager
            taskManager_->Process(event);

            // Check if we need to interrupt
            if (std::holds_alternative<TaskStatusUpdateEvent>(event)) {
                auto& status_update = std::get<TaskStatusUpdateEvent>(event);
                if (status_update.status.state == TaskState::AUTH_REQUIRED) {
                    // Interrupt on auth_required state
                    interrupted = true;
                }
            } else if (std::holds_alternative<Task>(event)) {
                auto& task = std::get<Task>(event);
                if (task.status.state == TaskState::AUTH_REQUIRED) {
                    // Interrupt on auth_required state
                    interrupted = true;
                }
            }
        },
        event);
}

std::future<void> ResultAggregator::CreateBackgroundProcessingFuture(std::shared_ptr<EventConsumer> consumer)
{
    return std::async(std::launch::async, [this, consumer]() {
        try {
            // This is a simplified version - in reality we would need to keep
            // a reference to the event stream, which isn't directly available
            // from our current interface
            while (!consumer->IsEmpty()) {
                try {
                    auto event = consumer->ConsumeOne();
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
}

std::tuple<bool, std::future<void>> ResultAggregator::ConsumeAndBreakOnInterrupt(
    std::shared_ptr<EventConsumer> consumer, const bool blocking)
{
    std::optional<std::variant<Task, Message>> result;
    bool interrupted = false;

    // Process events one by one
    while (!interrupted) {
        try {
            auto event = consumer->ConsumeOne();
            ProcessEvent(event, result, interrupted);

            // For non-blocking, return immediately when we have a result
            if (!blocking && result.has_value()) {
                interrupted = true;
            }

            // Check if we've hit a terminal state that should stop processing
            if (ShouldInterruptOnTerminalState(result)) {
                interrupted = true;
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
    std::future<void> backgroundFuture = CreateBackgroundProcessingFuture(consumer);
    return std::make_tuple(true, std::move(backgroundFuture));
}

} // namespace A2A::Server
