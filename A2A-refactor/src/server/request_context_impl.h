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
#include "types.h"
#include "utils/id_generator.h"
#include "server/request_context.h"

namespace A2A::Server {

// Request Context equivalent to Python's RequestContext
class RequestContextImpl {
public:
    explicit RequestContextImpl(const RequestContextParam& param,
                                std::shared_ptr<IDGenerator> taskIdGenerator = nullptr,
                                std::shared_ptr<IDGenerator> contextIdGenerator = nullptr);

    ~RequestContextImpl() = default;

    // Helpers
    std::string GetUserInput(const std::string& delimiter = "\n") const;
    void AttachRelatedTask(const Task& task);

    // Accessors
    const Message* GetMessage() const;

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

    std::shared_ptr<MessageSendConfiguration> Configuration() const;

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
