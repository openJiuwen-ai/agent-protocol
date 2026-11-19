/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <vector>

#include "client/http_card_resolver.h"
#include "client/client_transport.h"
#include "types.h"

namespace A2A::Client::Test {

using namespace A2A::Client;

// Test-only subclass: overrides the FetchCard injection point, registers the
// request and blocks until the test injects a response
class TestableHttpCardResolver : public HttpCardResolver {
public:
    TestableHttpCardResolver(std::string baseUrl, const std::optional<std::string>& relativeCardPath,
                            std::map<std::string, std::string> httpKwargs, const CardCacheConfig& cacheConfig = {})
        : HttpCardResolver(std::move(baseUrl), relativeCardPath, std::move(httpKwargs), cacheConfig)
    {
    }

    ~TestableHttpCardResolver() override = default;

    struct IssuedFetch {
        std::string requestId;
        std::string url;
        int timeout = 0;
    };

    IssuedFetch AwaitFetch(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000))
    {
        std::unique_lock<std::mutex> lock(fetchMutex_);
        fetchCv_.wait_for(lock, timeout, [this]() { return !arrivalOrder_.empty(); });
        if (arrivalOrder_.empty()) {
            // fail fast: fail immediately when the code under test issued no
            // request, instead of letting the later TriggerOnTransportEvent
            // spin idly and future.get() hang for a long time
            ADD_FAILURE() << "AwaitFetch timed out after " << timeout.count() << "ms: no card fetch was issued";
            return {};
        }
        const std::string requestId = arrivalOrder_.front();
        arrivalOrder_.pop_front();
        auto it = issued_.find(requestId);
        if (it == issued_.end()) {
            ADD_FAILURE() << "AwaitFetch: requestId '" << requestId << "' queued but missing from issued map";
            return {};
        }
        return it->second.info;
    }

    void TriggerOnTransportEvent(const std::string& requestId, const TransportEvent& event)
    {
        std::shared_ptr<std::promise<HttpCardResolver::FetchResult>> completion;
        {
            std::lock_guard<std::mutex> lock(fetchMutex_);
            auto it = issued_.find(requestId);
            if (it == issued_.end()) {
                return;
            }
            completion = it->second.completion;
            issued_.erase(it);
        }
        completion->set_value(TranslateEvent(event));
    }

    size_t GetFetchCount() const
    {
        return fetchCount_.load();
    }

    void FailNextFetch(const std::string& message)
    {
        std::lock_guard<std::mutex> lock(fetchMutex_);
        failNextFetch_ = message;
    }

    // Make the next FetchCard fail like a slow request: it sleeps for the
    // delay first, then returns the failure (injection point: the
    // settlement-time backoff anchor — a slow failure can outlast the whole
    // claim-anchored retry window)
    void FailNextFetchAfterDelay(const std::string& message, int delayMs)
    {
        std::lock_guard<std::mutex> lock(fetchMutex_);
        failNextFetch_ = message;
        nextFetchDelayMs_ = delayMs;
    }

    // Make the next FetchCard throw directly (injection point: the worker's
    // exception-safety path)
    void ThrowNextFetch(const std::string& message)
    {
        std::lock_guard<std::mutex> lock(fetchMutex_);
        throwNextFetch_ = message;
    }

    // Steal the response-injection channel for a request: the response can
    // still be delivered after the resolver enters destruction (blocked in
    // join) — the channel captures only the shared completion and no longer
    // touches object members
    std::function<void(const TransportEvent&)> StealCompleter(const std::string& requestId)
    {
        std::shared_ptr<std::promise<HttpCardResolver::FetchResult>> completion;
        {
            std::lock_guard<std::mutex> lock(fetchMutex_);
            auto it = issued_.find(requestId);
            if (it == issued_.end()) {
                return {};
            }
            completion = it->second.completion;
            issued_.erase(it);
        }
        return [completion](const TransportEvent& event) { completion->set_value(TranslateEvent(event)); };
    }

    size_t GetCacheSize()
    {
        std::shared_lock<std::shared_mutex> lock(cacheMutex_);
        return cardCache_.size();
    }

    std::optional<AgentCard> GetCachedCard(const std::string& key)
    {
        std::shared_lock<std::shared_mutex> lock(cacheMutex_);
        auto it = cardCache_.find(key);
        if (it == cardCache_.end()) {
            return std::nullopt;
        }
        return it->second->card;
    }

    // Expire the entry for a key (deterministically triggers
    // stale-while-revalidate, no sleep needed)
    void ExpireEntry(const std::string& key)
    {
        std::unique_lock<std::shared_mutex> lock(cacheMutex_);
        auto it = cardCache_.find(key);
        if (it != cardCache_.end()) {
            it->second->expiresAt = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        }
    }

    size_t GetPendingRefreshCount()
    {
        std::shared_lock<std::shared_mutex> lock(cacheMutex_);
        return pendingRefreshes_.size();
    }

