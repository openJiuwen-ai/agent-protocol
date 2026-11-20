/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef HTTP_CARD_RESOLVER_BUILDER
#define HTTP_CARD_RESOLVER_BUILDER

#include <cstdint>
#include <string>
#include <future>
#include <memory>

#include "client/a2a_card_resolver.h"
#include "types.h"

namespace A2A::Client {

/**
 * @brief TTL cache configuration for HttpCardResolver.
 * @note Agent cards are static in most deployments, so repeated discovery of
 *       the same card is pure overhead; disabled by default.
 */
struct CardCacheConfig {
    /** @brief Whether resolved agent cards are cached (default: false). */
    bool enabled = false;
    /** @brief Lifetime of a cached card in seconds (default: 60); expired
     *         entries are served while refreshed in the background. Enabled
     *         caching requires ttl > 0: non-positive values disable caching (WARN). */
    int ttl = 60;
    /** @brief Maximum number of cached cards (default: 64); the least recently
     *         refreshed entry is evicted first. Enabled caching requires
     *         maxEntries > 0: zero disables caching (WARN). */
    uint32_t maxEntries = 64;
    /** @brief Base retry backoff after a failed background refresh, in
     *         milliseconds (exponential, capped at 32x, reset on success);
     *         0 = retry on every expired hit; negative values count as 0. */
    int refreshRetryInterval = 1000;
};

/**
 * @brief Factory for HTTP-based A2ACardResolver instances.
 */
class HttpCardResolverBuilder final {
public:
    /** @brief Destructor. */
    ~HttpCardResolverBuilder();

    /**
     * @brief Build an HTTP card resolver.
     * @param[in] baseUrl        Server base URL (e.g. "http://host:port").
     * @param[in] agentCardPath  Path to the agent card (default: /.well-known/agent-card.json).
     * @param[in] httpKwargs     Extra HTTP headers or options.
     *            @note Reserved: not applied to discovery requests yet;
     *            values are currently ignored.
     * @param[in] cacheConfig    TTL cache configuration (default: disabled).
     * @return Shared pointer to the created resolver.
     * @note The discovery timeout is passed per call via A2ACardResolver::GetAgentCard().
     */
    static std::shared_ptr<A2ACardResolver> Build(std::string baseUrl, std::string agentCardPath,
        const std::map<std::string, std::string>& httpKwargs = {}, const CardCacheConfig& cacheConfig = {});
};

} // namespace A2A::Client

#endif
