/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_A2A_SERVER
#define A2A_A2A_SERVER

#include <memory>

#include "server/request_handler.h"
#include "utils/types.h"

namespace a2a::server {

enum ServerTransportType {
    SERVER_TRANSPORT_TYPE_HTTP,
    SERVER_TRANSPORT_TYPE_MAX,
};

struct HttpConfig {
    std::string ip;
    int port;
};

struct ServerConfig {
    ServerTransportType type;
    std::variant<HttpConfig> config;
};

class ServerImpl;

class Server {
public:
    /**
     * @brief constructor
     *
     * @param[in] transportType transport type
     * @param[in] handler request handler
     * @param[in] agentCard agent card
     */
    Server(ServerTransportType transportType, std::shared_ptr<RequestHandler> handler,
           std::shared_ptr<AgentCard> agentCard);

    /**
     * @brief destructor
     */
    ~Server();

    /**
     * @brief start server and listen
     *
     * @param[in] config server config
     */
    int Start(const ServerConfig& config);

    /**
     * @brief stop server
     */
    void Stop();

    /**
     * @brief retrive authenticated extended agent card
     *
     * @param[in] ctx server call context
     * @return AgentCard
     */
    AgentCard OnGetAuthenticatedExtendedCard(const ServerCallContext* context = nullptr);

    /**
     * @brief retrive agent card
     *
     * @param[in] ctx server call context
     * @return AgentCard
     */
    AgentCard OnGetCard(const ServerCallContext* context = nullptr);

private:
    std::unique_ptr<ServerImpl> impl_;
};

} // namespace a2a::server

#endif