/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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

namespace A2A::Server {

// Request Context equivalent to Python's RequestContext
class RequestContextImpl {
public:
    RequestContextImpl(const std::optional<A2A::MessageSendParams>& request = std::nullopt,
                       const std::optional<std::string>& taskId = std::nullopt,
                       const std::optional<std::string>& contextId = std::nullopt,
                       const std::optional<A2A::Task>& task = std::nullopt,
                       const std::vector<A2A::Task>& relatedTasks = {},
                       const A2A::Server::ServerCallContext* callContext = nullptr,
                       std::shared_ptr<IDGenerator> taskIdGenerator = nullptr,
                       std::shared_ptr<IDGenerator> contextIdGenerator = nullptr);

    ~RequestContextImpl() = default;

    // Helpers
    std::string GetUserInput(const std::string& delimiter = "\n") const;
    void AttachRelatedTask(const A2A::Task& task);

    // Accessors
    const A2A::Message* Message() const;

    const std::vector<A2A::Task>& RelatedTasks() const
    {
        return relatedTasks_;
    }

    const std::optional<A2A::Task>& CurrentTask() const
    {
        return currentTask_;
    }

    void SetCurrentTask(const A2A::Task& t)
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

    const A2A::Server::ServerCallContext* CallContext() const
    {
        return callContext_;
    }

    nlohmann::json Metadata() const;

    void AddActivatedExtension(const std::string& uri);

    std::unordered_set<std::string> RequestedExtensions() const;

private:
    void CheckOrGenerateTaskId();
    void CheckOrGenerateContextId();

    std::optional<A2A::MessageSendParams> params_;
    std::optional<std::string> taskId_;
    std::optional<std::string> contextId_;
    std::optional<A2A::Task> currentTask_;
    std::vector<A2A::Task> relatedTasks_;
    const A2A::Server::ServerCallContext* callContext_{};
    std::shared_ptr<IDGenerator> taskIdGenerator_;
    std::shared_ptr<IDGenerator> contextIdGenerator_;
};

} // namespace A2A::Server
#endif
