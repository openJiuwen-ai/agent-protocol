/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <nlohmann/json.hpp>

#include "common_types.h"
#include "uuid.h"
#include "a2a_log.h"
#include "jsonrpc.h"
#include "default_client.h"

namespace A2A::Client {

DefaultClient::DefaultClient(const AgentCard& card, const ClientConfig& config,
    std::shared_ptr<ClientTransport> transport, const std::vector<Consumer>& consumers)
    : card_(card), config_(config), transport_(transport), consumers_(consumers)
{
    transport_->SetTransportCallback([this](const std::string& requestId, const TransportEvent& event) {
        this->TransportEventCb(requestId, event);
    });
}

DefaultClient::~DefaultClient()
{
    if (transport_ != nullptr) {
        transport_->Close();
    }
}

std::future<void> DefaultClient::SendMessage(const Message& msg, const ClientCallContext* context,
    ResponseHandler handler, int timeout)
{
    MessageSendParams params;
    params.message = msg;
    MessageSendConfiguration cfg;
    if (config_.acceptedOutputModes.has_value()) {
        cfg.acceptedOutputModes = config_.acceptedOutputModes;
    }

    cfg.returnImmediately = config_.polling;
    if (!config_.pushNotificationConfigs.empty()) {
        cfg.pushNotificationConfig = config_.pushNotificationConfigs.front();
    }
    params.configuration = cfg;
    const bool canStream = config_.streaming && card_.capabilities.streaming.value_or(false);
    return ProcessMessageRequest(params, context, handler, timeout,
        canStream ? METHOD_MESSAGE_STREAM : METHOD_MESSAGE_SEND);
}

std::future<Task> DefaultClient::GetTask(const TaskQueryParams& params, const ClientCallContext* context, int timeout)
{
    if (transport_ == nullptr) {
        std::promise<Task> promise;
        promise.set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_INVALID_TRANSPORT),
            "Invalid transport"));
        return promise.get_future();
    }
    if (params.id.empty()) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "GetTask invalid parameter.");
        std::promise<Task> promise;
        promise.set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_INVALID_INPUT),
            "Invalid parameter"));
        return promise.get_future();
    }

    std::string requestId = GenerateUuid();
    std::shared_ptr<CallbackInfo> info;
    std::shared_ptr<std::promise<Task>> promise;
    try {
        info = std::make_shared<CallbackInfo>();
        promise = std::make_shared<std::promise<Task>>();
        info->promise = promise;
        info->method = METHOD_TASK_GET;
        info->requestId = requestId;
        {
            std::lock_guard<std::mutex> g(mutex_);
            callbackInfo_[info->requestId] = info;
        }

        transport_->GetTask(info->requestId, params, context, timeout);
    } catch (const std::bad_alloc& e) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, std::string("exception occured: ") + e.what());
        std::promise<Task> fallbackPromise;
        fallbackPromise.set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_BAD_ALLOC), e.what()));
        std::lock_guard<std::mutex> g(mutex_);
        callbackInfo_.erase(requestId);
        return fallbackPromise.get_future();
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> g(mutex_);
        callbackInfo_.erase(requestId);
        promise->set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_TRANSPORT_EXCEPTION),
            e.what()));
    }
    return promise->get_future();
}

std::future<Task> DefaultClient::CancelTask(const TaskIdParams& params, const ClientCallContext* context, int timeout)
{
    if (transport_ == nullptr) {
        std::promise<Task> promise;
        promise.set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_INVALID_TRANSPORT),
            "Invalid transport"));
        return promise.get_future();
    }
    if (params.id.empty()) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "CancelTask invalid parameter.");
        std::promise<Task> promise;
        promise.set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_INVALID_INPUT),
            "Invalid parameter"));
        return promise.get_future();
    }
    std::string requestId = GenerateUuid();
    std::shared_ptr<CallbackInfo> info;
    std::shared_ptr<std::promise<Task>> promise;
    try {
        info = std::make_shared<CallbackInfo>();
        promise = std::make_shared<std::promise<Task>>();
        info->promise = promise;
        info->method = METHOD_TASK_CANCEL;
        info->requestId = requestId;
        {
            std::lock_guard<std::mutex> g(mutex_);
            callbackInfo_[info->requestId] = info;
        }

        transport_->CancelTask(info->requestId, params, context, timeout);
    } catch (const std::bad_alloc& e) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, std::string("exception occured: ") + e.what());
        std::promise<Task> fallbackPromise;
        fallbackPromise.set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_BAD_ALLOC), e.what()));
        std::lock_guard<std::mutex> g(mutex_);
        callbackInfo_.erase(requestId);
        return fallbackPromise.get_future();
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> g(mutex_);
        callbackInfo_.erase(requestId);
        promise->set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_TRANSPORT_EXCEPTION), e.what()));
    }
    return promise->get_future();
}

