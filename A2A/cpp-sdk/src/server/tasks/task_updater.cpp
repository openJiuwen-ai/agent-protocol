/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <chrono>
#include <ctime>

#include "task_updater.h"
#include "utils/types.h"

namespace a2a::server {

TaskUpdater::TaskUpdater(std::shared_ptr<EventQueue> eventQueue, std::string taskId, std::string contextId,
                         std::shared_ptr<IDGenerator>& artifactIdGenerator,
                         std::shared_ptr<IDGenerator>& messageIdGenerator)
    : eventQueue_(std::move(eventQueue)),
      taskId_(std::move(taskId)),
      contextId_(std::move(contextId)),
      artifactIdGenerator_(artifactIdGenerator ? std::move(artifactIdGenerator) : std::make_shared<UUIDGenerator>()),
      messageIdGenerator_(messageIdGenerator ? std::move(messageIdGenerator) : std::make_shared<UUIDGenerator>())
{
}

void TaskUpdater::UpdateStatus(a2a::TaskState state, std::optional<a2a::Message> message, bool final,
                               std::optional<std::string> timestamp, std::optional<nlohmann::json> metadata)
{
    if (terminalStateReached_) {
        throw std::runtime_error("Task is already in a terminal state.");
    }
    if (state == a2a::TaskState::COMPLETED || state == a2a::TaskState::CANCELED || state == a2a::TaskState::FAILED ||
        state == a2a::TaskState::REJECTED) {
        terminalStateReached_ = true;
        final = true;
    }
    std::string ts;
    if (timestamp) {
        ts = *timestamp;
    } else {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%FT%TZ", std::gmtime(&t));
        ts = buf;
    }
    a2a::TaskStatusUpdateEvent ev{contextId_, final, "status-update", metadata, a2a::TaskStatus{message, state, ts},
                                  taskId_};
    eventQueue_->Enqueue(ev);
}

void TaskUpdater::AddArtifact(const std::vector<a2a::Part>& parts, std::optional<std::string> artifactId,
                              std::optional<std::string> name, std::optional<nlohmann::json> metadata,
                              std::optional<bool> append, std::optional<bool> last_chunk,
                              std::optional<std::vector<std::string>> extensions)
{
    std::string aid = artifactId.value_or(artifactIdGenerator_->Generate({taskId_, contextId_}));
    a2a::Artifact art;
    art.artifactId = aid;
    art.name = name;
    art.metadata = metadata;
    art.extensions = extensions;
    art.parts = parts;
    a2a::TaskArtifactUpdateEvent ev{append, art, contextId_, "artifact-update", last_chunk, std::nullopt, taskId_};
    eventQueue_->Enqueue(ev);
}

a2a::Message TaskUpdater::NewAgentMessage(const std::vector<a2a::Part>& parts, std::optional<nlohmann::json> metadata)
{
    a2a::Message msg;
    msg.role = a2a::Role::AGENT;
    msg.contextId = contextId_;
    msg.taskId = taskId_;
    msg.messageId = messageIdGenerator_->Generate({taskId_, contextId_});
    msg.metadata = metadata;
    msg.parts = parts;
    return msg;
}

void TaskUpdater::Complete(std::optional<a2a::Message> message)
{
    UpdateStatus(a2a::TaskState::COMPLETED, message, true);
}

void TaskUpdater::Failed(std::optional<a2a::Message> message)
{
    UpdateStatus(a2a::TaskState::FAILED, message, true);
}

void TaskUpdater::Reject(std::optional<a2a::Message> message)
{
    UpdateStatus(a2a::TaskState::REJECTED, message, true);
}

void TaskUpdater::Submit(std::optional<a2a::Message> message)
{
    UpdateStatus(a2a::TaskState::SUBMITTED, message);
}

void TaskUpdater::StartWork(std::optional<a2a::Message> message)
{
    UpdateStatus(a2a::TaskState::WORKING, message);
}

void TaskUpdater::Cancel(std::optional<a2a::Message> message)
{
    UpdateStatus(a2a::TaskState::CANCELED, message, true);
}

void TaskUpdater::RequiresInput(std::optional<a2a::Message> message, bool final)
{
    UpdateStatus(a2a::TaskState::INPUT_REQUIRED, message, final);
}

void TaskUpdater::RequiresAuth(std::optional<a2a::Message> message, bool final)
{
    UpdateStatus(a2a::TaskState::AUTH_REQUIRED, message, final);
}

} // namespace a2a::server
