/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <algorithm>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "error.h"
#include "uuid.h"
#include "a2a_log.h"
#include "client/jsonrpc_transport.h"
#include "http_card_resolver.h"

namespace A2A::Client {

namespace {

/** @brief Extra wait margin on top of the requested discovery timeout: a
 *         wedged transport event loop cannot hang a fetch worker forever. */
constexpr std::chrono::seconds FETCH_GRACE_MARGIN{5};

std::future<AgentCard> MakeReadyFuture(AgentCard card)
{
    auto promise = std::make_shared<std::promise<AgentCard>>();
    promise->set_value(std::move(card));
    return promise->get_future();
}

} // namespace

HttpCardResolver::HttpCardResolver(std::string baseUrl, const std::optional<std::string>& relativeCardPath,
    std::map<std::string, std::string> httpKwargs, const CardCacheConfig& cacheConfig)
    : baseUrl_(std::move(baseUrl)), relativeCardPath_(relativeCardPath), httpKwargs_(std::move(httpKwargs)),
      cacheConfig_(cacheConfig)
{
    if (cacheConfig_.refreshRetryInterval < 0) {
        cacheConfig_.refreshRetryInterval = 0;
    }
    // degenerate configs are rejected loudly instead of degrading silently
    if (cacheConfig_.enabled && (cacheConfig_.ttl <= 0 || cacheConfig_.maxEntries == 0)) {
        A2A_LOG(A2A_LOG_LEVEL::WARN, "card cache misconfigured (ttl <= 0 or maxEntries == 0); disabled");
        cacheConfig_.enabled = false;
    }
    // Discovery runs over a short-lived connection per request; no eager setup.
}

HttpCardResolver::~HttpCardResolver()
{
    // Explicit join: wait for every in-flight worker so none touches freed
    // state; FetchCard bounds each worker (discovery timeout + margin).
    std::vector<std::future<void>> pending;
    {
        std::lock_guard<std::mutex> lock(workerFuturesMutex_);
        pending.swap(workerFutures_);
    }
    for (auto& worker : pending) {
        worker.wait();
    }
}

std::string HttpCardResolver::ResolveFullPath(const std::string& relativePath) const
{
    std::string fullPath = baseUrl_;
    // remove ending '/' from baseUrl
    if (!fullPath.empty() && fullPath.back() == '/') {
        fullPath.pop_back();
    }
    // ensure path starts with '/'(avoiding '//')
    if (!relativePath.empty()) {
        if (relativePath.front() != '/') {
            fullPath += '/';
        }
        fullPath += relativePath;
    }
    return fullPath;
}

std::string HttpCardResolver::ResolveCacheKey(const std::optional<std::string>& relativeCardPath) const
{
    // effective path: explicit argument, otherwise the resolver's default path;
    // the per-call path is part of the discovery URL, so distinct paths are
    // distinct remote documents with distinct cache entries
    std::string key = relativeCardPath.value_or(relativeCardPath_.value_or(""));
    // ResolveFullPath joins "/x" and "x" into the same URL (it adds the
    // joining '/' only when missing), so strip leading slashes here: both
    // spellings of one document share a single cache entry instead of
    // double-caching it under two keys with independent TTLs and eviction
    key.erase(0, key.find_first_not_of('/'));
    return key;
}

void HttpCardResolver::StoreLocked(const std::string& key, const AgentCard& card) const
{
    // caller holds cacheMutex_ exclusively; config already validated (maxEntries > 0)
    const auto expiresAt = std::chrono::steady_clock::now() + std::chrono::seconds(cacheConfig_.ttl);
    auto it = cardCache_.find(key);
    if (it != cardCache_.end()) {
        CacheEntry& entry = *it->second;
        entry.card = card;
        entry.expiresAt = expiresAt;
        entry.refreshFailures = 0; // a successful store clears the retry backoff
        cacheOrder_.splice(cacheOrder_.end(), cacheOrder_, it->second);
        return;
    }
    if (cardCache_.size() >= cacheConfig_.maxEntries) {
        // evict least recently refreshed: list order = refresh recency, O(1), no clock ties
        cardCache_.erase(cacheOrder_.front().key);
        cacheOrder_.pop_front();
    }
    CacheEntry entry;
    entry.key = key;
    entry.card = card;
    entry.expiresAt = expiresAt;
    cacheOrder_.push_back(std::move(entry));
    cardCache_.emplace(key, std::prev(cacheOrder_.end()));
}

std::future<AgentCard> HttpCardResolver::StartForegroundFetch(const std::string& key,
    const std::string& url, int timeout) const
{
    std::shared_ptr<InFlightFetch> flight;
    std::future<AgentCard> result;
    {
        std::unique_lock<std::shared_mutex> lock(cacheMutex_);
        auto cached = cardCache_.find(key);
        if (cached != cardCache_.end() && std::chrono::steady_clock::now() < cached->second->expiresAt) {
            return MakeReadyFuture(cached->second->card); // refreshed while we waited
        }
        auto infl = inFlightFetches_.find(key);
        if (infl != inFlightFetches_.end()) {
            // single-flight: join the in-flight fetch for this key
            auto joined = std::make_shared<std::promise<AgentCard>>();
            result = joined->get_future();
            infl->second->waiters.push_back(std::move(joined));
            return result;
        }
        flight = std::make_shared<InFlightFetch>();
        result = flight->completion->get_future();
        inFlightFetches_.emplace(key, flight);
    }
    SpawnFetchWorker(flight, key, url, timeout);
    return result;
}

std::future<void> HttpCardResolver::LaunchWorker(std::function<void()> body) const
{
    return std::async(std::launch::async, std::move(body));
}

void HttpCardResolver::SpawnFetchWorker(const std::shared_ptr<InFlightFetch>& flight,
    const std::string& key, const std::string& url, int timeout) const
{
    try {
        auto worker = LaunchWorker([this, flight, key, url, timeout]() { RunFetchWorker(flight, key, url, timeout); });
        TrackWorker(std::move(worker));
    } catch (const std::exception& e) {
        // the launch failed, so RunFetchWorker/SettleFetch will never run:
        // settle the flight here. Abandoning the promises instead would surface
        // std::future_error{broken_promise} to joined waiters — outside the
        // runtime_error family of the error contract — so waiters and the
        // originator alike get the launch failure wrapped as A2AClientException
        // through their futures, keeping every error path future-delivered.
        std::vector<std::shared_ptr<std::promise<AgentCard>>> waiters;
        const std::exception_ptr error = CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_TRANSPORT_EXCEPTION),
            std::string("card fetch worker failed to start: ") + e.what());
        {
            // undo registration so later callers retry; drop only this
            // flight's entry — a ForceRefresh may have replaced it
            std::unique_lock<std::shared_mutex> lock(cacheMutex_);
            auto infl = inFlightFetches_.find(key);
            if (infl != inFlightFetches_.end() && infl->second == flight) {
                inFlightFetches_.erase(infl);
            }
            waiters = std::move(flight->waiters);
        }
        for (const auto& waiter : waiters) {
            waiter->set_exception(error);
        }
        flight->completion->set_exception(error);
    }
}

