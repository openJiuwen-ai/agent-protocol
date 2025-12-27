/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_TASK_MANAGER
#define A2A_TASK_MANAGER

#include <memory>
#include <optional>
#include <string>

#include "server/server_call_context.h"
#include "task_store.h"
#include "utils/types.h"

namespace a2a::server {

class TaskManager {
public:
    TaskManager(std::optional<std::string> taskId, std::optional<std::string> contextId,
                std::shared_ptr<TaskStore> taskStore, std::optional<a2a::Message> initialMessage,
                const a2a::server::ServerCallContext* context = nullptr);

    std::optional<a2a::Task> GetTask();

    a2a::Task SaveTaskEvent(
        const std::variant<a2a::Task, a2a::TaskStatusUpdateEvent, a2a::TaskArtifactUpdateEvent>& event);

    a2a::Task EnsureTask(const std::variant<a2a::TaskStatusUpdateEvent, a2a::TaskArtifactUpdateEvent>& event);

    std::variant<a2a::Task, a2a::Message, a2a::TaskArtifactUpdateEvent, a2a::TaskStatusUpdateEvent> Process(
        const std::variant<a2a::Task, a2a::Message, a2a::TaskArtifactUpdateEvent, a2a::TaskStatusUpdateEvent>& event);

    a2a::Task UpdateWithMessage(const a2a::Message& message, a2a::Task task);

private:
    a2a::Task InitTaskObj(const std::string& taskId, const std::string& contextId);
    void SaveTask(const a2a::Task& task);

    std::optional<std::string> taskId_;
    std::optional<std::string> contextId_;
    std::shared_ptr<TaskStore> taskStore_;
    std::optional<a2a::Message> initialMessage_;
    std::optional<a2a::Task> currentTask_;
    const a2a::server::ServerCallContext* callContext_;
};

} // namespace a2a::server

#endif
