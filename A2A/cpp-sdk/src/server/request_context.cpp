/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "request_context_impl.h"
#include "server/request_context.h"

namespace a2a::server {

RequestContext::RequestContext(const std::optional<a2a::MessageSendParams>& request,
                               const std::optional<std::string>& taskId, const std::optional<std::string>& contextId,
                               const std::optional<a2a::Task>& task, const std::vector<a2a::Task>& relatedTasks,
                               const a2a::server::ServerCallContext* callContext,
                               std::shared_ptr<IDGenerator> taskIdGenerator,
                               std::shared_ptr<IDGenerator> contextIdGenerator)
    : impl_(std::make_unique<RequestContextImpl>(request, taskId, contextId, task, relatedTasks, callContext,
                                                 taskIdGenerator, contextIdGenerator))
{
}

std::string RequestContext::GetUserInput(const std::string& delimiter) const
{
    return impl_->GetUserInput(delimiter);
}

void RequestContext::AttachRelatedTask(const a2a::Task& task)
{
    impl_->AttachRelatedTask(task);
}

const a2a::Message* RequestContext::Message() const
{
    return impl_->Message();
}

const std::vector<a2a::Task>& RequestContext::RelatedTasks() const
{
    return impl_->RelatedTasks();
}

const std::optional<a2a::Task>& RequestContext::CurrentTask() const
{
    return impl_->CurrentTask();
}

void RequestContext::SetCurrentTask(const a2a::Task& t)
{
    impl_->SetCurrentTask(t);
}

const std::optional<std::string>& RequestContext::TaskId() const
{
    return impl_->TaskId();
}

const std::optional<std::string>& RequestContext::ContextId() const
{
    return impl_->ContextId();
}

const std::optional<nlohmann::json> RequestContext::Configuration() const
{
    return impl_->Configuration();
}

const a2a::server::ServerCallContext* RequestContext::CallContext() const
{
    return impl_->CallContext();
}

nlohmann::json RequestContext::Metadata() const
{
    return impl_->Metadata();
}

void RequestContext::AddActivatedExtension(const std::string& uri)
{
    impl_->AddActivatedExtension(uri);
}

std::unordered_set<std::string> RequestContext::RequestedExtensions() const
{
    return impl_->RequestedExtensions();
}

} // namespace a2a::server