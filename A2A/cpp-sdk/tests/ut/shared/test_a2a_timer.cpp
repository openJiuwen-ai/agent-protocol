/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "a2a_timer.h"

namespace A2A::Test {

namespace {

constexpr int REFRESH_INITIAL_TIMEOUT_MS = 1000;
constexpr int REFRESH_REPEAT_COUNT = 10;
constexpr int REFRESH_INTERVAL_MS = 50;
constexpr int REFRESH_SETTLE_MS = 250;
constexpr int CONCURRENT_REFRESH_COUNT = 50;
constexpr int CONCURRENT_REFRESH_INTERVAL_MS = 20;
constexpr int CONCURRENT_REFRESH_SETTLE_MS = 300;

} // namespace

class A2ATimerTest : public ::testing::Test {
protected:
    void TearDown() override
    {
        A2ATimer::Instance().Stop();
    }
};

TEST_F(A2ATimerTest, AddTimer_InvokesCallback)
{
    std::atomic<int> fired{0};
    const int id = A2ATimer::Instance().AddTimer(
        50,
        [](void* arg) {
            (*static_cast<std::atomic<int>*>(arg))++;
        },
        &fired);
    ASSERT_GT(id, 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_EQ(fired.load(), 1);
}

TEST_F(A2ATimerTest, CancelTimer_PreventsCallback)
{
    std::atomic<int> fired{0};
    const int id = A2ATimer::Instance().AddTimer(
        200,
        [](void* arg) {
            (*static_cast<std::atomic<int>*>(arg))++;
        },
        &fired);
    ASSERT_GT(id, 0);

    A2ATimer::Instance().CancelTimer(id);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    EXPECT_EQ(fired.load(), 0);
}

TEST_F(A2ATimerTest, RefreshTimer_DelaysCallback)
{
    std::atomic<int> fired{0};
    const int id = A2ATimer::Instance().AddTimer(
        50,
        [](void* arg) {
            (*static_cast<std::atomic<int>*>(arg))++;
        },
        &fired);
    ASSERT_GT(id, 0);
    EXPECT_EQ(A2ATimer::Instance().RefreshTimer(id, 200), 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    EXPECT_EQ(fired.load(), 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(fired.load(), 1);
}

TEST_F(A2ATimerTest, RefreshTimer_RepeatedRefreshFiresOnce)
{
    std::atomic<int> fired{0};
    const int id = A2ATimer::Instance().AddTimer(
        REFRESH_INITIAL_TIMEOUT_MS,
        [](void* arg) {
            (*static_cast<std::atomic<int>*>(arg))++;
        },
        &fired);
    ASSERT_GT(id, 0);

    // Only the last arming may fire, and it must fire exactly once.
    for (int i = 0; i < REFRESH_REPEAT_COUNT; ++i) {
        EXPECT_EQ(A2ATimer::Instance().RefreshTimer(id, REFRESH_INTERVAL_MS), 0);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(REFRESH_SETTLE_MS));
    EXPECT_EQ(fired.load(), 1);
}

TEST_F(A2ATimerTest, RefreshTimer_ConcurrentRefreshFiresOnce)
{
    std::atomic<int> fired{0};
    const int id = A2ATimer::Instance().AddTimer(
        REFRESH_INITIAL_TIMEOUT_MS,
        [](void* arg) {
            (*static_cast<std::atomic<int>*>(arg))++;
        },
        &fired);
    ASSERT_GT(id, 0);

    auto refresher = [&id]() {
        for (int i = 0; i < CONCURRENT_REFRESH_COUNT; ++i) {
            // Superseded refreshes return -1 by design.
            (void)A2ATimer::Instance().RefreshTimer(id, CONCURRENT_REFRESH_INTERVAL_MS);
        }
    };
    std::thread first(refresher);
    std::thread second(refresher);
    first.join();
    second.join();

    std::this_thread::sleep_for(std::chrono::milliseconds(CONCURRENT_REFRESH_SETTLE_MS));
    EXPECT_EQ(fired.load(), 1);
}

TEST_F(A2ATimerTest, Stop_IsIdempotent)
{
    EXPECT_NO_THROW(A2ATimer::Instance().Stop());
    EXPECT_NO_THROW(A2ATimer::Instance().Stop());
}

} // namespace A2A::Test
