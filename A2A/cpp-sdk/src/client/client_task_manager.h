/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_CLIENT_TASK_MANAGER
#define A2A_CLIENT_TASK_MANAGER

#include <optional>
#include <string>

#include "utils/types.h"

namespace a2a::client {

class ClientTaskManager {
public:
    Task* GetTask();

    Task& GetTaskOrRaise();

    void SaveTaskEvent(const std::variant<Task, TaskStatusUpdateEvent, TaskArtifactUpdateEvent>& ev);

    Task UpdateWithMessage(const Message& msg, Task& task);

private:
    void SaveTask(const Task& task);

    std::optional<Task> currentTask_;
};

} // namespace a2a::client

#endif
