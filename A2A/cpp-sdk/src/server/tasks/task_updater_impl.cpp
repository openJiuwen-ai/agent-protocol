/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <chrono>
#include <ctime>
#include <mutex>
#include <id_generator.h>
#include <sstream>
#include <iomanip>

#include "utils_helpers.h"
#include "a2a_log.h"
#include "task_manager.h"
#include "task_updater_impl.h"

namespace A2A::Server {
TaskUpdaterImpl::TaskUpdaterImpl(std::string taskId, std::string contextId,
    const std::shared_ptr<TaskManager>& taskManager)
    : taskId_(std::move(taskId)),
    contextId_(std::move(contextId)),
    taskManager_(taskManager),
    artifactIdGenerator_(std::make_shared<UUIDGenerator>()),
    messageIdGenerator_(std::make_shared<UUIDGenerator>())
{
}

void TaskUpdaterImpl::UpdateStatus(const TaskState state,
    const std::optional<Message>& message,
    const std::optional<std::string>& timestamp,
    const std::optional<std::string>& metadata)
{
    if (terminalStateReached_) {
        A2A_LOG(A2A_LOG_LEVEL::WARN, "Task " + taskId_ + " is already in a terminal state!");
        return;
    }

    if (IsFinal(state)) {
        terminalStateReached_ = true;
    }

    std::string ts = timestamp.value_or(GetCurrentTimestamp());

    TaskStatusUpdateEvent event;
    event.contextId = contextId_;
    event.metadata = metadata;
    event.status = TaskStatus{message, state, ts};
    event.taskId = taskId_;

    if (taskManager_) {
        taskManager_->Process(taskId_, event);
    }
}

void TaskUpdaterImpl::AddArtifact(const TaskArtifactParam& artifactParam)
{
    if (terminalStateReached_) {
        A2A_LOG(A2A_LOG_LEVEL::WARN, "Task " + taskId_ + " is already in a terminal state!");
        return;
    }
    std::string aid = artifactParam.artifactId.value_or(artifactIdGenerator_->Generate({taskId_, contextId_}));

    Artifact artifact;
    artifact.artifactId = aid;
    artifact.parts = artifactParam.parts;
    artifact.name = artifactParam.name;
    artifact.metadata = artifactParam.metadata;
    artifact.extensions = artifactParam.extensions;

    TaskArtifactUpdateEvent event;
    event.artifact = artifact;
    event.contextId = contextId_;
    event.taskId = taskId_;
    event.append = artifactParam.append;
    event.lastChunk = artifactParam.lastChunk;
    event.metadata = artifactParam.metadata;

    if (taskManager_) {
        taskManager_->Process(taskId_, event);
    }
}

void TaskUpdaterImpl::Complete(const std::optional<Message>& message)
{
    UpdateStatus(TaskState::COMPLETED, message, std::nullopt, std::nullopt);
}

void TaskUpdaterImpl::Failed(const std::optional<Message>& message)
{
    UpdateStatus(TaskState::FAILED, message, std::nullopt, std::nullopt);
}

void TaskUpdaterImpl::Reject(const std::optional<Message>& message)
{
    UpdateStatus(TaskState::REJECTED, message, std::nullopt, std::nullopt);
}

void TaskUpdaterImpl::Submit(const std::optional<Message>& message)
{
    UpdateStatus(TaskState::SUBMITTED, message, std::nullopt, std::nullopt);
}

void TaskUpdaterImpl::StartWork(const std::optional<Message>& message)
{
    UpdateStatus(TaskState::WORKING, message, std::nullopt, std::nullopt);
}

void TaskUpdaterImpl::Cancel(const std::optional<Message>& message)
{
    UpdateStatus(TaskState::CANCELED, message, std::nullopt, std::nullopt);
}

void TaskUpdaterImpl::RequiresInput(const std::optional<Message>& message)
{
    UpdateStatus(TaskState::INPUT_REQUIRED, message, std::nullopt, std::nullopt);
}

void TaskUpdaterImpl::RequiresAuth(const std::optional<Message>& message)
{
    UpdateStatus(TaskState::AUTH_REQUIRED, message, std::nullopt, std::nullopt);
}

Message TaskUpdaterImpl::NewAgentMessage(const std::vector<Part>& parts, const std::optional<std::string>& metadata)
{
    Message message;
    message.parts = parts;
    message.role = Role::AGENT;
    message.messageId = messageIdGenerator_->Generate({taskId_, contextId_});
    message.taskId = taskId_;
    message.contextId = contextId_;
    message.metadata = metadata;

    return message;
}

void TaskUpdaterImpl::SendResponseMessage(const Message& message)
{
    if (taskManager_) {
        taskManager_->Process(taskId_, message);
    }
    terminalStateReached_ = true;
}

std::string TaskUpdaterImpl::GetCurrentTimestamp()
{
    auto now = std::chrono::system_clock::now();

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%S");
    // '0' character for padding & integer 3 for width
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return ss.str();
}

} // namespace A2A::Server