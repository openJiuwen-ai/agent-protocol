/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_CLIENT_FACTORY
#define A2A_CLIENT_FACTORY

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "client/client.h"
#include "utils/types.h"

namespace a2a::client {

class ClientFactoryImpl;

class ClientFactory {
public:
    /**
     * @brief constructor
     *
     * @param[in] config client config used when create client
     * @param[in] consumers consumers of client being created
     */
    explicit ClientFactory(ClientConfig config, std::vector<Consumer> consumers = {});

    /**
     * @brief destructor
     *
     */
    ~ClientFactory();

    /**
     * @brief create client
     *
     * @param[in] card agent card
     * @param[in] extraConsumers extra consumers of client being created
     * @param[in] interceptors interceptors of client being created
     * @return unique pointer of client been created
     */
    std::unique_ptr<Client> Create(const a2a::AgentCard& card, const std::vector<Consumer>& extraConsumers = {},
                                   const std::vector<ClientCallInterceptor*>& interceptors = {}) const;

private:
    std::unique_ptr<ClientFactoryImpl> impl_;
};

} // namespace a2a::client

#endif
