/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <nlohmann/json.hpp>

#include "jsonrpc.h"
#include "common_types.h"
#include "libcurl_conn.h"
#include "client/jsonrpc_transport.h"
#include "a2a_log.h"
#include "jsonrpc_transport_impl.h"

namespace A2A::Client {

void SessionTransportCallback::OnMessageReceived(const ConnEventData& message, const UserData& userData)
{
    if (t_ != nullptr) {
        t_->OnTransportMessage(message, userData);
    }
}

JsonRpcTransportImpl::JsonRpcTransportImpl(const std::string& url, const AgentCard& agentCard,
    [[maybe_unused]] const ClientConfig& config,
    const std::vector<std::shared_ptr<ClientCallInterceptor>>& interceptors)
    : url_(url), interceptors_(interceptors)
{
    agentCard_ = std::make_shared<AgentCard>(agentCard);
    conn_ = std::make_shared<Http::LibcurlConn>(url);
    conn_->SetCallback(std::make_shared<SessionTransportCallback>(this));
}

JsonRpcTransportImpl::~JsonRpcTransportImpl() = default;

void JsonRpcTransportImpl::SendMessage(const std::string& requestId, const MessageSendParams& request,
    const ClientCallContext* context, int timeout, const std::string& method)
{
    Send(requestId, method, nlohmann::json(request), context, timeout);
}

void JsonRpcTransportImpl::SendMessageStreaming(const std::string& requestId, const MessageSendParams& request,
    const ClientCallContext* context, int timeout)
{
    Send(requestId, METHOD_MESSAGE_STREAM, nlohmann::json(request), context, timeout);
}

void JsonRpcTransportImpl::GetTask(const std::string& requestId, const TaskQueryParams& params,
    const ClientCallContext* context, int timeout)
{
    Send(requestId, METHOD_TASK_GET, nlohmann::json(params), context, timeout);
}

void JsonRpcTransportImpl::CancelTask(const std::string& requestId, const TaskIdParams& params,
    const ClientCallContext* context, int timeout)
{
    Send(requestId, METHOD_TASK_CANCEL, nlohmann::json(params), context, timeout);
}

void JsonRpcTransportImpl::SetTaskPushNotificationConfig(const std::string& requestId,
    const TaskPushNotificationConfig& config, const ClientCallContext* context, int timeout)
{
    Send(requestId, METHOD_TASK_PUSH_NOTIFICATION_CONFIG_SET, nlohmann::json(config), context, timeout);
}

void JsonRpcTransportImpl::GetTaskPushNotificationConfig(const std::string& requestId,
    const GetTaskPushNotificationConfigParams& params, const ClientCallContext* context, int timeout)
{
    Send(requestId, METHOD_TASK_PUSH_NOTIFICATION_CONFIG_GET, nlohmann::json(params), context, timeout);
}

void JsonRpcTransportImpl::ListTaskPushNotificationConfigs(const std::string& requestId,
    const ListTaskPushNotificationConfigParams& params, const ClientCallContext* context, int timeout)
{
    Send(requestId, METHOD_TASK_PUSH_NOTIFICATION_CONFIG_LIST, nlohmann::json(params), context, timeout);
}

void JsonRpcTransportImpl::DeleteTaskPushNotificationConfig(const std::string& requestId,
    const DeleteTaskPushNotificationConfigParams& params, const ClientCallContext* context, int timeout)
{
    Send(requestId, METHOD_TASK_PUSH_NOTIFICATION_CONFIG_DELETE, nlohmann::json(params), context, timeout);
}

void JsonRpcTransportImpl::Resubscribe(const std::string& requestId, const TaskIdParams& params,
    const ClientCallContext* context, int timeout)
{
    Send(requestId, METHOD_TASK_RESUBSCRIBE, nlohmann::json(params), context, timeout);
}

void JsonRpcTransportImpl::GetCard(const std::string& requestId, [[maybe_unused]] const ClientCallContext* context,
    int timeout)
{
    std::shared_ptr<UserData> userData = std::make_shared<UserData>();
    userData->requestId = requestId;
    userData->method = METHOD_AGENT_CARD_GET;

    std::map<std::string, std::string> headers;
    {
        std::lock_guard<std::mutex> lock(requestMutex_);
        requestData_[requestId] = userData;
    }
    conn_->SendMessage("", headers, userData, timeout);
}

void JsonRpcTransportImpl::SetTransportCallback(TransportEventCallback callback)
{
    transportEventCb_ = callback;
}

void JsonRpcTransportImpl::OnTransportMessage(const ConnEventData& message, const UserData& userData)
{
    if (!transportEventCb_) {
        return;
    }

    UserData udata = userData;
    try {
        nlohmann::json j = nlohmann::json::parse(message.data);
        if (!userData.valid) {
            // find user data according to response payload
            if (!j.contains(JSON_FIELD_ID)) {
                // abandon invalid data and record warn log
                A2A_LOG(A2A_LOG_LEVEL_WARN, "invalid data from transport layer, abandon it");
                return;
            }

            auto id = j[JSON_FIELD_ID].get<std::string>();
            {
                std::lock_guard<std::mutex> lock(requestMutex_);
                auto it = requestData_.find(id);
                if (it == requestData_.end()) {
                    A2A_LOG(A2A_LOG_LEVEL_WARN, "requestId[" + id + "] not found, abandon response data");
                    return;
                }
                udata = *it->second;
            }
        }

        if (message.errCode != 0) {
            conn_->FinishRequest(udata.timerId);
            {
                std::lock_guard<std::mutex> lock(requestMutex_);
                if (requestData_.find(udata.requestId) == requestData_.end()) {
                    return;
                }
                requestData_.erase(udata.requestId);
            }
            transportEventCb_(udata.requestId, TransportError{message.errCode, message.data});
            return;
        }

        if (udata.isStream) {
            if (!j.contains(JSON_FIELD_ERROR)) {
                conn_->RefreshRequest(udata.timerId, udata.timeout);
            }
            OnStreamResp(message, udata);
            return;
        } else {
            conn_->FinishRequest(udata.timerId);
            {
                std::lock_guard<std::mutex> lock(requestMutex_);
                if (requestData_.find(udata.requestId) == requestData_.end()) {
                    return;
                }
                requestData_.erase(udata.requestId);
            }
            OnNonStreamResp(udata, j);
        }
    } catch (const nlohmann::json::exception& e) {
        A2A_LOG(A2A_LOG_LEVEL_WARN, "invalid data from transport layer");
        if (udata.valid) {
            TransportEvent ev = TransportError{static_cast<int>(A2AErrorCode::JSONRPC_PARSE_ERROR), e.what()};
            conn_->FinishRequest(udata.timerId);
            {
                std::lock_guard<std::mutex> lock(requestMutex_);
                if (requestData_.find(udata.requestId) == requestData_.end()) {
                    return;
                }
                requestData_.erase(udata.requestId);
            }
            transportEventCb_(udata.requestId, ev);
        }
    } catch (const std::exception& e) {
        A2A_LOG(A2A_LOG_LEVEL_WARN, "exception caught: " + std::string(e.what()));
    }
}

void JsonRpcTransportImpl::Close()
{
    conn_->Terminate();
    TransportEvent ev = TransportError{static_cast<int>(A2AErrorCode::A2A_STATUS_ERROR), "Transport is closed"};
    std::unordered_map<std::string, std::shared_ptr<UserData>> tmp;
    {
        std::lock_guard<std::mutex> lock(requestMutex_);
        tmp = requestData_;
        requestData_.clear();
    }

    for (auto it = tmp.begin(); it != tmp.end();) {
        conn_->FinishRequest(it->second->timerId);
        transportEventCb_(it->second->requestId, ev);
        it = tmp.erase(it);
    }
}

std::shared_ptr<ClientConn> JsonRpcTransportImpl::GetConn()
{
    return conn_;
}

void JsonRpcTransportImpl::AddRequestMiddleware(const std::shared_ptr<ClientCallInterceptor>& middleware)
{
    if (middleware == nullptr) {
        return;
    }

    interceptors_.push_back(middleware);
}

void JsonRpcTransportImpl::ApplyInterceptors(const std::string& method, std::string& payload,
    std::map<std::string, std::string>& headers, const ClientCallContext* context) const
{
    for (auto& i : interceptors_) {
        i->Intercept(method, payload, headers, agentCard_.get(), context);
    }
}

int JsonRpcTransportImpl::Send(const std::string& requestId, const std::string& method, const nlohmann::json& payload,
    const ClientCallContext* context, int timeout)
{
    nlohmann::json rpc {
        {JSON_FIELD_JSONRPC, JSON_VERSION},
        {JSON_FIELD_ID, requestId},
        {JSON_FIELD_METHOD, method},
        {JSON_FIELD_PARAMS, payload}
    };

    std::map<std::string, std::string> headers;
    std::string data = rpc.dump();
    ApplyInterceptors(method, data, headers, context);

    std::shared_ptr<UserData> userData = std::make_shared<UserData>();
    userData->requestId = requestId;
    userData->method = method;
    userData->timeout = timeout;
    if (method == METHOD_MESSAGE_STREAM || method == METHOD_TASK_RESUBSCRIBE) {
        userData->isStream = true;
    }
    {
        std::lock_guard<std::mutex> lock(requestMutex_);
        requestData_[requestId] = userData;
    }
    auto ret = conn_->SendMessage(data, headers, userData, timeout);
    if (ret != 0) {
        A2A_LOG(A2A_LOG_LEVEL_WARN, "ret: " + std::to_string(ret));
        std::lock_guard<std::mutex> lock(requestMutex_);
        requestData_.erase(requestId);
        throw std::runtime_error("transport exception");
    }
    return 0;
}

void JsonRpcTransportImpl::OnStreamResp(const ConnEventData& message, const UserData& userData)
{
    if (message.isStreamFin) {
        TransportError err;
        err.errorCode = 0;
        conn_->FinishRequest(userData.timerId);
        {
            std::lock_guard<std::mutex> lock(requestMutex_);
            if (requestData_.find(userData.requestId) == requestData_.end()) {
                return;
            }
            requestData_.erase(userData.requestId);
        }
        transportEventCb_(userData.requestId, err);
        return;
    }

    TransportEvent ev;
    nlohmann::json data = nlohmann::json::parse(message.data);
    if (data.contains(JSON_FIELD_ERROR)) {
        auto err = data[JSON_FIELD_ERROR].get<A2AError>();
        ev = TransportError{err.code, err.message.value_or("")};
        conn_->FinishRequest(userData.timerId);
        {
            std::lock_guard<std::mutex> lock(requestMutex_);
            if (requestData_.find(userData.requestId) == requestData_.end()) {
                return;
            }
            requestData_.erase(userData.requestId);
        }
        transportEventCb_(userData.requestId, ev);
        return;
    }
    if (data.contains(JSON_FIELD_ID) && data[JSON_FIELD_ID].get<std::string>() != userData.requestId) {
        ev = TransportError{static_cast<int>(A2AErrorCode::INVALID_AGENT_RESPONSE),
            "RequestId in response data does not match with requestId in request"};
        conn_->FinishRequest(userData.timerId);
        {
            std::lock_guard<std::mutex> lock(requestMutex_);
            if (requestData_.find(userData.requestId) == requestData_.end()) {
                return;
            }
            requestData_.erase(userData.requestId);
        }
        transportEventCb_(userData.requestId, ev);
        return;
    }
    auto resp = data.get<SendStreamingMessageSuccessResponse>();
    std::visit([&ev](auto&& result) {
        ev = result;
        }, resp.result);
    if (std::holds_alternative<TaskStatusUpdateEvent>(ev)) {
        auto m = std::get<TaskStatusUpdateEvent>(ev);
        if (m.status.state == TaskState::COMPLETED ||
            m.status.state == TaskState::CANCELED ||
            m.status.state == TaskState::FAILED ||
            m.status.state == TaskState::REJECTED ||
            m.status.state == TaskState::INPUT_REQUIRED ||
            m.status.state == TaskState::AUTH_REQUIRED) {
            conn_->FinishRequest(userData.timerId);
            {
                std::lock_guard<std::mutex> lock(requestMutex_);
                if (requestData_.find(userData.requestId) == requestData_.end()) {
                    // try to find requestData in lock first.
                    // if cannot find, means other thread(e.g. timeout thread) has already handled it, thus do nothing.
                    return;
                }
                requestData_.erase(userData.requestId);
            }
        }
    }
    transportEventCb_(userData.requestId, ev);
}

void JsonRpcTransportImpl::OnNonStreamResp(const UserData& userData, const nlohmann::json& data)
{
    TransportEvent ev;
    if (data.contains(JSON_FIELD_ID) && data[JSON_FIELD_ID].get<std::string>() != userData.requestId) {
        ev = TransportError{static_cast<int>(A2AErrorCode::INVALID_AGENT_RESPONSE),
            "RequestId in response data does not match with requestId in request"};
    } else if (data.contains(JSON_FIELD_ERROR)) {
        auto err = data[JSON_FIELD_ERROR].get<A2AError>();
        ev = TransportError{err.code, err.message.value_or("")};
    } else if (userData.method == METHOD_MESSAGE_SEND) {
        auto resp = data.get<SendMessageSuccessResponse>();
        std::visit([&ev](auto&& result) {
            ev = result;
            }, resp.result);
    } else if (userData.method == METHOD_TASK_GET) {
        auto resp = data.get<GetTaskSuccessResponse>();
        ev = resp.result;
    } else if (userData.method == METHOD_TASK_CANCEL) {
        auto resp = data.get<CancelTaskSuccessResponse>();
        ev = resp.result;
    } else if (userData.method == METHOD_TASK_PUSH_NOTIFICATION_CONFIG_SET) {
        auto resp = data.get<SetTaskPushNotificationConfigSuccessResponse>();
        ev = resp.result;
    } else if (userData.method == METHOD_TASK_PUSH_NOTIFICATION_CONFIG_GET) {
        auto resp = data.get<GetTaskPushNotificationConfigSuccessResponse>();
        ev = resp.result;
    } else if (userData.method == METHOD_TASK_PUSH_NOTIFICATION_CONFIG_LIST) {
        auto resp = data.get<ListTaskPushNotificationConfigSuccessResponse>();
        ev = resp.result;
    } else if (userData.method == METHOD_TASK_PUSH_NOTIFICATION_CONFIG_DELETE) {
        ev = std::monostate{};
    } else if (userData.method == METHOD_AGENT_CARD_GET) {
        if (data.contains(JSON_FIELD_RESULT)) {
            auto resp = data.get<GetAgentCardSuccessResponse>();
            ev = resp.result;
        } else {
            ev = data.get<AgentCard>();
        }
    } else {
        ev = TransportError{static_cast<int>(A2AErrorCode::JSONRPC_METHOD_NOT_FOUND),
            "Method not found in response data"};
    }
    transportEventCb_(userData.requestId, ev);
}


JsonRpcTransport::JsonRpcTransport(const std::string& url, const AgentCard& agentCard, const ClientConfig& config,
    const std::vector<std::shared_ptr<ClientCallInterceptor>>& interceptors)
    : impl_(std::make_unique<JsonRpcTransportImpl>(url, agentCard, config, interceptors))
{
}

JsonRpcTransport::~JsonRpcTransport()
{
}

void JsonRpcTransport::SendMessage(const std::string& requestId, const MessageSendParams& request,
    const ClientCallContext* context, int timeout)
{
    impl_->SendMessage(requestId, request, context, timeout, METHOD_MESSAGE_SEND);
}

void JsonRpcTransport::SendMessageStreaming(const std::string& requestId, const MessageSendParams& request,
    const ClientCallContext* context, int timeout)
{
    impl_->SendMessageStreaming(requestId, request, context, timeout);
}

void JsonRpcTransport::GetTask(const std::string& requestId, const TaskQueryParams& params,
    const ClientCallContext* context, int timeout)
{
    impl_->GetTask(requestId, params, context, timeout);
}

void JsonRpcTransport::CancelTask(const std::string& requestId, const TaskIdParams& params,
    const ClientCallContext* context, int timeout)
{
    impl_->CancelTask(requestId, params, context, timeout);
}

void JsonRpcTransport::SetTaskPushNotificationConfig(const std::string& requestId,
    const TaskPushNotificationConfig& config, const ClientCallContext* context, int timeout)
{
    impl_->SetTaskPushNotificationConfig(requestId, config, context, timeout);
}

void JsonRpcTransport::GetTaskPushNotificationConfig(const std::string& requestId,
    const GetTaskPushNotificationConfigParams& params, const ClientCallContext* context, int timeout)
{
    impl_->GetTaskPushNotificationConfig(requestId, params, context, timeout);
}

void JsonRpcTransport::ListTaskPushNotificationConfigs(const std::string& requestId,
    const ListTaskPushNotificationConfigParams& params, const ClientCallContext* context, int timeout)
{
    impl_->ListTaskPushNotificationConfigs(requestId, params, context, timeout);
}

void JsonRpcTransport::DeleteTaskPushNotificationConfig(const std::string& requestId,
    const DeleteTaskPushNotificationConfigParams& params, const ClientCallContext* context, int timeout)
{
    impl_->DeleteTaskPushNotificationConfig(requestId, params, context, timeout);
}

void JsonRpcTransport::Resubscribe(const std::string& requestId, const TaskIdParams& params,
    const ClientCallContext* context, int timeout)
{
    impl_->Resubscribe(requestId, params, context, timeout);
}

void JsonRpcTransport::GetCard(const std::string& requestId, const ClientCallContext* context, int timeout)
{
    impl_->GetCard(requestId, context, timeout);
}

void JsonRpcTransport::SetTransportCallback(TransportEventCallback callback)
{
    impl_->SetTransportCallback(callback);
}

void JsonRpcTransport::AddRequestMiddleware(const std::shared_ptr<ClientCallInterceptor>& middleware)
{
    impl_->AddRequestMiddleware(middleware);
}

void JsonRpcTransport::Close()
{
    impl_->Close();
}

std::shared_ptr<ClientConn> JsonRpcTransport::GetConn()
{
    return impl_->GetConn();
}

} // namespace A2A::Client