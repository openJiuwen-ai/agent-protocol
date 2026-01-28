/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_REQUEST_CONTEXT_IMPL
#define A2A_REQUEST_CONTEXT_IMPL

#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "server_call_context.h"
#include "id_generator.h"
#include "types.h"

namespace A2A::Server {

// Request Context equivalent to Python's RequestContext
class RequestContextImpl {
public:
    explicit RequestContextImpl(const std::optional<MessageSendParams>& request = std::nullopt,
                                const std::optional<std::string>& taskId = std::nullopt,
                                const std::optional<std::string>& contextId = std::nullopt,
                                const std::optional<Task>& task = std::nullopt,
                                const std::vector<Task>& relatedTasks = {},
                                const std::shared_ptr<ServerCallContext>& callContext = nullptr,
                                std::shared_ptr<IDGenerator> taskIdGenerator = nullptr,
                                std::shared_ptr<IDGenerator> contextIdGenerator = nullptr);

    ~RequestContextImpl() = default;

    // Helpers
    std::string GetUserInput(const std::string& delimiter = "\n") const;
    void AttachRelatedTask(const Task& task);

    // Accessors
    const Message* Message() const;

    const std::vector<Task>& RelatedTasks() const
    {
        return relatedTasks_;
    }

    const std::optional<Task>& CurrentTask() const
    {
        return currentTask_;
    }

    void SetCurrentTask(const Task& t)
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

    std::shared_ptr<ServerCallContext> CallContext() const
    {
        return callContext_;
    }

    nlohmann::json Metadata() const;

    void AddActivatedExtension(const std::string& uri);

    std::unordered_set<std::string> RequestedExtensions() const;

private:
    void CheckOrGenerateTaskId();
    void CheckOrGenerateContextId();

    std::optional<MessageSendParams> params_;
    std::optional<std::string> taskId_;
    std::optional<std::string> contextId_;
    std::optional<Task> currentTask_;
    std::vector<Task> relatedTasks_;
    const std::shared_ptr<ServerCallContext> callContext_{};
    std::shared_ptr<IDGenerator> taskIdGenerator_;
    std::shared_ptr<IDGenerator> contextIdGenerator_;
};

} // namespace A2A::Server
#endif