void HttpCardResolver::RunFetchWorker(const std::shared_ptr<InFlightFetch>& flight,
    const std::string& key, const std::string& url, int timeout) const
{
    AgentCard card;
    std::exception_ptr error;
    try {
        const FetchResult fetched = FetchCard(GenerateUuid(), url, timeout);
        if (fetched.success) {
            card = fetched.card;
            StoreUnlessInvalidated(flight, key, card);
        } else {
            error = CreateExceptionPtr(fetched.errorCode, fetched.errorInfo);
        }
    } catch (...) {
        error = std::current_exception();
    }
    SettleFetch(flight, key, std::move(card), std::move(error));
}

void HttpCardResolver::StoreUnlessInvalidated(const std::shared_ptr<InFlightFetch>& flight,
    const std::string& key, const AgentCard& card) const
{
    std::unique_lock<std::shared_mutex> lock(cacheMutex_);
    if (!flight->invalidated) {
        // a ForceRefresh that replaced this flight must not have its fresh
        // value overwritten by this older result
        StoreLocked(key, card);
    }
}

void HttpCardResolver::SettleFetch(const std::shared_ptr<InFlightFetch>& flight,
    const std::string& key, AgentCard card, std::exception_ptr error) const
{
    std::vector<std::shared_ptr<std::promise<AgentCard>>> waiters;
    {
        std::unique_lock<std::shared_mutex> lock(cacheMutex_);
        auto infl = inFlightFetches_.find(key);
        if (infl != inFlightFetches_.end() && infl->second == flight) {
            inFlightFetches_.erase(infl);
        }
        waiters = std::move(flight->waiters);
    }
    for (const auto& waiter : waiters) {
        if (error) {
            waiter->set_exception(error);
        } else {
            waiter->set_value(card);
        }
    }
    if (error) {
        flight->completion->set_exception(std::move(error));
    } else {
        flight->completion->set_value(std::move(card));
    }
}

