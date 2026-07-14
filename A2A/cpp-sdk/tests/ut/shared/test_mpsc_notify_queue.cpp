/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <thread>
#include <atomic>
#include <chrono>
#include <vector>

#include "message_queue/mpsc_notify_queue.h"
#include "event/event_system.h"

using namespace A2A;

// ===========================================================================
// MPSCNotifyQueue Tests
// ===========================================================================

class MPSCNotifyQueueTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        eventSystem = std::make_unique<EventSystem>(true, 0);
        ASSERT_TRUE(eventSystem->Init());
        // Start event system in background for async processing
        eventSystem->Start(true);
        // Give some time for event loop to start
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    void TearDown() override
    {
        if (eventSystem) {
            eventSystem->Stop();
            eventSystem.reset();
        }
    }

    std::unique_ptr<EventSystem> eventSystem;
};

TEST_F(MPSCNotifyQueueTest, Constructor_CreatesQueueWithCorrectCapacity)
{
    MPSCNotifyQueue<int> queue(32, 10);
    EXPECT_EQ(queue.GetQueueCapacity(), 32u);
    EXPECT_EQ(queue.GetMaxBatchSize(), 10u);
    EXPECT_EQ(queue.GetQueueSize(), 0u);
    EXPECT_TRUE(queue.Empty());
    EXPECT_FALSE(queue.IsInitialized());
}

TEST_F(MPSCNotifyQueueTest, Constructor_WithZeroBatchSize_DefaultsToOne)
{
    MPSCNotifyQueue<int> queue(16, 0);
    EXPECT_EQ(queue.GetMaxBatchSize(), 1u);
}

TEST_F(MPSCNotifyQueueTest, Initialize_WithValidEventSystem_ReturnsTrue)
{
    MPSCNotifyQueue<int> queue(16, 5);
    std::atomic<int> receivedCount{0};

    auto handler = [&receivedCount](const int& msg) {
        receivedCount++;
    };

    EXPECT_TRUE(queue.Initialize(eventSystem.get(), handler));
    EXPECT_TRUE(queue.IsInitialized());
}

TEST_F(MPSCNotifyQueueTest, Initialize_WithNullEventSystem_ReturnsFalse)
{
    MPSCNotifyQueue<int> queue(16, 5);
    auto handler = [](const int& msg) {};

    EXPECT_FALSE(queue.Initialize(nullptr, handler));
    EXPECT_FALSE(queue.IsInitialized());
}

TEST_F(MPSCNotifyQueueTest, Send_WhenNotInitialized_ReturnsFalse)
{
    MPSCNotifyQueue<int> queue(16, 5);
    EXPECT_FALSE(queue.Send(42));
}

TEST_F(MPSCNotifyQueueTest, GetQueueSize_ReturnsCorrectCount)
{
    MPSCNotifyQueue<int> queue(16, 5);
    auto handler = [](const int& msg) {};

    EXPECT_TRUE(queue.Initialize(eventSystem.get(), handler));

    EXPECT_EQ(queue.GetQueueSize(), 0u);
    queue.Send(1);
    EXPECT_EQ(queue.GetQueueSize(), 1u);
    queue.Send(2);
    EXPECT_EQ(queue.GetQueueSize(), 2u);
}

TEST_F(MPSCNotifyQueueTest, Empty_ReturnsCorrectState)
{
    MPSCNotifyQueue<int> queue(16, 5);
    auto handler = [](const int& msg) {};

    EXPECT_TRUE(queue.Initialize(eventSystem.get(), handler));

    EXPECT_TRUE(queue.Empty());
    queue.Send(1);
    EXPECT_FALSE(queue.Empty());
}

TEST_F(MPSCNotifyQueueTest, TryPop_ManuallyPopsMessages)
{
    MPSCNotifyQueue<int> queue(16, 5);
    auto handler = [](const int& msg) {};  // Not used

    EXPECT_TRUE(queue.Initialize(eventSystem.get(), handler));

    queue.Send(1);
    queue.Send(2);

    int value;
    EXPECT_TRUE(queue.TryPop(value));
    EXPECT_EQ(value, 1);

    EXPECT_TRUE(queue.TryPop(value));
    EXPECT_EQ(value, 2);

    EXPECT_FALSE(queue.TryPop(value));
    EXPECT_TRUE(queue.Empty());
}

TEST_F(MPSCNotifyQueueTest, Cleanup_RemovesEventAndClearsState)
{
    MPSCNotifyQueue<int> queue(16, 5);
    auto handler = [](const int& msg) {};

    EXPECT_TRUE(queue.Initialize(eventSystem.get(), handler));
    EXPECT_TRUE(queue.IsInitialized());

    queue.Cleanup();

    EXPECT_FALSE(queue.IsInitialized());
    EXPECT_FALSE(queue.Send(42));
    EXPECT_TRUE(queue.Empty());
}

TEST_F(MPSCNotifyQueueTest, DoubleInitialize_ReturnsTrue)
{
    MPSCNotifyQueue<int> queue(16, 5);
    auto handler = [](const int& msg) {};

    EXPECT_TRUE(queue.Initialize(eventSystem.get(), handler));
    EXPECT_TRUE(queue.IsInitialized());

    // Second initialize should return true
    EXPECT_TRUE(queue.Initialize(eventSystem.get(), handler));
    EXPECT_TRUE(queue.IsInitialized());
}

TEST_F(MPSCNotifyQueueTest, Destructor_CleansUpProperly)
{
    auto queue = std::make_unique<MPSCNotifyQueue<int>>(16, 5);
    auto handler = [](const int& msg) {};

    EXPECT_TRUE(queue->Initialize(eventSystem.get(), handler));
    queue->Send(42);
    queue->Send(100);

    // Destructor should clean up without crashing
    queue.reset();
    SUCCEED();
}

TEST_F(MPSCNotifyQueueTest, GetMaxBatchSize_ReturnsCorrectValue)
{
    MPSCNotifyQueue<int> queue(16, 5);
    EXPECT_EQ(queue.GetMaxBatchSize(), 5u);

    MPSCNotifyQueue<int> queue2(16, 10);
    EXPECT_EQ(queue2.GetMaxBatchSize(), 10u);
}

TEST_F(MPSCNotifyQueueTest, GetQueueCapacity_ReturnsCorrectValue)
{
    MPSCNotifyQueue<int> queue(16, 5);
    EXPECT_EQ(queue.GetQueueCapacity(), 16u);

    MPSCNotifyQueue<int> queue2(32, 5);
    EXPECT_EQ(queue2.GetQueueCapacity(), 32u);
}

TEST_F(MPSCNotifyQueueTest, IsInitialized_ReturnsCorrectState)
{
    MPSCNotifyQueue<int> queue(16, 5);
    EXPECT_FALSE(queue.IsInitialized());

    auto handler = [](const int& msg) {};
    EXPECT_TRUE(queue.Initialize(eventSystem.get(), handler));
    EXPECT_TRUE(queue.IsInitialized());

    queue.Cleanup();
    EXPECT_FALSE(queue.IsInitialized());
}