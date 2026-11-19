/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef HTTP_CARD_RESOLVER
#define HTTP_CARD_RESOLVER

#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "client/a2a_card_resolver.h"
#include "client/client_transport.h"
#include "client/http_card_resolver_builder.h"

namespace A2A::Client {

/**
 * @brief HTTP-based A2ACardResolver implementation.
 * @note Every discovery request runs over a dedicated short-lived connection
 *       (baseUrl + relative path) torn down right after the response; the
 *       path argument genuinely forms the request URL. Cache misses are
 *       single-flight: concurrent callers of one key share the same request,
 *       so a cold start does not amplify the request count.
 * @note Lifetime: the destructor explicitly joins all workers (including
 *       discovery requests on the cache-disabled path, see
 *       ~HttpCardResolver) and blocks until unfinished requests complete;
 *       every worker class is bounded from above by its FetchCard timeout.
 */
class HttpCardResolver : public A2ACardResolver {
public:
    /**
     * @brief Construct an HTTP card resolver.
     * @param[in] baseUrl          Server base URL.
     * @param[in] relativeCardPath Default card path relative to the base URL.
     * @param[in] httpKwargs       Extra HTTP headers or options. Reserved:
     *            not applied to discovery requests yet; values are ignored.
     * @param[in] cacheConfig      TTL cache configuration (default: disabled);
     *            enabled caching requires ttl > 0 and maxEntries > 0,
     *            otherwise caching is treated as disabled.
     */
    HttpCardResolver(std::string baseUrl, const std::optional<std::string>& relativeCardPath,
                    std::map<std::string, std::string> httpKwargs = {}, const CardCacheConfig& cacheConfig = {});

    /** @brief Explicitly join all unfinished workers (foreground
     *        single-flight, background refresh and cache-disabled discovery
     *        requests; each bounded by its FetchCard timeout) instead of
     *        relying on implicit blocking from member destruction. */
    ~HttpCardResolver() override;

    /**
     * @brief Fetch an agent card from a relative path.
     * @param[in] relativeCardPath Optional path relative to the resolver base URL;
     *            overrides the resolver's default path for this call and forms
     *            the discovery request URL together with the base URL.
     * @param[in] timeout Discovery request timeout in seconds (0 = SDK default 60s).
     * @return Future that completes with the resolved AgentCard.
     */
    std::future<AgentCard> GetAgentCard(const std::optional<std::string>& relativeCardPath = std::nullopt,
        int timeout = DEFAULT_CARD_TIMEOUT_SECONDS) const override;

    std::future<std::vector<AgentCard>> GetAllAgentCards(
        int timeout = DEFAULT_CARD_TIMEOUT_SECONDS) const override;

    /**
     * @brief Drop the cached entry for a path and re-resolve from the network.
     * @param[in] relativeCardPath Optional path relative to the resolver base URL.
     * @param[in] timeout Discovery request timeout in seconds (0 = SDK default 60s).
     * @return Future that completes with the freshly resolved AgentCard.
     * @note The forced refresh issues its own independent request (it never
     *       joins an in-progress single-flight fetch for the key):
     *       in-progress background refreshes and foreground fetches are
     *       marked invalidated, so their older results are neither stored
     *       nor overwrite the fresh value.
     */
    std::future<AgentCard> ForceRefresh(const std::optional<std::string>& relativeCardPath = std::nullopt,
        int timeout = DEFAULT_CARD_TIMEOUT_SECONDS) const override;

protected:
    /** @brief Outcome of one card fetch: either the resolved card or a transport error. */
    struct FetchResult {
        bool success = false;
        AgentCard card;
        int errorCode = 0;
        std::string errorInfo;
    };

    /**
     * @brief Resolve one card document over a dedicated short-lived connection.
     * @param[in] requestId Correlation ID for the transport callback.
     * @param[in] url       Full discovery URL of this fetch.
     * @param[in] timeout   Discovery timeout in seconds (already normalized to > 0).
     * @return The resolved card or the transport error, never throws.
     * @note Each fetch builds a one-shot JsonRpcTransport on the worker stack;
     *       this relies on the transport lifecycle contract (see Close()):
     *       it joins the I/O thread, so no registered callback can outlive the
     *       stack instance. The callback is registered before any request is
     *       issued (registration is not synchronized with the I/O thread) and
     *       captures only the completion promise, never the transport or the
     *       resolver.
     */
    virtual FetchResult FetchCard(const std::string& requestId, const std::string& url, int timeout) const;

    struct CacheEntry {
        std::string key;               ///< effective card path (also the cache key)
        AgentCard card;
        std::chrono::steady_clock::time_point expiresAt;
        uint32_t refreshFailures = 0;  ///< consecutive failed background refreshes
        std::chrono::steady_clock::time_point lastRefreshAttempt{}; ///< backoff anchor (claim + failure settlement)
    };

    /// In-flight single-flight fetch and its joined callers.
    struct InFlightFetch {
        std::shared_ptr<std::promise<AgentCard>> completion = std::make_shared<std::promise<AgentCard>>();
        std::vector<std::shared_ptr<std::promise<AgentCard>>> waiters;
        bool invalidated = false;      ///< set by ForceRefresh: result must not be stored
    };

    /// In-flight background refresh for a cache key.
    struct PendingRefresh {
        bool invalidated = false;      ///< set by ForceRefresh: result must not be stored
    };

