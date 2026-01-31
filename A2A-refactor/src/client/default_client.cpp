/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <nlohmann/json.hpp>

#include "common_types.h"
#include "uuid.h"
#include "a2a_log.h"
#include "default_client.h"

namespace A2A::Client {

DefaultClient::DefaultClient(const AgentCard& card, const ClientConfig& config,
    std::shared_ptr<ClientTransport> transport, const std::vector<Consumer>& consumers,
    const std::vector<std::shared_ptr<ClientCallInterceptor>>& middleware)
    : card_(card),
      config_(config),
      transport_(transport),
      consumers_(consumers),
      middleware_(middleware)
{
    transport_->SetTransportCallback([this](const std::string& requestId, const TransportEvent& event) {
        this->TransportEventCb(requestId, event);
    });
}

DefaultClient::~DefaultClient() = default;

void DefaultClient::SendMessage(const Message& msg, const ClientCallContext* context, ResponseHandler handler)
{
    MessageSendParams params;
    params.message = msg;
    MessageSendConfiguration cfg;
    if (!config_.acceptedOutputModes.empty()) {
        cfg.acceptedOutputModes = config_.acceptedOutputModes;
    }

    cfg.blocking = !config_.polling;
    if (!config_.pushNotificationConfigs.empty()) {
        cfg.pushNotificationConfig = config_.pushNotificationConfigs.front();
    }
    params.configuration = cfg;

    std::shared_ptr<CallbackInfo> info = std::make_shared<CallbackInfo>();
    info->handler = handler;
    info->requestId = GenerateUuid();
    {
        std::lock_guard<std::mutex> g(mutex_);
        callbackInfo_[info->requestId] = info;
    }
    const bool canStream = config_.streaming && card_.capabilities.streaming.value_or(false);
    if (!canStream) {
        info->method = METHOD_MESSAGE_SEND;
        transport_->SendMessage(info->requestId, params, context);
        return;
    }

    info->method = METHOD_MESSAGE_STREAM;
    info->mgr = std::make_shared<ClientTaskManager>();
    transport_->SendMessageStreaming(info->requestId, params, context);
}

std::future<Task> DefaultClient::GetTask(const TaskQueryParams& params, const ClientCallContext* context)
{
    std::shared_ptr<CallbackInfo> info = std::make_shared<CallbackInfo>();
    auto promise = std::make_shared<std::promise<Task>>();
    info->promise = promise;
    info->method = METHOD_TASK_GET;
    info->requestId = GenerateUuid();
    {
        std::lock_guard<std::mutex> g(mutex_);
        callbackInfo_[info->requestId] = info;
    }
    transport_->GetTask(info->requestId, params, context);
    return promise->get_future();
}

std::future<Task> DefaultClient::CancelTask(const TaskIdParams& params, const ClientCallContext* context)
{
    std::shared_ptr<CallbackInfo> info = std::make_shared<CallbackInfo>();
    auto promise = std::make_shared<std::promise<Task>>();
    info->promise = promise;
    info->method = METHOD_TASK_CANCEL;
    info->requestId = GenerateUuid();
    {
        std::lock_guard<std::mutex> g(mutex_);
        callbackInfo_[info->requestId] = info;
    }
    transport_->CancelTask(info->requestId, params, context);
    return promise->get_future();
}

std::future<TaskPushNotificationConfig> DefaultClient::SetTaskPushNotificationConfig(
    const TaskPushNotificationConfig& config, const ClientCallContext* context)
{
    std::shared_ptr<CallbackInfo> info = std::make_shared<CallbackInfo>();
    auto promise = std::make_shared<std::promise<TaskPushNotificationConfig>>();
    info->promise = promise;
    info->method = METHOD_TASK_PUSH_NOTIFICATION_CONFIG_SET;
    info->requestId = GenerateUuid();
    {
        std::lock_guard<std::mutex> g(mutex_);
        callbackInfo_[info->requestId] = info;
    }
    transport_->SetTaskPushNotificationConfig(info->requestId, config, context);
    return promise->get_future();
}

std::future<TaskPushNotificationConfig> DefaultClient::GetTaskPushNotificationConfig(
    const GetTaskPushNotificationConfigParams& params, const ClientCallContext* context)
{
    std::shared_ptr<CallbackInfo> info = std::make_shared<CallbackInfo>();
    auto promise = std::make_shared<std::promise<TaskPushNotificationConfig>>();
    info->promise = promise;
    info->method = METHOD_TASK_PUSH_NOTIFICATION_CONFIG_GET;
    info->requestId = GenerateUuid();
    {
        std::lock_guard<std::mutex> g(mutex_);
        callbackInfo_[info->requestId] = info;
    }
    transport_->GetTaskPushNotificationConfig(info->requestId, params, context);
    return promise->get_future();
}

std::future<std::vector<TaskPushNotificationConfig>> DefaultClient::ListTaskPushNotificationConfigs(
    const ListTaskPushNotificationConfigParams& params, const ClientCallContext* context)
{
    std::shared_ptr<CallbackInfo> info = std::make_shared<CallbackInfo>();
    auto promise = std::make_shared<std::promise<std::vector<TaskPushNotificationConfig>>>();
    info->promise = promise;
    info->method = METHOD_TASK_PUSH_NOTIFICATION_CONFIG_LIST;
    info->requestId = GenerateUuid();
    {
        std::lock_guard<std::mutex> g(mutex_);
        callbackInfo_[info->requestId] = info;
    }
    transport_->ListTaskPushNotificationConfigs(info->requestId, params, context);
    return promise->get_future();
}

std::future<void> DefaultClient::DeleteTaskPushNotificationConfig(const DeleteTaskPushNotificationConfigParams& params,
    const ClientCallContext* context)
{
    std::shared_ptr<CallbackInfo> info = std::make_shared<CallbackInfo>();
    auto promise = std::make_shared<std::promise<void>>();
    info->promise = promise;
    info->method = METHOD_TASK_PUSH_NOTIFICATION_CONFIG_DELETE;
    info->requestId = GenerateUuid();
    {
        std::lock_guard<std::mutex> g(mutex_);
        callbackInfo_[info->requestId] = info;
    }
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

    std::string requestId = GenerateUuid();
    std::shared_ptr<CallbackInfo> info = std::make_shared<CallbackInfo>();
    info->handler = handler;
    info->method = METHOD_TASK_RESUBSCRIBE;
    info->requestId = GenerateUuid();
    info->mgr = std::make_shared<ClientTaskManager>();
    {
        std::lock_guard<std::mutex> g(mutex_);
        callbackInfo_[info->requestId] = info;
    }
    transport_->Resubscribe(info->requestId, params, context);
}

std::future<AgentCard> DefaultClient::GetCard(const ClientCallContext* context)
{
    std::shared_ptr<CallbackInfo> info = std::make_shared<CallbackInfo>();
    auto promise = std::make_shared<std::promise<AgentCard>>();
    info->promise = promise;
    info->method = METHOD_AGENT_CARD_GET;
    info->requestId = GenerateUuid();
    {
        std::lock_guard<std::mutex> g(mutex_);
        callbackInfo_[info->requestId] = info;
    }
    transport_->GetCard(info->requestId, context);
    return promise->get_future();
}

void DefaultClient::AddEventConsumer(Consumer c)
{
    consumers_.push_back(std::move(c));
}

void DefaultClient::AddRequestMiddleware(std::shared_ptr<ClientCallInterceptor> middleware)
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

void DefaultClient::TransportEventCb(const std::string& requestId, const TransportEvent& event)
{
    std::shared_ptr<CallbackInfo> cb;
    {
        std::lock_guard<std::mutex> g(mutex_);
        auto it = callbackInfo_.find(requestId);
        if (it == callbackInfo_.end()) {
            A2A_LOG(A2A_LOG_LEVEL_WARN, "The eventId not found.");
            return;
        }
        cb = it->second;
        if (cb->method != METHOD_MESSAGE_STREAM && cb->method != METHOD_TASK_RESUBSCRIBE) {
            callbackInfo_.erase(it);
        }

        if (std::holds_alternative<TransportError>(event)) {
            auto e = std::get<TransportError>(event);
            HandlerErrorResp(cb, e);
            if (cb->method == METHOD_MESSAGE_STREAM || cb->method == METHOD_TASK_RESUBSCRIBE) {
                callbackInfo_.erase(it);
            }
            return;
        }
    }

    HandlerSuccessResp(cb, event);
}

void DefaultClient::HandlerErrorResp(std::shared_ptr<CallbackInfo> cb, const TransportError& e)
{
    if (cb->method == METHOD_MESSAGE_SEND || cb->method == METHOD_MESSAGE_STREAM ||
        cb->method == METHOD_TASK_RESUBSCRIBE) {
        return;
    } else if (cb->method == METHOD_TASK_GET || cb->method == METHOD_TASK_CANCEL) {
        auto p = std::get_if<std::shared_ptr<std::promise<Task>>>(&cb->promise);
        (*p)->set_exception(std::make_exception_ptr(std::runtime_error(std::to_string(e.errorCode))));
    } else if (cb->method == METHOD_TASK_PUSH_NOTIFICATION_CONFIG_SET ||
        cb->method == METHOD_TASK_PUSH_NOTIFICATION_CONFIG_GET) {
        auto p = std::get_if<std::shared_ptr<std::promise<TaskPushNotificationConfig>>>(&cb->promise);
        (*p)->set_exception(std::make_exception_ptr(std::runtime_error(std::to_string(e.errorCode))));
    } else if (cb->method == METHOD_TASK_PUSH_NOTIFICATION_CONFIG_LIST) {
        auto p = std::get_if<std::shared_ptr<std::promise<std::vector<TaskPushNotificationConfig>>>>(&cb->promise);
        (*p)->set_exception(std::make_exception_ptr(std::runtime_error(std::to_string(e.errorCode))));
    } else if (cb->method == METHOD_TASK_PUSH_NOTIFICATION_CONFIG_DELETE) {
        auto p = std::get_if<std::shared_ptr<std::promise<void>>>(&cb->promise);
        (*p)->set_exception(std::make_exception_ptr(std::runtime_error(std::to_string(e.errorCode))));
    } else if (cb->method == METHOD_AGENT_CARD_GET) {
        auto p = std::get_if<std::shared_ptr<std::promise<AgentCard>>>(&cb->promise);
        (*p)->set_exception(std::make_exception_ptr(std::runtime_error(std::to_string(e.errorCode))));
    }
}

void DefaultClient::HandlerSuccessResp(std::shared_ptr<CallbackInfo> cb, const TransportEvent& event)
{
    if (cb->method == METHOD_MESSAGE_SEND) {
        ClientEvent ev = std::get<Message>(event);
        cb->handler(ev, card_);
        Consume(ev);
    } else if (cb->method == METHOD_MESSAGE_STREAM || cb->method == METHOD_TASK_RESUBSCRIBE) {
        ClientEvent ev;
        if (std::holds_alternative<Message>(event)) {
            ev = std::get<Message>(event);
        } else if (std::holds_alternative<Task>(event)) {
            Task t = std::get<Task>(event);
            cb->mgr->SaveTaskEvent(t);
            ev = std::make_pair(t, std::monostate{});
        } else if (std::holds_alternative<TaskStatusUpdateEvent>(event)) {
            auto e = std::get<TaskStatusUpdateEvent>(event);
            cb->mgr->SaveTaskEvent(e);
            Task tsk = cb->mgr->GetTaskOrRaise();
            ev = std::make_pair(tsk, e);
        } else if (std::holds_alternative<TaskArtifactUpdateEvent>(event)) {
            auto e = std::get<TaskArtifactUpdateEvent>(event);
            cb->mgr->SaveTaskEvent(e);
            Task tsk = cb->mgr->GetTaskOrRaise();
            ev = std::make_pair(tsk, e);
        }
        cb->handler(ev, card_);
        Consume(ev);
    } else if (cb->method == METHOD_TASK_GET || cb->method == METHOD_TASK_CANCEL) {
        auto p = std::get_if<std::shared_ptr<std::promise<Task>>>(&cb->promise);
        (*p)->set_value(std::get<Task>(event));
    } else if (cb->method == METHOD_TASK_PUSH_NOTIFICATION_CONFIG_SET ||
        cb->method == METHOD_TASK_PUSH_NOTIFICATION_CONFIG_GET) {
        auto p = std::get_if<std::shared_ptr<std::promise<TaskPushNotificationConfig>>>(&cb->promise);
        (*p)->set_value(std::get<TaskPushNotificationConfig>(event));
    } else if (cb->method == METHOD_TASK_PUSH_NOTIFICATION_CONFIG_LIST) {
        auto p = std::get_if<std::shared_ptr<std::promise<std::vector<TaskPushNotificationConfig>>>>(&cb->promise);
        (*p)->set_value(std::get<std::vector<TaskPushNotificationConfig>>(event));
    } else if (cb->method == METHOD_TASK_PUSH_NOTIFICATION_CONFIG_DELETE) {
        auto p = std::get_if<std::shared_ptr<std::promise<void>>>(&cb->promise);
        (*p)->set_value();
    } else if (cb->method == METHOD_AGENT_CARD_GET) {
        auto p = std::get_if<std::shared_ptr<std::promise<AgentCard>>>(&cb->promise);
        (*p)->set_value(std::get<AgentCard>(event));
    }
}

} // namespace Client
