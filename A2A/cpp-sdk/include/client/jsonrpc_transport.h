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

    /**
     * @brief Send a non-streaming message/send JSON-RPC request.
     * @param[in] requestId Correlation ID for the transport callback.
     * @param[in] request   Message send parameters.
     * @param[in] context   Per-request client context.
     * @param[in] timeout   Timeout in seconds.
     */
    void SendMessage(const std::string& requestId, const MessageSendParams& request,
        const ClientCallContext* context, int timeout) override;

    /**
     * @brief Send a streaming message/stream JSON-RPC request.
     * @param[in] requestId Correlation ID for the transport callback.
     * @param[in] request   Message send parameters.
     * @param[in] context   Per-request client context.
     * @param[in] timeout   Timeout in seconds.
     */
    void SendMessageStreaming(const std::string& requestId, const MessageSendParams& request,
        const ClientCallContext* context, int timeout) override;

    /**
     * @brief Send a tasks/get JSON-RPC request.
     * @param[in] requestId Correlation ID for the transport callback.
     * @param[in] params    Task query parameters.
     * @param[in] context   Per-request client context.
     * @param[in] timeout   Timeout in seconds.
     */
    void GetTask(const std::string& requestId, const TaskQueryParams& params,
        const ClientCallContext* context, int timeout) override;

    /**
     * @brief Send a tasks/cancel JSON-RPC request.
     * @param[in] requestId Correlation ID for the transport callback.
     * @param[in] params    Task ID parameters.
     * @param[in] context   Per-request client context.
     * @param[in] timeout   Timeout in seconds.
     */
    void CancelTask(const std::string& requestId, const TaskIdParams& params,
        const ClientCallContext* context, int timeout) override;

    /**
     * @brief Send a push-notification config set JSON-RPC request.
     * @param[in] requestId Correlation ID for the transport callback.
     * @param[in] config    Push-notification configuration.
     * @param[in] context   Per-request client context.
     * @param[in] timeout   Timeout in seconds.
     */
    void SetTaskPushNotificationConfig(const std::string& requestId, const TaskPushNotificationConfig& config,
        const ClientCallContext* context, int timeout) override;

    /**
     * @brief Send a push-notification config get JSON-RPC request.
     * @param[in] requestId Correlation ID for the transport callback.
     * @param[in] params    Query parameters.
     * @param[in] context   Per-request client context.
     * @param[in] timeout   Timeout in seconds.
     */
    void GetTaskPushNotificationConfig(const std::string& requestId,
        const GetTaskPushNotificationConfigParams& params, const ClientCallContext* context, int timeout) override;

    /**
     * @brief Send a push-notification config list JSON-RPC request.
     * @param[in] requestId Correlation ID for the transport callback.
     * @param[in] params    Query parameters.
     * @param[in] context   Per-request client context.
     * @param[in] timeout   Timeout in seconds.
     */
    void ListTaskPushNotificationConfigs(const std::string& requestId,
        const ListTaskPushNotificationConfigParams& params, const ClientCallContext* context, int timeout) override;

    /**
     * @brief Send a push-notification config delete JSON-RPC request.
     * @param[in] requestId Correlation ID for the transport callback.
     * @param[in] params    Delete parameters.
     * @param[in] context   Per-request client context.
     * @param[in] timeout   Timeout in seconds.
     */
    void DeleteTaskPushNotificationConfig(const std::string& requestId,
        const DeleteTaskPushNotificationConfigParams& params, const ClientCallContext* context, int timeout) override;

    /**
     * @brief Resubscribe to task streaming events via JSON-RPC.
     * @param[in] requestId Correlation ID for the transport callback.
     * @param[in] params    Task ID parameters.
     * @param[in] context   Per-request client context.
     * @param[in] timeout   Timeout in seconds.
     */
    void Resubscribe(const std::string& requestId, const TaskIdParams& params,
        const ClientCallContext* context, int timeout) override;

    /**
     * @brief Send an agent/getCard JSON-RPC request.
     * @param[in] requestId Correlation ID for the transport callback.
     * @param[in] context   Per-request client context.
     * @param[in] timeout   Timeout in seconds.
     */
    void GetCard(const std::string& requestId, const ClientCallContext* context, int timeout) override;

    /**
     * @brief Register the callback for inbound transport events.
     * @param[in] callback Function invoked for each parsed JSON-RPC response event.
     */
    void SetTransportCallback(TransportEventCallback callback) override;

    /**
     * @brief Close the transport and release the underlying HTTP connection.
     * @note 调用后不应再执行任何操作。
     */
    void Close() override;

    /**
     * @brief Add a request interceptor middleware.
     * @param[in] middleware Interceptor applied to every outbound JSON-RPC call.
     */
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
