
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "a2a_card_resolver_impl.h"
#include "client/a2a_card_resolver.h"

namespace a2a::client {

A2ACardResolver::A2ACardResolver(std::string baseUrl, std::string agentCardPath, std::string localPath)
    : impl_(std::make_unique<A2ACardResolverImpl>(baseUrl, agentCardPath, localPath))
{
}

A2ACardResolver::~A2ACardResolver() = default;

AgentCard A2ACardResolver::GetAgentCard(const std::optional<std::string>& relativeCardPath, long connectTimeoutMs,
                                        long readTimeoutMs) const
{
    return impl_->GetAgentCard(relativeCardPath, connectTimeoutMs, readTimeoutMs);
}

std::vector<AgentCard> A2ACardResolver::GetAllAgentCards(const std::optional<std::string>& localPath) const
{
    return impl_->GetAllAgentCards(localPath);
}

} // namespace a2a::client
