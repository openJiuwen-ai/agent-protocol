/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_CARD_RESOLVER
#define A2A_CARD_RESOLVER

#include <optional>
#include <string>
#include <future>

#include "utils/types.h"

namespace A2A::Client {

class A2ACardResolver {
public:

    /**
     * @brief destructor
     */
    virtual ~A2ACardResolver() = default;

    /**
     * @brief get AgentCard from relative path
     *
     * @param[in] relativeCardPath relative card path
     * @return future to valid AgentCard if read successful
     */
    virtual std::future<AgentCard> GetAgentCard(
        const std::optional<std::string>& relativeCardPath = std::nullopt) const = 0;

    /**
     * @brief get all AgentCard from local path
     *
     * @return future to vector of AgentCards
     */
    virtual std::future<std::vector<AgentCard>> GetAllAgentCards() const = 0;
};

} // namespace A2A::Client

#endif