std::future<TaskPushNotificationConfig> DefaultClient::SetTaskPushNotificationConfig(
    const TaskPushNotificationConfig& config, const ClientCallContext* context, int timeout)
{
    if (transport_ == nullptr) {
        std::promise<TaskPushNotificationConfig> promise;
        promise.set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_INVALID_TRANSPORT),
            "Invalid transport"));
        return promise.get_future();
    }
    if (config.taskId.empty()) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "SetTaskPushNotificationConfig invalid parameter.");
        std::promise<TaskPushNotificationConfig> promise;
        promise.set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_INVALID_INPUT),
            "Invalid parameter"));
        return promise.get_future();
    }

    std::string requestId = GenerateUuid();
    std::shared_ptr<CallbackInfo> info;
    std::shared_ptr<std::promise<TaskPushNotificationConfig>> promise;
    try {
        info = std::make_shared<CallbackInfo>();
        promise = std::make_shared<std::promise<TaskPushNotificationConfig>>();
        info->promise = promise;
        info->method = METHOD_TASK_PUSH_NOTIFICATION_CONFIG_SET;
        info->requestId = requestId;
        {
            std::lock_guard<std::mutex> g(mutex_);
            callbackInfo_[info->requestId] = info;
        }

        transport_->SetTaskPushNotificationConfig(info->requestId, config, context, timeout);
    } catch (const std::bad_alloc& e) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, std::string("exception occured: ") + e.what());
        std::promise<TaskPushNotificationConfig> fallbackPromise;
        fallbackPromise.set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_BAD_ALLOC), e.what()));
        std::lock_guard<std::mutex> g(mutex_);
        callbackInfo_.erase(requestId);
        return fallbackPromise.get_future();
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> g(mutex_);
        callbackInfo_.erase(requestId);
        promise->set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_TRANSPORT_EXCEPTION), e.what()));
    }
    return promise->get_future();
}

std::future<TaskPushNotificationConfig> DefaultClient::GetTaskPushNotificationConfig(
    const GetTaskPushNotificationConfigParams& params, const ClientCallContext* context, int timeout)
{
    if (transport_ == nullptr) {
        std::promise<TaskPushNotificationConfig> promise;
        promise.set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_INVALID_TRANSPORT),
            "Invalid transport"));
        return promise.get_future();
    }
    if (params.id.empty()) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "GetTaskPushNotificationConfig invalid parameter.");
        std::promise<TaskPushNotificationConfig> promise;
        promise.set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_INVALID_INPUT),
            "Invalid parameter"));
        return promise.get_future();
    }

    std::string requestId = GenerateUuid();
    std::shared_ptr<CallbackInfo> info;
    std::shared_ptr<std::promise<TaskPushNotificationConfig>> promise;
    try {
        info = std::make_shared<CallbackInfo>();
        promise = std::make_shared<std::promise<TaskPushNotificationConfig>>();
        info->promise = promise;
        info->method = METHOD_TASK_PUSH_NOTIFICATION_CONFIG_GET;
        info->requestId = requestId;
        {
            std::lock_guard<std::mutex> g(mutex_);
            callbackInfo_[info->requestId] = info;
        }

        transport_->GetTaskPushNotificationConfig(info->requestId, params, context, timeout);
    } catch (const std::bad_alloc& e) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, std::string("exception occured: ") + e.what());
        std::promise<TaskPushNotificationConfig> fallbackPromise;
        fallbackPromise.set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_BAD_ALLOC), e.what()));
        std::lock_guard<std::mutex> g(mutex_);
        callbackInfo_.erase(requestId);
        return fallbackPromise.get_future();
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> g(mutex_);
        callbackInfo_.erase(requestId);
        promise->set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_TRANSPORT_EXCEPTION), e.what()));
    }
    return promise->get_future();
}

