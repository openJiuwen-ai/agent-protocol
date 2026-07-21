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

/**
 * @brief Parameters for publishing an artifact update.
 */
struct TaskArtifactParam {
    std::vector<Part> parts;
    std::optional<std::string> artifactId = std::nullopt;
    std::optional<std::string> name = std::nullopt;
    std::optional<std::string> metadata;
    bool append = false;
    bool lastChunk = false;
    std::vector<std::string> extensions;
};

/**
 * @brief Publishes task status and artifact events to the client.
 * @note 在 AgentExecutor::Execute 中通过此接口推送流式更新。
 */
class TaskUpdater {
public:
    /** @brief Virtual destructor. */
    virtual ~TaskUpdater() = default;

    /**
     * @brief Update the task status.
     * @param[in] state     New task state.
     * @param[in] message   Optional status message.
     * @param[in] timestamp Optional ISO timestamp.
     * @param[in] metadata  Optional metadata JSON.
     */
    virtual void UpdateStatus(TaskState state, const std::optional<Message>& message,
        const std::optional<std::string>& timestamp = std::nullopt,
        const std::optional<std::string>& metadata = std::nullopt) = 0;

    /**
     * @brief Append or publish an artifact chunk.
     * @param[in] artifactParam Artifact content and chunk flags.
     */
    virtual void AddArtifact(const TaskArtifactParam& artifactParam) = 0;

    /**
     * @brief Mark the task as completed and publish a final status update.
     * @param[in] message Optional completion message.
     */
    virtual void Complete(const std::optional<Message>& message = std::nullopt) = 0;

    /**
     * @brief Mark the task as failed and publish a final status update.
     * @param[in] message Optional failure message.
     */
    virtual void Failed(const std::optional<Message>& message = std::nullopt) = 0;

    /**
     * @brief Mark the task as rejected and publish a final status update.
     * @param[in] message Optional rejection message.
     */
    virtual void Reject(const std::optional<Message>& message = std::nullopt) = 0;

    /**
     * @brief Mark the task as submitted and publish a status update.
     * @param[in] message Optional status message.
     */
    virtual void Submit(const std::optional<Message>& message = std::nullopt) = 0;

    /**
     * @brief Mark the task as working and publish a status update.
     * @param[in] message Optional status message.
     */
    virtual void StartWork(const std::optional<Message>& message = std::nullopt) = 0;

    /**
     * @brief Mark the task as canceled and publish a status update.
     * @param[in] message Optional status message.
     */
    virtual void Cancel(const std::optional<Message>& message = std::nullopt) = 0;

    /**
     * @brief Mark the task as input-required and publish a status update.
     * @param[in] message Optional prompt message.
     */
    virtual void RequiresInput(const std::optional<Message>& message = std::nullopt) = 0;

    /**
     * @brief Mark the task as auth-required and publish a status update.
     * @param[in] message Optional auth challenge message.
     */
    virtual void RequiresAuth(const std::optional<Message>& message = std::nullopt) = 0;

    /**
     * @brief Create a new agent-role message for this task.
     * @param[in] parts    Message content parts.
     * @param[in] metadata Optional metadata JSON.
     * @return Constructed Message with agent role.
     */
    virtual A2A::Message NewAgentMessage(const std::vector<Part>& parts,
        const std::optional<std::string>& metadata = std::nullopt) = 0;

    /**
     * @brief Send a message directly to the event queue (non-task response).
     * @param[in] message Message to emit.
     */
    virtual void SendResponseMessage(const Message& message) = 0;
};

} // namespace A2A::Server

#endif
