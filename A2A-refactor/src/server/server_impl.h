/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_A2A_SERVER_IMPL
#define A2A_A2A_SERVER_IMPL

#include <memory>

#include "http_server_transport.h"
#include "jsonrpc_handler.h"
#include "server/agent_executor.h"
#include "server/ipc_server_builder.h"
#include "server/server.h"
#include "server/task_store.h"

namespace A2A::Server {

using ServerConfig = std::variant<HttpConfig, IpcConfig>;

class ServerImpl : public Server {
public:
    ServerImpl(std::shared_ptr<AgentCard> agentCard,
        std::shared_ptr<AgentCard> extendedAgentCard,
        const std::shared_ptr<AgentExecutor>& agent_executor,
        ServerConfig config,
        const std::shared_ptr<TaskStore>& taskStore);

    ~ServerImpl() override;

    int Start() override;

    void Stop() override;

    AgentCard OnGetAuthenticatedExtendedCard(const ServerCallContext* context = nullptr);

    AgentCard OnGetCard(const ServerCallContext* context = nullptr);

private:
    std::shared_ptr<AgentCard> agentCard_;
    std::shared_ptr<AgentCard> extendedAgentCard_;
    ServerConfig config_;
    std::shared_ptr<RequestHandler> handler_;
    std::unique_ptr<JSONRPCHandler> jsonRpcHandler_;
    std::unique_ptr<Transport::ServerTransport> transport_;
};

} // namespace A2A::Server

#endif