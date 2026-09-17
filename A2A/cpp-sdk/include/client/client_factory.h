/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_CLIENT_FACTORY
#define A2A_CLIENT_FACTORY

#include <memory>
#include <vector>

#include "client/client.h"
#include "client/client_transport.h"
#include "types.h"

namespace A2A::Client {

/**
 * @brief Factory for creating configured A2A Client instances.
 */
class ClientFactory {
public:
    /** @brief Destructor. */
    ~ClientFactory();

    /**
     * @brief Create a client with the default JSON-RPC transport.
     * @param[in] card         Resolved agent card.
     * @param[in] config       Client behaviour configuration.
     * @param[in] consumers    Optional event consumers.
     * @param[in] interceptors Optional request interceptors.
     * @return Shared pointer to the created client.
     */
    static std::shared_ptr<Client> Create(const AgentCard& card, const ClientConfig& config,
        const std::vector<Consumer>& consumers = {},
        const std::vector<std::shared_ptr<ClientCallInterceptor>>& interceptors = {});

    /**
     * @brief Create a client with a custom transport implementation.
     * @param[in] card       Resolved agent card.
     * @param[in] config     Client behaviour configuration.
     * @param[in] transport  Custom transport layer.
     * @param[in] consumers  Optional event consumers.
     * @return Shared pointer to the created client.
     */
    static std::shared_ptr<Client> Create(const AgentCard& card, const ClientConfig& config,
        std::shared_ptr<ClientTransport> transport, const std::vector<Consumer>& consumers = {});
};

} // namespace A2A::Client

#endif