std::future<std::vector<TaskPushNotificationConfig>> DefaultClient::ListTaskPushNotificationConfigs(
    const ListTaskPushNotificationConfigParams& params, const ClientCallContext* context, int timeout)
{
    if (transport_ == nullptr) {
        std::promise<std::vector<TaskPushNotificationConfig>> promise;
        promise.set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_INVALID_TRANSPORT),
            "Invalid transport"));
        return promise.get_future();
    }
    if (params.id.empty()) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "ListTaskPushNotificationConfigs invalid parameter.");
        std::promise<std::vector<TaskPushNotificationConfig>> promise;
        promise.set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_INVALID_INPUT),
            "Invalid parameter"));
        return promise.get_future();
    }

    std::string requestId = GenerateUuid();
    std::shared_ptr<CallbackInfo> info;
    std::shared_ptr<std::promise<std::vector<TaskPushNotificationConfig>>> promise;
    try {
        info = std::make_shared<CallbackInfo>();
        promise = std::make_shared<std::promise<std::vector<TaskPushNotificationConfig>>>();
        info->promise = promise;
        info->method = METHOD_TASK_PUSH_NOTIFICATION_CONFIG_LIST;
        info->requestId = requestId;
        {
            std::lock_guard<std::mutex> g(mutex_);
            callbackInfo_[info->requestId] = info;
        }
        transport_->ListTaskPushNotificationConfigs(info->requestId, params, context, timeout);
    } catch (const std::bad_alloc& e) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, std::string("exception occured: ") + e.what());
        std::promise<std::vector<TaskPushNotificationConfig>> fallbackPromise;
        fallbackPromise.set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_BAD_ALLOC), e.what()));
        std::lock_guard<std::mutex> g(mutex_);
        callbackInfo_.erase(requestId);
        return fallbackPromise.get_future();
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> g(mutex_);
        callbackInfo_.erase(requestId);
        promise->set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_TRANSPORT_EXCEPTION), e.what()));
    }
    return promise->get_future();
}

std::future<void> DefaultClient::DeleteTaskPushNotificationConfig(const DeleteTaskPushNotificationConfigParams& params,
    const ClientCallContext* context, int timeout)
{
    if (transport_ == nullptr) {
        std::promise<void> promise;
        promise.set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_INVALID_TRANSPORT),
            "Invalid transport"));
        return promise.get_future();
    }
    if (params.id.empty()) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "DeleteTaskPushNotificationConfig invalid parameter.");
        std::promise<void> promise;
        promise.set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_INVALID_INPUT),
            "Invalid parameter"));
        return promise.get_future();
    }

    std::string requestId = GenerateUuid();
    std::shared_ptr<CallbackInfo> info;
    std::shared_ptr<std::promise<void>> promise;
    try {
        info = std::make_shared<CallbackInfo>();
        promise = std::make_shared<std::promise<void>>();
        info->promise = promise;
        info->method = METHOD_TASK_PUSH_NOTIFICATION_CONFIG_DELETE;
        info->requestId = requestId;
        {
            std::lock_guard<std::mutex> g(mutex_);
            callbackInfo_[info->requestId] = info;
        }

        transport_->DeleteTaskPushNotificationConfig(info->requestId, params, context, timeout);
    } catch (const std::bad_alloc& e) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, std::string("exception occured: ") + e.what());
        std::promise<void> fallbackPromise;
        fallbackPromise.set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_BAD_ALLOC), e.what()));
        std::lock_guard<std::mutex> g(mutex_);
        callbackInfo_.erase(requestId);
        return fallbackPromise.get_future();
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> g(mutex_);
        callbackInfo_.erase(requestId);
        promise->set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_TRANSPORT_EXCEPTION), e.what()));
    }
    return promise->get_future();
}

void DefaultClient::Resubscribe(const TaskIdParams& params, const ClientCallContext* context,
    ResponseHandler handler, int timeout)
{
    const bool canStream = config_.streaming && card_.capabilities.streaming.value_or(false);
    if (!canStream) {
        throw std::runtime_error("client and/or server do not support resubscription.");
    }

    std::string requestId = GenerateUuid();
    std::shared_ptr<CallbackInfo> info;
    try {
        info = std::make_shared<CallbackInfo>();
        info->handler = handler;
        info->method = METHOD_TASK_RESUBSCRIBE;
        info->requestId = requestId;
        info->mgr = std::make_shared<ClientTaskManager>();
        {
            std::lock_guard<std::mutex> g(mutex_);
            callbackInfo_[info->requestId] = info;
        }

        transport_->Resubscribe(info->requestId, params, context, timeout);
    } catch (const std::bad_alloc& e) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, std::string("exception occured: ") + e.what());
        std::lock_guard<std::mutex> g(mutex_);
        callbackInfo_.erase(requestId);
        throw;
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> g(mutex_);
        callbackInfo_.erase(requestId);
        throw;
    }
}

