/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_REQUEST_CONTEXT_IMPL
#define A2A_REQUEST_CONTEXT_IMPL

#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "server/server_call_context.h"
#include "utils/id_generator.h"
#include "utils/types.h"

namespace a2a::server {

// Request Context equivalent to Python's RequestContext
class RequestContextImpl {
public:
    RequestContextImpl(const std::optional<a2a::MessageSendParams>& request = std::nullopt,
                       const std::optional<std::string>& taskId = std::nullopt,
                       const std::optional<std::string>& contextId = std::nullopt,
                       const std::optional<a2a::Task>& task = std::nullopt,
                       const std::vector<a2a::Task>& relatedTasks = {},
                       const a2a::server::ServerCallContext* callContext = nullptr,
                       std::shared_ptr<IDGenerator> taskIdGenerator = nullptr,
                       std::shared_ptr<IDGenerator> contextIdGenerator = nullptr);

    // Helpers
    std::string GetUserInput(const std::string& delimiter = "\n") const;
    void AttachRelatedTask(const a2a::Task& task);

    // Accessors
    const a2a::Message* Message() const;

    const std::vector<a2a::Task>& RelatedTasks() const
    {
        return relatedTasks_;
    }

    const std::optional<a2a::Task>& CurrentTask() const
    {
        return currentTask_;
    }

    void SetCurrentTask(const a2a::Task& t)
    {
        currentTask_ = t;
    }

    const std::optional<std::string>& TaskId() const
    {
        return taskId_;
    }

    const std::optional<std::string>& ContextId() const
    {
        return contextId_;
    }

    const std::optional<nlohmann::json> Configuration() const;

    const a2a::server::ServerCallContext* CallContext() const
    {
        return callContext_;
    }

    nlohmann::json Metadata() const;

    void AddActivatedExtension(const std::string& uri);

    std::unordered_set<std::string> RequestedExtensions() const;

private:
    void CheckOrGenerateTaskId();
    void CheckOrGenerateContextId();

    std::optional<a2a::MessageSendParams> params_;
    std::optional<std::string> taskId_;
    std::optional<std::string> contextId_;
    std::optional<a2a::Task> currentTask_;
    std::vector<a2a::Task> relatedTasks_;
    const a2a::server::ServerCallContext* callContext_{};
    std::shared_ptr<IDGenerator> taskIdGenerator_;
    std::shared_ptr<IDGenerator> contextIdGenerator_;
};

} // namespace a2a::server
#endif
