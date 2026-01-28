/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include "task_manager.h"
#include "errors.h"
#include "utils_helpers.h"

namespace A2A::Server {

TaskManager::TaskManager(std::optional<std::string> task_id, std::optional<std::string> contextId,
                         std::shared_ptr<TaskStore> task_store, std::optional<Message> initial_message,
                         const std::shared_ptr<ServerCallContext> context)
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

std::optional<Task> TaskManager::GetTask()
{
    if (!taskId_) {
        return std::nullopt;
    }
    if (currentTask_) {
        return *currentTask_;
    }
    if (auto taskOpt = taskStore_->Get(*taskId_, callContext_)) {
        currentTask_ = std::make_shared<Task>(std::move(*taskOpt));
        return *currentTask_;
    }
    return std::nullopt;
}

void TaskManager::SaveTaskContextId(const EventType &event)
{
    std::string eid;
    std::string ecid;
    if (std::holds_alternative<Task>(event)) {
        const auto& t = std::get<Task>(event);
        eid = t.id;
        ecid = t.contextId;
    } else if (std::holds_alternative<TaskStatusUpdateEvent>(event)) {
        const auto& e = std::get<TaskStatusUpdateEvent>(event);
        eid = e.taskId;
        ecid = e.contextId;
    } else { // TaskArtifactUpdateEvent
        const auto& e = std::get<TaskArtifactUpdateEvent>(event);
        eid = e.taskId;
        ecid = e.contextId;
    }

    if (taskId_ && !taskId_.value().empty() && *taskId_ != eid) {
        throw A2AServerError("Task in event doesn't match TaskManager " + *taskId_ + " : " + eid);
    }
    if (!taskId_) {
        taskId_ = eid;
    }
    if (contextId_ && !contextId_.value().empty() && *contextId_ != ecid) {
        throw A2AServerError("Context in event doesn't match TaskManager " + *contextId_ + " : " + ecid);
    }
    if (!contextId_) {
        contextId_ = ecid;
    }
}

void TaskManager::SaveTaskEvent(const EventType& event)
{
    // Determine task id/context id from event
    SaveTaskContextId(event);

    if (std::holds_alternative<Task>(event)) {
        const auto& t = std::get<Task>(event);
        SaveTask(t);
        return;
    }

    Task task;
    if (std::holds_alternative<TaskStatusUpdateEvent>(event)) {
        task = EnsureTask(std::variant<TaskStatusUpdateEvent, TaskArtifactUpdateEvent>{
            std::get<TaskStatusUpdateEvent>(event)});
    } else {
        task = EnsureTask(std::variant<TaskStatusUpdateEvent, TaskArtifactUpdateEvent>{
            std::get<TaskArtifactUpdateEvent>(event)});
    }

    if (std::holds_alternative<TaskStatusUpdateEvent>(event)) {
        const auto& e = std::get<TaskStatusUpdateEvent>(event);
        if (task.status.message) {
            if (task.history) {
                task.history->push_back(std::move(*task.status.message));
            } else {
                task.history = std::vector<Message>{std::move(*task.status.message)};
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
        const auto& e = std::get<TaskArtifactUpdateEvent>(event);
        AppendArtifactToTask(task, e);
    }

    SaveTask(task);
}

Task TaskManager::EnsureTask(const std::variant<TaskStatusUpdateEvent, TaskArtifactUpdateEvent>& event)
{
    if (currentTask_) {
        return *currentTask_;
    }

    // Convert to the wider variant type to reuse EnsureTaskForEvent
    if (std::holds_alternative<TaskStatusUpdateEvent>(event)) {
        auto taskVariant =
            EventType{std::get<TaskStatusUpdateEvent>(event)};
        return EnsureTaskForEvent(taskVariant);
    } else {
        auto taskVariant =
            EventType{std::get<TaskArtifactUpdateEvent>(event)};
        return EnsureTaskForEvent(taskVariant);
    }
}

std::variant<Task, Message, TaskArtifactUpdateEvent, TaskStatusUpdateEvent> TaskManager::Process(
    const std::variant<Task, Message, TaskArtifactUpdateEvent, TaskStatusUpdateEvent>& event)
{
    // If it's task-related, update and save
    if (std::holds_alternative<Task>(event)) {
        SaveTaskEvent(EventType{std::get<Task>(event)});
    } else if (std::holds_alternative<TaskStatusUpdateEvent>(event)) {
        SaveTaskEvent(EventType{std::get<TaskStatusUpdateEvent>(event)});
    } else if (std::holds_alternative<TaskArtifactUpdateEvent>(event)) {
        SaveTaskEvent(EventType{std::get<TaskArtifactUpdateEvent>(event)});
    }
    return event;
}

std::shared_ptr<Task> TaskManager::InitTaskObj(const std::string& task_id, const std::string& contextId)
{
    auto t = std::make_shared<Task>();
    t->id = task_id;
    t->contextId = contextId;
    t->status.state = TaskState::SUBMITTED;
    if (initialMessage_) {
        t->history = std::make_optional<std::vector<Message>>();
        t->history->reserve(1);
        t->history->push_back(*initialMessage_);
    }
    return t;
}

Task TaskManager::EnsureTaskForEvent(const EventType& event)
{
    if (currentTask_) {
        return *currentTask_;
    }

    if (taskId_) {
        if (auto maybe = taskStore_->Get(*taskId_, callContext_)) {
            currentTask_ = std::make_shared<Task>(std::move(*maybe));
            return *currentTask_;
        }
    }

    // Create new task from event
    std::string eid = std::holds_alternative<TaskStatusUpdateEvent>(event)
                          ? std::get<TaskStatusUpdateEvent>(event).taskId
                          : std::get<TaskArtifactUpdateEvent>(event).taskId;
    std::string ecid = std::holds_alternative<TaskStatusUpdateEvent>(event)
                           ? std::get<TaskStatusUpdateEvent>(event).contextId
                           : std::get<TaskArtifactUpdateEvent>(event).contextId;
    auto t = InitTaskObj(eid, ecid);
    SaveTask(*t);
    return *currentTask_;
}

void TaskManager::SaveTask(const Task& task)
{
    taskStore_->Save(task, callContext_);
    currentTask_ = std::make_shared<Task>(task);
    if (!taskId_) {
        taskId_ = task.id;
        contextId_ = task.contextId;
    }
}

Task TaskManager::UpdateWithMessage(const Message& message, Task task)
{
    if (task.status.message) {
        if (task.history) {
            task.history->push_back(std::move(*task.status.message));
        } else {
            task.history = std::vector<Message>{std::move(*task.status.message)};
        }
        task.status.message.reset();
    }
    if (task.history) {
        task.history->push_back(message);
    } else {
        task.history = std::vector<Message>{message};
    }
    currentTask_ = std::make_shared<Task>(std::move(task));
    return task;
}

} // namespace A2A::Server
