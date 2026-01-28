/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_JSON_HANDLER
#define A2A_JSON_HANDLER

#include <nlohmann/json.hpp>

#include "request_handler.h"

namespace A2A::Server {

// Light-weight JSON-RPC handler similar to Python's JSONRPCHandler.
class JSONRPCHandler {
public:
    explicit JSONRPCHandler(std::shared_ptr<RequestHandler> handler) : handler_(handler)
    {
    }

    ~JSONRPCHandler() = default;

    /**
     * @brief handle non-streaming request reveiced
     *
     * @param[in] req requset data in json format
     * @return response json data
     */
    nlohmann::json OnMessageSend(const nlohmann::json& req);

    /**
     * @brief handle get task request
     *
     * @param[in] req requset data in json format
     * @return response json data
     */
    nlohmann::json OnGetTask(const nlohmann::json& req);

    /**
     * @brief handle cancel task request
     *
     * @param[in] req requset data in json format
     * @return response json data
     */
    nlohmann::json OnCancelTask(const nlohmann::json& req);

    /**
     * @brief handle set or update push notification config request reveiced
     *
     * @param[in] req requset data in json format
     * @return response json data
     */
    nlohmann::json OnSetPushNotificationConfig(const nlohmann::json& req);

    /**
     * @brief handle get push notification config request reveiced
     *
     * @param[in] req requset data in json format
     * @return response json data
     */
    nlohmann::json OnGetPushNotificationConfig(const nlohmann::json& req);

    /**
     * @brief handle get all push notification config request reveiced
     *
     * @param[in] req requset data in json format
     * @return response json data
     */
    nlohmann::json OnListPushNotificationConfig(const nlohmann::json& req);

    /**
     * @brief handle delete push notification config request reveiced
     *
     * @param[in] req requset data in json format
     * @return response json data
     */
    nlohmann::json OnDeletePushNotificationConfig(const nlohmann::json& req);

    /**
     * @brief handle get agent card request reveiced
     *
     * @param[in] req requset data in json format
     * @return response json data
     */
    nlohmann::json OnGetAgentCard(const nlohmann::json& req);

    /**
     * @brief handle get extened agent card request reveiced
     *
     * @param[in] req requset data in json format
     * @return response json data
     */
    nlohmann::json OnGetAuthenticatedExtendedCard(const nlohmann::json& req);

    /**
     * @brief handle streaming request reveiced
     *
     * @param[in] req requset data in json format
     * @param[in] emit emiter to call to update message status
     * @return response json data
     */
    void OnMessageSendStreaming(const nlohmann::json& req, const RequestHandler::StreamEmitter& emit);

    /**
     * @brief handle resubscribe to task request
     *
     * @param[in] req requset data in json format
     * @param[in] emit emiter to call to update message status
     * @return response json data
     */
    void OnResubscribeToTask(const nlohmann::json& req, const RequestHandler::StreamEmitter& emit);

private:
    std::shared_ptr<RequestHandler> handler_;
};

} // namespace A2A::Server

#endif