void HttpCardResolver::TriggerBackgroundRefresh(const std::string& key) const
{
    const std::string url = ResolveFullPath(key);
    if (!TryRegisterBackgroundRefresh(key)) {
        return;
    }
    try {
        auto worker = std::async(std::launch::async, [this, key, url]() { RunBackgroundRefreshWorker(key, url); });
        TrackWorker(std::move(worker));
    } catch (const std::exception& e) {
        // std::async failed to start: clear the mark so later hits can retry
        ClearPendingRefresh(key);
        A2A_LOG(A2A_LOG_LEVEL::WARN, std::string("background agent card refresh failed to start: ") + e.what());
    }
}

bool HttpCardResolver::TryRegisterBackgroundRefresh(const std::string& key) const
{
    std::unique_lock<std::shared_mutex> lock(cacheMutex_);
    auto it = cardCache_.find(key);
    if (it == cardCache_.end()) {
        return false; // the entry vanished (evicted/force-refreshed) meanwhile
    }
    if (pendingRefreshes_.count(key) != 0) {
        return false; // a refresh for this key is already in flight
    }
    CacheEntry& entry = *it->second;
    if (IsUnderRefreshBackoff(entry)) {
        return false; // keep serving stale without new requests
    }
    pendingRefreshes_.emplace(key, PendingRefresh{});
    entry.lastRefreshAttempt = std::chrono::steady_clock::now();
    return true;
}

bool HttpCardResolver::IsUnderRefreshBackoff(const CacheEntry& entry) const
{
    if (entry.refreshFailures == 0 || cacheConfig_.refreshRetryInterval <= 0) {
        return false;
    }
    // exponential backoff after consecutive failures, capped at 32x the base
    const uint32_t doublings = std::min<uint32_t>(entry.refreshFailures - 1, 5);
    const auto retryAfter = std::chrono::milliseconds(cacheConfig_.refreshRetryInterval) * (1u << doublings);
    return std::chrono::steady_clock::now() - entry.lastRefreshAttempt < retryAfter;
}

void HttpCardResolver::RunBackgroundRefreshWorker(const std::string& key, const std::string& url) const
{
    FetchResult result;
    try {
        result = FetchCard(GenerateUuid(), url, DEFAULT_CARD_TIMEOUT_SECONDS);
    } catch (const std::exception& e) {
        // FetchCard is never-throw by contract; this guard keeps the worker's
        // bookkeeping exception-safe anyway (FetchCard is a virtual seam, and
        // construction-time bad_alloc is possible). Clear the in-flight mark
        // so later expiry hits can retry.
        ClearPendingRefresh(key);
        A2A_LOG(A2A_LOG_LEVEL::WARN, std::string("background agent card refresh worker failed: ") + e.what());
        return;
    } catch (...) {
        ClearPendingRefresh(key);
        A2A_LOG(A2A_LOG_LEVEL::WARN, "background agent card refresh worker failed: unknown exception");
        return;
    }
    SettleBackgroundRefresh(key, result);
    if (!result.success) {
        A2A_LOG(A2A_LOG_LEVEL::WARN, std::string("background agent card refresh failed: ") + result.errorInfo);
    }
}

