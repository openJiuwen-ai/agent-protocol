/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_TASK_UPDATER
#define A2A_TASK_UPDATER

#include <optional>
#include <memory>
#include <string>
#include <vector>
#include <map>

#include "utils/types.h"

namespace A2A::Server {

class TaskUpdaterImpl;
class EventQueue;

using Metadata = std::map<std::string, std::string>;

struct TaskArtifactParam {
    std::vector<A2A::Part> parts;
    std::optional<std::string> artifactId = std::nullopt;
    std::optional<std::string> name = std::nullopt;
    Metadata metadata;
    bool append = false;
    bool lastChunk = false;
    std::vector<std::string> extensions;
};

class TaskUpdater {
public:
    /**
     * @brief constructor
     *
     * @param[in] eventQueue event queue
     * @param[in] taskId task id
     * @param[in] contextId context id
     */
    TaskUpdater(std::shared_ptr<A2A::Server::EventQueue> eventQueue, std::string taskId, std::string contextId);

    /**
     * @brief destructor
     */
    ~TaskUpdater();

    /**
     * @brief update task status
     *
     * @param[in] state task state
     * @param[in] message message object
     * @param[in] final whether current updation is last updation
     * @param[in] timestamp timestamp of response
     * @param[in] metadata metadata of response
     */
    void UpdateStatus(A2A::TaskState state, std::optional<A2A::Message> message = std::nullopt, bool final = false,
        std::optional<std::string> timestamp = std::nullopt, const Metadata& metadata = {});

    /**
     * @brief add artifact to task
     *
     * @param[in] artifactParam params for artifact
     */
    void AddArtifact(const TaskArtifactParam& artifactParam);

    /**
     * @brief marks the task as completed and publishes a final status update
     *
     * @param[in] message message object
     */
    void Complete(std::optional<A2A::Message> message = std::nullopt);

    /**
     * @brief marks the task as failed and publishes a final status update
     *
     * @param[in] message message object
     */
    void Failed(std::optional<A2A::Message> message = std::nullopt);

    /**
     * @brief marks the task as rejected and publishes a final status update
     *
     * @param[in] message message object
     */
    void Reject(std::optional<A2A::Message> message = std::nullopt);

    /**
     * @brief marks the task as submitted and publishes a status update
     *
     * @param[in] message message object
     */
    void Submit(std::optional<A2A::Message> message = std::nullopt);

    /**
     * @brief marks the task as working and publishes a status update
     *
     * @param[in] message message object
     */
    void StartWork(std::optional<A2A::Message> message = std::nullopt);

    /**
     * @brief marks the task as cancelled and publishes a status update
     *
     * @param[in] message message object
     */
    void Cancel(std::optional<A2A::Message> message = std::nullopt);

    /**
     * @brief marks the task as input required and publishes a status update
     *
     * @param[in] message message object
     * @param[in] final boolean flag indicating if this is the last chunk
     */
    void RequiresInput(std::optional<A2A::Message> message = std::nullopt, bool final = false);

    /**
     * @brief marks the task as auth required and publishes a status update
     *
     * @param[in] message message object
     * @param[in] final boolean flag indicating if this is the last chunk
     */
    void RequiresAuth(std::optional<A2A::Message> message = std::nullopt, bool final = false);

    /**
     * @brief create a new message object sent by the agent for this task/context
     *
     * @param[in] parts a list of 'Part' object for the message content
     * @param[in] metadata metadata for the message
     */
    A2A::Message NewAgentMessage(const std::vector<A2A::Part>& parts, const Metadata& metadata = std::nullopt);

private:
    std::unique_ptr<TaskUpdaterImpl> impl_;
};

} // namespace A2A::Server

#endif
