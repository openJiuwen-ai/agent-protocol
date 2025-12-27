/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_CLIENT_FACTORY_IMPL
#define A2A_CLIENT_FACTORY_IMPL

#include <map>
#include <string>
#include <memory>
#include <vector>
#include <functional>

#include "utils/types.h"
#include "client_transport.h"
#include "client/client_factory.h"

namespace a2a::client {

using TransportProducer = std::function<std::shared_ptr<ClientTransport>(const a2a::AgentCard&, const std::string&,
    const ClientConfig&, const std::vector<ClientCallInterceptor*>&)>;

class ClientFactoryImpl {
public:
    ClientFactoryImpl(ClientConfig config, std::vector<Consumer> consumers);

    std::unique_ptr<Client> Create(const a2a::AgentCard& card, const std::vector<Consumer>& extraConsumers,
        const std::vector<ClientCallInterceptor*>& interceptors) const;

private:
    void RegisterTransport(const std::string& label, TransportProducer producer);
    void RegisterDefaults();

    ClientConfig config_;
    std::vector<Consumer> consumers_;
    std::map<std::string, TransportProducer> registry_;
};

} // namespace a2a::client

#endif
