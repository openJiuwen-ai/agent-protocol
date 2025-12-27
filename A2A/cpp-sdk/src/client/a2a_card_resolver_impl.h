/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_CARD_RESOLVER_IMPL
#define A2A_CARD_RESOLVER_IMPL

#include <optional>
#include <string>

#include "utils/types.h"

namespace a2a::client {

class A2ACardResolverImpl {
public:
    /**
     * @brief constructor
     *
     * @param[in] baseUrl base url
     * @param[in] agentCardPath agent card path
     * @param[in] localPath local path
     */
    A2ACardResolverImpl(std::string baseUrl, std::string agentCardPath, std::string localPath);

    /**
     * @brief destructor
     */
    ~A2ACardResolverImpl();

    /**
     * @brief get AgentCard from relative path
     *
     * @param[in] relativeCardPath relative card path (defaults to well-known path)
     * @param[in] connectTimeoutMs connect timeout, defaults to 10000ms
     * @param[in] readTimeoutMs read data timeout, defaults to 10000ms
     * @return valid AgentCard if read successful
     */
    AgentCard GetAgentCard(const std::optional<std::string>& relativeCardPath, long connectTimeoutMs,
                           long readTimeoutMs) const;

    /**
     * @brief get all AgentCard from local path
     *
     * @param[in] localPath local path
     * @return vector of AgentCards
     */
    std::vector<AgentCard> GetAllAgentCards(const std::optional<std::string>& localPath) const;

private:
    std::string baseUrl_;
    std::string agentCardPath_;
    std::string localPath_;
};

} // namespace a2a::client
#endif
