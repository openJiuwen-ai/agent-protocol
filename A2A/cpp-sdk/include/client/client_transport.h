/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_CLIENT_TRANSPORT
#define A2A_CLIENT_TRANSPORT

#include <functional>
#include <vector>
#include <future>

#include "client/client.h"
#include "types.h"

namespace A2A::Client {

/**
 * @brief Transport-layer error delivered via TransportEventCallback.
 */
struct TransportError {
    /** @brief A2AErrorCode or transport-specific code. */
    int errorCode;
    /** @brief Human-readable error description. */
    std::string errInfo;
};

/**
 * @brief Union of all event types a transport may deliver.
 */
using TransportEvent = std::variant<TransportError, Message, Task, TaskStatusUpdateEvent, TaskArtifactUpdateEvent,
    TaskPushNotificationConfig, std::vector<TaskPushNotificationConfig>, std::monostate, AgentCard>;

/**
 * @brief Callback invoked when a transport response arrives.
 * @param requestId Correlates with the outbound request ID.
 * @param event     Parsed response event.
 */
using TransportEventCallback = std::function<void(const std::string&, const TransportEvent&)>;

/**
 * @brief Low-level A2A client transport interface (JSON-RPC over HTTP, etc.).
 * @note 异步回调模型；由 Client 层封装为 std::future。
 */
class ClientTransport {
public:
    /** @brief Virtual destructor. */
    virtual ~ClientTransport() = default;

    /**
     * @brief Send a non-streaming message/send request.
     * @param[in] requestId Correlation ID for the callback.
     * @param[in] request   Message send parameters.
     * @param[in] context   Per-request client context.
     * @param[in] timeout   Timeout in seconds.
     */
    virtual void SendMessage(const std::string& requestId, const MessageSendParams& request,
        const ClientCallContext* context, int timeout) = 0;

    /**
     * @brief Send a streaming message/stream request.
     * @param[in] requestId Correlation ID for the callback.
     * @param[in] request   Message send parameters.
     * @param[in] context   Per-request client context.
     * @param[in] timeout   Timeout in seconds.
     */
    virtual void SendMessageStreaming(const std::string& requestId, const MessageSendParams& request,
        const ClientCallContext* context, int timeout) = 0;

    /**
     * @brief Send a tasks/get request.
     * @param[in] requestId Correlation ID.
     * @param[in] params    Task query parameters.
     * @param[in] context   Per-request client context.
     * @param[in] timeout   Timeout in seconds.
     */
    virtual void GetTask(const std::string& requestId, const TaskQueryParams& params,
        const ClientCallContext* context, int timeout) = 0;

    /**
     * @brief Send a tasks/cancel request.
     * @param[in] requestId Correlation ID.
     * @param[in] params    Task ID parameters.
     * @param[in] context   Per-request client context.
     * @param[in] timeout   Timeout in seconds.
     */
    virtual void CancelTask(const std::string& requestId, const TaskIdParams& params,
        const ClientCallContext* context, int timeout) = 0;

    /**
     * @brief Send a push-notification config set request.
     * @param[in] requestId Correlation ID.
     * @param[in] config    Push-notification configuration.
     * @param[in] context   Per-request client context.
     * @param[in] timeout   Timeout in seconds.
     */
    virtual void SetTaskPushNotificationConfig(const std::string& requestId, const TaskPushNotificationConfig& config,
        const ClientCallContext* context, int timeout) = 0;

    /**
     * @brief Send a push-notification config get request.
     * @param[in] requestId Correlation ID.
     * @param[in] params    Query parameters.
     * @param[in] context   Per-request client context.
     * @param[in] timeout   Timeout in seconds.
     */
    virtual void GetTaskPushNotificationConfig(const std::string& requestId,
        const GetTaskPushNotificationConfigParams& params, const ClientCallContext* context, int timeout) = 0;

    /**
     * @brief Send a push-notification config list request.
     * @param[in] requestId Correlation ID.
     * @param[in] params    Query parameters.
     * @param[in] context   Per-request client context.
     * @param[in] timeout   Timeout in seconds.
     */
    virtual void ListTaskPushNotificationConfigs(const std::string& requestId,
        const ListTaskPushNotificationConfigParams& params, const ClientCallContext* context, int timeout) = 0;

    /**
     * @brief Send a push-notification config delete request.
     * @param[in] requestId Correlation ID.
     * @param[in] params    Delete parameters.
     * @param[in] context   Per-request client context.
     * @param[in] timeout   Timeout in seconds.
     */
    virtual void DeleteTaskPushNotificationConfig(const std::string& requestId,
        const DeleteTaskPushNotificationConfigParams& params, const ClientCallContext* context, int timeout) = 0;

    /**
     * @brief Resubscribe to task streaming events.
     * @param[in] requestId Correlation ID.
     * @param[in] params    Task ID parameters.
     * @param[in] context   Per-request client context.
     * @param[in] timeout   Timeout in seconds.
     */
    virtual void Resubscribe(const std::string& requestId, const TaskIdParams& params,
        const ClientCallContext* context, int timeout) = 0;

    /**
     * @brief Send an agent/getCard request.
     * @param[in] requestId Correlation ID.
     * @param[in] context   Per-request client context.
     * @param[in] timeout   Timeout in seconds.
     */
    virtual void GetCard(const std::string& requestId, const ClientCallContext* context, int timeout) = 0;

    /**
     * @brief Register the callback for inbound transport events.
     * @param[in] callback Function invoked for each response event.
     */
    virtual void SetTransportCallback(TransportEventCallback callback) = 0;

    /**
     * @brief Close the transport and release resources.
     * @note 调用后不应再执行任何操作。
     */
    virtual void Close() = 0;

    /**
     * @brief Add a request interceptor middleware.
     * @param[in] middleware Interceptor instance.
     */
    virtual void AddRequestMiddleware(const std::shared_ptr<ClientCallInterceptor>& middleware) = 0;
};

} // namespace A2A::Client

#endif
