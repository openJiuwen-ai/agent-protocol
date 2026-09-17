/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include "server/server_call_context.h"
#include "types.h"
#include "utils/id_generator.h"
#include "utils_message.h"
#include "jsonrpc.h"
#include "server/task_store.h"
#include "server/request_context.h"

namespace A2A::Server {
class RequestContextImpl {
public:
    explicit RequestContextImpl(const RequestContextParam& param)
        : params_(param.request),
        taskId_(param.taskId),
        contextId_(param.contextId),
        taskStore_(param.taskStore),
        relatedTasks_(param.relatedTasks),
        callContext_(param.callContext)
    {
        if (params_) {
            params_->message.taskId = taskId_;
            params_->message.contextId = contextId_;
        }
    }

    ~RequestContextImpl() = default;

    // Helpers
    std::string GetUserInput(const std::string& delimiter = "\n") const
    {
        if (!params_) {
            return {};
        }
        return GetMessageText(params_->message, delimiter);
    }

    void AttachRelatedTask(const Task& task)
    {
        relatedTasks_.push_back(task);
    }

    // Accessors
    const Message* GetMessage() const
    {
        if (!params_) {
            return nullptr;
        }
        return &params_->message;
    }

    const std::vector<Task>& RelatedTasks() const
    {
        return relatedTasks_;
    }

    std::shared_ptr<Task> CurrentTask() const
    {
        return taskId_.has_value() ? taskStore_->Get(taskId_.value(), nullptr) : nullptr;
    }

    const std::optional<std::string>& TaskId() const
    {
        return taskId_;
    }

    const std::optional<std::string>& ContextId() const
    {
        return contextId_;
    }

    std::shared_ptr<MessageSendConfiguration> Configuration() const
    {
        if (!params_ || !params_->configuration.has_value()) {
            return nullptr;
        }

        auto config = std::make_shared<MessageSendConfiguration>(params_->configuration.value());
        return config;
    }

    std::shared_ptr<ServerCallContext> CallContext() const
    {
        return callContext_;
    }

    std::string Metadata() const
    {
        if (!params_) {
            return "";
        }
        return params_->metadata.value_or("");
    }

    void AddActivatedExtension(const std::string& uri)
    {
        if (callContext_) {
            callContext_->activatedExtensions.insert(uri);
        }
    }

    std::unordered_set<std::string> RequestedExtensions() const
    {
        if (!callContext_) {
            return {};
        }
        return callContext_->requestedExtensions;
    }

private:
    std::optional<MessageSendParams> params_;
    std::optional<std::string> taskId_;
    std::optional<std::string> contextId_;
    std::shared_ptr<A2A::Server::TaskStore> taskStore_;
    std::vector<Task> relatedTasks_;
    const std::shared_ptr<ServerCallContext> callContext_{};
};


RequestContext::RequestContext(const RequestContextParam& param)
    : impl_(std::make_unique<RequestContextImpl>(param))
{
}

RequestContext::~RequestContext() {}

std::string RequestContext::GetUserInput(const std::string& delimiter) const
{
    return impl_->GetUserInput(delimiter);
}

void RequestContext::AttachRelatedTask(const Task& task)
{
    impl_->AttachRelatedTask(task);
}

const A2A::Message* RequestContext::GetMessage() const
{
    return impl_->GetMessage();
}

const std::vector<Task>& RequestContext::GetRelatedTasks() const
{
    return impl_->RelatedTasks();
}

std::shared_ptr<Task> RequestContext::GetCurrentTask() const
{
    return impl_->CurrentTask();
}

const std::optional<std::string>& RequestContext::GetTaskId() const
{
    return impl_->TaskId();
}

const std::optional<std::string>& RequestContext::GetContextId() const
{
    return impl_->ContextId();
}

std::shared_ptr<MessageSendConfiguration> RequestContext::GetConfiguration() const
{
    return impl_->Configuration();
}

std::shared_ptr<ServerCallContext> RequestContext::GetCallContext() const
{
    return impl_->CallContext();
}

std::string RequestContext::GetMetadata() const
{
    return impl_->Metadata();
}

void RequestContext::AddActivatedExtension(const std::string& uri)
{
    impl_->AddActivatedExtension(uri);
}

std::unordered_set<std::string> RequestContext::GetRequestedExtensions() const
{
    return impl_->RequestedExtensions();
}

} // namespace A2A::Server