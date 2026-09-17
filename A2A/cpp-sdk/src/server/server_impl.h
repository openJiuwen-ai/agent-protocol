/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_SERVER_IMPL
#define A2A_SERVER_IMPL

#include <memory>
#include <atomic>

#include "http_server_transport.h"
#include "server_transport.h"
#include "jsonrpc_handler.h"
#include "server/agent_executor.h"
#include "server/server.h"
#include "server/task_store.h"

namespace A2A::Server {

using ServerConfig = HttpConfig;

using json = nlohmann::json;

class ServerImpl : public Server {
public:
    ServerImpl(std::shared_ptr<AgentCard> agentCard,
        std::shared_ptr<AgentCard> extendedAgentCard,
        const std::shared_ptr<AgentExecutor>& agentExecutor,
        ServerConfig config,
        std::shared_ptr<TaskStore> taskStore);

    // Constructor that accepts a custom transport
    ServerImpl(std::shared_ptr<AgentCard> agentCard,
        std::shared_ptr<AgentCard> extendedAgentCard,
        const std::shared_ptr<AgentExecutor>& agentExecutor,
        ServerConfig config,
        std::shared_ptr<Transport::ServerTransport> transport,
        std::shared_ptr<TaskStore> taskStore);

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
    std::shared_ptr<Transport::ServerTransport> transport_;
    std::atomic<bool> started_;

    void HandleStreamingRequest(
        const nlohmann::json& req, const std::string& method,
        const std::shared_ptr<Transport::TransportEmitter>& emitter);

    void HandleNonStreamingRequest(
        const nlohmann::json& req, const std::string& reqBody, std::string& respBody,
        const std::string& method, const std::shared_ptr<Transport::TransportEmitter>& emitter);

    static void CreateStreamEmitter(const nlohmann::json& req, std::function<void(const StreamEvent&)>& streamEmit,
        const std::shared_ptr<Transport::TransportEmitter>& emitter, const bool streaming);

    void ProcessStandardJsonRpc(const nlohmann::json& req, std::string& respBody, const std::string& method,
        const std::shared_ptr<Transport::TransportEmitter>& emitter);
};

} // namespace A2A::Server

#endif
