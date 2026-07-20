/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_HTTP_SERVER_BUILDER
#define A2A_HTTP_SERVER_BUILDER

#include <memory>
#include <types.h>

#include "task_store.h"
#include "server/server.h"
#include "server/agent_executor.h"

namespace A2A::Server {

struct HttpConfig {
    std::string ip;
    int port;
    unsigned int ioThreadNum = 1;
    std::string endpoint = "/jsonrpc";
};

class HttpServerBuilder {
public:
    /**
    * @brief destructor
    */
    ~HttpServerBuilder();

    /**
    * @brief create http server
    *
    * @param[in] config http config
    * @param[in] agentCard agent card object
    * @param[in] extendedAgentCard extended agent card object
    * @param[in] agentExecutor agent executor object
    * @param[in] taskStore store object to store task object
    * @return A shared pointer to the created server instance
    */
    static std::shared_ptr<Server> Build(const HttpConfig& config, const AgentCard& agentCard,
        const AgentCard& extendedAgentCard, std::shared_ptr<AgentExecutor> agentExecutor,
        std::shared_ptr<TaskStore> taskStore = nullptr);
};

}

#endif