    void AwaitRefreshSettled(const std::string& key)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline) {
            {
                std::shared_lock<std::shared_mutex> lock(cacheMutex_);
                if (pendingRefreshes_.count(key) == 0) {
                    return;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    size_t GetTrackedWorkerCount()
    {
        std::lock_guard<std::mutex> lock(workerFuturesMutex_);
        return workerFutures_.size();
    }

    // Poll until every tracked worker future is ready: TrackWorker only reaps
    // futures that have already completed, so tests asserting on the
    // registration-time reap must first let the previous worker finish
    void AwaitTrackedWorkersReady()
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline) {
            {
                std::lock_guard<std::mutex> lock(workerFuturesMutex_);
                bool allReady = true;
                for (const auto& f : workerFutures_) {
                    if (f.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                        allReady = false;
                        break;
                    }
                }
                if (allReady) {
                    return;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    // Arm the launch-failure gate: the next LaunchWorker call reports itself
    // at SpawnEntered(), blocks until ReleaseSpawnFailure(), then throws like
    // a failed std::async launch (injection point: the launch-failure path
    // in SpawnFetchWorker, which settles the originator's and joined waiters'
    // futures with the wrapped error and undoes the registration)
    void ArmSpawnFailure()
    {
        failNextSpawn_ = true;
        spawnEntered_ = std::make_shared<std::promise<void>>();
        releaseSpawn_ = std::make_shared<std::promise<void>>();
    }

    std::shared_future<void> SpawnEntered() const
    {
        return spawnEntered_->get_future().share();
    }

    void ReleaseSpawnFailure()
    {
        releaseSpawn_->set_value();
    }

protected:
    HttpCardResolver::FetchResult FetchCard(const std::string& requestId, const std::string& url,
        int timeout) const override
    {
        // slow-request injection: runs before the failure checks and outside
        // every lock, so concurrent FetchCards are not serialized behind it
        const int delayMs = nextFetchDelayMs_.exchange(0);
        if (delayMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        }
        {
            std::lock_guard<std::mutex> lock(fetchMutex_);
            if (!failNextFetch_.empty()) {
                HttpCardResolver::FetchResult result;
                result.errorCode = static_cast<int>(A2AErrorCode::A2A_TRANSPORT_EXCEPTION);
                result.errorInfo = failNextFetch_;
                failNextFetch_.clear();
                return result;
            }
            if (!throwNextFetch_.empty()) {
                const std::string message = throwNextFetch_;
                throwNextFetch_.clear();
                throw std::runtime_error(message);
            }
        }
        auto completion = std::make_shared<std::promise<HttpCardResolver::FetchResult>>();
        {
            std::lock_guard<std::mutex> lock(fetchMutex_);
            issued_[requestId] = PendingFetch{IssuedFetch{requestId, url, timeout}, completion};
            arrivalOrder_.push_back(requestId);
        }
        ++fetchCount_;
        fetchCv_.notify_all();
        // Same synchronous contract as the real transport: blocks until the
        // response arrives
        auto done = completion->get_future();
        if (done.wait_for(std::chrono::seconds(30)) != std::future_status::ready) {
            HttpCardResolver::FetchResult result;
            result.errorInfo = "test did not deliver a response in time";
            return result;
        }
        return done.get();
    }

    std::future<void> LaunchWorker(std::function<void()> body) const override
    {
        if (failNextSpawn_) {
            failNextSpawn_ = false; // one-shot: later calls launch normally
            spawnEntered_->set_value();
            // hold the seam open until the test has joined a waiter to the
            // flight, then fail like a std::async launch
            releaseSpawn_->get_future().wait();
            throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again),
                "simulated worker launch failure");
        }
        return HttpCardResolver::LaunchWorker(std::move(body));
    }

private:
    struct PendingFetch {
        IssuedFetch info;
        std::shared_ptr<std::promise<HttpCardResolver::FetchResult>> completion;
    };

    mutable std::mutex fetchMutex_;
    mutable std::condition_variable fetchCv_;
    mutable std::unordered_map<std::string, PendingFetch> issued_;
    mutable std::deque<std::string> arrivalOrder_;
    mutable std::atomic<size_t> fetchCount_{0};
    mutable std::string failNextFetch_;
    mutable std::string throwNextFetch_;
    mutable std::atomic<int> nextFetchDelayMs_{0};
    mutable std::atomic<bool> failNextSpawn_{false};
    std::shared_ptr<std::promise<void>> spawnEntered_;
    std::shared_ptr<std::promise<void>> releaseSpawn_;
};

class HttpCardResolverTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        baseUrl = "https://api.example.com";
        relativePath = "agent/card";
        httpKwargs = {{"timeout", "30"}};

        testCard.name = "Test Agent";
        testCard.description = "Test Description";
        testCard.version = "1.0.0";

        AgentCapabilities caps;
        caps.streaming = true;
        caps.pushNotifications = false;
        caps.extendedAgentCard = true;
        testCard.capabilities = caps;

        testCard.defaultInputModes = {"text/plain"};
        testCard.defaultOutputModes = {"text/plain"};

        resolver = std::make_shared<TestableHttpCardResolver>(baseUrl, relativePath, httpKwargs);
    }

    std::string baseUrl;
    std::string relativePath;
    std::map<std::string, std::string> httpKwargs;
    AgentCard testCard;
    std::shared_ptr<TestableHttpCardResolver> resolver;
};

namespace {

AgentCard MakeNamedCard(const std::string& name)
{
    AgentCard card;
    card.name = name;
    card.version = "1.0.0";
    return card;
}

// ttl in seconds, retryInterval in milliseconds, matching the CardCacheConfig fields
CardCacheConfig EnabledCache(int ttl, uint32_t maxEntries = 64, int retryInterval = 1000)
{
    CardCacheConfig config;
    config.enabled = true;
    config.ttl = ttl;
    config.maxEntries = maxEntries;
    config.refreshRetryInterval = retryInterval;
    return config;
}

} // namespace