std::future<AgentCard> DefaultClient::GetCard(const ClientCallContext* context, int timeout)
{
    std::string requestId = GenerateUuid();
    std::shared_ptr<CallbackInfo> info;
    std::shared_ptr<std::promise<AgentCard>> promise;
    try {
        info = std::make_shared<CallbackInfo>();
        promise = std::make_shared<std::promise<AgentCard>>();
        info->promise = promise;
        info->method = METHOD_AGENT_CARD_GET;
        info->requestId = requestId;
        {
            std::lock_guard<std::mutex> g(mutex_);
            callbackInfo_[info->requestId] = info;
        }
        transport_->GetCard(info->requestId, context, timeout);
    } catch (const std::bad_alloc& e) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, std::string("exception occured: ") + e.what());
        std::promise<AgentCard> fallbackPromise;
        fallbackPromise.set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_BAD_ALLOC), e.what()));
        std::lock_guard<std::mutex> g(mutex_);
        callbackInfo_.erase(requestId);
        return fallbackPromise.get_future();
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> g(mutex_);
        callbackInfo_.erase(requestId);
        promise->set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_TRANSPORT_EXCEPTION), e.what()));
    }
    return promise->get_future();
}

void DefaultClient::AddEventConsumer(Consumer c)
{
    consumers_.push_back(std::move(c));
}

void DefaultClient::AddRequestMiddleware(std::shared_ptr<ClientCallInterceptor> middleware)
{
    transport_->AddRequestMiddleware(middleware);
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
            A2A_LOG(A2A_LOG_LEVEL_WARN, "The eventId not found: " + requestId);
            return;
        }
        cb = it->second;
        if (std::holds_alternative<TransportError>(event) ||
            (cb->method != METHOD_MESSAGE_STREAM && cb->method != METHOD_TASK_RESUBSCRIBE)) {
            callbackInfo_.erase(requestId);
        }
    }

    if (std::holds_alternative<TransportError>(event)) {
        auto e = std::get<TransportError>(event);
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "requestId: " + requestId + ", error code: " + std::to_string(e.errorCode) +
            ", err msg: " + e.errInfo);
        HandlerErrorResp(cb, e);
        return;
    }

    try {
        HandlerSuccessResp(cb, event);
    } catch (const std::exception& e) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, std::string("exception occured: ") + e.what() +
            ", abandon response and release request: " + requestId);
        std::lock_guard<std::mutex> g(mutex_);
        callbackInfo_.erase(requestId);
    }
}

void DefaultClient::HandlerErrorResp(std::shared_ptr<CallbackInfo> cb, const TransportError& e)
{
    if (cb->method == METHOD_MESSAGE_SEND || cb->method == METHOD_MESSAGE_STREAM ||
        cb->method == METHOD_TASK_RESUBSCRIBE) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "method: " + cb->method + ", error code: " +
            std::to_string(e.errorCode) + ", err msg: " + e.errInfo);
        A2AError err;
        err.code = e.errorCode;
        err.message = e.errInfo;
        cb->handler(err, card_);
        auto p = std::get_if<std::shared_ptr<std::promise<void>>>(&cb->promise);
        (*p)->set_exception(CreateExceptionPtr(e.errorCode, e.errInfo));
    } else if (cb->method == METHOD_TASK_GET || cb->method == METHOD_TASK_CANCEL) {
        auto p = std::get_if<std::shared_ptr<std::promise<Task>>>(&cb->promise);
        (*p)->set_exception(CreateExceptionPtr(e.errorCode, e.errInfo));
    } else if (cb->method == METHOD_TASK_PUSH_NOTIFICATION_CONFIG_SET ||
        cb->method == METHOD_TASK_PUSH_NOTIFICATION_CONFIG_GET) {
        auto p = std::get_if<std::shared_ptr<std::promise<TaskPushNotificationConfig>>>(&cb->promise);
        (*p)->set_exception(CreateExceptionPtr(e.errorCode, e.errInfo));
    } else if (cb->method == METHOD_TASK_PUSH_NOTIFICATION_CONFIG_LIST) {
        auto p = std::get_if<std::shared_ptr<std::promise<std::vector<TaskPushNotificationConfig>>>>(&cb->promise);
        (*p)->set_exception(CreateExceptionPtr(e.errorCode, e.errInfo));
    } else if (cb->method == METHOD_TASK_PUSH_NOTIFICATION_CONFIG_DELETE) {
        auto p = std::get_if<std::shared_ptr<std::promise<void>>>(&cb->promise);
        (*p)->set_exception(CreateExceptionPtr(e.errorCode, e.errInfo));
    } else if (cb->method == METHOD_AGENT_CARD_GET) {
        auto p = std::get_if<std::shared_ptr<std::promise<AgentCard>>>(&cb->promise);
        (*p)->set_exception(CreateExceptionPtr(e.errorCode, e.errInfo));
    } else {
        A2A_LOG(A2A_LOG_LEVEL_WARN, "method: " + cb->method + "is ignored by HandlerErrorResp");
    }
}

