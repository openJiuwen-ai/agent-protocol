/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <gtest/gtest.h>

#include "event/event_system.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>
#include <memory>
#include <thread>

using namespace std::chrono_literals;

namespace {

constexpr int SOCKET_PAIR_FD_COUNT = 2;
constexpr long BEFORE_INIT_TIMER_TIMEOUT_MS = 10;
constexpr long ONE_SHOT_TIMER_TIMEOUT_MS = 50;
constexpr int ONE_SHOT_WAIT_TIMEOUT_MS = 500;
constexpr int ONE_SHOT_GRACE_PERIOD_MS = 200;
constexpr long REPEAT_TIMER_TIMEOUT_MS = 30;
constexpr int REPEAT_EXPECTED_CALL_COUNT = 3;
constexpr int REPEAT_WAIT_TIMEOUT_MS = 1000;
constexpr int INVALID_EVENT_ID = 999999;
constexpr int SOCKET_POLL_ATTEMPTS = 100;
constexpr int SOCKET_POLL_SLEEP_MS = 10;
constexpr uint64_t FIRST_NOTIFY_INCREMENT = 3;
constexpr int NOTIFY_GAP_MS = 20;
constexpr int NOTIFY_WAIT_TIMEOUT_MS = 1000;
constexpr std::size_t EXPECTED_NOTIFY_VALUE_COUNT = 2;
constexpr int NOTIFIER_HEAD_START_MS = 20;
constexpr int CLOSE_NOTIFY_RETRY_ATTEMPTS = 100;

}

TEST(EventSystemTest, InitAndGetEventBase)
{
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());
    ASSERT_NE(es.GetEventBase(), nullptr);
}

TEST(EventSystemTest, AddEventBeforeInit)
{
    Mcp::EventSystem es;
    std::atomic<int> called{0};
    auto cb = [&called](int, short, auto*) { called.fetch_add(1); };

    // addTimer requires init first -> should fail when not initialized
    int id = es.AddTimer(BEFORE_INIT_TIMER_TIMEOUT_MS, cb, nullptr, false);
    EXPECT_EQ(id, -1);

    // addEvent also fails when not initialized
    id = es.AddEvent(-1, Mcp::EventType::TIMEOUT, cb, nullptr, BEFORE_INIT_TIMER_TIMEOUT_MS);
    EXPECT_EQ(id, -1);
}

TEST(EventSystemTest, AddTimerOneShot)
{
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    std::mutex m;
    std::condition_variable cv;
    std::atomic<int> calls{0};

    auto cb = [&calls, &cv](int, short, auto*) {
        calls.fetch_add(1);
        cv.notify_one();
    };

    int id = es.AddTimer(ONE_SHOT_TIMER_TIMEOUT_MS, cb, nullptr, false);
    ASSERT_GT(id, 0);

    es.Start(true);

    {
        std::unique_lock<std::mutex> lk(m);
        EXPECT_TRUE(cv.wait_for(lk, std::chrono::milliseconds(ONE_SHOT_WAIT_TIMEOUT_MS), [&calls]() {
            return calls.load() >= 1;
        }));
    }

    // allow small grace period to ensure it does not fire again
    std::this_thread::sleep_for(std::chrono::milliseconds(ONE_SHOT_GRACE_PERIOD_MS));
    EXPECT_EQ(calls.load(), 1);

    EXPECT_FALSE(es.RemoveEvent(id));

    es.Stop();
}

TEST(EventSystemTest, AddTimerRepeatAndRemove)
{
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    std::mutex m;
    std::condition_variable cv;
    std::atomic<int> calls{0};

    auto cb = [&calls, &cv](int, short, auto*) {
        int c = calls.fetch_add(1) + 1;
        if (c >= REPEAT_EXPECTED_CALL_COUNT) {
            cv.notify_one();
        }
    };

    int id = es.AddTimer(REPEAT_TIMER_TIMEOUT_MS, cb, nullptr, true);
    ASSERT_GT(id, 0);

    es.Start(true);

    {
        std::unique_lock<std::mutex> lk(m);
        EXPECT_TRUE(cv.wait_for(lk, std::chrono::milliseconds(REPEAT_WAIT_TIMEOUT_MS), [&calls]() {
            return calls.load() >= REPEAT_EXPECTED_CALL_COUNT;
        }));
    }

    EXPECT_TRUE(es.RemoveEvent(id));

    int after = calls.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(ONE_SHOT_GRACE_PERIOD_MS));

    EXPECT_EQ(after, calls.load());

    es.Stop();
}

TEST(EventSystemTest, InvalidArgs)
{
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    std::atomic<int> called{0};
    auto cb = [&called](int, short, auto*) { called.fetch_add(1); };

    int id = es.AddEvent(-1, static_cast<Mcp::EventType>(0), cb, nullptr, BEFORE_INIT_TIMER_TIMEOUT_MS);
    EXPECT_EQ(id, -1);

    id = es.AddEvent(-1, Mcp::EventType::TIMEOUT, nullptr, nullptr, BEFORE_INIT_TIMER_TIMEOUT_MS);
    EXPECT_EQ(id, -1);

    id = es.AddTimer(0, cb, nullptr, false);
    EXPECT_EQ(id, -1);

    EXPECT_FALSE(es.RemoveEvent(INVALID_EVENT_ID));
}

