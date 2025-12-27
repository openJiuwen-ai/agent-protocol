/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "task_manager.h"
#include "utils/errors.h"
#include "utils_helpers.h"

namespace a2a::server {

TaskManager::TaskManager(std::optional<std::string> task_id, std::optional<std::string> contextId,
                         std::shared_ptr<TaskStore> task_store, std::optional<a2a::Message> initial_message,
                         const a2a::server::ServerCallContext* context)
    : taskId_(std::move(task_id)),
      contextId_(std::move(contextId)),
      taskStore_(std::move(task_store)),
      initialMessage_(std::move(initial_message)),
      callContext_(context)
{
    if (taskId_.has_value() && taskId_->empty()) {
        throw std::invalid_argument("Task ID must be a non-empty string");
    }
}

std::optional<a2a::Task> TaskManager::GetTask()
{
    if (!taskId_) {
        return std::nullopt;
    }
    if (currentTask_) {
        return currentTask_;
    }
    currentTask_ = taskStore_->Get(*taskId_, callContext_);
    return currentTask_;
}

a2a::Task TaskManager::SaveTaskEvent(
    const std::variant<a2a::Task, a2a::TaskStatusUpdateEvent, a2a::TaskArtifactUpdateEvent>& event)
{
    // Determine task id/context id from event
    std::string eid;
    std::string ecid;
    if (std::holds_alternative<a2a::Task>(event)) {
        const auto& t = std::get<a2a::Task>(event);
        eid = t.id;
        ecid = t.contextId;
    } else if (std::holds_alternative<a2a::TaskStatusUpdateEvent>(event)) {
        const auto& e = std::get<a2a::TaskStatusUpdateEvent>(event);
        eid = e.taskId;
        ecid = e.contextId;
    } else { // TaskArtifactUpdateEvent
        const auto& e = std::get<a2a::TaskArtifactUpdateEvent>(event);
        eid = e.taskId;
        ecid = e.contextId;
    }

    if (taskId_ && !taskId_.value().empty() && *taskId_ != eid) {
        throw a2a::A2AServerError("Task in event doesn't match TaskManager " + *taskId_ + " : " + eid);
    }
    if (!taskId_) {
        taskId_ = eid;
    }
    if (contextId_ && !contextId_.value().empty() && *contextId_ != ecid) {
        throw a2a::A2AServerError("Context in event doesn't match TaskManager " + *contextId_ + " : " + ecid);
    }
    if (!contextId_) {
        contextId_ = ecid;
    }

    if (std::holds_alternative<a2a::Task>(event)) {
        const auto& t = std::get<a2a::Task>(event);
        SaveTask(t);
        return t;
    }

    a2a::Task task;
    if (std::holds_alternative<a2a::TaskStatusUpdateEvent>(event)) {
        task = EnsureTask(std::variant<a2a::TaskStatusUpdateEvent, a2a::TaskArtifactUpdateEvent>{
            std::get<a2a::TaskStatusUpdateEvent>(event)});
    } else {
        task = EnsureTask(std::variant<a2a::TaskStatusUpdateEvent, a2a::TaskArtifactUpdateEvent>{
            std::get<a2a::TaskArtifactUpdateEvent>(event)});
    }

    if (std::holds_alternative<a2a::TaskStatusUpdateEvent>(event)) {
        const auto& e = std::get<a2a::TaskStatusUpdateEvent>(event);
        if (task.status.message) {
            if (task.history) {
                task.history->push_back(*task.status.message);
            } else {
                task.history = std::vector<a2a::Message>{*task.status.message};
            }
        }
        if (e.metadata) {
            if (!task.metadata) {
                task.metadata = nlohmann::json::object();
            }
            for (auto it = e.metadata->begin(); it != e.metadata->end(); ++it) {
                (*task.metadata)[it.key()] = it.value();
            }
        }
        task.status = e.status;
    } else {
        const auto& e = std::get<a2a::TaskArtifactUpdateEvent>(event);
        AppendArtifactToTask(task, e);
    }

    SaveTask(task);
    return task;
}

a2a::Task TaskManager::EnsureTask(const std::variant<a2a::TaskStatusUpdateEvent, a2a::TaskArtifactUpdateEvent>& event)
{
    if (currentTask_) {
        return *currentTask_;
    }
    if (taskId_) {
        auto maybe = taskStore_->Get(*taskId_, callContext_);
        if (maybe) {
            currentTask_ = maybe;
            return *currentTask_;
        }
    }
    // Create new task from event
    std::string eid = std::holds_alternative<a2a::TaskStatusUpdateEvent>(event)
                          ? std::get<a2a::TaskStatusUpdateEvent>(event).taskId
                          : std::get<a2a::TaskArtifactUpdateEvent>(event).taskId;
    std::string ecid = std::holds_alternative<a2a::TaskStatusUpdateEvent>(event)
                           ? std::get<a2a::TaskStatusUpdateEvent>(event).contextId
                           : std::get<a2a::TaskArtifactUpdateEvent>(event).contextId;
    a2a::Task t = InitTaskObj(eid, ecid);
    SaveTask(t);
    return *currentTask_;
}

std::variant<a2a::Task, a2a::Message, a2a::TaskArtifactUpdateEvent, a2a::TaskStatusUpdateEvent> TaskManager::Process(
    const std::variant<a2a::Task, a2a::Message, a2a::TaskArtifactUpdateEvent, a2a::TaskStatusUpdateEvent>& event)
{
    // If it's task-related, update and save
    if (std::holds_alternative<a2a::Task>(event)) {
        SaveTaskEvent(std::variant<a2a::Task, a2a::TaskStatusUpdateEvent, a2a::TaskArtifactUpdateEvent>{
            std::get<a2a::Task>(event)});
    } else if (std::holds_alternative<a2a::TaskStatusUpdateEvent>(event)) {
        SaveTaskEvent(std::variant<a2a::Task, a2a::TaskStatusUpdateEvent, a2a::TaskArtifactUpdateEvent>{
            std::get<a2a::TaskStatusUpdateEvent>(event)});
    } else if (std::holds_alternative<a2a::TaskArtifactUpdateEvent>(event)) {
        SaveTaskEvent(std::variant<a2a::Task, a2a::TaskStatusUpdateEvent, a2a::TaskArtifactUpdateEvent>{
            std::get<a2a::TaskArtifactUpdateEvent>(event)});
    }
    return event;
}

a2a::Task TaskManager::InitTaskObj(const std::string& task_id, const std::string& contextId)
{
    a2a::Task t;
    t.id = task_id;
    t.contextId = contextId;
    t.status = a2a::TaskStatus{std::nullopt, a2a::TaskState::SUBMITTED, std::nullopt};
    if (initialMessage_) {
        t.history = std::vector<a2a::Message>{*initialMessage_};
    }
    return t;
}

void TaskManager::SaveTask(const a2a::Task& task)
{
    taskStore_->Save(task, callContext_);
    currentTask_ = task;
    if (!taskId_) {
        taskId_ = task.id;
        contextId_ = task.contextId;
    }
}

a2a::Task TaskManager::UpdateWithMessage(const a2a::Message& message, a2a::Task task)
{
    if (task.status.message) {
        if (task.history) {
            task.history->push_back(*task.status.message);
        } else {
            task.history = std::vector<a2a::Message>{*task.status.message};
        }
        task.status.message.reset();
    }
    if (task.history) {
        task.history->push_back(message);
    } else {
        task.history = std::vector<a2a::Message>{message};
    }
    currentTask_ = task;
    return task;
}

} // namespace a2a::server
