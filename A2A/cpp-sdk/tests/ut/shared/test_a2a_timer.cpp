/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "a2a_timer.h"

namespace A2A::Test {

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

TEST_F(A2ATimerTest, Stop_IsIdempotent)
{
    EXPECT_NO_THROW(A2ATimer::Instance().Stop());
    EXPECT_NO_THROW(A2ATimer::Instance().Stop());
}

} // namespace A2A::Test