TEST(EventSystemIOTest, ReadEventTriggeredFromSocketpair)
{
    int sv[SOCKET_PAIR_FD_COUNT];
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sv));
    Mcp::EventSystem es(true);
    ASSERT_TRUE(es.Init());
    std::atomic<bool> called{false};
    int eventId = es.AddEvent(sv[0],
        Mcp::EventType::READ,
        [&called](int, short, auto*) {
            called.store(true);
        },
        nullptr);
    ASSERT_NE(-1, eventId);

    es.Start(true);

    const char payload[] = "x";
    ssize_t n = write(sv[1], payload, sizeof(payload));
    ASSERT_GT(n, 0);

    for (int i = 0; i < SOCKET_POLL_ATTEMPTS && !called.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(SOCKET_POLL_SLEEP_MS));
    }
    ASSERT_TRUE(called.load());

    es.RemoveEvent(eventId);
    es.Stop();
    close(sv[0]);
    close(sv[1]);
}

TEST(EventSystemTest, StartStopIdempotent)
{
    Mcp::EventSystem es(true);
    ASSERT_TRUE(es.Init());

    es.Start(true);
    es.Start(true);

    es.Stop();
    es.Stop();
}

TEST(EventSystemTest, EventFdCreateNotifyReadClose)
{
    Mcp::EventSystem es(true);
    ASSERT_TRUE(es.Init());

    std::mutex m;
    std::condition_variable cv;
    std::shared_ptr<std::vector<uint64_t>> values = std::make_shared<std::vector<uint64_t>>();
    std::atomic<int> calls{0};

    auto cb = [&es, values, &m, &cv, &calls](int fd, short, auto*) {
        uint64_t v = 0;
        if (es.ReadEventFd(fd, v)) {
            {
                std::lock_guard<std::mutex> lk(m);
                values->push_back(v);
            }
            calls.fetch_add(1);
            cv.notify_one();
        }
    };

    int notifyEventId = es.CreateNotifyEventId(cb, nullptr, true);
    ASSERT_GT(notifyEventId, 0);

    es.Start(true);

    ASSERT_TRUE(es.NotifyEventId(notifyEventId, FIRST_NOTIFY_INCREMENT));
    std::this_thread::sleep_for(std::chrono::milliseconds(NOTIFY_GAP_MS));
    ASSERT_TRUE(es.NotifyEventId(notifyEventId, 1));

    {
        std::unique_lock<std::mutex> lk(m);
        EXPECT_TRUE(cv.wait_for(lk, std::chrono::milliseconds(NOTIFY_WAIT_TIMEOUT_MS), [&calls]() {
            return calls.load() >= static_cast<int>(EXPECTED_NOTIFY_VALUE_COUNT);
        }));
    }

    {
        std::lock_guard<std::mutex> lk(m);
        ASSERT_GE(values->size(), EXPECTED_NOTIFY_VALUE_COUNT);
        EXPECT_EQ(values->at(0), FIRST_NOTIFY_INCREMENT);
        EXPECT_EQ(values->at(1), 1u);
    }

    // cleanup
    EXPECT_TRUE(es.CloseNotifyEventId(notifyEventId));
    es.Stop();
}

TEST(EventSystemTest, NotifyAndCloseNotifyEventIdConcurrentStress)
{
    Mcp::EventSystem es(true);
    ASSERT_TRUE(es.Init());

    std::atomic<int> callbackCalls{0};
    std::atomic<bool> stop{false};

    auto cb = [&es, &callbackCalls](int fd, short, auto*) {
        // Drain eventfd so writers don't hit EAGAIN.
        uint64_t v = 0;
        while (es.ReadEventFd(fd, v)) {
        }
        callbackCalls.fetch_add(1, std::memory_order_relaxed);
    };

    int notifyEventId = es.CreateNotifyEventId(cb, nullptr, true);
    ASSERT_GT(notifyEventId, 0);

    es.Start(true);

    std::atomic<int> notifyOk{0};
    std::atomic<int> notifyFail{0};

    std::thread notifier([&es, &notifyOk, &notifyFail, &stop, notifyEventId]() {
        // Tight loop to increase the chance of racing with Close.
        while (!stop.load(std::memory_order_acquire)) {
            if (es.NotifyEventId(notifyEventId)) {
                notifyOk.fetch_add(1, std::memory_order_relaxed);
            } else {
                notifyFail.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::yield();
            }
        }
    });

    // Give the notifier a brief head start.
    std::this_thread::sleep_for(std::chrono::milliseconds(NOTIFIER_HEAD_START_MS));

    // Close concurrently.
    EXPECT_TRUE(es.CloseNotifyEventId(notifyEventId));

    // After close, Notify should eventually fail.
    for (int i = 0; i < CLOSE_NOTIFY_RETRY_ATTEMPTS; ++i) {
        if (!es.NotifyEventId(notifyEventId)) {
            break;
        }
        std::this_thread::sleep_for(1ms);
    }
    EXPECT_FALSE(es.NotifyEventId(notifyEventId));
    stop.store(true, std::memory_order_release);
    if (notifier.joinable()) {
        notifier.join();
    }

    es.Stop();

    // Sanity: we should have had at least some successful notifies before close.
    EXPECT_GT(notifyOk.load(), 0);
}