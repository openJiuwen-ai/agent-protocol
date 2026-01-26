/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_CLIENT_TASK_MANAGER
#define A2A_CLIENT_TASK_MANAGER

#include <optional>
#include <string>

#include "types.h"

namespace A2A::Client {

class ClientTaskManager {
public:
    ~ClientTaskManager() = default;

    Task* GetTask();

    Task& GetTaskOrRaise();

    void SaveTaskEvent(const std::variant<Task, TaskStatusUpdateEvent, TaskArtifactUpdateEvent>& ev);

    Task UpdateWithMessage(const Message& msg, Task& task);

private:
    void SaveTask(const Task& task);

    std::optional<Task> currentTask_;
};

} // namespace A2A::Client

#endif
