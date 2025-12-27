/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_TASK_STORE
#define A2A_TASK_STORE

#include <optional>
#include <string>

#include "server/server_call_context.h"
#include "utils/types.h"

namespace a2a::server {

// Agent Task Store interface (sync API in C++)
struct TaskStore {
    virtual ~TaskStore() = default;

    virtual void Save(const a2a::Task& task, const a2a::server::ServerCallContext* context = nullptr) = 0;

    virtual std::optional<a2a::Task> Get(const std::string& taskId,
                                         const a2a::server::ServerCallContext* context = nullptr) = 0;

    virtual void Delete(const std::string& taskId, const a2a::server::ServerCallContext* context = nullptr) = 0;
};

} // namespace a2a::server

#endif
