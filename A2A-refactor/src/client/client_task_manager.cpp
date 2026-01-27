/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <optional>
#include <stdexcept>
#include <string>

#include "client_task_manager.h"

namespace A2A::Client {

Task* ClientTaskManager::GetTask()
{
    return currentTask_.has_value() ? &*currentTask_ : nullptr;
}

Task& ClientTaskManager::GetTaskOrRaise()
{
    if (!currentTask_) {
        throw std::runtime_error("no current Task");
    }

    return *currentTask_;
}


static void AppendArtifactToTask(Task& task, const TaskArtifactUpdateEvent& event)
{
    if (!task.artifacts) {
        task.artifacts = std::vector<Artifact>{};
    }

    const auto& newArtifact = event.artifact;
    const auto& id = newArtifact.artifactId;
    const bool appendParts = event.append.value_or(false);

    auto& list = *task.artifacts;
    auto it = std::find_if(list.begin(), list.end(), [&](const Artifact& a) { return a.artifactId == id; });

    if (!appendParts) {
        if (it != list.end()) {
            *it = newArtifact;
        } else {
            list.push_back(newArtifact);
        }
        return;
    }

    if (it != list.end()) {
        it->parts.insert(it->parts.end(), newArtifact.parts.begin(), newArtifact.parts.end());
    }
}

void ClientTaskManager::SaveTaskEvent(const std::variant<Task, TaskStatusUpdateEvent, TaskArtifactUpdateEvent>& ev)
{
    if (std::holds_alternative<Task>(ev)) {
        if (currentTask_) {
            throw std::runtime_error("Task is already set, create new manager for new tasks.");
        }
        auto t = std::get<Task>(ev);
        SaveTask(t);
        return;
    }

    // initialize task shell if needed
    if (!currentTask_) {
        if (std::holds_alternative<TaskStatusUpdateEvent>(ev)) {
            const auto& u = std::get<TaskStatusUpdateEvent>(ev);
            Task t;
            t.id = u.taskId;
            t.contextId = u.contextId;
            t.status = u.status;
            currentTask_ = t;
        } else {
            const auto& u = std::get<TaskArtifactUpdateEvent>(ev);
            Task t;
            t.id = u.taskId;
            t.contextId = u.contextId;
            currentTask_ = t;
        }
    }

    auto& task = *currentTask_;
    if (std::holds_alternative<TaskStatusUpdateEvent>(ev)) {
        const auto& u = std::get<TaskStatusUpdateEvent>(ev);
        if (u.status.message) {
            if (task.history) {
                task.history->push_back(*u.status.message);
            } else {
                task.history = std::vector<Message>{*u.status.message};
            }
        }
        if (u.metadata && !task.metadata) {
            task.metadata = nlohmann::json::object();
            task.metadata->update(*u.metadata);
        }
        task.status = u.status;
    } else {
        const auto& u = std::get<TaskArtifactUpdateEvent>(ev);
        AppendArtifactToTask(task, u);
    }
}

Task ClientTaskManager::UpdateWithMessage(const Message& msg, Task& task)
{
    if (task.status.message) {
        if (task.history) {
            task.history->push_back(*task.status.message);
        } else {
            task.history = std::vector<Message>{*task.status.message};
            task.status.message.reset();
        }
    }
    if (task.history) {
        task.history->push_back(msg);
    } else {
        task.history = std::vector<Message>{msg};
    }

    currentTask_ = task;
    return task;
}

void ClientTaskManager::SaveTask(const Task& task)
{
    currentTask_ = task;
}

} // namespace A2A::Client
