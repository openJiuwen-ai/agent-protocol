/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_JSONRPC_TRANSPORT
#define A2A_JSONRPC_TRANSPORT

#include <memory>
#include <vector>

#include "client/client_call_interceptor.h"
#include "client/client_transport.h"
#include "types.h"

namespace A2A {
class ClientConn;

namespace Client {

class JsonRpcTransportImpl;

class JsonRpcTransport : public ClientTransport {
public:
    JsonRpcTransport(const std::string& url, const AgentCard& agentCard, const ClientConfig& config,
        const std::vector<std::shared_ptr<ClientCallInterceptor>>& interceptors);

    ~JsonRpcTransport() override;

    /**
    * @brief send message to server and get response
    *
    * @param[in] requestId requestId
    * @param[in] request request data info
    * @param[in] context client call context
    */
    void SendMessage(const std::string& requestId, const MessageSendParams& request,
        const ClientCallContext* context, int timeout) override;

    /**
    * @brief send stream message to server and get response with callback
    *
    * @param[in] requestId requestId
    * @param[in] request request data info
    * @param[in] context client call context
    */
    void SendMessageStreaming(const std::string& requestId, const MessageSendParams& request,
        const ClientCallContext* context, int timeout) override;

    /**
    * @brief retrieve task information from server
    *
    * @param[in] requestId requestId
    * @param[in] params query params
    * @param[in] context client call context
    */
    void GetTask(const std::string& requestId, const TaskQueryParams& params,
        const ClientCallContext* context, int timeout) override;

    /**
    * @brief call server to cancel task
    *
    * @param[in] requestId requestId
    * @param[in] params task id params
    * @param[in] context client call context
    */
    void CancelTask(const std::string& requestId, const TaskIdParams& params,
        const ClientCallContext* context, int timeout) override;

    /**
    * @brief set push notification config for a specific task
    *
    * @param[in] requestId requestId
    * @param[in] config push notification config
    * @param[in] context client call context
    */
    void SetTaskPushNotificationConfig(const std::string& requestId, const TaskPushNotificationConfig& config,
        const ClientCallContext* context, int timeout) override;

    /**
    * @brief retrieve push notification config for a specific task
    *
    * @param[in] requestId requestId
    * @param[in] params task id and metadata information
    * @param[in] context client call context
    */
    void GetTaskPushNotificationConfig(const std::string& requestId,
        const GetTaskPushNotificationConfigParams& params, const ClientCallContext* context, int timeout) override;

    /**
    * @brief retrieve the list of push notification config for a specific task
    *
    * @param[in] requestId requestId
    * @param[in] params task id and metadata information
    * @param[in] context client call context
    */
    void ListTaskPushNotificationConfigs(const std::string& requestId,
        const ListTaskPushNotificationConfigParams& params, const ClientCallContext* context, int timeout) override;

    /**
    * @brief delete the list of push notification config for a specific task
    *
    * @param[in] requestId requestId
    * @param[in] params task id and metadata information
    * @param[in] context client call context
    */
    void DeleteTaskPushNotificationConfig(const std::string& requestId,
        const DeleteTaskPushNotificationConfigParams& params, const ClientCallContext* context, int timeout) override;

    /**
    * @brief resubscribe to server
    *
    * @param[in] requestId requestId
    * @param[in] params task id params
    * @param[in] context client call context
    */
    void Resubscribe(const std::string& requestId, const TaskIdParams& params,
        const ClientCallContext* context, int timeout) override;

    /**
    * @brief retrieve agent card from server
    *
    * @param[in] requestId requestId
    * @param[in] context client call context
    */
    void GetCard(const std::string& requestId, const ClientCallContext* context, int timeout) override;

    /**
    * @brief set transport callback
    *
    * @param[in] callback callback function triggered when receive response data
    */
    void SetTransportCallback(TransportEventCallback callback) override;

    /**
    * @brief close client connection and release associated resources
    *
    * @note after calling this, no further operations should be performed
    */
    void Close() override;

    /**
    * @brief add request middleware
    *
    * @param[in] middleware interceptor object
    */
    void AddRequestMiddleware(const std::shared_ptr<ClientCallInterceptor>& middleware) override;

protected:
    std::shared_ptr<ClientConn> GetConn();

private:
    std::unique_ptr<JsonRpcTransportImpl> impl_;
};

}
} // namespace A2A::Client
#endif