void HttpCardResolver::SettleBackgroundRefresh(const std::string& key, const FetchResult& result) const
{
    std::unique_lock<std::shared_mutex> lock(cacheMutex_);
    bool invalidated = false;
    auto pending = pendingRefreshes_.find(key);
    if (pending != pendingRefreshes_.end()) {
        invalidated = pending->second.invalidated;
        pendingRefreshes_.erase(pending);
    }
    if (!result.success) {
        // keep the stale entry and count the failure for backoff; re-anchor
        // the backoff window at settlement: a slow failure (e.g. a
        // multi-second timeout vs the 1s base) outlasts a claim-anchored
        // window, so the next expired hit would immediately re-refresh and
        // the exponential growth would never engage for slow failures
        auto it = cardCache_.find(key);
        if (it != cardCache_.end()) {
            ++it->second->refreshFailures;
            it->second->lastRefreshAttempt = std::chrono::steady_clock::now();
        }
        return;
    }
    if (!invalidated) {
        StoreLocked(key, result.card);
    }
}

void HttpCardResolver::ClearPendingRefresh(const std::string& key) const
{
    std::unique_lock<std::shared_mutex> lock(cacheMutex_);
    pendingRefreshes_.erase(key);
}

// workerFuturesMutex_ must be held by the caller
void HttpCardResolver::ReapFinishedWorkersLocked() const
{
    for (auto it = workerFutures_.begin(); it != workerFutures_.end();) {
        if (it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                it->get(); // fetch failures are logged inside the worker
            } catch (const std::exception& e) {
                A2A_LOG(A2A_LOG_LEVEL::WARN, std::string("card resolver worker failed: ") + e.what());
            }
            it = workerFutures_.erase(it);
        } else {
            ++it;
        }
    }
}

void HttpCardResolver::ReapWorkerFutures() const
{
    std::lock_guard<std::mutex> lock(workerFuturesMutex_);
    ReapFinishedWorkersLocked();
}

void HttpCardResolver::TrackWorker(std::future<void> worker) const
{
    std::lock_guard<std::mutex> lock(workerFuturesMutex_);
    // reap at registration: ReapWorkerFutures only runs on the GetAgentCard
    // path, so a resolver driven without it (e.g. ForceRefresh-only polling)
    // would otherwise let finished futures accumulate until destruction
    ReapFinishedWorkersLocked();
    workerFutures_.push_back(std::move(worker));
}

HttpCardResolver::FetchResult HttpCardResolver::TimedOutFetchResult()
{
    FetchResult result;
    result.errorCode = static_cast<int>(A2AErrorCode::A2A_REQUEST_TIMEOUT);
    result.errorInfo = "card fetch timed out";
    return result;
}

HttpCardResolver::FetchResult HttpCardResolver::TranslateEvent(const TransportEvent& event)
{
    if (const TransportError* err = std::get_if<TransportError>(&event)) {
        FetchResult result;
        result.errorCode = err->errorCode;
        result.errorInfo = err->errInfo;
        return result;
    }
    if (const AgentCard* card = std::get_if<AgentCard>(&event)) {
        FetchResult result;
        result.success = true;
        result.card = *card;
        return result;
    }
    FetchResult result;
    result.errorCode = static_cast<int>(A2AErrorCode::A2A_INVALID_FORMAT);
    result.errorInfo = "invalid response format for GetCard";
    return result;
}

