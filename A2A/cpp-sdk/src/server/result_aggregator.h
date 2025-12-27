/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
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
#include "utils/types.h"

namespace a2a::server {

class ResultAggregator {
public:
    explicit ResultAggregator(std::shared_ptr<TaskManager> taskManager) : taskManager_(std::move(taskManager))
    {
    }

    std::optional<std::variant<a2a::Task, a2a::Message>> CurrentResult();

    // Consume and re-emit events to a callback
    void ConsumeAndEmit(EventConsumer& consumer, const std::function<void(const RequestHandler::StreamEvent&)>& emit);

    // Consume all and return final result
    std::optional<std::variant<a2a::Task, a2a::Message>> ConsumeAll(EventConsumer& consumer);

    std::tuple<bool, std::future<void>> ConsumeAndBreakOnInterrupt(EventConsumer& consumer, bool blocking);

private:
    std::shared_ptr<TaskManager> taskManager_;
    std::optional<a2a::Message> message_;

    static bool ShouldInterruptOnTerminalState(const std::optional<std::variant<a2a::Task, a2a::Message>> &result);
};

} // namespace a2a::server

#endif