void DefaultClient::HandlerSuccessResp(std::shared_ptr<CallbackInfo> cb, const TransportEvent& event)
{
    if (cb->method == METHOD_MESSAGE_SEND) {
        ClientEvent ev;
        if (std::holds_alternative<Message>(event)) {
            ev = std::get<Message>(event);
        } else if (std::holds_alternative<Task>(event)) {
            Task t = std::get<Task>(event);
            ev = std::make_pair(t, std::monostate{});
        }
        cb->handler(ev, card_);
        Consume(ev);
        auto p = std::get_if<std::shared_ptr<std::promise<void>>>(&cb->promise);
        (*p)->set_value();
    } else if (cb->method == METHOD_MESSAGE_STREAM || cb->method == METHOD_TASK_RESUBSCRIBE) {
        ClientEvent ev;
        bool end = false;
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
            if (e.status.state == TaskState::COMPLETED || e.status.state == TaskState::CANCELED ||
                e.status.state == TaskState::FAILED || e.status.state == TaskState::REJECTED ||
                e.status.state == TaskState::INPUT_REQUIRED || e.status.state == TaskState::AUTH_REQUIRED) {
                end = true;
            }
        } else if (std::holds_alternative<TaskArtifactUpdateEvent>(event)) {
            auto e = std::get<TaskArtifactUpdateEvent>(event);
            cb->mgr->SaveTaskEvent(e);
            Task tsk = cb->mgr->GetTaskOrRaise();
            ev = std::make_pair(tsk, e);
        }
        cb->handler(ev, card_);
        Consume(ev);
        if (end) {
            auto p = std::get_if<std::shared_ptr<std::promise<void>>>(&cb->promise);
            (*p)->set_value();
            std::lock_guard<std::mutex> g(mutex_);
            callbackInfo_.erase(cb->requestId);
        }
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

std::future<void> DefaultClient::ProcessMessageRequest(const MessageSendParams& params,
    const ClientCallContext* context, ResponseHandler handler, int timeout, const std::string& method)
{
    if (transport_ == nullptr) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "SendMessage invalid transport.");
        std::promise<void> promise;
        promise.set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_INVALID_TRANSPORT),
            "Invalid transport"));
        return promise.get_future();
    }

    if (params.message.messageId.empty() || params.message.parts.empty() || !handler) {
        std::promise<void> promise;
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "SendMessage invalid parameter");
        promise.set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_INVALID_INPUT),
            "Invalid parameter"));
        return promise.get_future();
    }

    std::string requestId = GenerateUuid();
    std::shared_ptr<std::promise<void>> promise;
    try {
        std::shared_ptr<CallbackInfo> info = std::make_shared<CallbackInfo>();
        info->handler = handler;
        promise = std::make_shared<std::promise<void>>();
        info->promise = promise;
        info->requestId = requestId;
        {
            std::lock_guard<std::mutex> g(mutex_);
            callbackInfo_[info->requestId] = info;
        }

        info->method = method;
        if (method == METHOD_MESSAGE_SEND) {
            transport_->SendMessage(info->requestId, params, context, timeout);
            return promise->get_future();
        }
        // SendStreamingMessage
        info->mgr = std::make_shared<ClientTaskManager>();
        transport_->SendMessageStreaming(info->requestId, params, context, timeout);
    } catch (const std::bad_alloc& e) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, std::string("exception occured: ") + e.what());
        std::promise<void> fallbackPromise;
        fallbackPromise.set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_BAD_ALLOC), e.what()));
        std::lock_guard<std::mutex> g(mutex_);
        callbackInfo_.erase(requestId);
        return fallbackPromise.get_future();
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> g(mutex_);
        callbackInfo_.erase(requestId);
        promise->set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_TRANSPORT_EXCEPTION),
            e.what()));
    }
    return promise->get_future();
}

std::exception_ptr DefaultClient::CreateExceptionPtr(int code, const std::string& msg) const
{
    A2AError error;
    error.code = code;
    error.message = msg;
    return std::make_exception_ptr(std::runtime_error(nlohmann::json(error).dump()));
}

} // namespace Client