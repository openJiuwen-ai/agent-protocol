/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_CARD_RESOLVER
#define A2A_CARD_RESOLVER

#include <optional>
#include <string>
#include <future>

#include "types.h"

namespace A2A::Client {

/**
 * @brief Resolves AgentCard documents from a remote or local source.
 * @note 代理卡片解析器抽象接口。
 */
class A2ACardResolver {
public:
    /** @brief Virtual destructor. */
    virtual ~A2ACardResolver() = default;

    /**
     * @brief Fetch an agent card from a relative path.
     * @param[in] relativeCardPath Optional path relative to the resolver base URL.
     * @return Future that completes with the resolved AgentCard.
     * @throws A2AClientHTTPError on non-2xx HTTP status.
     * @throws A2AClientJSONError on invalid card JSON.
     */
    virtual std::future<AgentCard> GetAgentCard(
        const std::optional<std::string>& relativeCardPath = std::nullopt) const = 0;

    /**
     * @brief Fetch all agent cards from the resolver source.
     * @return Future that completes with a vector of AgentCard objects.
     * @throws A2AClientHTTPError on transport failure.
     */
    virtual std::future<std::vector<AgentCard>> GetAllAgentCards() const = 0;
};

} // namespace A2A::Client

#endif // A2A_CARD_RESOLVER
