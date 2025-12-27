/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_CARD_RESOLVER
#define A2A_CARD_RESOLVER

#include <map>
#include <memory>
#include <optional>
#include <string>

#include "utils/types.h"

namespace a2a::client {

class A2ACardResolverImpl;

class A2ACardResolver {
public:
    /**
     * @brief constructor
     *
     * @param[in] baseUrl base url
     * @param[in] agentCardPath agent card path
     * @param[in] localPath local path
     */
    A2ACardResolver(std::string baseUrl, std::string agentCardPath = "/.well-known/agent-card.json",
                    std::string localPath = "");

    /**
     * @brief destructor
     */
    ~A2ACardResolver();

    /**
     * @brief get AgentCard from relative path
     *
     * @param[in] relativeCardPath relative card path
     * @param[in] connectTimeoutMs connect timeout, defaults to 10000ms
     * @param[in] readTimeoutMs read data timeout, defaults to 10000ms
     * @return valid AgentCard if read successful
     */
    AgentCard GetAgentCard(const std::optional<std::string>& relativeCardPath = std::nullopt,
                           long connectTimeoutMs = 10000, long readTimeoutMs = 10000) const;

    /**
     * @brief get all AgentCard from local path
     *
     * @param[in] localPath local path
     * @return vector of AgentCards
     */
    std::vector<AgentCard> GetAllAgentCards(const std::optional<std::string>& localPath = std::nullopt) const;

private:
    std::unique_ptr<A2ACardResolverImpl> impl_;
};

} // namespace a2a::client
#endif
