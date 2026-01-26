/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_TASK_MANAGER
#define A2A_TASK_MANAGER

#include <memory>
#include <optional>
#include <string>

#include "server/server_call_context.h"
#include "server/task_store.h"
#include "utils/types.h"

namespace A2A::Server {

class TaskManager {
public:
    TaskManager(std::optional<std::string> taskId, std::optional<std::string> contextId,
                std::shared_ptr<TaskStore> taskStore, std::optional<A2A::Message> initialMessage,
                const A2A::Server::ServerCallContext* context = nullptr);

    ~TaskManager() = default;

    std::optional<A2A::Task> GetTask();

    A2A::Task SaveTaskEvent(
        const std::variant<A2A::Task, A2A::TaskStatusUpdateEvent, A2A::TaskArtifactUpdateEvent>& event);

    A2A::Task EnsureTask(const std::variant<A2A::TaskStatusUpdateEvent, A2A::TaskArtifactUpdateEvent>& event);

    std::variant<A2A::Task, A2A::Message, A2A::TaskArtifactUpdateEvent, A2A::TaskStatusUpdateEvent> Process(
        const std::variant<A2A::Task, A2A::Message, A2A::TaskArtifactUpdateEvent, A2A::TaskStatusUpdateEvent>& event);

    A2A::Task UpdateWithMessage(const A2A::Message& message, A2A::Task task);

private:
    A2A::Task InitTaskObj(const std::string& taskId, const std::string& contextId);
    void SaveTask(const A2A::Task& task);

    std::optional<std::string> taskId_;
    std::optional<std::string> contextId_;
    std::shared_ptr<TaskStore> taskStore_;
    std::optional<A2A::Message> initialMessage_;
    std::optional<A2A::Task> currentTask_;
    const A2A::Server::ServerCallContext* callContext_;
};

} // namespace A2A::Server

#endif
