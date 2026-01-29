/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_RESULT_AGGREGATOR
#define A2A_RESULT_AGGREGATOR

#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <variant>

#include "events/event_consumer.h"
#include "tasks/task_manager.h"
#include "types.h"

namespace A2A::Server {

class ResultAggregator {
public:
    explicit ResultAggregator(std::shared_ptr<TaskManager> taskManager) : taskManager_(std::move(taskManager))
    {
    }

    ~ResultAggregator() = default;

    std::optional<std::variant<A2A::Task, A2A::Message>> CurrentResult();

    // Consume and re-emit events to a callback
    void ConsumeAndEmit(std::shared_ptr<EventConsumer> consumer,
        const std::function<void(const RequestHandler::StreamEvent&)>& emit);

    // Consume all and return final result
    std::optional<std::variant<A2A::Task, A2A::Message>> ConsumeAll(std::shared_ptr<EventConsumer> consumer);

    std::tuple<bool, std::future<void>> ConsumeAndBreakOnInterrupt(std::shared_ptr<EventConsumer> consumer,
        bool blocking);

private:
    std::shared_ptr<TaskManager> taskManager_;
    std::optional<A2A::Message> message_;

    static bool ShouldInterruptOnTerminalState(const std::optional<std::variant<A2A::Task, A2A::Message>> &result);
    void ProcessEvent(const RequestHandler::StreamEvent& event,
                      std::optional<std::variant<Task, Message>>& result,
                      bool& interrupted);
    std::future<void> CreateBackgroundProcessingFuture(std::shared_ptr<EventConsumer> consumer);
};

} // namespace A2A::Server

#endif
