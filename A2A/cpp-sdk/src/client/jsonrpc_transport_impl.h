/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_JSONRPC_TRANSPORT_IMPL
#define A2A_JSONRPC_TRANSPORT_IMPL

#include <map>
#include <memory>
#include <vector>
#include <string>
#include <nlohmann/json.hpp>
#include <unordered_map>

#include "client/client_call_interceptor.h"
#include "client/client_transport.h"
#include "types.h"
#include "client_conn.h"


namespace A2A::Client {

class JsonRpcTransportImpl {
public:
    JsonRpcTransportImpl(const std::string& url, const AgentCard& agentCard, const ClientConfig& config,
        const std::vector<std::shared_ptr<ClientCallInterceptor>>& interceptors);

    ~JsonRpcTransportImpl();

    void SendMessage(const std::string& requestId, const MessageSendParams& request,
        const ClientCallContext* context, int timeout, const std::string& method);

    void SendMessageStreaming(const std::string& requestId, const MessageSendParams& request,
        const ClientCallContext* context, int timeout);

    void GetTask(const std::string& requestId, const TaskQueryParams& params,
        const ClientCallContext* context, int timeout);

    void CancelTask(const std::string& requestId, const TaskIdParams& params,
        const ClientCallContext* context, int timeout);

    void SetTaskPushNotificationConfig(const std::string& requestId, const TaskPushNotificationConfig& config,
        const ClientCallContext* context, int timeout);

    void GetTaskPushNotificationConfig(const std::string& requestId,
        const GetTaskPushNotificationConfigParams& params, const ClientCallContext* context, int timeout);

    void ListTaskPushNotificationConfigs(const std::string& requestId,
        const ListTaskPushNotificationConfigParams& params, const ClientCallContext* context, int timeout);

    void DeleteTaskPushNotificationConfig(const std::string& requestId,
        const DeleteTaskPushNotificationConfigParams& params, const ClientCallContext* context, int timeout);

    void Resubscribe(const std::string& requestId, const TaskIdParams& params,
        const ClientCallContext* context, int timeout);

    void GetCard(const std::string& requestId, const ClientCallContext* context, int timeout);

    void SetTransportCallback(TransportEventCallback callback);

    void Close();

    void OnTransportMessage(const ConnEventData& message, const UserData& userData);

    std::shared_ptr<ClientConn> GetConn();

    void AddRequestMiddleware(const std::shared_ptr<ClientCallInterceptor>& middleware);

private:
    void ApplyInterceptors(const std::string& method, std::string& payload,
        std::map<std::string, std::string>& headers, const ClientCallContext* context) const;

    int Send(const std::string& requestId, const std::string& method, const nlohmann::json& payload,
        const ClientCallContext* context, int timeout);

    void OnStreamResp(const ConnEventData& message, const UserData& userData);

    void OnNonStreamResp(const UserData& userData, const nlohmann::json& data);

    std::string url_;
    std::shared_ptr<AgentCard> agentCard_;
    std::vector<std::shared_ptr<ClientCallInterceptor>> interceptors_;
    TransportEventCallback transportEventCb_;
    std::shared_ptr<ClientConn> conn_;

    std::mutex requestMutex_;
    std::unordered_map<std::string, std::shared_ptr<UserData>> requestData_;
};

class SessionTransportCallback : public ConnCallback {
public:
    ~SessionTransportCallback() override = default;

    explicit SessionTransportCallback(JsonRpcTransportImpl* t) : t_(t)
    {
    }

    void OnMessageReceived(const ConnEventData& message, const UserData& userData) override;

    void OnDisconnected([[maybe_unused]] const std::string& reason) override
    {
    }

private:
    JsonRpcTransportImpl* t_;
};

} // namespace A2A::Client
#endif