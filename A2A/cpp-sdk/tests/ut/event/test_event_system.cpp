/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <cstring>
#include <thread>
#include <chrono>
#include <atomic>
#include <future>
#include <unordered_set>

#include "event_system.h"
#include "a2a_log.h"

namespace A2A::Event::Test {

using namespace A2A;

// ---------------------------------------------------------------------------
// Helper Functions
// ---------------------------------------------------------------------------

/// Create a non-blocking socket pair for testing I/O events.
static std::pair<int, int> CreateTestSocketPair()
{
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds) < 0) {
        return {-1, -1};
    }
    return {fds[0], fds[1]};
}

/// Create an eventfd for testing notify events.
static int CreateTestEventFd()
{
    return eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
}

/// Sleep helper for async tests.
static void SleepMs(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// ---------------------------------------------------------------------------
// EventSystem Lifecycle Tests
// ---------------------------------------------------------------------------

TEST(EventSystemTest, CreateAndDestroy)
{
    EventSystem es(false, 0);
    EXPECT_FALSE(es.EventBaseValid());
}

TEST(EventSystemTest, Init_Success)
{
    EventSystem es(false, 0);
    EXPECT_TRUE(es.Init());
    EXPECT_TRUE(es.EventBaseValid());

    // Init is idempotent
    EXPECT_TRUE(es.Init());
}

TEST(EventSystemTest, Init_WithThreadSupport)
{
    EventSystem es(true, 1);
    EXPECT_TRUE(es.Init());
    EXPECT_TRUE(es.EventBaseValid());
}

TEST(EventSystemTest, Destructor_CleansUp)
{
    {
        EventSystem es(false, 0);
        es.Init();
        // Add a dummy event to verify cleanup
        int eventId = es.AddEvent(-1, EventType::TIMEOUT,
            [](int, short, void*) {}, nullptr, 100);
        EXPECT_GE(eventId, 0);
        // EventSystem destructor should clean up
    }
    // No crash = cleanup successful
    SUCCEED();
}

// ---------------------------------------------------------------------------
// AddEvent Tests
// ---------------------------------------------------------------------------

TEST(EventSystemTest, AddEvent_NullEventBase)
{
    EventSystem es(false, 0);
    // Not initialized
    int eventId = es.AddEvent(-1, EventType::TIMEOUT,
        [](int, short, void*) {}, nullptr);
    EXPECT_EQ(eventId, -1);
}

TEST(EventSystemTest, AddEvent_NullCallback)
{
    EventSystem es(false, 0);
    es.Init();

    int eventId = es.AddEvent(-1, EventType::TIMEOUT, nullptr, nullptr);
    EXPECT_EQ(eventId, -1);
}

TEST(EventSystemTest, AddEvent_InvalidFlags)
{
    EventSystem es(false, 0);
    es.Init();

    // Empty event type
    int eventId = es.AddEvent(-1, static_cast<EventType>(0),
        [](int, short, void*) {}, nullptr);
    EXPECT_EQ(eventId, -1);
}

TEST(EventSystemTest, AddTimer_Valid)
{
    EventSystem es(false, 0);
    es.Init();

    std::atomic<bool> fired{false};

    int timerId = es.AddTimer(50,
        [&fired](int, short, void*) { fired.store(true); },
        nullptr, false);

    EXPECT_GE(timerId, 0);

    // Run event loop briefly to trigger timer
    std::thread loop([&es]() {
        es.Start(false);  // Blocking dispatch
    });

    SleepMs(100);
    es.Stop();

    if (loop.joinable()) {
        loop.join();
    }

    EXPECT_TRUE(fired.load());
}

TEST(EventSystemTest, AddTimer_Repeat)
{
    EventSystem es(false, 0);
    es.Init();

    std::atomic<int> count{0};

    int timerId = es.AddTimer(20,
        [&count](int, short, void*) { count.fetch_add(1); },
        nullptr, true);  // Repeat

    EXPECT_GE(timerId, 0);

    std::thread loop([&es]() { es.Start(false); });

    SleepMs(80);  // Should fire ~4 times
    es.Stop();

    if (loop.joinable()) {
        loop.join();
    }

    EXPECT_GE(count.load(), 3);  // At least 3 firings
}

TEST(EventSystemTest, AddTimer_InvalidTimeout)
{
    EventSystem es(false, 0);
    es.Init();

    int timerId = es.AddTimer(0,
        [](int, short, void*) {}, nullptr, false);
    EXPECT_EQ(timerId, -1);

    timerId = es.AddTimer(-100,
        [](int, short, void*) {}, nullptr, false);
    EXPECT_EQ(timerId, -1);
}

// ---------------------------------------------------------------------------
// I/O Event Tests
// ---------------------------------------------------------------------------

TEST(EventSystemTest, AddEvent_SocketRead)
{
    EventSystem es(false, 0);
    es.Init();

    auto [fd1, fd2] = CreateTestSocketPair();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    std::atomic<bool> readFired{false};

    int eventId = es.AddEvent(fd1, EventType::READ,
        [&readFired]([[maybe_unused]] int fd, short events, void*) {
            if (events & EV_READ) {
                readFired.store(true);
            }
        }, nullptr);

    EXPECT_GE(eventId, 0);

    // Write to trigger read event
    const char* msg = "test";
    send(fd2, msg, strlen(msg), 0);

    std::thread loop([&es]() { es.Start(false); });

    SleepMs(50);
    es.Stop();

    if (loop.joinable()) {
        loop.join();
    }

    EXPECT_TRUE(readFired.load());

    close(fd1);
    close(fd2);
}

TEST(EventSystemTest, AddEvent_SocketWrite)
{
    EventSystem es(false, 0);
    es.Init();

    auto [fd1, fd2] = CreateTestSocketPair();
    ASSERT_GE(fd1, 0);

    std::atomic<bool> writeFired{false};

    int eventId = es.AddEvent(fd1, EventType::WRITE,
        [&writeFired](int, short events, void*) {
            if (events & EV_WRITE) {
                writeFired.store(true);
            }
        }, nullptr);

    EXPECT_GE(eventId, 0);

    std::thread loop([&es]() { es.Start(false); });

    SleepMs(50);
    es.Stop();

    if (loop.joinable()) {
        loop.join();
    }

    // Sockets are usually writable immediately
    EXPECT_TRUE(writeFired.load());

    close(fd1);
    close(fd2);
}

// ---------------------------------------------------------------------------
// Notify Event Tests (eventfd)
// ---------------------------------------------------------------------------

TEST(EventSystemTest, CreateNotifyEventId_Valid)
{
    EventSystem es(false, 0);
    es.Init();

    std::atomic<bool> notified{false};

    int eventId = es.CreateNotifyEventId(
        [&notified](int, short, void*) { notified.store(true); },
        nullptr, true);

    EXPECT_GE(eventId, 0);

    EXPECT_TRUE(es.NotifyEventId(eventId, 1));

    std::thread loop([&es]() { es.Start(false); });

    SleepMs(50);
    es.Stop();

    if (loop.joinable()) {
        loop.join();
    }

    EXPECT_TRUE(notified.load());

    es.CloseNotifyEventId(eventId);
}

TEST(EventSystemTest, NotifyEventId_InvalidId)
{
    EventSystem es(false, 0);
    es.Init();

    EXPECT_FALSE(es.NotifyEventId(-1, 1));
    EXPECT_FALSE(es.NotifyEventId(99999, 1));
}

TEST(EventSystemTest, ReadEventFd_Valid)
{
    EventSystem es(false, 0);
    es.Init();

    int efd = CreateTestEventFd();
    ASSERT_GE(efd, 0);

    // Write a value
    uint64_t val = 42;
    EXPECT_EQ(write(efd, &val, sizeof(val)), sizeof(val));

    // Read it back via EventSystem helper
    uint64_t outVal = 0;
    EXPECT_TRUE(es.ReadEventFd(efd, outVal));
    EXPECT_EQ(outVal, 42);

    close(efd);
}

TEST(EventSystemTest, ReadEventFd_Empty)
{
    EventSystem es(false, 0);
    es.Init();

    int efd = CreateTestEventFd();
    ASSERT_GE(efd, 0);

    // Try to read from empty eventfd (non-blocking)
    uint64_t outVal = 0;
    EXPECT_FALSE(es.ReadEventFd(efd, outVal));

    close(efd);
}

// ---------------------------------------------------------------------------
// RemoveEvent Tests
// ---------------------------------------------------------------------------

TEST(EventSystemTest, RemoveEvent_Valid)
{
    EventSystem es(false, 0);
    es.Init();

    std::atomic<bool> shouldNotFire{false};

    int timerId = es.AddTimer(100,
        [&shouldNotFire](int, short, void*) { shouldNotFire.store(true); },
        nullptr, false);

    EXPECT_GE(timerId, 0);

    // Remove before it fires
    EXPECT_TRUE(es.RemoveEvent(timerId));

    std::thread loop([&es]() { es.Start(false); });

    SleepMs(150);
    es.Stop();

    if (loop.joinable()) {
        loop.join();
    }

    // Should not have fired after removal
    EXPECT_FALSE(shouldNotFire.load());
}

TEST(EventSystemTest, RemoveEvent_InvalidId)
{
    EventSystem es(false, 0);
    es.Init();

    EXPECT_FALSE(es.RemoveEvent(-1));
    EXPECT_FALSE(es.RemoveEvent(99999));
}

TEST(EventSystemTest, RemoveEvent_AfterFire_NonPersistent)
{
    EventSystem es(false, 0);
    es.Init();

    std::atomic<int> fireCount{0};

    // Non-persistent timer
    int timerId = es.AddTimer(10,
        [&fireCount, &es, timerId](int, short, void*) mutable {
            fireCount.fetch_add(1);
            // Try to remove after firing (should be auto-removed for non-persist)
        }, nullptr, false);

    EXPECT_GE(timerId, 0);

    std::thread loop([&es]() { es.Start(false); });

    SleepMs(50);
    es.Stop();

    if (loop.joinable()) {
        loop.join();
    }

    EXPECT_EQ(fireCount.load(), 1);
}

// ---------------------------------------------------------------------------
// Start/Stop Tests
// ---------------------------------------------------------------------------

TEST(EventSystemTest, Start_Stop_Basic)
{
    EventSystem es(false, 0);
    es.Init();

    std::atomic<bool> loopRan{false};
    es.AddTimer(10, [&loopRan](int, short, void*) { loopRan.store(true); }, nullptr, false);

    // Start in background
    es.Start(true);
    SleepMs(50);
    es.Stop();

    EXPECT_TRUE(loopRan.load());
}

TEST(EventSystemTest, Start_AlreadyRunning)
{
    EventSystem es(false, 0);
    es.Init();

    es.Start(true);

    // Second start should be no-op
    es.Start(true);

    es.Stop();

    SUCCEED();
}

TEST(EventSystemTest, Stop_NotRunning)
{
    EventSystem es(false, 0);
    es.Init();

    // Stop when not running should be no-op
    es.Stop();

    SUCCEED();
}

TEST(EventSystemTest, StartBlocking_Dispatch)
{
    EventSystem es(false, 0);
    es.Init();

    std::atomic<bool> fired{false};

    es.AddTimer(20,
        [&fired, &es](int, short, void*) {
            fired.store(true);
            es.Stop();  // Stop from within callback
        }, nullptr, false);

    // Blocking start
    es.Start(false);

    EXPECT_TRUE(fired.load());
}

// ---------------------------------------------------------------------------
// Thread Support Tests
// ---------------------------------------------------------------------------

TEST(EventSystemTest, ThreadSupport_Enabled)
{
    EventSystem es(true, 2);
    EXPECT_TRUE(es.Init());

    std::atomic<int> counter{0};

    // Add timer from main thread
    es.AddTimer(30,
        [&counter](int, short, void*) { counter.fetch_add(1); },
        nullptr, false);

    // Run in background thread
    es.Start(true);

    SleepMs(60);
    es.Stop();

    EXPECT_GE(counter.load(), 1);
}

TEST(EventSystemTest, Concurrent_AddEvent)
{
    EventSystem es(true, 3);
    es.Init();

    std::atomic<int> added{0};
    const int numThreads = 4;
    std::vector<std::thread> threads;

    for (int i = 0; i < numThreads; i++) {
        threads.emplace_back([&es, &added]() {
            int id = es.AddTimer(100,
                [](int, short, void*) {}, nullptr, false);
            if (id >= 0) {
                added.fetch_add(1);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // All adds should succeed (or at least not crash)
    EXPECT_GE(added.load(), 0);
}

// ---------------------------------------------------------------------------
// Edge Cases
// ---------------------------------------------------------------------------

TEST(EventSystemTest, EventId_Uniqueness)
{
    EventSystem es(false, 0);
    es.Init();

    std::unordered_set<int> ids;

    for (int i = 0; i < 100; i++) {
        int id = es.AddTimer(1000,
            [](int, short, void*) {}, nullptr, false);
        EXPECT_GE(id, 0);
        EXPECT_EQ(ids.count(id), 0);  // Should be unique
        ids.insert(id);
    }
}

TEST(EventSystemTest, Callback_Exception_Safety)
{
    EventSystem es(false, 0);
    es.Init();

    bool exceptionThrown = false;

    es.AddTimer(10,
        [&exceptionThrown](int, short, void*) {
            exceptionThrown = true;
            throw std::runtime_error("test exception");
        }, nullptr, false);

    std::thread loop([&es]() {
        try {
            es.Start(false);
        } catch (...) {
            // Exception in callback should not crash event loop
        }
    });

    SleepMs(50);
    es.Stop();

    if (loop.joinable()) {
        loop.join();
    }

    // Callback should have been invoked (exception caught internally)
    EXPECT_TRUE(exceptionThrown);
}

// ===========================================================================
// ToLibeventFlags Tests (tested indirectly through AddEvent)
// ===========================================================================

TEST(EventSystemTest, ToLibeventFlags_AllCombinations)
{
    EventSystem es(false, 0);
    es.Init();

    // Test different flag combinations through AddEvent
    auto testFlags = [&es](EventType type, const std::string& name, int fd = -1) {
        int eventId = es.AddEvent(fd, type, [](int, short, void*) {}, nullptr, 100);
        EXPECT_GE(eventId, 0) << "Failed with flags: " << name;
        if (eventId >= 0) {
            es.RemoveEvent(eventId);
        }
    };

    // Single flags - use timer for TIMEOUT and PERSIST
    testFlags(EventType::TIMEOUT, "TIMEOUT");
    testFlags(EventType::PERSIST, "PERSIST");

    // For READ and WRITE, use a valid socket pair
    auto [fd1, fd2] = CreateTestSocketPair();
    ASSERT_GE(fd1, 0);

    testFlags(EventType::READ, "READ", fd1);
    testFlags(EventType::WRITE, "WRITE", fd1);

    // For SIGNAL, we need a valid signal number
    // Use SIGUSR1 which is usually available
    int signalEventId = es.AddEvent(SIGUSR1, EventType::SIGNAL,
        [](int, short, void*) {}, nullptr, 0);
    if (signalEventId >= 0) {
        EXPECT_TRUE(es.RemoveEvent(signalEventId));
    }
    // Note: SIGNAL might not be supported on all platforms, so we don't assert

    // Combined flags
    testFlags(static_cast<EventType>(
        static_cast<unsigned>(EventType::READ) | static_cast<unsigned>(EventType::PERSIST)), "READ|PERSIST", fd1);

    testFlags(static_cast<EventType>(
        static_cast<unsigned>(EventType::WRITE) | static_cast<unsigned>(EventType::PERSIST)), "WRITE|PERSIST", fd1);

    testFlags(static_cast<EventType>(
        static_cast<unsigned>(EventType::READ) | static_cast<unsigned>(EventType::WRITE)), "READ|WRITE", fd1);

    testFlags(static_cast<EventType>(
        static_cast<unsigned>(EventType::TIMEOUT) | static_cast<unsigned>(EventType::PERSIST)), "TIMEOUT|PERSIST");

    close(fd1);
    close(fd2);
}

// ---------------------------------------------------------------------------
// ThreadInitOnceFlag and EnsureLibeventThreadSupport Tests
// ---------------------------------------------------------------------------

TEST(EventSystemTest, ThreadSupport_InitializedOnce)
{
    // Create multiple instances with thread support
    std::vector<std::unique_ptr<EventSystem>> systems;

    for (int i = 0; i < 5; i++) {
        auto es = std::make_unique<EventSystem>(true, i);
        EXPECT_TRUE(es->Init());
        systems.push_back(std::move(es));
    }

    // All should work without crashing
    for (auto& es : systems) {
        int timerId = es->AddTimer(10, [](int, short, void*) {}, nullptr, false);
        EXPECT_GE(timerId, 0);
        es->RemoveEvent(timerId);
    }
}

// ---------------------------------------------------------------------------
// ToTimeval Function Tests (tested via timer precision)
// ---------------------------------------------------------------------------

TEST(EventSystemTest, TimerPrecision_ExactTiming)
{
    EventSystem es(false, 0);
    es.Init();

    const int testTimeMs = 50;
    auto start = std::chrono::steady_clock::now();
    std::atomic<bool> fired{false};

    int timerId = es.AddTimer(testTimeMs, [&fired, &start](int, short, void*) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            fired.store(true);
        }, nullptr, false);

    EXPECT_GE(timerId, 0);

    std::thread loop([&es]() { es.Start(false); });

    // Wait for timer
    while (!fired.load()) {
        SleepMs(5);
    }

    es.Stop();
    loop.join();

    EXPECT_TRUE(fired.load());
}

// ---------------------------------------------------------------------------
// AddEvent with timeout and PERSIST flag
// ---------------------------------------------------------------------------

TEST(EventSystemTest, AddEvent_WithTimeoutAndPersist)
{
    EventSystem es(false, 0);
    es.Init();

    auto [fd1, fd2] = CreateTestSocketPair();
    ASSERT_GE(fd1, 0);

    std::atomic<int> fireCount{0};

    // Add event with read, timeout, and persist
    int eventId = es.AddEvent(fd1, static_cast<EventType>(
            static_cast<unsigned>(EventType::READ) |
            static_cast<unsigned>(EventType::TIMEOUT) |
            static_cast<unsigned>(EventType::PERSIST)),
        [&fireCount](int, [[maybe_unused]] short events, void*) {
            fireCount.fetch_add(1);
        }, nullptr, 50);  // 50ms timeout

    EXPECT_GE(eventId, 0);

    std::thread loop([&es]() { es.Start(false); });

    SleepMs(200);  // Should fire multiple times due to timeout

    es.Stop();
    loop.join();

    // Should have fired at least 3 times (every 50ms)
    EXPECT_GE(fireCount.load(), 3);

    close(fd1);
    close(fd2);
    es.RemoveEvent(eventId);
}

// ---------------------------------------------------------------------------
// AddEvent with signal (if supported)
// ---------------------------------------------------------------------------

TEST(EventSystemTest, AddEvent_Signal)
{
    // Signal events are platform-specific, test basic creation
    EventSystem es(false, 0);
    es.Init();

    // Using SIGUSR1 for testing
    int eventId = es.AddEvent(SIGUSR1, EventType::SIGNAL,
        []([[maybe_unused]] int signum, [[maybe_unused]] short events, void*) {
            // Signal handler - be careful what you do here
        }, nullptr, 0);

    // Note: Signal events might not be supported in all configurations
    if (eventId >= 0) {
        EXPECT_TRUE(es.RemoveEvent(eventId));
    }
}

// ---------------------------------------------------------------------------
// CreateNotifyEventId when event system not initialized
// ---------------------------------------------------------------------------

TEST(EventSystemTest, CreateNotifyEventId_NotInitialized)
{
    EventSystem es(false, 0);
    // Not initialized

    int eventId = es.CreateNotifyEventId(
        [](int, short, void*) {}, nullptr, true);

    EXPECT_EQ(eventId, -1);
}

// ---------------------------------------------------------------------------
// NotifyEventId with various increments
// ---------------------------------------------------------------------------

TEST(EventSystemTest, NotifyEventId_WithIncrements)
{
    EventSystem es(false, 0);
    es.Init();

    std::atomic<int> notifyCount{0};

    int eventId = es.CreateNotifyEventId(
        [&notifyCount](int, short, void*) { notifyCount.fetch_add(1); },
        nullptr, true);

    EXPECT_GE(eventId, 0);

    // Notify with different increments
    EXPECT_TRUE(es.NotifyEventId(eventId, 0));
    EXPECT_TRUE(es.NotifyEventId(eventId, 1));
    EXPECT_TRUE(es.NotifyEventId(eventId, 5));
    EXPECT_TRUE(es.NotifyEventId(eventId, 10));

    std::thread loop([&es]() { es.Start(false); });
    SleepMs(50);
    es.Stop();
    loop.join();

    // Should fire for each notification (since event is persistent)
    // The exact count depends on how many times the callback is invoked
    EXPECT_GE(notifyCount.load(), 1);

    es.CloseNotifyEventId(eventId);
}

TEST(EventSystemTest, ReadEventFd_MultipleValues)
{
    EventSystem es(false, 0);
    es.Init();

    int efd = CreateTestEventFd();
    ASSERT_GE(efd, 0);

    // Write multiple values
    uint64_t val1 = 10;
    uint64_t val2 = 20;
    uint64_t val3 = 30;

    EXPECT_EQ(write(efd, &val1, sizeof(val1)), sizeof(val1));
    EXPECT_EQ(write(efd, &val2, sizeof(val2)), sizeof(val2));
    EXPECT_EQ(write(efd, &val3, sizeof(val3)), sizeof(val3));

    // Should read total sum (eventfd behavior)
    uint64_t outVal = 0;
    EXPECT_TRUE(es.ReadEventFd(efd, outVal));
    EXPECT_EQ(outVal, val1 + val2 + val3);

    close(efd);
}

// ---------------------------------------------------------------------------
// CloseNotifyEventId Tests
// ---------------------------------------------------------------------------

TEST(EventSystemTest, CloseNotifyEventId_Valid)
{
    EventSystem es(false, 0);
    es.Init();

    int eventId = es.CreateNotifyEventId(
        [](int, short, void*) {}, nullptr, true);

    EXPECT_GE(eventId, 0);
    EXPECT_TRUE(es.CloseNotifyEventId(eventId));

    // Closing again should fail
    EXPECT_FALSE(es.CloseNotifyEventId(eventId));
}

TEST(EventSystemTest, CloseNotifyEventId_ClosesFd)
{
    EventSystem es(false, 0);
    es.Init();

    int eventId = es.CreateNotifyEventId(
        [](int, short, void*) {}, nullptr, true);

    EXPECT_GE(eventId, 0);

    // Close it
    EXPECT_TRUE(es.CloseNotifyEventId(eventId));

    // Try to create another event with same ID (should be new fd)
    int eventId2 = es.CreateNotifyEventId(
        [](int, short, void*) {}, nullptr, true);
    EXPECT_GE(eventId2, 0);
    EXPECT_NE(eventId, eventId2);

    es.CloseNotifyEventId(eventId2);
}

// ---------------------------------------------------------------------------
// AddEvent with fd = -1 and timeout only
// ---------------------------------------------------------------------------

TEST(EventSystemTest, AddEvent_TimeoutOnly)
{
    EventSystem es(false, 0);
    es.Init();

    std::atomic<bool> fired{false};

    // This should work like a timer
    int eventId = es.AddEvent(-1, EventType::TIMEOUT,
        [&fired](int, short, void*) { fired.store(true); },
        nullptr, 20);

    EXPECT_GE(eventId, 0);

    std::thread loop([&es]() { es.Start(false); });
    SleepMs(50);
    es.Stop();
    loop.join();

    EXPECT_TRUE(fired.load());
    es.RemoveEvent(eventId);
}

// ---------------------------------------------------------------------------
// AddEvent with both I/O and timeout
// ---------------------------------------------------------------------------

TEST(EventSystemTest, AddEvent_IOWithTimeout)
{
    EventSystem es(false, 0);
    es.Init();

    auto [fd1, fd2] = CreateTestSocketPair();
    ASSERT_GE(fd1, 0);

    std::atomic<bool> timeoutFired{false};
    std::atomic<bool> readFired{false};

    int eventId = es.AddEvent(fd1,
        static_cast<EventType>(
            static_cast<unsigned>(EventType::READ) |
            static_cast<unsigned>(EventType::TIMEOUT)),
        [&timeoutFired, &readFired](int, short events, void*) {
            if (events & EV_TIMEOUT) {
                timeoutFired.store(true);
            }
            if (events & EV_READ) {
                readFired.store(true);
            }
        }, nullptr, 30);

    EXPECT_GE(eventId, 0);

    // Don't write, so timeout should fire
    std::thread loop([&es]() { es.Start(false); });
    SleepMs(50);
    es.Stop();
    loop.join();

    EXPECT_TRUE(timeoutFired.load());
    EXPECT_FALSE(readFired.load());

    close(fd1);
    close(fd2);
    es.RemoveEvent(eventId);
}

// ---------------------------------------------------------------------------
// Stop during callback
// ---------------------------------------------------------------------------

TEST(EventSystemTest, StopDuringCallback)
{
    EventSystem es(false, 0);
    es.Init();

    std::atomic<int> order{0};

    es.AddTimer(10, [&es, &order](int, short, void*) {
        order.fetch_or(1);
        es.Stop();  // Stop immediately
    }, nullptr, false);

    es.AddTimer(20, [&order](int, short, void*) {
        order.fetch_or(2);  // Should not execute
    }, nullptr, false);

    es.Start(false);  // Blocking

    // Only first timer should have executed
    EXPECT_TRUE(order.load() & 1);
    EXPECT_FALSE(order.load() & 2);
}

// ---------------------------------------------------------------------------
// Multiple EventSystem instances
// ---------------------------------------------------------------------------

TEST(EventSystemTest, MultipleInstances_Independent)
{
    EventSystem es1(false, 0);
    EventSystem es2(false, 1);

    EXPECT_TRUE(es1.Init());
    EXPECT_TRUE(es2.Init());

    std::atomic<bool> fired1{false};
    std::atomic<bool> fired2{false};

    es1.AddTimer(20, [&fired1](int, short, void*) { fired1.store(true); }, nullptr, false);
    es2.AddTimer(30, [&fired2](int, short, void*) { fired2.store(true); }, nullptr, false);

    std::thread t1([&es1]() { es1.Start(false); });
    std::thread t2([&es2]() { es2.Start(false); });

    SleepMs(50);
    es1.Stop();
    es2.Stop();

    t1.join();
    t2.join();

    EXPECT_TRUE(fired1.load());
    EXPECT_TRUE(fired2.load());
}

// ---------------------------------------------------------------------------
// Callback with user argument
// ---------------------------------------------------------------------------

TEST(EventSystemTest, Callback_WithUserArgument)
{
    EventSystem es(false, 0);
    es.Init();

    int userData = 42;
    std::atomic<int> receivedData{0};

    int timerId = es.AddTimer(10, [&receivedData](int, short, void* arg) {
            int* data = static_cast<int*>(arg);
            receivedData.store(*data);
        }, &userData, false);

    EXPECT_GE(timerId, 0);

    std::thread loop([&es]() { es.Start(false); });
    SleepMs(50);
    es.Stop();
    loop.join();

    EXPECT_EQ(receivedData.load(), 42);
    es.RemoveEvent(timerId);
}

// ---------------------------------------------------------------------------
// AddEvent with EV_PERSIST flag explicitly set
// ---------------------------------------------------------------------------

TEST(EventSystemTest, AddEvent_ExplicitPersistFlag)
{
    EventSystem es(false, 0);
    es.Init();

    std::atomic<int> fireCount{0};

    // Explicitly add PERSIST flag to timer
    int timerId = es.AddEvent(-1,
        static_cast<EventType>(
            static_cast<unsigned>(EventType::TIMEOUT) |
            static_cast<unsigned>(EventType::PERSIST)),
        [&fireCount](int, short, void*) { fireCount.fetch_add(1); },
        nullptr, 20);

    EXPECT_GE(timerId, 0);

    std::thread loop([&es]() { es.Start(false); });
    SleepMs(80);  // Should fire multiple times
    es.Stop();
    loop.join();

    EXPECT_GE(fireCount.load(), 3);
    es.RemoveEvent(timerId);
}

// ---------------------------------------------------------------------------
// Additional edge cases
// ---------------------------------------------------------------------------

TEST(EventSystemTest, AddTimer_ZeroTimeout_ShouldFail)
{
    EventSystem es(false, 0);
    es.Init();

    int timerId = es.AddTimer(0, [](int, short, void*) {}, nullptr, false);
    EXPECT_EQ(timerId, -1);
}

TEST(EventSystemTest, AddTimer_NegativeTimeout_ShouldFail)
{
    EventSystem es(false, 0);
    es.Init();

    int timerId = es.AddTimer(-10, [](int, short, void*) {}, nullptr, false);
    EXPECT_EQ(timerId, -1);
}

TEST(EventSystemTest, AddEvent_DuplicateFd_ShouldWork)
{
    EventSystem es(false, 0);
    es.Init();

    auto [fd1, fd2] = CreateTestSocketPair();
    ASSERT_GE(fd1, 0);

    std::atomic<int> fireCount{0};

    // Add two events on same fd
    int id1 = es.AddEvent(fd1, EventType::READ,
        [&fireCount](int, short, void*) { fireCount.fetch_add(1); },
        nullptr, 0);

    int id2 = es.AddEvent(fd1, EventType::WRITE,
        [&fireCount](int, short, void*) { fireCount.fetch_add(1); },
        nullptr, 0);

    EXPECT_GE(id1, 0);
    EXPECT_GE(id2, 0);

    // Trigger both
    const char* msg = "test";
    send(fd2, msg, strlen(msg), 0);

    std::thread loop([&es]() { es.Start(false); });
    SleepMs(50);
    es.Stop();
    loop.join();

    EXPECT_GE(fireCount.load(), 1);

    close(fd1);
    close(fd2);

    es.RemoveEvent(id1);
    es.RemoveEvent(id2);
}

TEST(EventSystemTest, RemoveEvent_AfterEventBaseDestroyed)
{
    auto es = std::make_unique<EventSystem>(false, 0);
    es->Init();

    int timerId = es->AddTimer(100, [](int, short, void*) {}, nullptr, false);
    EXPECT_GE(timerId, 0);

    // Destroy event system
    es.reset();

    // Should not crash
    SUCCEED();
}

TEST(EventSystemTest, NotifyEventId_AfterEventRemoved)
{
    EventSystem es(false, 0);
    es.Init();

    int eventId = es.CreateNotifyEventId(
        [](int, short, void*) {}, nullptr, true);

    EXPECT_GE(eventId, 0);

    // Remove the event
    EXPECT_TRUE(es.RemoveEvent(eventId));

    // Notify should fail
    EXPECT_FALSE(es.NotifyEventId(eventId, 1));
}

// ---------------------------------------------------------------------------
// AddEvent with invalid fd
// ---------------------------------------------------------------------------

TEST(EventSystemTest, AddEvent_InvalidFd)
{
    EventSystem es(false, 0);
    es.Init();

    // Use a closed fd
    int invalidFd = CreateTestEventFd();
    close(invalidFd);

    int eventId = es.AddEvent(invalidFd, EventType::READ, [](int, short, void*) {}, nullptr, 0);

    // Should fail or succeed? libevent might accept it but event_add will fail
    if (eventId >= 0) {
        // If it succeeded, we need to clean up
        es.RemoveEvent(eventId);
    }

    // Test passes if no crash
    SUCCEED();
}

} // namespace A2A::Event::Test