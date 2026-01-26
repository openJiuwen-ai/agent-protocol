/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_REQUEST_CONTEXT_BUILDER
#define A2A_REQUEST_CONTEXT_BUILDER

#include <memory>
#include <optional>

#include "server/request_context.h"
#include "server/server_call_context.h"
#include "server/task_store.h"
#include "utils/types.h"

namespace A2A::Server {
class RequestContextBuilder {
public:
    explicit RequestContextBuilder(bool shouldPopulateReferredTasks = false,
                                         std::shared_ptr<TaskStore> taskStore = nullptr);

    ~RequestContextBuilder() = default;

    RequestContext Build(const std::optional<A2A::MessageSendParams>& params = std::nullopt,
                         const std::optional<std::string>& taskId = std::nullopt,
                         const std::optional<std::string>& contextId = std::nullopt,
                         const std::optional<A2A::Task>& task = std::nullopt,
                         const ServerCallContext* context = nullptr);

private:
    bool shouldPopulate_ = false;
    std::shared_ptr<TaskStore> taskStore_;
};

} // namespace A2A::Server

#endif
