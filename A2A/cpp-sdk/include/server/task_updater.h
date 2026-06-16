/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_TASK_UPDATER
#define A2A_TASK_UPDATER

#include <optional>
#include <string>
#include <vector>

#include "types.h"

namespace A2A::Server {

struct TaskArtifactParam {
    std::vector<Part> parts;
    std::optional<std::string> artifactId = std::nullopt;
    std::optional<std::string> name = std::nullopt;
    std::optional<std::string> metadata;
    bool append = false;
    bool lastChunk = false;
    std::vector<std::string> extensions;
};

class TaskUpdater {
public:
    /**
    * @brief destructor
    */
    virtual ~TaskUpdater() = default;

    /**
    * @brief update task status
    *
    * @param[in] state task state
    * @param[in] message message object
    * @param[in] timestamp timestamp of response
    * @param[in] metadata metadata of response
    */
    virtual void UpdateStatus(TaskState state, const std::optional<Message>& message,
        const std::optional<std::string>& timestamp = std::nullopt,
        const std::optional<std::string>& metadata = std::nullopt) = 0;

    /**
    * @brief add artifact to task
    *
    * @param[in] artifactParam params for artifact
    */
    virtual void AddArtifact(const TaskArtifactParam& artifactParam) = 0;

    /**
    * @brief marks the task as completed and publishes a final status update
    *
    * @param[in] message message object
    */
    virtual void Complete(const std::optional<Message>& message = std::nullopt) = 0;

    /**
    * @brief marks the task as failed and publishes a final status update
    *
    * @param[in] message message object
    */
    virtual void Failed(const std::optional<Message>& message = std::nullopt) = 0;

    /**
    * @brief marks the task as rejected and publishes a final status update
    *
    * @param[in] message message object
    */
    virtual void Reject(const std::optional<Message>& message = std::nullopt) = 0;

    /**
    * @brief marks the task as submitted and publishes a status update
    *
    * @param[in] message message object
    */
    virtual void Submit(const std::optional<Message>& message = std::nullopt) = 0;

    /**
    * @brief marks the task as working and publishes a status update
    *
    * @param[in] message message object
    */
    virtual void StartWork(const std::optional<Message>& message = std::nullopt) = 0;

    /**
    * @brief marks the task as cancelled and publishes a status update
    *
    * @param[in] message message object
    */
    virtual void Cancel(const std::optional<Message>& message = std::nullopt) = 0;

    /**
    * @brief marks the task as input required and publishes a status update
    *
    * @param[in] message message object
    */
    virtual void RequiresInput(const std::optional<Message>& message = std::nullopt) = 0;

    /**
    * @brief marks the task as auth required and publishes a status update
    *
    * @param[in] message message object
    */
    virtual void RequiresAuth(const std::optional<Message>& message = std::nullopt) = 0;

    /**
    * @brief create a new message object sent by the agent for this task/context
    *
    * @param[in] parts a list of 'Part' object for the message content
    * @param[in] metadata metadata for the message
    */
    virtual A2A::Message NewAgentMessage(const std::vector<Part>& parts,
        const std::optional<std::string>& metadata = std::nullopt) = 0;

    /**
    * @brief send a message directly to the event queue
    *
    * @param[in] message the message to send
    */
    virtual void SendResponseMessage(const Message& message) = 0;
};

} // namespace A2A::Server

#endif