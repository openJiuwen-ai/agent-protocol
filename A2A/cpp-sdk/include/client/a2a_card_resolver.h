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
 * @note Abstract interface for agent card resolvers.
 */
class A2ACardResolver {
public:
    virtual ~A2ACardResolver() = default;

    /**
     * @brief Fetch an agent card from a relative path.
     * @param[in] relativeCardPath Optional path relative to the resolver base URL;
     *            overrides the resolver's default path for this call and forms
     *            the discovery request URL together with the base URL.
     * @param[in] timeout Discovery request timeout in seconds (0 = SDK default 60s).
     * @return Future that completes with the resolved AgentCard.
     * @throws A2AClientHTTPError on non-2xx HTTP status.
     * @throws A2AClientJSONError on invalid card JSON.
     * @note Error contract: every failure (transport errors, timeouts and
     *       worker launch failures included) is delivered through the returned
     *       future as an A2AClientException-family exception; the call itself
     *       throws synchronously only on allocation failure.
     */
    virtual std::future<AgentCard> GetAgentCard(
        const std::optional<std::string>& relativeCardPath = std::nullopt,
        int timeout = DEFAULT_CARD_TIMEOUT_SECONDS) const = 0;

    /**
     * @brief Fetch all agent cards from the resolver source.
     * @param[in] timeout Discovery request timeout in seconds (0 = SDK default 60s).
     * @return Future that completes with a vector of AgentCard objects.
     * @throws A2AClientHTTPError on transport failure.
     * @note Error contract: like GetAgentCard, every failure is delivered
     *       through the returned future; synchronous throws are limited to
     *       allocation failure.
     */
    virtual std::future<std::vector<AgentCard>> GetAllAgentCards(
        int timeout = DEFAULT_CARD_TIMEOUT_SECONDS) const = 0;

    /**
     * @brief Fetch an agent card, bypassing any locally cached copy.
     * @param[in] relativeCardPath Optional path relative to the resolver base URL.
     * @param[in] timeout Discovery request timeout in seconds (0 = SDK default 60s).
     * @return Future that completes with the freshly resolved AgentCard.
     * @note Caching implementations issue an independent new network request
     *       (never merged into an in-progress single-flight fetch for the
     *       path); in-progress background refreshes and foreground fetches
     *       are marked invalidated, so their older results are neither
     *       stored nor overwrite the fresh value.
     * @note The default implementation forwards to GetAgentCard: a resolver
     *       without caching semantics is always fresh per request and needs
     *       no override.
     * @note Error contract: like GetAgentCard, every failure (including
     *       worker launch failures) is delivered through the returned future;
     *       synchronous throws are limited to allocation failure.
     */
    virtual std::future<AgentCard> ForceRefresh(const std::optional<std::string>& relativeCardPath = std::nullopt,
        int timeout = DEFAULT_CARD_TIMEOUT_SECONDS) const
    {
        return GetAgentCard(relativeCardPath, timeout);
    }
};

} // namespace A2A::Client

#endif // A2A_CARD_RESOLVER
