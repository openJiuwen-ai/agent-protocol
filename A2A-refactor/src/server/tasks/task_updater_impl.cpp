/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <ctime>
#include <sstream>
#include <set>

#include "event_queue.h"

namespace A2A::Server {

// Private implementation class
class TaskUpdaterImpl {
public:
    TaskUpdaterImpl(std::shared_ptr<EventQueue> eventQueue, std::string taskId, std::string contextId)
        : eventQueue_(std::move(eventQueue)),
        taskId_(std::move(taskId)),
        contextId_(std::move(contextId)),
        artifactIdGenerator_(std::make_shared<UUIDGenerator>()),
        messageIdGenerator_(std::make_shared<UUIDGenerator>())
    {
    }

    ~TaskUpdaterImpl() = default;

    void UpdateStatus(const TaskState state, const std::optional<Message>& message, bool final,
                      const std::optional<std::string>& timestamp, const std::optional<nlohmann::json>& metadata)
    {
        if (terminalStateReached_) {
            throw std::runtime_error("Task " + taskId_ + " is already in a terminal state.");
        }
        
        if (terminalStates_.count(state)) {
            terminalStateReached_ = true;
            final = true;
        }

        std::string ts = timestamp.value_or(GetCurrentTimestamp());
        
        TaskStatusUpdateEvent event;
        event.contextId = contextId_;
        event.final = final;
        event.metadata = metadata;
        event.status = TaskStatus{message, state, ts};
        event.taskId = taskId_;

        eventQueue_->Enqueue(event);
    }

    void AddArtifact(const TaskArtifactParam& artifactParam)
    {
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

        eventQueue_->Enqueue(event);
    }

    void Complete(std::optional<Message> message)
    {
        UpdateStatus(TaskState::COMPLETED, message, true, std::nullopt, std::nullopt);
    }

    void Failed(std::optional<Message> message)
    {
        UpdateStatus(TaskState::FAILED, message, true, std::nullopt, std::nullopt);
    }

    void Reject(std::optional<Message> message)
    {
        UpdateStatus(TaskState::REJECTED, message, true, std::nullopt, std::nullopt);
    }

    void Submit(std::optional<Message> message)
    {
        UpdateStatus(TaskState::SUBMITTED, message, false, std::nullopt, std::nullopt);
    }

    void StartWork(std::optional<Message> message)
    {
        UpdateStatus(TaskState::WORKING, message, false, std::nullopt, std::nullopt);
    }

    void Cancel(std::optional<Message> message)
    {
        UpdateStatus(TaskState::CANCELED, message, true, std::nullopt, std::nullopt);
    }

    void RequiresInput(std::optional<Message> message, bool final)
    {
        UpdateStatus(TaskState::INPUT_REQUIRED, message, final, std::nullopt, std::nullopt);
    }

    void RequiresAuth(std::optional<Message> message, bool final)
    {
        UpdateStatus(TaskState::AUTH_REQUIRED, message, final, std::nullopt, std::nullopt);
    }

    Message NewAgentMessage(const std::vector<Part>& parts, const std::optional<nlohmann::json> &metadata)
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

private:
    static std::string GetCurrentTimestamp()
    {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%SZ");
        return ss.str();
    }

    std::shared_ptr<EventQueue> eventQueue_;
    std::string taskId_;
    std::string contextId_;
    bool terminalStateReached_ = false;
    std::shared_ptr<IDGenerator> artifactIdGenerator_;
    std::shared_ptr<IDGenerator> messageIdGenerator_;
    std::set<TaskState> terminalStates_ = {
        TaskState::COMPLETED,
        TaskState::CANCELED,
        TaskState::FAILED,
        TaskState::REJECTED
    };
};

// Public interface implementation
TaskUpdater::TaskUpdater(std::shared_ptr<EventQueue> eventQueue, std::string taskId, std::string contextId,
                         std::shared_ptr<IDGenerator> artifactIdGenerator,
                         std::shared_ptr<IDGenerator> messageIdGenerator)
    : impl_(std::make_unique<TaskUpdaterImpl>(std::move(eventQueue), std::move(taskId), std::move(contextId),
                                               std::move(artifactIdGenerator), std::move(messageIdGenerator)))
{
}

TaskUpdater::~TaskUpdater() = default;

void TaskUpdater::UpdateStatus(TaskState state, std::optional<Message> message, bool final,
    std::optional<std::string> timestamp, const Metadata& metadata)
{
    impl_->UpdateStatus(state, message, final, timestamp, metadata);
}

void TaskUpdater::AddArtifact(const std::vector<Part>& parts, std::optional<std::string> artifactId,
                              std::optional<std::string> name, std::optional<nlohmann::json> metadata,
                              std::optional<bool> append, std::optional<bool> lastChunk,
                              std::optional<std::vector<std::string>> extensions)
{
    impl_->AddArtifact(parts, artifactId, name, metadata, append, lastChunk, extensions);
}

void TaskUpdater::Complete(std::optional<Message> message)
{
    impl_->Complete(message);
}

void TaskUpdater::Failed(std::optional<Message> message)
{
    impl_->Failed(message);
}

void TaskUpdater::Reject(std::optional<Message> message)
{
    impl_->Reject(message);
}

void TaskUpdater::Submit(std::optional<Message> message)
{
    impl_->Submit(message);
}

void TaskUpdater::StartWork(std::optional<Message> message)
{
    impl_->StartWork(message);
}

void TaskUpdater::Cancel(std::optional<Message> message)
{
    impl_->Cancel(message);
}

void TaskUpdater::RequiresInput(std::optional<Message> message, const bool final)
{
    impl_->RequiresInput(message, final);
}

void TaskUpdater::RequiresAuth(std::optional<Message> message, const bool final)
{
    impl_->RequiresAuth(message, final);
}

Message TaskUpdater::NewAgentMessage(const std::vector<Part>& parts, const Metadata& metadata)
{
    return impl_->NewAgentMessage(parts, metadata);
}

} // namespace A2A::Server