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

/**
 * @brief HTTP server binding and threading configuration.
 */
struct HttpConfig {
    /** @brief Listen IP address. */
    std::string ip;
    /** @brief Listen port. */
    int port;
    /** @brief Number of I/O event-loop threads. */
    unsigned int ioThreadNum = 1;
    /** @brief JSON-RPC endpoint path. */
    std::string endpoint = "/jsonrpc";
    /**
     * @brief Idle connection timeout in seconds, default 300.
     *
     * Applies only while no request is in flight: such a connection is closed once it has been
     * silent for longer than this, so idle or hung clients cannot leak file descriptors.
     * 0 disables idle reclamation.
     */
    unsigned int connectionTimeoutSeconds = 300;
    /** @brief Interval in seconds between connection sweeps, default 30 (clamped to the timeouts). */
    unsigned int connectionSweepIntervalSeconds = 30;
    /**
     * @brief In-flight request timeout in seconds, default 1800.
     *
     * Applies while a handler is still producing the response, where silence on the wire is
     * normal. Measured from the last progress on the connection (request read, or last chunk
     * written), so it only bounds handlers that never finish. 0 disables it.
     */
    unsigned int requestTimeoutSeconds = 1800;
};

/**
 * @brief Factory for creating HTTP-based A2A Server instances.
 */
class HttpServerBuilder {
public:
    /** @brief Destructor. */
    ~HttpServerBuilder();

    /**
     * @brief Build and return a configured HTTP server.
     * @param[in] config            HTTP listen configuration.
     * @param[in] agentCard         Public agent card.
     * @param[in] extendedAgentCard Authenticated extended card (may be empty).
     * @param[in] agentExecutor     User agent logic implementation.
     * @param[in] taskStore         Task persistence backend (nullptr = in-memory).
     * @return Shared pointer to the started-ready Server instance.
     * @throws std::runtime_error on invalid configuration.
     */
    static std::shared_ptr<Server> Build(const HttpConfig& config, const AgentCard& agentCard,
        const AgentCard& extendedAgentCard, std::shared_ptr<AgentExecutor> agentExecutor,
        std::shared_ptr<TaskStore> taskStore = nullptr);
};

} // namespace A2A::Server

#endif
