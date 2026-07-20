/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_JSONRPC_TRANSPORT
#define A2A_JSONRPC_TRANSPORT

#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <vector>

#include "client/client_call_interceptor.h"
#include "client_transport.h"
#include "types.h"

namespace A2A::Client {

class JsonRpcTransport;

class SessionTransportCallback : public ConnCallback {
public:
    ~SessionTransportCallback() override = default;

    explicit SessionTransportCallback(JsonRpcTransport* t) : t_(t)
    {
    }

    void OnMessageReceived(const ConnEventData& message, const UserData* userData) override;

    void OnDisconnected([[maybe_unused]] const std::string& reason) override
    {
    }

private:
    JsonRpcTransport* t_;
};

class JsonRpcTransport : public ClientTransport {
public:
    JsonRpcTransport(const std::string& url, const AgentCard& agentCard,
        const std::vector<std::shared_ptr<ClientCallInterceptor>>& interceptors);

    ~JsonRpcTransport();

    void SendMessage(const std::string& requestId, const MessageSendParams& request,
        const ClientCallContext* context) override;

    void SendMessageStreaming(const std::string& requestId, const MessageSendParams& request,
        const ClientCallContext* context) override;

    void GetTask(const std::string& requestId, const TaskQueryParams& params,
        const ClientCallContext* context) override;

    void CancelTask(const std::string& requestId, const TaskIdParams& params,
        const ClientCallContext* context) override;

    void SetTaskPushNotificationConfig(const std::string& requestId, const TaskPushNotificationConfig& config,
        const ClientCallContext* context) override;

    void GetTaskPushNotificationConfig(const std::string& requestId,
        const GetTaskPushNotificationConfigParams& params, const ClientCallContext* context) override;

    void ListTaskPushNotificationConfigs(const std::string& requestId,
        const ListTaskPushNotificationConfigParams& params, const ClientCallContext* context) override;

    void DeleteTaskPushNotificationConfig(const std::string& requestId,
        const DeleteTaskPushNotificationConfigParams& params, const ClientCallContext* context) override;

    void Resubscribe(const std::string& requestId, const TaskIdParams& params,
        const ClientCallContext* context) override;

    void GetCard(const std::string& requestId, const ClientCallContext* context) override;

    void SetTransportCallback(TransportEventCallback callback) override;

    void OnTransportMessage(const ConnEventData& message, const UserData* userData) override;

    void Close() override;

private:
    void ApplyInterceptors(const std::string& method, nlohmann::json& payload,
        std::map<std::string, std::string>& headers, const ClientCallContext* context) const;

    void Send(const std::string& requestId, const std::string& method, const nlohmann::json& payload,
        const ClientCallContext* context);

    void OnStreamResp(const ConnEventData& message, const UserData* userData);

    std::string url_;
    std::shared_ptr<AgentCard> agentCard_;
    std::vector<std::shared_ptr<ClientCallInterceptor>> interceptors_;
    TransportEventCallback transportEventCb_;
    std::shared_ptr<ClientConn> conn_;
};

} // namespace A2A::Client
#endif
