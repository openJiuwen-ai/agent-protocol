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

/** @brief Default AgentCard discovery timeout in seconds. */
constexpr int DEFAULT_CARD_TIMEOUT_SECONDS = 60;

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
     * @param[in] timeout Discovery request timeout in seconds (0 = SDK default 60s).
     * @return Future that completes with the resolved AgentCard.
     * @throws A2AClientHTTPError on non-2xx HTTP status.
     * @throws A2AClientJSONError on invalid card JSON.
     */
    virtual std::future<AgentCard> GetAgentCard(
        const std::optional<std::string>& relativeCardPath = std::nullopt,
        int timeout = DEFAULT_CARD_TIMEOUT_SECONDS) const = 0;

    /**
     * @brief Fetch all agent cards from the resolver source.
     * @param[in] timeout Discovery request timeout in seconds (0 = SDK default 60s).
     * @return Future that completes with a vector of AgentCard objects.
     * @throws A2AClientHTTPError on transport failure.
     */
    virtual std::future<std::vector<AgentCard>> GetAllAgentCards(
        int timeout = DEFAULT_CARD_TIMEOUT_SECONDS) const = 0;
};

} // namespace A2A::Client

#endif // A2A_CARD_RESOLVER
