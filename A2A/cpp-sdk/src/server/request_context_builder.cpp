/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "request_context_builder.h"
#include "request_context_impl.h"
#include "server/request_context.h"

namespace a2a::server {

SimpleRequestContextBuilder::SimpleRequestContextBuilder(bool shouldPopulateReferredTasks,
                                                         std::shared_ptr<TaskStore> taskStore)
    : shouldPopulate_(shouldPopulateReferredTasks), taskStore_(std::move(taskStore))
{
}

RequestContext SimpleRequestContextBuilder::Build(const std::optional<a2a::MessageSendParams>& params,
                                                  const std::optional<std::string>& taskId,
                                                  const std::optional<std::string>& contextId,
                                                  const std::optional<a2a::Task>& task,
                                                  const a2a::server::ServerCallContext* context)
{
    std::vector<a2a::Task> related;
    if (taskStore_ && shouldPopulate_ && params && params->message.referenceTaskIds) {
        for (const auto& rid : *params->message.referenceTaskIds) {
            if (auto t = taskStore_->Get(rid)) {
                related.push_back(*t);
            }
        }
    }
    return RequestContext(params, taskId, contextId, task, related, context);
}

} // namespace a2a::server
