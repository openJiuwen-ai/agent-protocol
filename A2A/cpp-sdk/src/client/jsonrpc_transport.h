/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
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
#include "http_client_transport.h"
#include "utils/id_generator.h"
#include "utils/types.h"

namespace a2a::client {

class JsonRpcTransport : public ClientTransport {
public:
    JsonRpcTransport(std::string url, const a2a::AgentCard* agentCard = nullptr,
                     std::vector<ClientCallInterceptor*> interceptors = {});

    /**
     * @brief send message to server and get response
     *
     * @param[in] request request data info
     * @param[in] context client call context
     * @return task or message
     */
    std::variant<Task, Message> SendMessage(const MessageSendParams& request,
                                            const ClientCallContext* context = nullptr) override;

    /**
     * @brief send stream message to server and get response with callback
     *
     * @param[in] request request data info
     * @param[in] onEvent callback function to receive response
     * @param[in] context client call context
     */
    void SendMessageStreaming(const MessageSendParams& request, const TransportEventCallback& onEvent,
                              const ClientCallContext* context = nullptr) override;

    /**
     * @brief retrive task information from server
     *
     * @param[in] params query params
     * @param[in] context client call context
     * @return Task get
     */
    Task GetTask(const TaskQueryParams& params, const ClientCallContext* context = nullptr) override;

    /**
     * @brief call server to cancel task
     *
     * @param[in] params task id params
     * @param[in] context client call context
     * @return Task canceled
     */
    Task CancelTask(const TaskIdParams& params, const ClientCallContext* context = nullptr) override;

    /**
     * @brief retrive authenticated extended agent card
     *
     * @param[in] config push notification config
     * @param[in] context client call context
     * @return TaskPushNotificationConfig
     */
    TaskPushNotificationConfig SetTaskPushNotificationConfig(const TaskPushNotificationConfig& config,
                                                             const ClientCallContext* context = nullptr) override;

    /**
     * @brief retrive authenticated extended agent card
     *
     * @param[in] params get push notification config
     * @param[in] context client call context
     * @return TaskPushNotificationConfig
     */
    TaskPushNotificationConfig GetTaskPushNotificationConfig(const GetTaskPushNotificationConfigParams& params,
                                                             const ClientCallContext* context = nullptr) override;

    /**
     * @brief retrive the list of push notification config for a specific task
     *
     * @param[in] params task id and metadata information
     * @param[in] context client call context
     * @return vector of TaskPushNotificationConfig
     */
    std::vector<TaskPushNotificationConfig> ListTaskPushNotificationConfigs(
        const ListTaskPushNotificationConfigParams& params, const ClientCallContext* context = nullptr) override;

    /**
     * @brief delete the list of push notification config for a specific task
     *
     * @param[in] params task id and metadata information
     * @param[in] context client call context
     */
    void DeleteTaskPushNotificationConfig(const DeleteTaskPushNotificationConfigParams& params,
                                          const ClientCallContext* context = nullptr) override;

    /**
     * @brief resubscribe to server
     *
     * @param[in] params task id params
     * @param[in] onEvent callback function to receive response
     * @param[in] context client call context
     */
    void Resubscribe(const TaskIdParams& params, const TransportEventCallback& onEvent,
                     const ClientCallContext* context = nullptr) override;

    /**
     * @brief retrive agent card from server
     *
     * @param[in] context client call context
     * @return AgentCard
     */
    a2a::AgentCard GetCard(const ClientCallContext* context = nullptr) override;

    /**
     * @brief close client connection and release associated resources
     *
     * @note after calling this, no further operations should be performed
     */
    void Close() override;

private:
    /**
     * @brief retrive authenticated extended agent card
     *
     * @param[in] ctx client call context
     * @return TaskPushNotificationConfig
     */
    nlohmann::json ApplyInterceptors(const std::string& method, nlohmann::json payload,
                                     std::map<std::string, std::string>& headers,
                                     const ClientCallContext* context) const;

    std::string url_;
    a2a::transport::HttpClientTransport http_;
    std::optional<a2a::AgentCard> agentCard_;
    std::vector<ClientCallInterceptor*> interceptors_;
    UUIDGenerator generator_;
};

} // namespace a2a::client
#endif
