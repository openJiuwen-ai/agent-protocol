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

class ClientFactory {
public:
    /**
    * @brief destructor
    */
    ~ClientFactory();

    /**
    * @brief create client
    *
    * @param[in] card agent card
    * @param[in] config client config
    * @param[in] consumers consumers of client being created
    * @param[in] interceptors interceptors of client being created
    * @return unique pointer of client been created
    */
    static std::shared_ptr<Client> Create(const AgentCard& card, const ClientConfig& config,
        const std::vector<Consumer>& consumers = {},
        const std::vector<std::shared_ptr<ClientCallInterceptor>>& interceptors = {});

    /**
    * @brief create client with a custom transport
    *
    * @param[in] card agent card
    * @param[in] config client config
    * @param[in] transport client transport
    * @param[in] consumers consumers of client being created
    * @return unique pointer of client been created
    */
    static std::shared_ptr<Client> Create(const AgentCard& card, const ClientConfig& config,
        std::shared_ptr<ClientTransport> transport, const std::vector<Consumer>& consumers = {});
};

} // namespace A2A::Client

#endif