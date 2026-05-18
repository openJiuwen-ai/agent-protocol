/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_TASK_MANAGER
#define A2A_TASK_MANAGER

#include <memory>
#include <optional>
#include <string>

#include "../../../include/server/server_call_context.h"
#include "server/task_store.h"
#include "types.h"

namespace A2A::Server {
using EventType = std::variant<Task, TaskStatusUpdateEvent, TaskArtifactUpdateEvent>;

class TaskManager {
public:
    TaskManager(std::optional<std::string> taskId, std::optional<std::string> contextId,
                std::shared_ptr<TaskStore> taskStore, std::optional<Message> initialMessage,
                std::shared_ptr<ServerCallContext> context = nullptr);

    ~TaskManager() = default;

    std::optional<Task> GetTask();

    void SaveTaskEvent(const EventType& event);

    Task EnsureTask(const std::variant<TaskStatusUpdateEvent, TaskArtifactUpdateEvent>& event);

    std::variant<Task, Message, TaskArtifactUpdateEvent, TaskStatusUpdateEvent> Process(
        const std::variant<Task, Message, TaskArtifactUpdateEvent, TaskStatusUpdateEvent>& event);

    Task UpdateWithMessage(const Message& message, Task task);

private:
    std::shared_ptr<Task> InitTaskObj(const std::string& taskId, const std::string& contextId);
    void SaveTaskContextId(const EventType &event);
    void SaveTask(const Task& task);
    Task EnsureTaskForEvent(const EventType& event);

    std::optional<std::string> taskId_;
    std::optional<std::string> contextId_;
    std::shared_ptr<TaskStore> taskStore_;
    std::optional<Message> initialMessage_;
    std::shared_ptr<Task> currentTask_;
    const std::shared_ptr<ServerCallContext> callContext_;
};

} // namespace A2A::Server

#endif
