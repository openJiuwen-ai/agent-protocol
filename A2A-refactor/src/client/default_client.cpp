/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <nlohmann/json.hpp>

#include "client_task_manager.h"
#include "utils/errors.h"
#include "utils/uuid.h"
#include "default_client.h"

namespace A2A::Client {

DefaultClient::DefaultClient(const A2A::AgentCard& card, const ClientConfig& config,
    std::shared_ptr<ClientTransport> transport, const std::vector<Consumer>& consumers,
    const std::vector<std::shared_ptr<ClientCallInterceptor>>& middleware)
    : card_(card),
      config_(config),
      transport_(transport),
      consumers_(consumers),
      middleware_(middleware)
{
    transport_->SetTransportCallback([this](const std::string& requestId, const TrasnportEvent& event) {
        this->TransportEventCb(requestId, event);
    });
}

DefaultClient::~DefaultClient() = default;

void DefaultClient::SendMessage(const Message& msg, const ClientCallContext* context, ResponseHandler handler)
{
    MessageSendParams params;
    params.message = msg;
    if (!config_.acceptedOutputModes.empty()) {
        params.configuration.acceptedOutputModes = config_.acceptedOutputModes;
    }

    params.configuration.blocking = !config_.polling;
    if (!config_.pushNotificationConfigs.empty()) {
        params.configuration.pushNotificationConfig = config_.pushNotificationConfigs.front();
    }

    std::shared_ptr<CallbackInfo> info = std::make_shared<CallbackInfo>();
    info->handler = handler;
    info->requestId = generateUuid();
    callbackInfo_[info->requestId] = info;
    const bool canStream = config_.streaming && card_.capabilities.streaming.value_or(false);
    if (!canStream) {
        auto result = transport_->SendMessage(info->requestId, params, context);
        return;
    }

    info->mgr = std::make_shared<ClientTaskManager>();
    transport_->SendMessageStreaming(requestId, params, context);
}

std::future<Task> DefaultClient::GetTask(const TaskQueryParams& params, const ClientCallContext* context)
{
    std::shared_ptr<CallbackInfo> info = std::make_shared<CallbackInfo>();
    info->promise = std::make_shared<std::promise<Task>>();
    info->requestId = generateUuid();
    callbackInfo_[info->requestId] = info;
    transport_->GetTask(info->requestId, params, context);
    return promise->get_future();
}

std::future<Task> DefaultClient::CancelTask(const TaskIdParams& params, const ClientCallContext* context)
{
    std::shared_ptr<CallbackInfo> info = std::make_shared<CallbackInfo>();
    info->promise = std::make_shared<std::promise<Task>>();
    info->requestId = generateUuid();
    callbackInfo_[info->requestId] = info;
    transport_->CancelTask(info->requestId, params, context);
    return promise->get_future();
}

std::future<TaskPushNotificationConfig> DefaultClient::SetTaskPushNotificationConfig(
    const TaskPushNotificationConfig& config, const ClientCallContext* context)
{
    std::shared_ptr<CallbackInfo> info = std::make_shared<CallbackInfo>();
    info->promise = std::make_shared<std::promise<TaskPushNotificationConfig>>();
    info->requestId = generateUuid();
    callbackInfo_[info->requestId] = info;
    transport_->SetTaskPushNotificationConfig(info->requestId, config, context);
    return promise->get_future();
}

std::future<TaskPushNotificationConfig> DefaultClient::GetTaskPushNotificationConfig(
    const GetTaskPushNotificationConfigParams& params, const ClientCallContext* context)
{
    std::shared_ptr<CallbackInfo> info = std::make_shared<CallbackInfo>();
    info->promise = std::make_shared<std::promise<TaskPushNotificationConfig>>();
    info->requestId = generateUuid();
    callbackInfo_[info->requestId] = info;
    transport_->GetTaskPushNotificationConfig(info->requestId, params, context);
    return promise->get_future();
}

std::future<std::vector<TaskPushNotificationConfig>> DefaultClient::ListTaskPushNotificationConfigs(
    const ListTaskPushNotificationConfigParams& params, const ClientCallContext* context)
{
    std::shared_ptr<CallbackInfo> info = std::make_shared<CallbackInfo>();
    info->promise = std::make_shared<std::promise<std::vector<TaskPushNotificationConfig>>>();
    info->requestId = generateUuid();
    callbackInfo_[info->requestId] = info;
    transport_->ListTaskPushNotificationConfigs(info->requestId, params, context);
    return promise->get_future();
}

std::future<void> DefaultClient::DeleteTaskPushNotificationConfig(const DeleteTaskPushNotificationConfigParams& params,
    const ClientCallContext* context)
{
    std::shared_ptr<CallbackInfo> info = std::make_shared<CallbackInfo>();
    info->promise = std::make_shared<std::promise<void>>();
    info->requestId = generateUuid();
    callbackInfo_[info->requestId] = info;
    transport_->DeleteTaskPushNotificationConfig(info->requestId, params, context);
    return promise->get_future();
}

void DefaultClient::Resubscribe(const TaskIdParams& params, const ClientCallContext* context,
    ResponseHandler handler)
{
    const bool canStream = config_.streaming && card_.capabilities.streaming.value_or(false);
    if (!canStream) {
        throw std::runtime_error("client and/or server do not support resubscription.");
    }

    std::string requestId = generateUuid();
    std::shared_ptr<CallbackInfo> info = std::make_shared<CallbackInfo>();
    info->handler = handler;
    info->requestId = generateUuid();
    info->mgr = std::make_shared<ClientTaskManager>();
    callbackInfo_[info->requestId] = info;
    transport_->Resubscribe(info->requestId, params, context);
}

std::future<A2A::AgentCard> DefaultClient::GetCard(const ClientCallContext* context)
{
    std::shared_ptr<CallbackInfo> info = std::make_shared<CallbackInfo>();
    info->promise = std::make_shared<std::promise<std::vector<TaskPushNotificationConfig>>>();
    info->requestId = generateUuid();
    callbackInfo_[info->requestId] = info;
    transport_->GetCard(info->requestId, context);
    return promise->get_future();
}

void DefaultClient::AddEventConsumer(Consumer c)
{
    consumers_.push_back(std::move(c));
}

void DefaultClient::AddRequestMiddleware(ClientCallInterceptor* middleware)
{
    middleware_.push_back(std::move(middleware));
}

void DefaultClient::Close()
{
    transport_->Close();
}

void DefaultClient::Consume(const ClientEvent& ev)
{
    for (auto& c : consumers_) {
        c(ev, card_);
    }
}

void DefaultClient::TransportEventCb(const std::string& requestId, const TrasnportEvent& event)
{
}

} // namespace A2A::Client
