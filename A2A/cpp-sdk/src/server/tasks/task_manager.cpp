/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <nlohmann/json.hpp>

#include "utils_helpers.h"
#include "error.h"
#include "a2a_log.h"
#include "task_manager.h"

namespace A2A::Server {

TaskManager::TaskManager(const std::shared_ptr<TaskStore>& taskStore) : taskStore_(taskStore) {}

void TaskManager::RegisterTask(const std::string& taskId, const std::shared_ptr<TaskExecuteInfo>& info)
{
    if (taskId.empty()) {
        throw A2AServerError("Task ID must be a non-empty string");
    }
    taskExecuteMap_[taskId] = info;
}

std::shared_ptr<Task> TaskManager::GetTask(const std::string& taskId)
{
    auto it = taskExecuteMap_.find(taskId);
    if (it != taskExecuteMap_.end() && it->second != nullptr) {
        return taskStore_->Get(taskId, it->second->callContext);
    }
    return taskStore_->Get(taskId, nullptr);
}

std::string TaskManager::GetContextId(const std::string& taskId) const
{
    if (const auto task = taskStore_->Get(taskId, nullptr); task != nullptr) {
        return task->contextId;
    }
    return "";
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

    if (auto it = taskExecuteMap_.find(eid); it == taskExecuteMap_.end() || it->second == nullptr) {
        HandleError(eid, "Task in event has not been registered to task manager");
        return;
    }

    auto task = GetTask(eid);
    if (task && !task->contextId.empty() && task->contextId != ecid) {
        HandleError(eid, "Context in event doesn't match TaskManager " + task->contextId + " : " + ecid);
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
        if (!task.metadata) {
            task.metadata = e.metadata;
        } else if (e.metadata) {
            nlohmann::json merged = *task.metadata;
            for (auto it = e.metadata->begin(); it != e.metadata->end(); ++it) {
                merged[it.key()] = it.value();
            }
            task.metadata = merged;
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
    // Convert to the wider variant type to reuse EnsureTaskForEvent
    if (std::holds_alternative<TaskStatusUpdateEvent>(event)) {
        auto taskVariant =
            EventType{std::get<TaskStatusUpdateEvent>(event)};
        return EnsureTaskForEvent(taskVariant);
    }
    auto taskVariant = EventType{std::get<TaskArtifactUpdateEvent>(event)};
    return EnsureTaskForEvent(taskVariant);
}

void TaskManager::Process(const std::string& taskId, const StreamEvent& event)
{
    auto it = taskExecuteMap_.find(taskId);
    if (it == taskExecuteMap_.end() || it->second == nullptr) {
        HandleError(taskId, "Process task failed, task id has expired or is not registered to task manager");
        return;
    }
    auto info = it->second;

    std::lock_guard<std::mutex> lock(info->callbackMutex);

    // If it's task-related, update and save
    if (std::holds_alternative<Task>(event)) {
        SaveTaskEvent(EventType{std::get<Task>(event)});
    } else if (std::holds_alternative<TaskStatusUpdateEvent>(event)) {
        SaveTaskEvent(EventType{std::get<TaskStatusUpdateEvent>(event)});
    } else if (std::holds_alternative<TaskArtifactUpdateEvent>(event)) {
        SaveTaskEvent(EventType{std::get<TaskArtifactUpdateEvent>(event)});
    }

    for (const auto& callback : info->eventCb) {
        if (callback) {
            callback(event);
        } else {
            A2A_LOG(A2A_LOG_LEVEL_WARN,
                "Transport callback is null or has expired, will not send response, task id: " +
                taskId);
        }
    }
    if (IsFinalEvent(event)) {
        taskExecuteMap_.erase(taskId);
    }
}

Task TaskManager::EnsureTaskForEvent(const EventType& event)
{
    // Create new task from event
    std::string eid = std::holds_alternative<TaskStatusUpdateEvent>(event)
                            ? std::get<TaskStatusUpdateEvent>(event).taskId
                            : std::get<TaskArtifactUpdateEvent>(event).taskId;
    if (auto taskObj = taskStore_->Get(eid, nullptr); taskObj != nullptr) {
        return *taskObj;
    }

    Task t;
    t.id = eid;
    t.contextId = std::holds_alternative<TaskStatusUpdateEvent>(event)
        ? std::get<TaskStatusUpdateEvent>(event).contextId
        : std::get<TaskArtifactUpdateEvent>(event).contextId;
    t.status.state = TaskState::SUBMITTED;
    RegisterTask(eid, {});
    SaveTask(t);
    return t;
}

void TaskManager::SaveTask(const Task& task)
{
    auto it = taskExecuteMap_.find(task.id);
    if (it != taskExecuteMap_.end() && it->second != nullptr) {
        taskStore_->Save(task, it->second->callContext);
        return;
    }
    taskStore_->Save(task, nullptr);
}

Task TaskManager::UpdateWithMessage(const Message& message, Task task)
{
    auto it = taskExecuteMap_.find(task.id);
    if (it != taskExecuteMap_.end() && it->second != nullptr) {
        std::lock_guard<std::mutex> lock(it->second->callbackMutex);
    }
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
    SaveTask(task);
    return task;
}

bool TaskManager::ExchangeMessageSent(const std::string& taskId, bool value)
{
    auto it = taskExecuteMap_.find(taskId);
    if (it != taskExecuteMap_.end() && it->second != nullptr) {
        return it->second->messageSent.exchange(value);
    }
    return false;
}

void TaskManager::AddEventCallback(const std::string& taskId, EventCb callback)
{
    auto it = taskExecuteMap_.find(taskId);
    if (it != taskExecuteMap_.end() && it->second != nullptr) {
        std::lock_guard<std::mutex> lock(it->second->callbackMutex);
        it->second->eventCb.emplace_back(std::move(callback));
    }
}

void TaskManager::CancelTask(const std::shared_ptr<Task>& task)
{
    if (IsFinal(task->status.state)) {
        // Agent may have already cancelled this task
        A2A_LOG(A2A_LOG_LEVEL_DEBUG, "Task already canceled by agent, task id: " + task->id);
        return;
    }

    auto it = taskExecuteMap_.find(task->id);
    if (it == taskExecuteMap_.end() || it->second == nullptr) {
        // No agent is actively working on this task (input required or auth required)
        task->status.state = TaskState::CANCELED;
        return;
    }

    // Agent is actively working on this task, need to stop all streaming responses
    std::lock_guard<std::mutex> lock(it->second->callbackMutex);
    TaskStatusUpdateEvent event;
    event.contextId = task->contextId;
    event.status = TaskStatus{std::nullopt, TaskState::CANCELED, std::nullopt};
    event.taskId = task->id;
    task->status.state = TaskState::CANCELED;
    for (const auto& callback : it->second->eventCb) {
        if (callback) {
            callback(event);
        } else {
            A2A_LOG(A2A_LOG_LEVEL_WARN,
                "Transport callback is null or has expired, will not notify task cancellation, task id: " + task->id);
        }
    }
    taskExecuteMap_.erase(task->id);
}

void TaskManager::HandleError(const std::string& taskId, const std::string& message, int code) const
{
    A2A_LOG(A2A_LOG_LEVEL_ERROR, "Process task failed, task id: " + taskId +
        ", error msg: " + message +
        ", error code: " + std::to_string(code));
}
} // namespace A2A::Server