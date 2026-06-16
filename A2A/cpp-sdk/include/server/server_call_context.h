/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_SERVER_CALL_CONTEXT
#define A2A_SERVER_CALL_CONTEXT

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "types.h"

namespace A2A::Server {
struct TaskStore;

// Server-side call context analogous to Python's ServerCallContext
struct ServerCallContext {
    // Arbitrary per-request state (e.g., headers, auth info)
    std::unordered_map<std::string, std::string> state;
    // Requested and activated protocol extensions
    std::unordered_set<std::string> requestedExtensions;
    std::unordered_set<std::string> activatedExtensions;
};

struct RequestContextParam {
    std::optional<A2A::MessageSendParams> request = std::nullopt;
    std::optional<std::string> taskId = std::nullopt;
    std::optional<std::string> contextId = std::nullopt;
    std::shared_ptr<TaskStore> taskStore = nullptr;
    std::vector<A2A::Task> relatedTasks;
    std::shared_ptr<ServerCallContext> callContext;
};

} // namespace A2A::Server

#endif