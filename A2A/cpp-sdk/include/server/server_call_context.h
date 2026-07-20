/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_SERVER_CALL_CONTEXT
#define A2A_SERVER_CALL_CONTEXT

#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace A2A::Server {

// Server-side call context analogous to Python's ServerCallContext
struct ServerCallContext {
    // Arbitrary per-request state (e.g., headers, auth info)
    std::unordered_map<std::string, nlohmann::json> state;
    // Requested and activated protocol extensions
    std::unordered_set<std::string> requestedExtensions;
    std::unordered_set<std::string> activatedExtensions;
};

} // namespace A2A::Server

#endif
