/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <algorithm>

#include "uuid.h"
#include "utils_helpers.h"

namespace A2A::Server {

Task CreateTaskObj(MessageSendParams& params)
{
    if (!params.message.contextId) {
        params.message.contextId = GenerateUuid();
    }
    Task t;
    t.id = GenerateUuid();
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
    const bool appendParts = event.append.value_or(false);

    auto& list = *task.artifacts;
    auto it = std::find_if(list.begin(), list.end(), [&](const Artifact& a) { return a.artifactId == id; });

    if (!appendParts) {
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
    Part p;
    p.text = text;
    p.mediaType = "text/plain";
    Artifact a;
    a.artifactId = artifactId;
    a.parts = {p};
    return a;
}

} // namespace A2A::Server
