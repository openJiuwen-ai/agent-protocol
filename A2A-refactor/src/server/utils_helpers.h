/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_UTILS_HELPER
#define A2A_UTILS_HELPER

#include "errors.h"
#include "types.h"

namespace A2A::Server {

Task CreateTaskObj(MessageSendParams& params);

void AppendArtifactToTask(Task& task, const TaskArtifactUpdateEvent& event);

bool AreModalitiesCompatible(const std::optional<std::vector<std::string>>& serverOutputModes,
                             const std::optional<std::vector<std::string>>& clientOutputModes);

// Create a simple text artifact with provided id (parity with Python helpers.BuildTextArtifact)
Artifact BuildTextArtifact(const std::string& text, const std::string& artifactId);

// Validate helper: throws A2AServerError with message if expr is false
inline void ValidateOrThrow(const bool expr, const std::string& errorMessage)
{
    if (!expr) {
        throw A2AServerError(errorMessage);
    }
}

} // namespace A2A::Server

#endif
