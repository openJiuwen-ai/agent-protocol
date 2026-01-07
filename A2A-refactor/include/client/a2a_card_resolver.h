/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_CARD_RESOLVER
#define A2A_CARD_RESOLVER

#include <optional>
#include <string>
#include <future>

#include "utils/types.h"

namespace A2A::Client {

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
    A2ACardResolver(std::string baseUrl, std::string agentCardPath, std::string localPath = "");

    /**
     * @brief destructor
     */
    ~A2ACardResolver();

    /**
     * @brief get AgentCard from relative path
     *
     * @param[in] relativeCardPath relative card path
     * @param[in] httpKwargs http arguments
     * @return future to valid AgentCard if read successful
     */
    std::future<AgentCard> GetAgentCard(const std::optional<std::string>& relativeCardPath = std::nullopt,
        const std::map<std::string, std::string>& httpKwargs) const;

    /**
     * @brief get all AgentCard from local path
     *
     * @param[in] localPath local path
     * @return future to vector of AgentCards
     */
    std::future<std::vector<AgentCard>> GetAllAgentCards(
        const std::optional<std::string>& localPath = std::nullopt) const;

private:
    std::unique_ptr<A2ACardResolverImpl> impl_;
};

} // namespace A2A::Client

#endif
