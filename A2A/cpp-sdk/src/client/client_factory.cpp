/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "client/client_factory.h"
#include "client_factory_impl.h"

namespace a2a::client {

ClientFactory::ClientFactory(ClientConfig config, std::vector<Consumer> consumers)
    : impl_(std::make_unique<ClientFactoryImpl>(config, consumers))
{
}

ClientFactory::~ClientFactory() = default;

std::unique_ptr<Client> ClientFactory::Create(const a2a::AgentCard& card, const std::vector<Consumer>& extraConsumers,
                                              const std::vector<ClientCallInterceptor*>& interceptors) const
{
    return impl_->Create(card, extraConsumers, interceptors);
}

} // namespace a2a::client
