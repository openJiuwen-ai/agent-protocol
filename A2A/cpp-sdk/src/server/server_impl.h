/*
* Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_A2A_SERVER_IMPL
#define A2A_A2A_SERVER_IMPL

#include <memory>

#include "http_server_transport.h"
#include "jsonrpc_handler.h"
#include "server/server.h"

namespace a2a::server {

class ServerImpl {
public:
    ServerImpl(const std::string& transportType, std::shared_ptr<RequestHandler> handler,
               std::shared_ptr<AgentCard> agentCard);

    ~ServerImpl();

    int Start(const std::string& ip, int port);

    void Stop();

    AgentCard OnGetAuthenticatedExtendedCard(const ServerCallContext* context = nullptr);

    AgentCard OnGetCard(const ServerCallContext* context = nullptr);

private:
    std::string transportType_;
    std::shared_ptr<RequestHandler> handler_;
    std::shared_ptr<AgentCard> agentCard_;
    std::unique_ptr<JSONRPCHandler> jsonRpcHandler_;
    std::unique_ptr<a2a::transport::HttpServerTransport> transport_;
};

} // namespace a2a::server

#endif