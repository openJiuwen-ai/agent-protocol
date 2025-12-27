/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_TASK_UPDATER
#define A2A_TASK_UPDATER

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "server/event_queue.h"
#include "utils/id_generator.h"
#include "utils/types.h"

namespace a2a::server {

class TaskUpdater {
public:
    TaskUpdater(std::shared_ptr<a2a::server::EventQueue> eventQueue, std::string taskId, std::string contextId,
                std::shared_ptr<a2a::IDGenerator>& artifactIdGenerator,
                std::shared_ptr<a2a::IDGenerator>& messageIdGenerator);

    void UpdateStatus(a2a::TaskState state, std::optional<a2a::Message> message = std::nullopt, bool final = false,
                      std::optional<std::string> timestamp = std::nullopt,
                      std::optional<nlohmann::json> metadata = std::nullopt);

    void AddArtifact(const std::vector<a2a::Part>& parts, std::optional<std::string> artifactId = std::nullopt,
                     std::optional<std::string> name = std::nullopt,
                     std::optional<nlohmann::json> metadata = std::nullopt, std::optional<bool> append = std::nullopt,
                     std::optional<bool> lastChunk = std::nullopt,
                     std::optional<std::vector<std::string>> extensions = std::nullopt);

    void Complete(std::optional<a2a::Message> message = std::nullopt);

    void Failed(std::optional<a2a::Message> message = std::nullopt);

    void Reject(std::optional<a2a::Message> message = std::nullopt);

    void Submit(std::optional<a2a::Message> message = std::nullopt);

    void StartWork(std::optional<a2a::Message> message = std::nullopt);

    void Cancel(std::optional<a2a::Message> message = std::nullopt);

    void RequiresInput(std::optional<a2a::Message> message = std::nullopt, bool final = false);

    void RequiresAuth(std::optional<a2a::Message> message = std::nullopt, bool final = false);

    a2a::Message NewAgentMessage(const std::vector<a2a::Part>& parts,
                                 std::optional<nlohmann::json> metadata = std::nullopt);

private:
    std::shared_ptr<a2a::server::EventQueue> eventQueue_;
    std::string taskId_;
    std::string contextId_;
    bool terminalStateReached_ = false;
    std::shared_ptr<a2a::IDGenerator> artifactIdGenerator_;
    std::shared_ptr<a2a::IDGenerator> messageIdGenerator_;
};

} // namespace a2a::server

#endif
