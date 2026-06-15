/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <nlohmann/json.hpp>
#include "jsonrpc.h"
#include "common_types.h"
#include "libcurl_conn.h"
#include "a2a_errno.h"
#include "jsonrpc_transport.h"

namespace A2A::Client {

void SessionTransportCallback::OnMessageReceived(const ConnEventData& message, const UserData* userData)
{
    if (t_ != nullptr) {
        t_->OnTransportMessage(message, userData);
    }
}

JsonRpcTransport::JsonRpcTransport(const std::string& url, const AgentCard& agentCard,
    const std::vector<std::shared_ptr<ClientCallInterceptor>>& interceptors)
    : url_(url), interceptors_(interceptors)
{
    agentCard_ = std::make_shared<AgentCard>(agentCard);
    conn_ = std::make_shared<Http::LibcurlConn>(url);
    conn_->SetCallback(std::make_shared<SessionTransportCallback>(this));
}

JsonRpcTransport::~JsonRpcTransport() = default;

void JsonRpcTransport::SendMessage(const std::string& requestId, const MessageSendParams& request,
    const ClientCallContext* context)
{
    Send(requestId, METHOD_MESSAGE_SEND, nlohmann::json(request), context);
}

void JsonRpcTransport::SendMessageStreaming(const std::string& requestId, const MessageSendParams& request,
    const ClientCallContext* context)
{
    Send(requestId, METHOD_MESSAGE_STREAM, nlohmann::json(request), context);
}

void JsonRpcTransport::GetTask(const std::string& requestId, const TaskQueryParams& params,
    const ClientCallContext* context)
{
    Send(requestId, METHOD_TASK_GET, nlohmann::json(params), context);
}

void JsonRpcTransport::CancelTask(const std::string& requestId, const TaskIdParams& params,
    const ClientCallContext* context)
{
    Send(requestId, METHOD_TASK_CANCEL, nlohmann::json(params), context);
}

void JsonRpcTransport::SetTaskPushNotificationConfig(const std::string& requestId,
    const TaskPushNotificationConfig& config, const ClientCallContext* context)
{
    Send(requestId, METHOD_TASK_PUSH_NOTIFICATION_CONFIG_SET, nlohmann::json(config), context);
}

void JsonRpcTransport::GetTaskPushNotificationConfig(const std::string& requestId,
    const GetTaskPushNotificationConfigParams& params, const ClientCallContext* context)
{
    Send(requestId, METHOD_TASK_PUSH_NOTIFICATION_CONFIG_GET, nlohmann::json(params), context);
}

void JsonRpcTransport::ListTaskPushNotificationConfigs(const std::string& requestId,
    const ListTaskPushNotificationConfigParams& params, const ClientCallContext* context)
{
    Send(requestId, METHOD_TASK_PUSH_NOTIFICATION_CONFIG_LIST, nlohmann::json(params), context);
}

void JsonRpcTransport::DeleteTaskPushNotificationConfig(const std::string& requestId,
    const DeleteTaskPushNotificationConfigParams& params, const ClientCallContext* context)
{
    Send(requestId, METHOD_TASK_PUSH_NOTIFICATION_CONFIG_DELETE, nlohmann::json(params), context);
}

void JsonRpcTransport::Resubscribe(const std::string& requestId, const TaskIdParams& params,
    const ClientCallContext* context)
{
    Send(requestId, METHOD_TASK_RESUBSCRIBE, nlohmann::json(params), context);
}

void JsonRpcTransport::GetCard(const std::string& requestId, const ClientCallContext* context)
{
    nlohmann::json rpc {
        {JSON_FIELD_JSONRPC, "2.0"},
        {JSON_FIELD_ID, requestId}
    };

    UserData userData;
    userData.requestId = requestId;
    userData.method = METHOD_AGENT_CARD_GET;
    conn_->SendMessage(rpc.dump(), {}, &userData);
}

void JsonRpcTransport::SetTransportCallback(TransportEventCallback callback)
{
    transportEventCb_ = callback;
}

void JsonRpcTransport::OnTransportMessage(const ConnEventData& message, const UserData* userData)
{
    if (!transportEventCb_) {
        return;
    }

    if (message.errCode != 0) {
        transportEventCb_(userData->requestId, TransportError{message.errCode, message.data});
        return;
    }

    if (message.isStream) {
        OnStreamResp(message, userData);
        return;
    }

    TransportEvent ev;
    try {
        nlohmann::json j = nlohmann::json::parse(message.data);
        if (j.contains("error")) {
            const auto& err = j.at("error");
            ev = TransportError{err.at("code").get<int>(), err.at("message").get<std::string>()};
        } else if (j.contains(JSON_FIELD_ID) && j.at(JSON_FIELD_ID).get<std::string>() != userData->requestId) {
            ev = TransportError{static_cast<int>(JSONRPCErrorCode::INVALID_AGENT_RESPONSE),
                "RequestId in response data does not match with requestId in request"};
        } else if (userData->method == METHOD_MESSAGE_SEND) {
            ev = j.at(JSON_FIELD_RESULT).get<Message>();
        } else if (userData->method == METHOD_TASK_GET) {
            ev = j.at(JSON_FIELD_RESULT).get<Task>();
        } else if (userData->method == METHOD_TASK_CANCEL) {
            ev = j.at(JSON_FIELD_RESULT).get<Task>();
        } else if (userData->method == METHOD_TASK_PUSH_NOTIFICATION_CONFIG_SET) {
            ev = j.at(JSON_FIELD_RESULT).get<TaskPushNotificationConfig>();
        } else if (userData->method == METHOD_TASK_PUSH_NOTIFICATION_CONFIG_GET) {
            ev = j.at(JSON_FIELD_RESULT).get<TaskPushNotificationConfig>();
        } else if (userData->method == METHOD_TASK_PUSH_NOTIFICATION_CONFIG_LIST) {
            ev = j.at(JSON_FIELD_RESULT).get<std::vector<TaskPushNotificationConfig>>();
        } else if (userData->method == METHOD_TASK_PUSH_NOTIFICATION_CONFIG_DELETE) {
            ev = std::monostate{};
        } else if (userData->method == METHOD_AGENT_CARD_GET) {
            ev = j.get<AgentCard>();
        } else {
            ev = TransportError{static_cast<int>(JSONRPCErrorCode::METHOD_NOT_FOUND),
                "Method not found in response data"};
        }
    } catch (const nlohmann::json::exception& e) {
        ev = TransportError{static_cast<int>(JSONRPCErrorCode::PARSE_ERROR), e.what()};
    }

    transportEventCb_(userData->requestId, ev);
}

void JsonRpcTransport::Close()
{
    conn_->Terminate();
}

void JsonRpcTransport::ApplyInterceptors(const std::string& method, nlohmann::json& payload,
    std::map<std::string, std::string>& headers, const ClientCallContext* context) const
{
    for (auto& i : interceptors_) {
        i->Intercept(method, payload, headers, agentCard_.get(), context);
    }
}

void JsonRpcTransport::Send(const std::string& requestId, const std::string& method, const nlohmann::json& payload,
    const ClientCallContext* context)
{
    nlohmann::json rpc {
        {JSON_FIELD_JSONRPC, JSON_VERSION},
        {JSON_FIELD_ID, requestId},
        {JSON_FIELD_METHOD, method},
        {JSON_FIELD_PARAMS, payload}
    };

    std::map<std::string, std::string> headers;
    ApplyInterceptors(method, rpc, headers, context);

    UserData userData;
    userData.requestId = requestId;
    userData.method = method;
    if (method == METHOD_MESSAGE_STREAM || method == METHOD_TASK_RESUBSCRIBE) {
        userData.isStream = true;
    }
    conn_->SendMessage(rpc.dump(), headers, &userData);
}

void JsonRpcTransport::OnStreamResp(const ConnEventData& message, const UserData* userData)
{
    TransportEvent ev;
    if (message.isStreamFin) {
        transportEventCb_(userData->requestId, TransportError{0, ""});
        return;
    }

    try {
        nlohmann::json data = nlohmann::json::parse(message.data);
        if (!data.contains(JSON_FIELD_RESULT)) {
            transportEventCb_(userData->requestId, TransportError{1, "result not found"});
            return;
        }
        if (data.contains(JSON_FIELD_ID) && data.at(JSON_FIELD_ID).get<std::string>() != userData->requestId) {
            ev = TransportError{static_cast<int>(JSONRPCErrorCode::INVALID_AGENT_RESPONSE),
                "RequestId in response data does not match with requestId in request"};
            transportEventCb_(userData->requestId, ev);
            return;
        }

        auto& j = data.at(JSON_FIELD_RESULT);
        if (j.contains(JSON_FIELD_KIND) && j.at(JSON_FIELD_KIND) == STREAM_RESPONSE_TYPE_TASK) {
            ev = j.get<Task>();
        } else if (j.contains(JSON_FIELD_KIND) && j.at(JSON_FIELD_KIND) == STREAM_RESPONSE_TYPE_STATUS_UPDATE) {
            ev = j.get<TaskStatusUpdateEvent>();
        } else if (j.contains(JSON_FIELD_KIND) && j.at(JSON_FIELD_KIND) == STREAM_RESPONSE_TYPE_ARTIFACT_UPDATE) {
            ev = j.get<TaskArtifactUpdateEvent>();
        }
    } catch (const nlohmann::json::exception& e) {
        ev = TransportError{static_cast<int>(JSONRPCErrorCode::PARSE_ERROR), e.what()};
    }
    transportEventCb_(userData->requestId, ev);
}

} // namespace A2A::Client
