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
    auto it = std::find_if(list.begin(), list.end(), [&id](const Artifact& a) { return a.artifactId == id; });

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

bool IsFinalEvent(const std::variant<Task, Message, TaskArtifactUpdateEvent, TaskStatusUpdateEvent>& ev)
{
    return std::visit(
        [](auto&& x) -> bool {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, Task>) {
                auto st = x.status.state;
                return IsFinalOrInterrupted(st) || st == TaskState::UNSPECIFIED;
            } else if constexpr (std::is_same_v<T, Message>) {
                return true; // message regarded as final
            } else if constexpr (std::is_same_v<T, TaskStatusUpdateEvent>) {
                return IsFinalOrInterrupted(x.status.state);
            } else { // TaskArtifactUpdateEvent
                return false;
            }
        },
        ev);
}

// Validate helper: throws A2AServerError with message if expr is false
void ValidateOrThrow(const bool expr, const std::string& errorMessage)
{
    if (!expr) {
        throw A2AServerError(errorMessage);
    }
}

bool IsFinal(const TaskState& state)
{
    return state == TaskState::COMPLETED ||
            state == TaskState::CANCELED ||
            state == TaskState::FAILED ||
            state == TaskState::REJECTED;
}

bool IsInterrupted(const TaskState& state)
{
    return state == TaskState::INPUT_REQUIRED ||
            state == TaskState::AUTH_REQUIRED;
}

bool IsFinalOrInterrupted(const TaskState& state)
{
    return IsFinal(state) || IsInterrupted(state);
}

nlohmann::json MakeError(const std::string& id, int code, const std::string& msg)
{
    return nlohmann::json{{JSON_FIELD_JSONRPC, JSON_VERSION}, {JSON_FIELD_ID, id},
        {"error", {{"code", code}, {"message", msg}}}};
}

} // namespace A2A::Server