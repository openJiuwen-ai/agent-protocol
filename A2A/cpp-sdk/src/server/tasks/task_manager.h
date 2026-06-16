/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_TASK_MANAGER
#define A2A_TASK_MANAGER

#include <atomic>
#include <memory>
#include <string>
#include <mutex>

#include "server/server_call_context.h"
#include "server/task_store.h"
#include "utils_helpers.h"
#include "types.h"

namespace A2A::Server {
using EventType = std::variant<Task, TaskStatusUpdateEvent, TaskArtifactUpdateEvent>;
using EventCb = std::function<void(const StreamEvent&)>;

struct TaskExecuteInfo {
    mutable std::mutex callbackMutex;

    // nonstreaming only: whether response message is already sent
    std::atomic<bool> messageSent{false};

    // callbacks on event processing (sending response)
    std::vector<EventCb> eventCb = {};

    // call context bound to task
    std::shared_ptr<ServerCallContext> callContext;
};

class TaskManager {
public:
    explicit TaskManager(const std::shared_ptr<TaskStore>& taskStore);

    ~TaskManager() = default;

    void RegisterTask(const std::string& taskId, const std::shared_ptr<TaskExecuteInfo>& info);

    [[nodiscard]] std::shared_ptr<Task> GetTask(const std::string& taskId);

    [[nodiscard]] std::string GetContextId(const std::string& taskId) const;

    void Process(const std::string& taskId, const StreamEvent& event);

    Task UpdateWithMessage(const Message& message, Task task);

    bool ExchangeMessageSent(const std::string& taskId, bool value);

    void AddEventCallback(const std::string& taskId, EventCb callback);

    void CancelTask(const std::shared_ptr<Task>& task);

private:
    void SaveTask(const Task& task);
    void SaveTaskContextId(const EventType &event);
    void SaveTaskEvent(const EventType& event);
    Task EnsureTaskForEvent(const EventType& event);
    Task EnsureTask(const std::variant<TaskStatusUpdateEvent, TaskArtifactUpdateEvent>& event);
    void HandleError(const std::string& taskId, const std::string& message,
        int code = static_cast<int>(A2AErrorCode::JSONRPC_INTERNAL_ERROR)) const;

    std::shared_ptr<TaskStore> taskStore_;
    std::unordered_map<std::string, std::shared_ptr<TaskExecuteInfo>> taskExecuteMap_;
};

} // namespace A2A::Server

#endif