TEST_F(HttpCardResolverTest, GetAgentCardIssuesFetchWithDefaults)
{
    auto future = resolver->GetAgentCard(std::nullopt);
    const auto fetch = resolver->AwaitFetch();
    EXPECT_EQ(fetch.url, "https://api.example.com/agent/card");
    EXPECT_EQ(fetch.timeout, 60);  // default discovery timeout (fail-fast, not the transport's 300s)
    EXPECT_EQ(resolver->GetFetchCount(), 1u);
    resolver->TriggerOnTransportEvent(fetch.requestId, testCard);
    EXPECT_EQ(future.get().name, testCard.name);
}

TEST_F(HttpCardResolverTest, GetAgentCardUsesRelativePathInUrl)
{
    auto future = resolver->GetAgentCard(std::string("agent/other-card"));
    const auto fetch = resolver->AwaitFetch();
    EXPECT_EQ(fetch.url, "https://api.example.com/agent/other-card");
    resolver->TriggerOnTransportEvent(fetch.requestId, testCard);
    EXPECT_EQ(future.get().name, testCard.name);
}

TEST_F(HttpCardResolverTest, HandlesErrorResponse)
{
    auto future = resolver->GetAgentCard(std::nullopt);
    const auto fetch = resolver->AwaitFetch();

    TransportError error;
    error.errorCode = 404;
    error.errInfo = "Not found";
    resolver->TriggerOnTransportEvent(fetch.requestId, error);

    EXPECT_THROW(future.get(), std::runtime_error);
}

TEST_F(HttpCardResolverTest, HandlesTransportException)
{
    resolver->FailNextFetch("Transport error");

    auto future = resolver->GetAgentCard(std::nullopt);

    EXPECT_EQ(resolver->GetFetchCount(), 0u);
    EXPECT_THROW(future.get(), std::runtime_error);
}

TEST_F(HttpCardResolverTest, GetAllAgentCardsWorks)
{
    auto future = resolver->GetAllAgentCards();
    const auto fetch = resolver->AwaitFetch();
    resolver->TriggerOnTransportEvent(fetch.requestId, testCard);

    std::vector<AgentCard> cards = future.get();
    ASSERT_EQ(cards.size(), 1u);
    EXPECT_EQ(cards[0].name, testCard.name);
}

TEST_F(HttpCardResolverTest, CacheDisabledByDefaultKeepsFetchingEveryTime)
{
    auto first = resolver->GetAgentCard(std::nullopt);
    auto f1 = resolver->AwaitFetch();
    resolver->TriggerOnTransportEvent(f1.requestId, MakeNamedCard("Card A"));

    auto second = resolver->GetAgentCard(std::nullopt);
    auto f2 = resolver->AwaitFetch();
    resolver->TriggerOnTransportEvent(f2.requestId, MakeNamedCard("Card B"));

    EXPECT_EQ(first.get().name, "Card A");
    EXPECT_EQ(second.get().name, "Card B");
    EXPECT_EQ(resolver->GetFetchCount(), 2u);
    EXPECT_EQ(resolver->GetCacheSize(), 0u);
}

