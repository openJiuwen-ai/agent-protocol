/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include "request_context_impl.h"
#include "server/request_context.h"

namespace A2A::Server {

RequestContext::RequestContext(const RequestContextParam& param)
    : impl_(std::make_unique<RequestContextImpl>(param))
{
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

const std::optional<Task>& RequestContext::GetCurrentTask() const
{
    return impl_->CurrentTask();
}

void RequestContext::SetCurrentTask(const Task& t)
{
    impl_->SetCurrentTask(t);
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

std::shared_ptr<A2A::Server::ServerCallContext> RequestContext::GetCallContext() const
{
    return impl_->CallContext();
}

nlohmann::json RequestContext::GetMetadata() const
{
    return impl_->Metadata();
}
} // namespace A2A::Server