/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_SERVER_TASK_UPDATER_IMPL_H
#define A2A_SERVER_TASK_UPDATER_IMPL_H

#include <memory>
#include <string>
#include <optional>

#include "task_manager.h"
#include "id_generator.h"
#include "server/task_updater.h"

namespace A2A::Server {
class TaskUpdaterImpl : public TaskUpdater {
public:
    TaskUpdaterImpl(std::string taskId, std::string contextId, const std::shared_ptr<TaskManager>& taskManager);
    ~TaskUpdaterImpl() override = default;

    void UpdateStatus(const TaskState state,
        const std::optional<Message>& message,
        const std::optional<std::string>& timestamp,
        const std::optional<std::string>& metadata) override;

    void AddArtifact(const TaskArtifactParam& artifactParam) override;

    void Complete(const std::optional<Message>& message) override;
    void Failed(const std::optional<Message>& message) override;
    void Reject(const std::optional<Message>& message) override;
    void Submit(const std::optional<Message>& message) override;
    void StartWork(const std::optional<Message>& message) override;
    void Cancel(const std::optional<Message>& message) override;
    void RequiresInput(const std::optional<Message>& message) override;
    void RequiresAuth(const std::optional<Message>& message) override;

    Message NewAgentMessage(const std::vector<Part>& parts, const std::optional<std::string>& metadata) override;
    void SendResponseMessage(const Message& message) override;

private:
    static std::string GetCurrentTimestamp();

    std::string taskId_;
    std::string contextId_;
    std::shared_ptr<TaskManager> taskManager_;
    bool terminalStateReached_ = false;
    std::shared_ptr<IDGenerator> artifactIdGenerator_;
    std::shared_ptr<IDGenerator> messageIdGenerator_;
};

} // namespace A2A::Server

#endif // A2A_SERVER_TASK_UPDATER_IMPL_H