// Discovery workers on the cache-disabled path are tracked too: destruction
// blocks in the explicit join until the response is delivered and must not
// free the object early (same lifetime guarantee as the cached path)
TEST_F(HttpCardResolverTest, DestructorJoinsUncachedFetchWorker)
{
    auto future = resolver->GetAgentCard(std::nullopt);
    const auto fetch = resolver->AwaitFetch();
    // Take the response-injection channel out before destruction: the
    // resolver no longer touches its members once destroyed
    auto completer = resolver->StealCompleter(fetch.requestId);

    std::promise<void> destroyed;
    auto destroyedFuture = destroyed.get_future();
    // ownership hand-off: the thread receives the last shared_ptr by value,
    // so resetting it there runs ~HttpCardResolver inside the thread without
    // capturing the fixture's member through an implicit this
    std::thread destroyer([victim = std::move(resolver), &destroyed]() mutable {
        victim.reset();
        destroyed.set_value();
    });

    // worker not finished yet: destruction must block on the join
    EXPECT_EQ(destroyedFuture.wait_for(std::chrono::milliseconds(200)), std::future_status::timeout);
    completer(testCard);
    ASSERT_EQ(destroyedFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    destroyer.join();
    EXPECT_EQ(future.get().name, testCard.name);
}

TEST_F(HttpCardResolverTest, CacheHitSkipsTransport)
{
    resolver = std::make_shared<TestableHttpCardResolver>(baseUrl, relativePath, httpKwargs, EnabledCache(60));

    auto first = resolver->GetAgentCard(std::nullopt);
    auto f1 = resolver->AwaitFetch();
    resolver->TriggerOnTransportEvent(f1.requestId, MakeNamedCard("Card A"));
    EXPECT_EQ(first.get().name, "Card A");

    auto second = resolver->GetAgentCard(std::nullopt);
    EXPECT_EQ(second.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
    EXPECT_EQ(second.get().name, "Card A");
    EXPECT_EQ(resolver->GetFetchCount(), 1u);
    EXPECT_EQ(resolver->GetCacheSize(), 1u);
}

// Expired hit (stale-while-revalidate): return the stale value immediately
// plus a background refresh
TEST_F(HttpCardResolverTest, ExpiredEntryServesStaleAndRefreshesInBackground)
{
    resolver = std::make_shared<TestableHttpCardResolver>(baseUrl, relativePath, httpKwargs, EnabledCache(60));

    auto first = resolver->GetAgentCard(std::nullopt);
    auto f1 = resolver->AwaitFetch();
    resolver->TriggerOnTransportEvent(f1.requestId, MakeNamedCard("Card A"));
    EXPECT_EQ(first.get().name, "Card A");

    resolver->ExpireEntry("agent/card");
    auto second = resolver->GetAgentCard(std::nullopt);
    EXPECT_EQ(second.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
    EXPECT_EQ(second.get().name, "Card A");
    EXPECT_EQ(resolver->GetPendingRefreshCount(), 1u);

    auto refresh = resolver->AwaitFetch();
    resolver->TriggerOnTransportEvent(refresh.requestId, MakeNamedCard("Card B"));
    resolver->AwaitRefreshSettled("agent/card");
    auto cached = resolver->GetCachedCard("agent/card");
    ASSERT_TRUE(cached.has_value());
    EXPECT_EQ(cached->name, "Card B");
}

TEST_F(HttpCardResolverTest, BackgroundRefreshIsDeduplicatedPerKey)
{
    resolver = std::make_shared<TestableHttpCardResolver>(baseUrl, relativePath, httpKwargs, EnabledCache(60));

    auto first = resolver->GetAgentCard(std::nullopt);
    auto f1 = resolver->AwaitFetch();
    resolver->TriggerOnTransportEvent(f1.requestId, MakeNamedCard("Card A"));
    EXPECT_EQ(first.get().name, "Card A");

    resolver->ExpireEntry("agent/card");
    auto second = resolver->GetAgentCard(std::nullopt);
    auto third = resolver->GetAgentCard(std::nullopt);
    EXPECT_EQ(second.get().name, "Card A");
    EXPECT_EQ(third.get().name, "Card A");
    EXPECT_EQ(resolver->GetPendingRefreshCount(), 1u);
    auto refresh = resolver->AwaitFetch();
    EXPECT_EQ(resolver->GetFetchCount(), 2u);

    resolver->TriggerOnTransportEvent(refresh.requestId, MakeNamedCard("Card B"));
    resolver->AwaitRefreshSettled("agent/card");
}

TEST_F(HttpCardResolverTest, ForceRefreshBypassesCache)
{
    resolver = std::make_shared<TestableHttpCardResolver>(baseUrl, relativePath, httpKwargs, EnabledCache(60));

    auto first = resolver->GetAgentCard(std::nullopt);
    auto f1 = resolver->AwaitFetch();
    resolver->TriggerOnTransportEvent(f1.requestId, MakeNamedCard("Card A"));
    EXPECT_EQ(first.get().name, "Card A");

    auto refreshed = resolver->ForceRefresh(std::nullopt);
    auto f2 = resolver->AwaitFetch();
    resolver->TriggerOnTransportEvent(f2.requestId, MakeNamedCard("Card B"));
    EXPECT_EQ(refreshed.get().name, "Card B");

    auto afterRefresh = resolver->GetAgentCard(std::nullopt);
    EXPECT_EQ(afterRefresh.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
    EXPECT_EQ(afterRefresh.get().name, "Card B");
    EXPECT_EQ(resolver->GetFetchCount(), 2u);
}

// ForceRefresh honors the per-call timeout like GetAgentCard (0 = SDK default)
TEST_F(HttpCardResolverTest, ForceRefreshHonorsPerCallTimeout)
{
    resolver = std::make_shared<TestableHttpCardResolver>(baseUrl, relativePath, httpKwargs, EnabledCache(60));

    auto forced = resolver->ForceRefresh(std::nullopt, 10);
    const auto shortFetch = resolver->AwaitFetch();
    EXPECT_EQ(shortFetch.timeout, 10);
    resolver->TriggerOnTransportEvent(shortFetch.requestId, MakeNamedCard("Card A"));
    EXPECT_EQ(forced.get().name, "Card A");

    auto defaulted = resolver->ForceRefresh(std::nullopt, 0);
    const auto defaultFetch = resolver->AwaitFetch();
    EXPECT_EQ(defaultFetch.timeout, 60); // 0 falls back to the SDK default
    resolver->TriggerOnTransportEvent(defaultFetch.requestId, MakeNamedCard("Card B"));
    EXPECT_EQ(defaulted.get().name, "Card B");
}

TEST_F(HttpCardResolverTest, CacheKeyDistinguishesRelativePaths)
{
    resolver = std::make_shared<TestableHttpCardResolver>(baseUrl, relativePath, httpKwargs, EnabledCache(60));

    auto cardA = resolver->GetAgentCard(std::string("agent/card/a"));
    auto fA = resolver->AwaitFetch();
    EXPECT_EQ(fA.url, "https://api.example.com/agent/card/a");
    resolver->TriggerOnTransportEvent(fA.requestId, MakeNamedCard("Card A"));
    auto cardB = resolver->GetAgentCard(std::string("agent/card/b"));
    auto fB = resolver->AwaitFetch();
    EXPECT_EQ(fB.url, "https://api.example.com/agent/card/b");
    resolver->TriggerOnTransportEvent(fB.requestId, MakeNamedCard("Card B"));
    EXPECT_EQ(cardA.get().name, "Card A");
    EXPECT_EQ(cardB.get().name, "Card B");

    auto cardAAgain = resolver->GetAgentCard(std::string("agent/card/a"));
    auto cardBAgain = resolver->GetAgentCard(std::string("agent/card/b"));
    EXPECT_EQ(cardAAgain.get().name, "Card A");
    EXPECT_EQ(cardBAgain.get().name, "Card B");
    EXPECT_EQ(resolver->GetFetchCount(), 2u);
    EXPECT_EQ(resolver->GetCacheSize(), 2u);
}

// "/x" and "x" spellings of one path join into the same discovery URL, so
// they must share a single cache entry instead of double-caching the
// document under two keys with independent TTLs and eviction
TEST_F(HttpCardResolverTest, LeadingSlashPathSharesCacheEntryWithBarePath)
{
    resolver = std::make_shared<TestableHttpCardResolver>(baseUrl, relativePath, httpKwargs, EnabledCache(60));

    auto first = resolver->GetAgentCard(std::string("agent/card"));
    const auto fetch = resolver->AwaitFetch();
    EXPECT_EQ(fetch.url, "https://api.example.com/agent/card");
    resolver->TriggerOnTransportEvent(fetch.requestId, MakeNamedCard("Card A"));
    EXPECT_EQ(first.get().name, "Card A");

    auto slashed = resolver->GetAgentCard(std::string("/agent/card"));
    EXPECT_EQ(slashed.wait_for(std::chrono::milliseconds(0)), std::future_status::ready); // hit, no fetch
    EXPECT_EQ(slashed.get().name, "Card A");
    EXPECT_EQ(resolver->GetFetchCount(), 1u);
    EXPECT_EQ(resolver->GetCacheSize(), 1u);
}

// maxEntries eviction is by refresh recency (not timestamp/insertion order):
// the least recently refreshed entry is evicted first
TEST_F(HttpCardResolverTest, CacheMaxEntriesEvictsLeastRecentlyRefreshed)
{
    resolver = std::make_shared<TestableHttpCardResolver>(baseUrl, relativePath, httpKwargs, EnabledCache(60, 2));

    // future.get() waits synchronously for the store, removing the race
    // between response delivery and the immediately following assertions
    auto cardA = resolver->GetAgentCard(std::string("a"));
    auto fA = resolver->AwaitFetch();
    resolver->TriggerOnTransportEvent(fA.requestId, MakeNamedCard("Card A"));
    EXPECT_EQ(cardA.get().name, "Card A");
    auto cardB = resolver->GetAgentCard(std::string("b"));
    auto fB = resolver->AwaitFetch();
    resolver->TriggerOnTransportEvent(fB.requestId, MakeNamedCard("Card B"));
    EXPECT_EQ(cardB.get().name, "Card B");
    EXPECT_EQ(resolver->GetCacheSize(), 2u);

    // Re-fetch "a": a becomes the most recently refreshed entry
    auto refreshedA = resolver->ForceRefresh(std::string("a"));
    auto fA2 = resolver->AwaitFetch();
    resolver->TriggerOnTransportEvent(fA2.requestId, MakeNamedCard("Card A2"));
    EXPECT_EQ(refreshedA.get().name, "Card A2");

    // Insert a third entry: least recently refreshed "b" gets evicted
    auto cardC = resolver->GetAgentCard(std::string("c"));
    auto fC = resolver->AwaitFetch();
    resolver->TriggerOnTransportEvent(fC.requestId, MakeNamedCard("Card C"));
    EXPECT_EQ(cardC.get().name, "Card C");
    EXPECT_EQ(resolver->GetCacheSize(), 2u);
    EXPECT_FALSE(resolver->GetCachedCard("b").has_value());
    ASSERT_TRUE(resolver->GetCachedCard("a").has_value());
    EXPECT_EQ(resolver->GetCachedCard("a")->name, "Card A2");
}

TEST_F(HttpCardResolverTest, FailedResultsDoNotPopulateCache)
{
    resolver = std::make_shared<TestableHttpCardResolver>(baseUrl, relativePath, httpKwargs, EnabledCache(60));

    auto failed = resolver->GetAgentCard(std::nullopt);
    auto f1 = resolver->AwaitFetch();
    TransportError error;
    error.errorCode = 404;
    error.errInfo = "Not found";
    resolver->TriggerOnTransportEvent(f1.requestId, error);
    EXPECT_THROW(failed.get(), std::runtime_error);

    auto invalid = resolver->GetAgentCard(std::nullopt);
    auto f2 = resolver->AwaitFetch();
    A2A::Message message;
    message.messageId = "test-msg";
    resolver->TriggerOnTransportEvent(f2.requestId, message);
    EXPECT_THROW(invalid.get(), std::runtime_error);

    EXPECT_EQ(resolver->GetCacheSize(), 0u);
}

TEST_F(HttpCardResolverTest, ConcurrentMissesShareSingleFlightFetch)
{
    resolver = std::make_shared<TestableHttpCardResolver>(baseUrl, relativePath, httpKwargs, EnabledCache(60));

    auto first = resolver->GetAgentCard(std::nullopt);
    auto second = resolver->GetAgentCard(std::nullopt);
    auto third = resolver->GetAgentCard(std::nullopt);

    const auto fetch = resolver->AwaitFetch();
    resolver->TriggerOnTransportEvent(fetch.requestId, MakeNamedCard("Card A"));

    EXPECT_EQ(first.get().name, "Card A");
    EXPECT_EQ(second.get().name, "Card A");
    EXPECT_EQ(third.get().name, "Card A");
    EXPECT_EQ(resolver->GetFetchCount(), 1u);
    EXPECT_EQ(resolver->GetCacheSize(), 1u);
}

// A std::async launch failure must not strand callers that already joined the
// flight: the flight is settled with the contract error instead of abandoned
// (an abandoned promise would surface std::future_error{broken_promise},
// outside the runtime_error family callers catch), and the registration is
// undone so the next caller starts a fresh fetch
TEST_F(HttpCardResolverTest, SpawnFailureSettlesJoinedWaitersAndRetriesLater)
{
    resolver = std::make_shared<TestableHttpCardResolver>(baseUrl, relativePath, httpKwargs, EnabledCache(60));
    resolver->ArmSpawnFailure();

    std::thread joiner([this]() {
        const auto entered = resolver->SpawnEntered();
        if (entered.wait_for(std::chrono::seconds(2)) != std::future_status::ready) {
            resolver->ReleaseSpawnFailure(); // never leave the main thread blocked on a failed setup
            ADD_FAILURE() << "launch seam was never entered";
            return;
        }
        auto joined = resolver->GetAgentCard(std::nullopt); // joins the in-flight flight
        resolver->ReleaseSpawnFailure();
        EXPECT_THROW(joined.get(), std::runtime_error);
    });

    // flight originator: the launch failure is settled into its future like
    // every other error path, not thrown synchronously
    auto originator = resolver->GetAgentCard(std::nullopt);
    joiner.join();
    EXPECT_THROW(originator.get(), std::runtime_error);

    EXPECT_EQ(resolver->GetFetchCount(), 0u); // the worker never reached FetchCard
    EXPECT_EQ(resolver->GetCacheSize(), 0u);

    // registration was undone: a later caller issues a new fetch normally
    auto retry = resolver->GetAgentCard(std::nullopt);
    const auto fetch = resolver->AwaitFetch();
    resolver->TriggerOnTransportEvent(fetch.requestId, MakeNamedCard("Card A"));
    EXPECT_EQ(retry.get().name, "Card A");
}

// The uncached discovery path has the same error contract: a launch failure
// settles the caller's future with the wrapped error instead of throwing the
// raw std::system_error synchronously
TEST_F(HttpCardResolverTest, UncachedSpawnFailureSettlesTheCallersFuture)
{
    resolver = std::make_shared<TestableHttpCardResolver>(baseUrl, relativePath, httpKwargs); // cache disabled

    resolver->ArmSpawnFailure();
    std::thread releaser([this]() {
        const auto entered = resolver->SpawnEntered();
        if (entered.wait_for(std::chrono::seconds(2)) != std::future_status::ready) {
            resolver->ReleaseSpawnFailure(); // never leave the main thread blocked on a failed setup
            ADD_FAILURE() << "launch seam was never entered";
            return;
        }
        resolver->ReleaseSpawnFailure();
    });

    auto result = resolver->GetAgentCard(std::nullopt);
    releaser.join();
    EXPECT_THROW(result.get(), std::runtime_error);
    EXPECT_EQ(resolver->GetFetchCount(), 0u); // the worker never reached FetchCard

    // one-shot gate cleared: a later call fetches normally
    auto retry = resolver->GetAgentCard(std::nullopt);
    const auto fetch = resolver->AwaitFetch();
    resolver->TriggerOnTransportEvent(fetch.requestId, MakeNamedCard("Card A"));
    EXPECT_EQ(retry.get().name, "Card A");
}

// Finished worker futures are reaped when a new worker registers: the reap
// otherwise only runs on the GetAgentCard path, so a resolver driven solely by
// ForceRefresh (no GetAgentCard calls) would accumulate one future per call
// until destruction
TEST_F(HttpCardResolverTest, ForceRefreshOnlyWorkloadDoesNotAccumulateWorkerFutures)
{
    resolver = std::make_shared<TestableHttpCardResolver>(baseUrl, relativePath, httpKwargs, EnabledCache(60));

    auto first = resolver->ForceRefresh(std::nullopt);
    const auto fetch1 = resolver->AwaitFetch();
    resolver->TriggerOnTransportEvent(fetch1.requestId, MakeNamedCard("Card A"));
    EXPECT_EQ(first.get().name, "Card A");
    // TrackWorker only reaps futures that already completed: let the first
    // worker finish before the second ForceRefresh registers its own
    resolver->AwaitTrackedWorkersReady();
    EXPECT_EQ(resolver->GetTrackedWorkerCount(), 1u);

    auto second = resolver->ForceRefresh(std::nullopt);
    const auto fetch2 = resolver->AwaitFetch();
    EXPECT_EQ(resolver->GetTrackedWorkerCount(), 1u); // first reaped at the second registration
    resolver->TriggerOnTransportEvent(fetch2.requestId, MakeNamedCard("Card B"));
    EXPECT_EQ(second.get().name, "Card B");
}

// ForceRefresh completes concurrently with an in-progress background refresh:
// the older background result must not overwrite the forced fresh value
TEST_F(HttpCardResolverTest, ForceRefreshInvalidatesInFlightBackgroundRefresh)
{
    resolver = std::make_shared<TestableHttpCardResolver>(baseUrl, relativePath, httpKwargs, EnabledCache(60));

    auto first = resolver->GetAgentCard(std::nullopt);
    auto f1 = resolver->AwaitFetch();
    resolver->TriggerOnTransportEvent(f1.requestId, MakeNamedCard("Card A"));
    EXPECT_EQ(first.get().name, "Card A");

    // Expired hit: the background refresh (which will return Card Old) has
    // started but not finished yet
    resolver->ExpireEntry("agent/card");
    auto stale = resolver->GetAgentCard(std::nullopt);
    EXPECT_EQ(stale.get().name, "Card A");
    auto background = resolver->AwaitFetch();

    // ForceRefresh: invalidate the in-progress background refresh and issue
    // a foreground fetch (which will return Card New)
    auto forced = resolver->ForceRefresh(std::nullopt);
    auto foreground = resolver->AwaitFetch();

    // Background returns the old value first, foreground the new value later:
    // the old value must not be written back over the new one
    resolver->TriggerOnTransportEvent(background.requestId, MakeNamedCard("Card Old"));
    resolver->TriggerOnTransportEvent(foreground.requestId, MakeNamedCard("Card New"));

    EXPECT_EQ(forced.get().name, "Card New");
    resolver->AwaitRefreshSettled("agent/card");
    auto cached = resolver->GetCachedCard("agent/card");
    ASSERT_TRUE(cached.has_value());
    EXPECT_EQ(cached->name, "Card New");

    auto after = resolver->GetAgentCard(std::nullopt);
    EXPECT_EQ(after.get().name, "Card New");
}

// ForceRefresh concurrent with an in-progress foreground single-flight
// request: must not join the old request — it must issue an independent new
// request, and the old result must not be stored over the fresh value
TEST_F(HttpCardResolverTest, ForceRefreshDoesNotJoinInFlightForegroundFetch)
{
    resolver = std::make_shared<TestableHttpCardResolver>(baseUrl, relativePath, httpKwargs, EnabledCache(60));

    // Cold-start miss: the foreground fetch has started but not finished
    // (it will return Card Old)
    auto cold = resolver->GetAgentCard(std::nullopt);
    auto oldFetch = resolver->AwaitFetch();

    // ForceRefresh: must issue a new request instead of sharing the
    // unfinished old one
    auto forced = resolver->ForceRefresh(std::nullopt);
    auto freshFetch = resolver->AwaitFetch();
    EXPECT_EQ(resolver->GetFetchCount(), 2u);

    resolver->TriggerOnTransportEvent(oldFetch.requestId, MakeNamedCard("Card Old"));
    resolver->TriggerOnTransportEvent(freshFetch.requestId, MakeNamedCard("Card New"));

    EXPECT_EQ(cold.get().name, "Card Old");    // the original caller still gets its own request's result
    EXPECT_EQ(forced.get().name, "Card New"); // the forcing caller gets the new request's result
    auto cached = resolver->GetCachedCard("agent/card");
    ASSERT_TRUE(cached.has_value());
    EXPECT_EQ(cached->name, "Card New");       // the old request's result was not stored
}

// A failed background refresh backs off exponentially: during the backoff
// window the stale value keeps being served without new requests, and
// retrying resumes afterwards
TEST_F(HttpCardResolverTest, BackgroundRefreshFailureBacksOffBeforeRetrying)
{
    resolver = std::make_shared<TestableHttpCardResolver>(baseUrl, relativePath, httpKwargs, EnabledCache(60, 64, 300));

    auto first = resolver->GetAgentCard(std::nullopt);
    auto f1 = resolver->AwaitFetch();
    resolver->TriggerOnTransportEvent(f1.requestId, MakeNamedCard("Card A"));
    EXPECT_EQ(first.get().name, "Card A");

    resolver->ExpireEntry("agent/card");
    resolver->FailNextFetch("server down");
    auto stale = resolver->GetAgentCard(std::nullopt);
    EXPECT_EQ(stale.get().name, "Card A");
    resolver->AwaitRefreshSettled("agent/card");

    // During backoff: the stale value still hits, but the failed request is
    // not repeated (no retry storm)
    auto duringBackoff = resolver->GetAgentCard(std::nullopt);
    EXPECT_EQ(duringBackoff.get().name, "Card A");
    EXPECT_EQ(resolver->GetPendingRefreshCount(), 0u);
    EXPECT_EQ(resolver->GetFetchCount(), 1u);

    std::this_thread::sleep_for(std::chrono::milliseconds(900));
    auto afterBackoff = resolver->GetAgentCard(std::nullopt);
    EXPECT_EQ(afterBackoff.get().name, "Card A");
    auto retry = resolver->AwaitFetch();
    resolver->TriggerOnTransportEvent(retry.requestId, MakeNamedCard("Card B"));
    resolver->AwaitRefreshSettled("agent/card");
    auto cached = resolver->GetCachedCard("agent/card");
    ASSERT_TRUE(cached.has_value());
    EXPECT_EQ(cached->name, "Card B");
}

// A slow failure (e.g. a transport timeout) can outlast the whole retry base:
// the backoff window must be anchored at failure settlement, not at the
// refresh claim, or the next expired hit re-refreshes immediately and the
// exponential growth never engages for slow failures
TEST_F(HttpCardResolverTest, SlowFailureStillBacksOffAfterSettlement)
{
    resolver = std::make_shared<TestableHttpCardResolver>(baseUrl, relativePath, httpKwargs, EnabledCache(60, 64, 300));

    auto first = resolver->GetAgentCard(std::nullopt);
    const auto fetch = resolver->AwaitFetch();
    resolver->TriggerOnTransportEvent(fetch.requestId, MakeNamedCard("Card A"));
    EXPECT_EQ(first.get().name, "Card A");

    resolver->ExpireEntry("agent/card");
    resolver->FailNextFetchAfterDelay("request timed out", 600); // slow: outlasts the 300ms base
    auto stale = resolver->GetAgentCard(std::nullopt);
    EXPECT_EQ(stale.get().name, "Card A");
    resolver->AwaitRefreshSettled("agent/card");

    // settlement happened 600ms after the claim: a claim-anchored window has
    // already elapsed here, so this hit would immediately claim a new refresh
    auto duringBackoff = resolver->GetAgentCard(std::nullopt);
    EXPECT_EQ(duringBackoff.get().name, "Card A");
    EXPECT_EQ(resolver->GetPendingRefreshCount(), 0u);
    EXPECT_EQ(resolver->GetFetchCount(), 1u);

    // once the settlement-anchored window elapses, retrying resumes
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    auto afterBackoff = resolver->GetAgentCard(std::nullopt);
    EXPECT_EQ(afterBackoff.get().name, "Card A");
    const auto retryFetch = resolver->AwaitFetch();
    resolver->TriggerOnTransportEvent(retryFetch.requestId, MakeNamedCard("Card B"));
    resolver->AwaitRefreshSettled("agent/card");
    auto cached = resolver->GetCachedCard("agent/card");
    ASSERT_TRUE(cached.has_value());
    EXPECT_EQ(cached->name, "Card B");
}

// A throwing FetchCard (FetchCard is a virtual injection point, and
// construction can also raise bad_alloc) must not permanently exclude the
// key from background refresh: the in-progress mark must be cleaned up so
// later expired hits can still refresh successfully
TEST_F(HttpCardResolverTest, BackgroundRefreshRecoversAfterFetchThrow)
{
    resolver = std::make_shared<TestableHttpCardResolver>(baseUrl, relativePath, httpKwargs, EnabledCache(60));

    auto first = resolver->GetAgentCard(std::nullopt);
    auto f1 = resolver->AwaitFetch();
    resolver->TriggerOnTransportEvent(f1.requestId, MakeNamedCard("Card A"));
    EXPECT_EQ(first.get().name, "Card A");

    // Expired hit triggers a background refresh, but FetchCard throws
    resolver->ExpireEntry("agent/card");
    resolver->ThrowNextFetch("injected FetchCard failure");
    auto stale = resolver->GetAgentCard(std::nullopt);
    EXPECT_EQ(stale.get().name, "Card A");

    resolver->AwaitRefreshSettled("agent/card");
    EXPECT_EQ(resolver->GetPendingRefreshCount(), 0u); // in-progress mark cleaned up by the exception path
    EXPECT_EQ(resolver->GetFetchCount(), 1u);

    // The key can still refresh: after another expired hit the background
    // refresh succeeds and the cache updates
    resolver->ExpireEntry("agent/card");
    auto stillStale = resolver->GetAgentCard(std::nullopt);
    EXPECT_EQ(stillStale.get().name, "Card A");
    auto retry = resolver->AwaitFetch();
    resolver->TriggerOnTransportEvent(retry.requestId, MakeNamedCard("Card B"));
    resolver->AwaitRefreshSettled("agent/card");
    auto cached = resolver->GetCachedCard("agent/card");
    ASSERT_TRUE(cached.has_value());
    EXPECT_EQ(cached->name, "Card B");
}

// Degenerate cache configs (ttl<=0 / maxEntries==0) are treated as
// disabled: no caching, no background tasks
TEST_F(HttpCardResolverTest, DegenerateCacheConfigDisablesCaching)
{
    resolver = std::make_shared<TestableHttpCardResolver>(baseUrl, relativePath, httpKwargs, CardCacheConfig{true, 0});
    auto first = resolver->GetAgentCard(std::nullopt);
    resolver->TriggerOnTransportEvent(resolver->AwaitFetch().requestId, MakeNamedCard("Card A"));
    EXPECT_EQ(first.get().name, "Card A");
    EXPECT_EQ(resolver->GetCacheSize(), 0u);

    resolver = std::make_shared<TestableHttpCardResolver>(baseUrl, relativePath, httpKwargs,
        CardCacheConfig{true, 60, 0});
    auto second = resolver->GetAgentCard(std::nullopt);
    resolver->TriggerOnTransportEvent(resolver->AwaitFetch().requestId, MakeNamedCard("Card B"));
    EXPECT_EQ(second.get().name, "Card B");
    EXPECT_EQ(resolver->GetCacheSize(), 0u);
}

} // namespace A2A::Client::Test
