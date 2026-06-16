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

/** @brief Opaque HTTP client connection (libcurl). */
class ClientConn;

namespace Client {

/** @brief PIMPL holder for JsonRpcTransport. */
class JsonRpcTransportImpl;

/**
 * @brief JSON-RPC over HTTP implementation of ClientTransport.
 * @note 默认客户端传输层，支持 SSE 流式响应。
 */
class JsonRpcTransport : public ClientTransport {
public:
    /**
     * @brief Construct a JSON-RPC transport.
     * @param[in] url          JSON-RPC endpoint URL.
     * @param[in] agentCard    Resolved agent card.
     * @param[in] config       Client configuration.
     * @param[in] interceptors Request interceptors applied to every call.
     */
    JsonRpcTransport(const std::string& url, const AgentCard& agentCard, const ClientConfig& config,
        const std::vector<std::shared_ptr<ClientCallInterceptor>>& interceptors);

    /** @brief Destructor; closes the underlying connection. */
    ~JsonRpcTransport() override;

    /** @copydoc ClientTransport::SendMessage */
    void SendMessage(const std::string& requestId, const MessageSendParams& request,
        const ClientCallContext* context, int timeout) override;

    /** @copydoc ClientTransport::SendMessageStreaming */
    void SendMessageStreaming(const std::string& requestId, const MessageSendParams& request,
        const ClientCallContext* context, int timeout) override;

    /** @copydoc ClientTransport::GetTask */
    void GetTask(const std::string& requestId, const TaskQueryParams& params,
        const ClientCallContext* context, int timeout) override;

    /** @copydoc ClientTransport::CancelTask */
    void CancelTask(const std::string& requestId, const TaskIdParams& params,
        const ClientCallContext* context, int timeout) override;

    /** @copydoc ClientTransport::SetTaskPushNotificationConfig */
    void SetTaskPushNotificationConfig(const std::string& requestId, const TaskPushNotificationConfig& config,
        const ClientCallContext* context, int timeout) override;

    /** @copydoc ClientTransport::GetTaskPushNotificationConfig */
    void GetTaskPushNotificationConfig(const std::string& requestId,
        const GetTaskPushNotificationConfigParams& params, const ClientCallContext* context, int timeout) override;

    /** @copydoc ClientTransport::ListTaskPushNotificationConfigs */
    void ListTaskPushNotificationConfigs(const std::string& requestId,
        const ListTaskPushNotificationConfigParams& params, const ClientCallContext* context, int timeout) override;

    /** @copydoc ClientTransport::DeleteTaskPushNotificationConfig */
    void DeleteTaskPushNotificationConfig(const std::string& requestId,
        const DeleteTaskPushNotificationConfigParams& params, const ClientCallContext* context, int timeout) override;

    /** @copydoc ClientTransport::Resubscribe */
    void Resubscribe(const std::string& requestId, const TaskIdParams& params,
        const ClientCallContext* context, int timeout) override;

    /** @copydoc ClientTransport::GetCard */
    void GetCard(const std::string& requestId, const ClientCallContext* context, int timeout) override;

    /** @copydoc ClientTransport::SetTransportCallback */
    void SetTransportCallback(TransportEventCallback callback) override;

    /** @copydoc ClientTransport::Close */
    void Close() override;

    /** @copydoc ClientTransport::AddRequestMiddleware */
    void AddRequestMiddleware(const std::shared_ptr<ClientCallInterceptor>& middleware) override;

protected:
    /**
     * @brief Get the underlying HTTP connection (for testing / extension).
     * @return Shared pointer to the active ClientConn.
     */
    std::shared_ptr<ClientConn> GetConn();

private:
    std::unique_ptr<JsonRpcTransportImpl> impl_;
};

} // namespace Client
} // namespace A2A

#endif
