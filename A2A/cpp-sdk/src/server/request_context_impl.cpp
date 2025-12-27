/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <stdexcept>

#include "request_context_impl.h"
#include "utils/utils_message.h"

namespace a2a::server {

// RequestContext implementation
RequestContextImpl::RequestContextImpl(const std::optional<a2a::MessageSendParams>& request,
                                       const std::optional<std::string>& taskId,
                                       const std::optional<std::string>& contextId,
                                       const std::optional<a2a::Task>& task, const std::vector<a2a::Task>& relatedTasks,
                                       const a2a::server::ServerCallContext* callContext,
                                       std::shared_ptr<IDGenerator> taskIdGenerator,
                                       std::shared_ptr<IDGenerator> contextIdGenerator)
    : params_(request),
      taskId_(taskId),
      contextId_(contextId),
      currentTask_(task),
      relatedTasks_(relatedTasks),
      callContext_(callContext)
{
    if (!taskIdGenerator) {
        taskIdGenerator_ = std::make_shared<UUIDGenerator>();
    } else {
        taskIdGenerator_ = std::move(taskIdGenerator);
    }
    if (!contextIdGenerator) {
        contextIdGenerator_ = std::make_shared<UUIDGenerator>();
    } else {
        contextIdGenerator_ = std::move(contextIdGenerator);
    }
    // If ids provided, enforce consistency; otherwise generate as needed
    if (params_) {
        if (taskId_) {
            params_->message.taskId = *taskId_;
            if (currentTask_ && currentTask_->id != *taskId_) {
                throw std::runtime_error("Invalid params: bad task id");
            }
        } else {
            CheckOrGenerateTaskId();
        }
        if (contextId_) {
            params_->message.contextId = *contextId_;
            if (currentTask_ && currentTask_->contextId != *contextId_) {
                throw std::runtime_error("Invalid params: bad context id");
            }
        } else {
            CheckOrGenerateContextId();
        }
    }
}

std::string RequestContextImpl::GetUserInput(const std::string& delimiter) const
{
    if (!params_)
        return {};
    return GetMessageText(params_->message, delimiter);
}

void RequestContextImpl::AttachRelatedTask(const a2a::Task& task)
{
    relatedTasks_.push_back(task);
}

const a2a::Message* RequestContextImpl::Message() const
{
    if (!params_)
        return nullptr;
    return &params_->message;
}

const std::optional<nlohmann::json> RequestContextImpl::Configuration() const
{
    if (!params_)
        return std::nullopt;
    return params_->configuration;
}

nlohmann::json RequestContextImpl::Metadata() const
{
    if (!params_)
        return nlohmann::json::object();
    return params_->metadata.value_or(nlohmann::json::object());
}

void RequestContextImpl::AddActivatedExtension(const std::string& uri)
{
    if (callContext_) {
        const_cast<a2a::server::ServerCallContext*>(callContext_)->activatedExtensions.insert(uri);
    }
}

std::unordered_set<std::string> RequestContextImpl::RequestedExtensions() const
{
    if (!callContext_)
        return {};
    return callContext_->requestedExtensions;
}

void RequestContextImpl::CheckOrGenerateTaskId()
{
    if (!params_)
        return;
    if (!taskId_ && !params_->message.taskId) {
        params_->message.taskId = taskIdGenerator_->Generate({.taskId = std::nullopt, .contextId = contextId_});
    }
    if (params_->message.taskId)
        taskId_ = params_->message.taskId;
}

void RequestContextImpl::CheckOrGenerateContextId()
{
    if (!params_)
        return;
    if (!contextId_ && !params_->message.contextId) {
        params_->message.contextId = contextIdGenerator_->Generate({.taskId = taskId_, .contextId = std::nullopt});
    }
    if (params_->message.contextId)
        contextId_ = params_->message.contextId;
}

} // namespace a2a::server
