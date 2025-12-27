/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_UTILS_HELPER
#define A2A_UTILS_HELPER

#include <functional>

#include "utils/errors.h"
#include "utils/types.h"

namespace a2a::server {

Task CreateTaskObj(MessageSendParams& params);

void AppendArtifactToTask(Task& task, const TaskArtifactUpdateEvent& event);

bool AreModalitiesCompatible(const std::optional<std::vector<std::string>>& serverOutputModes,
                             const std::optional<std::vector<std::string>>& clientOutputModes);

// Create a simple text artifact with provided id (parity with Python helpers.BuildTextArtifact)
Artifact BuildTextArtifact(const std::string& text, const std::string& artifactId);

// Validate helper: throws A2AServerError with message if expr is false
inline void ValidateOrThrow(bool expr, const std::string& errorMessage);

} // namespace a2a::server

#endif