    std::string baseUrl_;
    std::optional<std::string> relativeCardPath_;
    std::map<std::string, std::string> httpKwargs_;

    CardCacheConfig cacheConfig_;      ///< TTL cache config; inert while disabled
    mutable std::shared_mutex cacheMutex_;  ///< guards all cache/flight state below
    mutable std::list<CacheEntry> cacheOrder_;  ///< refresh recency order (oldest first)
    mutable std::unordered_map<std::string, std::list<CacheEntry>::iterator> cardCache_; ///< key -> entry
    mutable std::unordered_map<std::string, PendingRefresh> pendingRefreshes_;  ///< key -> in-flight refresh
    mutable std::unordered_map<std::string, std::shared_ptr<InFlightFetch>> inFlightFetches_; ///< key -> fetch
    mutable std::mutex workerFuturesMutex_;  ///< guards workerFutures_ (reaped lazily)
    /// Fetch/refresh workers; joined at destruction so none outlive the resolver.
    mutable std::vector<std::future<void>> workerFutures_;

    std::string ResolveFullPath(const std::string& relativePath) const;
    /** @brief Effective path for a discovery call, normalized into the cache
     *         key: leading slashes are stripped so "/x" and "x" (which join
     *         into the same discovery URL) share one cache entry. */
    std::string ResolveCacheKey(const std::optional<std::string>& relativeCardPath) const;
    /** @brief Insert/refresh a cached card, evicting the least recently refreshed
     *        entry at the cap; cacheMutex_ must already be held exclusively. */
    void StoreLocked(const std::string& key, const AgentCard& card) const;
    /** @brief Start (or join) the single-flight foreground fetch for a cache key. */
    std::future<AgentCard> StartForegroundFetch(const std::string& key, const std::string& url, int timeout) const;
    /** @brief Launch a card fetch worker thread (std::async seam).
     * @note Virtual so tests can simulate std::launch::async launch failures
     *       (resource exhaustion) while the failure handling in SpawnFetchWorker
     *       still runs unmodified. */
    virtual std::future<void> LaunchWorker(std::function<void()> body) const;
    /** @brief Spawn the shared fetch worker for a registered flight; on
     *         launch failure settle the flight's waiters with the contract
     *         error, then undo the registration (only dropping this flight's
     *         own entry) so later callers retry. */
    void SpawnFetchWorker(const std::shared_ptr<InFlightFetch>& flight, const std::string& key,
        const std::string& url, int timeout) const;
    /** @brief Body of a spawned fetch worker: fetch, store unless invalidated
     *         by a ForceRefresh, then settle the flight. */
    void RunFetchWorker(const std::shared_ptr<InFlightFetch>& flight, const std::string& key,
        const std::string& url, int timeout) const;
    /** @brief Store a fetched card unless a ForceRefresh invalidated this
     *         flight: its fresh value must not be overwritten by older data. */
    void StoreUnlessInvalidated(const std::shared_ptr<InFlightFetch>& flight,
        const std::string& key, const AgentCard& card) const;
    /** @brief Unregister a finished flight and complete its waiters and promise. */
    void SettleFetch(const std::shared_ptr<InFlightFetch>& flight, const std::string& key,
        AgentCard card, std::exception_ptr error) const;
    /** @brief Kick off a background refresh (deduplicated per key, throttled by retry backoff). */
    void TriggerBackgroundRefresh(const std::string& key) const;
    /** @brief Claim the background-refresh slot for a key; false when the entry
     *         is gone, a refresh is already in flight, or the backoff window
     *         after failed refreshes has not elapsed yet. */
    bool TryRegisterBackgroundRefresh(const std::string& key) const;
    /** @brief True while a failed refresh's exponential backoff window is active. */
    bool IsUnderRefreshBackoff(const CacheEntry& entry) const;
    /** @brief Body of a background refresh worker: fetch, settle, log failures. */
    void RunBackgroundRefreshWorker(const std::string& key, const std::string& url) const;
    /** @brief Apply a finished background refresh: store on success (unless
     *         invalidated by a ForceRefresh), count the failure on error. */
    void SettleBackgroundRefresh(const std::string& key, const FetchResult& result) const;
    /** @brief Drop the pending-refresh mark so later expiry hits can retry. */
    void ClearPendingRefresh(const std::string& key) const;
    /** @brief Uncached one-shot discovery: the worker is tracked so destruction
     *         joins it, while the caller's future stays independent. */
    std::future<AgentCard> FetchUncached(const std::string& url, int timeout) const;
    /** @brief Reap finished worker futures; workerFuturesMutex_ must be held. */
    void ReapFinishedWorkersLocked() const;
    /** @brief Reap finished worker futures (runs on the GetAgentCard path). */
    void ReapWorkerFutures() const;
    /** @brief Reap finished workers, then register a new worker future:
     *         reaping at registration bounds workerFutures_ growth even on a
     *         resolver that never calls GetAgentCard (e.g. ForceRefresh-only). */
    void TrackWorker(std::future<void> worker) const;
    static FetchResult TranslateEvent(const TransportEvent& event);
    /** @brief Standard result used when the safety-net wait past the
     *         requested discovery timeout trips. */
    static FetchResult TimedOutFetchResult();
    std::exception_ptr CreateExceptionPtr(int code, const std::string& msg) const;
};

} // namespace A2A::Client

#endif