HttpCardResolver::FetchResult HttpCardResolver::FetchCard(const std::string& requestId,
    const std::string& url, int timeout) const
{
    // one-shot HTTP GET over a dedicated short-lived connection; the whole
    // body (including the promise and transport construction, which can
    // raise bad_alloc / connection-start failures) is guarded so the
    // never-throws contract actually holds
    try {
        auto completion = std::make_shared<std::promise<FetchResult>>();
        auto done = completion->get_future();
        JsonRpcTransport transport(url, AgentCard{}, ClientConfig{},
            std::vector<std::shared_ptr<ClientCallInterceptor>>{});
        transport.SetTransportCallback([completion](const std::string&, const TransportEvent& event) {
            const FetchResult result = TranslateEvent(event);
            try {
                completion->set_value(result);
            } catch (const std::future_error&) {
                // Intentional no-op: the promise completes exactly once and the
                // first transport event wins. Close() below can race a second
                // event in (e.g. an error alongside the card), whose set_value
                // then throws promise_already_satisfied — swallowed here so the
                // duplicate event is dropped instead of failing the fetch.
            }
        });
        transport.GetCard(requestId, nullptr, timeout);

        // safety net past the requested timeout: a wedged event loop cannot
        // hang the fetch worker
        const auto waitBudget = std::chrono::seconds(timeout) + FETCH_GRACE_MARGIN;
        if (done.wait_for(waitBudget) != std::future_status::ready) {
            transport.Close();
            return TimedOutFetchResult();
        }
        transport.Close();
        return done.get();
    } catch (const std::bad_alloc& e) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, std::string("exception occured: ") + e.what());
        FetchResult result;
        result.errorCode = static_cast<int>(A2AErrorCode::A2A_BAD_ALLOC);
        result.errorInfo = e.what();
        return result;
    } catch (const std::exception& e) {
        FetchResult result;
        result.errorCode = static_cast<int>(A2AErrorCode::A2A_TRANSPORT_EXCEPTION);
        result.errorInfo = e.what();
        return result;
    }
}

std::future<AgentCard> HttpCardResolver::FetchUncached(const std::string& url, int timeout) const
{
    // promise split: the caller keeps a future independent of the worker,
    // while the worker itself is tracked so destruction joins it — the
    // same lifetime guarantee as the cached paths
    auto completion = std::make_shared<std::promise<AgentCard>>();
    std::future<AgentCard> result = completion->get_future();
    try {
        auto worker = LaunchWorker([this, completion, url, timeout]() {
            AgentCard card;
            std::exception_ptr error;
            try {
                const FetchResult fetched = FetchCard(GenerateUuid(), url, timeout);
                if (fetched.success) {
                    card = fetched.card;
                } else {
                    error = CreateExceptionPtr(fetched.errorCode, fetched.errorInfo);
                }
            } catch (...) {
                // FetchCard never throws by contract, but it is a virtual
                // seam: don't strand the caller's future on an override that does
                error = std::current_exception();
            }
            if (error) {
                completion->set_exception(std::move(error));
            } else {
                completion->set_value(std::move(card));
            }
        });
        TrackWorker(std::move(worker));
    } catch (const std::exception& e) {
        // launch failure settles the caller's future with the wrapped error
        // (like every other failure) instead of throwing synchronously
        completion->set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_TRANSPORT_EXCEPTION),
            std::string("card fetch worker failed to start: ") + e.what()));
    }
    return result;
}

