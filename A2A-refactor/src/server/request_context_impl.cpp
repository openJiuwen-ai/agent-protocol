/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <stdexcept>

#include "utils_message.h"
#include "jsonrpc.h"
#include "id_generator.h"
#include "server/request_context.h"
#include "request_context_impl.h"

namespace A2A::Server {

// RequestContext implementation
RequestContextImpl::RequestContextImpl(const RequestContextParam& param,
                                       std::shared_ptr<IDGenerator> taskIdGenerator,
                                       std::shared_ptr<IDGenerator> contextIdGenerator)
    : params_(param.request),
      taskId_(param.taskId),
      contextId_(param.contextId),
      currentTask_(param.task),
      relatedTasks_(param.relatedTasks),
      callContext_(param.callContext)
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
    if (!params_) {
        return {};
    }
    return GetMessageText(params_->message, delimiter);
}

void RequestContextImpl::AttachRelatedTask(const A2A::Task& task)
{
    relatedTasks_.push_back(task);
}

const A2A::Message* RequestContextImpl::GetMessage() const
{
    if (!params_) {
        return nullptr;
    }
    return &params_->message;
}

std::shared_ptr<MessageSendConfiguration> RequestContextImpl::Configuration() const
{
    if (!params_ || !params_->configuration.has_value()) {
        return nullptr;
    }

    auto config = std::make_shared<MessageSendConfiguration>(params_->configuration.value());
    return config;
}

nlohmann::json RequestContextImpl::Metadata() const
{
    if (!params_) {
        return nlohmann::json::object();
    }
    return params_->metadata.value_or(nlohmann::json::object());
}

void RequestContextImpl::AddActivatedExtension(const std::string& uri)
{
    if (callContext_) {
        callContext_->activatedExtensions.insert(uri);
    }
}

std::unordered_set<std::string> RequestContextImpl::RequestedExtensions() const
{
    if (!callContext_) {
        return {};
    }
    return callContext_->requestedExtensions;
}

void RequestContextImpl::CheckOrGenerateTaskId()
{
    if (!params_) {
        return;
    }
    if (!taskId_ && !params_->message.taskId) {
        params_->message.taskId = taskIdGenerator_->Generate({.taskId = std::nullopt, .contextId = contextId_});
    }
    if (params_->message.taskId)
        taskId_ = params_->message.taskId;
}

void RequestContextImpl::CheckOrGenerateContextId()
{
    if (!params_) {
        return;
    }
    if (!contextId_ && !params_->message.contextId) {
        params_->message.contextId = contextIdGenerator_->Generate({.taskId = taskId_, .contextId = std::nullopt});
    }
    if (params_->message.contextId)
        contextId_ = params_->message.contextId;
}

} // namespace A2A::Server
