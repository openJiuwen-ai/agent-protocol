/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_UTILS_HELPER
#define A2A_UTILS_HELPER

#include "common_types.h"
#include "types.h"
#include "error.h"
#include "shared/jsonrpc.h"

namespace A2A::Server {

using StreamEvent = std::variant<Task, Message, TaskArtifactUpdateEvent, TaskStatusUpdateEvent>;
using StreamEmitter = std::function<void(const StreamEvent&)>;

Task CreateTaskObj(MessageSendParams& params);

void AppendArtifactToTask(Task& task, const TaskArtifactUpdateEvent& event);

bool AreModalitiesCompatible(const std::optional<std::vector<std::string>>& serverOutputModes,
    const std::optional<std::vector<std::string>>& clientOutputModes);

// Create a simple text artifact with provided id (parity with Python helpers.BuildTextArtifact)
Artifact BuildTextArtifact(const std::string& text, const std::string& artifactId);

bool IsFinalEvent(const std::variant<Task, Message, TaskArtifactUpdateEvent, TaskStatusUpdateEvent>& ev);

// Validate helper: throws A2AServerError with message if expr is false
void ValidateOrThrow(const bool expr, const std::string& errorMessage);

bool IsFinal(const TaskState& state);

bool IsInterrupted(const TaskState& state);

bool IsFinalOrInterrupted(const TaskState& state);

nlohmann::json MakeError(const std::string& id, int code, const std::string& msg);

} // namespace A2A::Server

#endif