/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <algorithm>

#include "utils/uuid.h"
#include "utils_helpers.h"

namespace a2a::server {

Task CreateTaskObj(MessageSendParams& params)
{
    if (!params.message.contextId) {
        params.message.contextId = generateUuid();
    }
    Task t;
    t.id = generateUuid();
    t.contextId = *params.message.contextId;
    t.status = TaskStatus{std::nullopt, TaskState::SUBMITTED, std::nullopt};
    t.history = std::vector<Message>{params.message};
    return t;
}

void AppendArtifactToTask(Task& task, const TaskArtifactUpdateEvent& event)
{
    if (!task.artifacts) {
        task.artifacts = std::vector<Artifact>{};
    }

    const auto& new_artifact = event.artifact;
    const auto& id = new_artifact.artifactId;
    const bool append_parts = event.append.value_or(false);

    auto& list = *task.artifacts;
    auto it = std::find_if(list.begin(), list.end(), [&](const Artifact& a) { return a.artifactId == id; });

    if (!append_parts) {
        if (it != list.end()) {
            *it = new_artifact;
        } else {
            list.push_back(new_artifact);
        }
        return;
    }

    if (it != list.end()) {
        it->parts.insert(it->parts.end(), new_artifact.parts.begin(), new_artifact.parts.end());
    } else {
        // ignore chunk
    }
}

bool AreModalitiesCompatible(const std::optional<std::vector<std::string>>& serverOutputModes,
                             const std::optional<std::vector<std::string>>& clientOutputModes)
{
    if (!clientOutputModes || clientOutputModes->empty()) {
        return true;
    }
    if (!serverOutputModes || serverOutputModes->empty()) {
        return true;
    }
    for (const auto& c : *clientOutputModes) {
        if (std::find(serverOutputModes->begin(), serverOutputModes->end(), c) != serverOutputModes->end()) {
            return true;
        }
    }
    return false;
}

Artifact BuildTextArtifact(const std::string& text, const std::string& artifactId)
{
    TextPart t{.kind = "text", .metadata = std::nullopt, .text = text};
    Part p = t;
    Artifact a;
    a.artifactId = artifactId;
    a.parts = {p};
    return a;
}

inline void ValidateOrThrow(bool expr, const std::string& errorMessage)
{
    if (!expr) {
        throw A2AServerError(errorMessage);
    }
}

} // namespace a2a::server
