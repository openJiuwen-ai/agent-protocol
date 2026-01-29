/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <memory>
#include <variant>

#include "server/server_impl.h"
#include "server/task_store.h"
#include "server/agent_executor.h"
#include "types.h"
#include "server/http_server_builder.h"

namespace A2A::Server {

HttpServerBuilder::~HttpServerBuilder() = default;

std::shared_ptr<Server> HttpServerBuilder::Build(const HttpConfig& config,
    std::shared_ptr<AgentCard> agentCard,
    std::shared_ptr<AgentCard> extendedAgentCard,
    std::shared_ptr<AgentExecutor> agentExecutor,
    std::shared_ptr<TaskStore> taskStore)
{
    ServerConfig serverConfig = HttpConfig{config.ip, config.port};
    auto server = std::make_shared<ServerImpl>(
        agentCard,
        extendedAgentCard,
        agentExecutor,
        serverConfig,
        taskStore
    );

    return server;
}

} // namespace A2A::Server