std::future<AgentCard> HttpCardResolver::GetAgentCard(const std::optional<std::string>& relativeCardPath,
    int timeout) const
{
    ReapWorkerFutures();
    const int effectiveTimeout = timeout > 0 ? timeout : DEFAULT_CARD_TIMEOUT_SECONDS;
    if (!cacheConfig_.enabled) {
        const std::string url = ResolveFullPath(relativeCardPath.value_or(relativeCardPath_.value_or("")));
        return FetchUncached(url, effectiveTimeout);
    }

    const std::string cacheKey = ResolveCacheKey(relativeCardPath);
    std::optional<AgentCard> cached;
    bool expired = false;
    {
        std::shared_lock<std::shared_mutex> lock(cacheMutex_);
        auto it = cardCache_.find(cacheKey);
        if (it != cardCache_.end()) {
            cached = it->second->card;
            expired = std::chrono::steady_clock::now() >= it->second->expiresAt;
        }
    }
    if (cached.has_value()) {
        if (!expired) {
            return MakeReadyFuture(*cached);
        }
        // stale-while-revalidate: serve stale now, refresh in background
        TriggerBackgroundRefresh(cacheKey);
        return MakeReadyFuture(*cached);
    }
    // cache miss: single-flight fetch shared by concurrent callers of this key
    return StartForegroundFetch(cacheKey, ResolveFullPath(cacheKey), effectiveTimeout);
}

std::future<AgentCard> HttpCardResolver::ForceRefresh(const std::optional<std::string>& relativeCardPath,
    int timeout) const
{
    const int effectiveTimeout = timeout > 0 ? timeout : DEFAULT_CARD_TIMEOUT_SECONDS;
    if (!cacheConfig_.enabled) {
        return GetAgentCard(relativeCardPath, effectiveTimeout); // nothing cached: a plain fetch is already fresh
    }
    const std::string key = ResolveCacheKey(relativeCardPath);
    auto flight = std::make_shared<InFlightFetch>();
    std::future<AgentCard> result = flight->completion->get_future();
    {
        // one critical section for the whole invalidation protocol: a
        // pre-refresh worker settling outside it either stored before the
        // erase below (the erase removes it) or checks invalidated after it
        // (and never stores). A second lock in between let its older card be
        // written back over the just-erased entry with a fresh TTL and be
        // served as a hit until the forced fetch lands
        std::unique_lock<std::shared_mutex> lock(cacheMutex_);
        // mark the in-flight background refresh stale: its older result must
        // not overwrite what the forced fetch stores
        auto pending = pendingRefreshes_.find(key);
        if (pending != pendingRefreshes_.end()) {
            pending->second.invalidated = true;
        }
        auto it = cardCache_.find(key);
        if (it != cardCache_.end()) {
            cacheOrder_.erase(it->second);
            cardCache_.erase(it);
        }
        // the forced fetch never joins a pre-refresh single-flight request:
        // its older snapshot must neither be served to the forced caller nor
        // stored back over the fresh card; replacing the registration also
        // makes later callers join the forced fetch instead
        auto infl = inFlightFetches_.find(key);
        if (infl != inFlightFetches_.end()) {
            infl->second->invalidated = true;
        }
        inFlightFetches_[key] = flight;
    }
    SpawnFetchWorker(flight, key, ResolveFullPath(key), effectiveTimeout);
    return result;
}

std::future<std::vector<AgentCard>> HttpCardResolver::GetAllAgentCards(int timeout) const
{
    auto outerCardFuture = GetAgentCard(std::nullopt, timeout);
    try {
        return std::async(std::launch::async,
            [cardFuture = std::move(outerCardFuture)]() mutable -> std::vector<AgentCard> {
                AgentCard card = cardFuture.get();
                return {std::move(card)};
            });
    } catch (const std::exception& e) {
        // launch failure settles a ready future instead of throwing
        // synchronously, keeping the future-only error contract
        auto completion = std::make_shared<std::promise<std::vector<AgentCard>>>();
        completion->set_exception(CreateExceptionPtr(static_cast<int>(A2AErrorCode::A2A_TRANSPORT_EXCEPTION),
            std::string("card fetch worker failed to start: ") + e.what()));
        return completion->get_future();
    }
}

std::exception_ptr HttpCardResolver::CreateExceptionPtr(int code, const std::string& msg) const
{
    return A2AClientException::Make(code, msg);
}

} // namespace A2A::Client
