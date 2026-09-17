/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_CLIENT
#define A2A_CLIENT

#include <functional>
#include <future>
#include <string>
#include <memory>
#include <vector>

#include "client/client_call_interceptor.h"
#include "types.h"

namespace A2A::Client {

/**
 * @brief Configuration for A2A client behaviour.
 */
struct ClientConfig {
    /** @brief Whether the client supports streaming (message/stream). */
    bool streaming = true;
    /** @brief Prefer polling over streaming for message/send. */
    bool polling = false;
    /** @brief Ordered transport labels (e.g. "JSONRPC"). */
    std::vector<std::string> supportedTransports;
    /** @brief Prefer client transport list over server preferences. */
    bool useClientPreference = false;
    /** @brief Accepted output modes for message/send. */
    std::optional<std::vector<std::string>> acceptedOutputModes;
    /** @brief Default push-notification configs. */
    std::vector<PushNotificationConfig> pushNotificationConfigs;
};

/** @brief Streaming update payload: status or artifact event. */
using UpdateEvent = std::variant<std::monostate, TaskStatusUpdateEvent, TaskArtifactUpdateEvent>;

/** @brief Client-side event: message, error, or task update. */
using ClientEvent = std::variant<Message, A2AError, std::pair<Task, UpdateEvent>>;

/** @brief Callback that receives ClientEvent updates with the agent card. */
using Consumer = std::function<void(const ClientEvent&, const A2A::AgentCard&)>;

/** @brief Per-request response handler (alias of Consumer). */
using ResponseHandler = std::function<void(const ClientEvent&, const A2A::AgentCard&)>;

/**
 * @brief High-level A2A client API (message/send, tasks, push notifications).
 * @note 对应 A2A JSON-RPC 方法的客户端封装。
 */
class Client {
public:
    /** @brief Virtual destructor. */
    virtual ~Client() = default;

    /**
     * @brief Send a message and receive responses via handler (message/send or message/stream).
     * @param[in] msg      Message to send.
     * @param[in] context  Per-request client context.
     * @param[in] handler  Called per streaming update or once for non-streaming.
     * @param[in] timeout  Request timeout in seconds (0 = default).
     * @return Future that completes when the request finishes.
     * @throws A2AClientException via future on transport or protocol errors.
     * @note 流式模式下 handler 会被同步多次调用。
     */
    virtual std::future<void> SendMessage(const Message& msg, const ClientCallContext* context,
        ResponseHandler handler, int timeout = 0) = 0;

    /**
     * @brief Retrieve a task by ID (tasks/get).
     * @param[in] params   Task query parameters.
     * @param[in] context  Per-request client context.
     * @param[in] timeout  Request timeout in seconds (0 = default).
     * @return Future that completes with the Task object.
     * @throws A2AClientException via future on error.
     */
    virtual std::future<Task> GetTask(const TaskQueryParams& params, const ClientCallContext* context = nullptr,
        int timeout = 0) = 0;

    /**
     * @brief Cancel a task by ID (tasks/cancel).
     * @param[in] params   Task ID parameters.
     * @param[in] context  Per-request client context.
     * @param[in] timeout  Request timeout in seconds (0 = default).
     * @return Future that completes with the canceled Task.
     * @throws A2AClientException via future on error.
     */
    virtual std::future<Task> CancelTask(const TaskIdParams& params, const ClientCallContext* context = nullptr,
        int timeout = 0) = 0;

    /**
     * @brief Create or update push-notification config (tasks/pushNotificationConfig/set).
     * @param[in] cfg      Push-notification configuration.
     * @param[in] context  Per-request client context.
     * @param[in] timeout  Request timeout in seconds (0 = default).
     * @return Future that completes with the saved config.
     * @throws A2AClientException via future on error.
     */
    virtual std::future<TaskPushNotificationConfig> SetTaskPushNotificationConfig(const TaskPushNotificationConfig& cfg,
        const ClientCallContext* context = nullptr, int timeout = 0) = 0;

    /**
     * @brief Get push-notification config for a task (tasks/pushNotificationConfig/get).
     * @param[in] params   Query parameters.
     * @param[in] context  Per-request client context.
     * @param[in] timeout  Request timeout in seconds (0 = default).
     * @return Future that completes with the config.
     * @throws A2AClientException via future on error.
     */
    virtual std::future<TaskPushNotificationConfig> GetTaskPushNotificationConfig(
        const GetTaskPushNotificationConfigParams& params, const ClientCallContext* context = nullptr,
        int timeout = 0) = 0;

    /**
     * @brief List push-notification configs for a task (tasks/pushNotificationConfig/list).
     * @param[in] params   Query parameters.
     * @param[in] context  Per-request client context.
     * @param[in] timeout  Request timeout in seconds (0 = default).
     * @return Future that completes with the config list.
     * @throws A2AClientException via future on error.
     */
    virtual std::future<std::vector<TaskPushNotificationConfig>> ListTaskPushNotificationConfigs(
        const ListTaskPushNotificationConfigParams& params, const ClientCallContext* context = nullptr,
        int timeout = 0) = 0;

    /**
     * @brief Delete a push-notification config (tasks/pushNotificationConfig/delete).
     * @param[in] params   Delete parameters.
     * @param[in] context  Per-request client context.
     * @param[in] timeout  Request timeout in seconds (0 = default).
     * @return Future that completes when deletion succeeds.
     * @throws A2AClientException via future on error.
     */
    virtual std::future<void> DeleteTaskPushNotificationConfig(const DeleteTaskPushNotificationConfigParams& params,
        const ClientCallContext* context = nullptr, int timeout = 0) = 0;

    /**
     * @brief Resubscribe to task events for a given task ID.
     * @param[in] params   Task ID parameters.
     * @param[in] context  Per-request client context.
     * @param[in] handler  Called for each streaming event.
     * @param[in] timeout  Request timeout in seconds (0 = default).
     * @throws A2AClientException on transport or protocol errors.
     */
    virtual void Resubscribe(const TaskIdParams& params, const ClientCallContext* context,
        ResponseHandler handler, int timeout = 0) = 0;

    /**
     * @brief Retrieve the agent card (agent/getCard).
     * @param[in] context  Per-request client context.
     * @param[in] timeout  Request timeout in seconds (0 = default).
     * @return Future that completes with the AgentCard.
     * @throws A2AClientException via future on error.
     */
    virtual std::future<A2A::AgentCard> GetCard(const ClientCallContext* context = nullptr, int timeout = 0) = 0;

    /**
     * @brief Register a global event consumer.
     * @param[in] c Consumer callback.
     */
    virtual void AddEventConsumer(Consumer c) = 0;

    /**
     * @brief Add a request interceptor middleware.
     * @param[in] middleware Interceptor instance.
     */
    virtual void AddRequestMiddleware(std::shared_ptr<ClientCallInterceptor> middleware) = 0;

    /**
     * @brief Close the client and release all resources.
     * @note 调用后不应再执行任何操作。
     */
    virtual void Close() = 0;
};

} // namespace A2A::Client

#endif
