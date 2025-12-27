/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_REQUEST_CONTEXT_BUILDER
#define A2A_REQUEST_CONTEXT_BUILDER

#include <memory>
#include <optional>
#include <vector>

#include "server/request_context.h"
#include "server/server_call_context.h"
#include "tasks/task_store.h"
#include "utils/types.h"

namespace a2a::server {
struct RequestContextBuilder {
    virtual ~RequestContextBuilder() = default;
    virtual RequestContext Build(const std::optional<a2a::MessageSendParams>& params = std::nullopt,
                                 const std::optional<std::string>& taskId = std::nullopt,
                                 const std::optional<std::string>& contextId = std::nullopt,
                                 const std::optional<a2a::Task>& task = std::nullopt,
                                 const a2a::server::ServerCallContext* context = nullptr) = 0;
};

class SimpleRequestContextBuilder : public RequestContextBuilder {
public:
    explicit SimpleRequestContextBuilder(bool shouldPopulateReferredTasks = false,
                                         std::shared_ptr<TaskStore> taskStore = nullptr);

    RequestContext Build(const std::optional<a2a::MessageSendParams>& params = std::nullopt,
                         const std::optional<std::string>& taskId = std::nullopt,
                         const std::optional<std::string>& contextId = std::nullopt,
                         const std::optional<a2a::Task>& task = std::nullopt,
                         const a2a::server::ServerCallContext* context = nullptr) override;

private:
    bool shouldPopulate_ = false;
    std::shared_ptr<TaskStore> taskStore_;
};

} // namespace a2a::server

#endif
