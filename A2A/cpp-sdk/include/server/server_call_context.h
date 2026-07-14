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

/** @brief Forward declaration; see task_store.h. */
struct TaskStore;

/**
 * @brief Per-request server-side call context (headers, auth, extensions).
 * @note 服务端单次请求的上下文，类比 Python ServerCallContext。
 */
struct ServerCallContext {
    /** @brief Arbitrary per-request key-value state (e.g. headers, auth info). */
    std::unordered_map<std::string, std::string> state;
    /** @brief Protocol extensions requested by the client. */
    std::unordered_set<std::string> requestedExtensions;
    /** @brief Protocol extensions activated for this request. */
    std::unordered_set<std::string> activatedExtensions;
};

/**
 * @brief Parameters used to construct a RequestContext.
 */
struct RequestContextParam {
    /** @brief Incoming message/send parameters, if any. */
    std::optional<A2A::MessageSendParams> request = std::nullopt;
    /** @brief Task ID associated with the request. */
    std::optional<std::string> taskId = std::nullopt;
    /** @brief Conversation context ID. */
    std::optional<std::string> contextId = std::nullopt;
    /** @brief Task persistence backend. */
    std::shared_ptr<TaskStore> taskStore = nullptr;
    /** @brief Tasks related to the current request. */
    std::vector<A2A::Task> relatedTasks;
    /** @brief Server call context for this request. */
    std::shared_ptr<ServerCallContext> callContext;
};

} // namespace A2A::Server

#endif
