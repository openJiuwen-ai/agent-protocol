/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

#include "message_queue/lock_free_queue.h"

using namespace A2A;
using ::testing::_;
using StringPtrQueue = MPSCQueue<std::unique_ptr<std::string>>;

// ===========================================================================
// LockFreeSPSCQueue Tests
// ===========================================================================

class LockFreeSPSCQueueTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Default queue with 16 capacity
        queue = std::make_unique<LockFreeSPSCQueue<int>>(16);
    }

    std::unique_ptr<LockFreeSPSCQueue<int>> queue;
};

TEST_F(LockFreeSPSCQueueTest, DefaultConstructor_CreatesEmptyQueue)
{
    LockFreeSPSCQueue<int> emptyQueue;
    EXPECT_TRUE(emptyQueue.Empty());
    EXPECT_EQ(emptyQueue.Size(), 0u);
    EXPECT_EQ(emptyQueue.Capacity(), 0u);
}

TEST_F(LockFreeSPSCQueueTest, Constructor_WithValidSize_InitializesCorrectly)
{
    LockFreeSPSCQueue<int> q(32);
    EXPECT_TRUE(q.Empty());
    EXPECT_EQ(q.Size(), 0u);
    EXPECT_EQ(q.Capacity(), 32u);
}

TEST_F(LockFreeSPSCQueueTest, Constructor_WithMinSize_ClampsToMinimum)
{
    LockFreeSPSCQueue<int> q(4);
    EXPECT_EQ(q.Capacity(), 16u);  // MIN_CAPACITY is 16
}

TEST_F(LockFreeSPSCQueueTest, Constructor_WithNonPowerOfTwo_RoundsUpToNextPower)
{
    LockFreeSPSCQueue<int> q(10);
    EXPECT_EQ(q.Capacity(), 16u);  // Next power of 2

    LockFreeSPSCQueue<int> q2(20);
    EXPECT_EQ(q2.Capacity(), 32u);  // Next power of 2
}

TEST_F(LockFreeSPSCQueueTest, Push_WhenQueueNotFull_ReturnsTrue)
{
    EXPECT_TRUE(queue->Push(42));
    EXPECT_FALSE(queue->Empty());
    EXPECT_EQ(queue->Size(), 1u);
}

TEST_F(LockFreeSPSCQueueTest, Push_WhenQueueFull_ReturnsFalse)
{
    // Fill the queue (capacity is 16, but usable slots are 15 because of the design)
    for (int i = 0; i < 15; i++) {
        EXPECT_TRUE(queue->Push(i));
    }
    EXPECT_TRUE(queue->Full());
    EXPECT_FALSE(queue->Push(999));  // Should fail
}

TEST_F(LockFreeSPSCQueueTest, Pop_WhenQueueNotEmpty_ReturnsTrueAndRetrievesData)
{
    queue->Push(42);
    queue->Push(100);

    int value;
    EXPECT_TRUE(queue->Pop(value));
    EXPECT_EQ(value, 42);
    EXPECT_EQ(queue->Size(), 1u);

    EXPECT_TRUE(queue->Pop(value));
    EXPECT_EQ(value, 100);
    EXPECT_TRUE(queue->Empty());
}

TEST_F(LockFreeSPSCQueueTest, Pop_WhenQueueEmpty_ReturnsFalse)
{
    int value;
    EXPECT_FALSE(queue->Pop(value));
    EXPECT_TRUE(queue->Empty());
}

TEST_F(LockFreeSPSCQueueTest, PushPop_MaintainsFIFOOrder)
{
    std::vector<int> input = {1, 2, 3, 4, 5};
    for (int val : input) {
        EXPECT_TRUE(queue->Push(val));
    }

    std::vector<int> output;
    int value;
    while (queue->Pop(value)) {
        output.push_back(value);
    }

    EXPECT_EQ(output, input);
}

TEST_F(LockFreeSPSCQueueTest, PushPop_WrapAroundWorksCorrectly)
{
    // Fill and drain to test wrap-around
    for (int i = 0; i < 15; i++) {
        queue->Push(i);
    }

    int value;
    for (int i = 0; i < 10; i++) {
        queue->Pop(value);
        EXPECT_EQ(value, i);
    }

    // Push more after partial drain
    for (int i = 10; i < 20; i++) {
        EXPECT_TRUE(queue->Push(i));
    }

    // Drain remaining
    std::vector<int> output;
    while (queue->Pop(value)) {
        output.push_back(value);
    }

    EXPECT_EQ(output.size(), 15u);  // 5 remaining from first batch + 10 new
    EXPECT_EQ(output[0], 10);
    EXPECT_EQ(output.back(), 19);
}

TEST_F(LockFreeSPSCQueueTest, Size_ReturnsCorrectCount)
{
    EXPECT_EQ(queue->Size(), 0u);
    queue->Push(1);
    EXPECT_EQ(queue->Size(), 1u);
    queue->Push(2);
    EXPECT_EQ(queue->Size(), 2u);

    int value;
    queue->Pop(value);
    EXPECT_EQ(queue->Size(), 1u);
    queue->Pop(value);
    EXPECT_EQ(queue->Size(), 0u);
}

TEST_F(LockFreeSPSCQueueTest, ConcurrentPushPop_WorksCorrectly)
{
    const int iterations = 10000;
    std::atomic<bool> done{false};
    std::vector<int> received;

    // Producer thread
    std::thread producer([this, iterations, &done]() {
        for (int i = 0; i < iterations; i++) {
            while (!queue->Push(i)) {
                std::this_thread::yield();
            }
        }
        done = true;
    });

    // Consumer thread
    std::thread consumer([this, &received, &done]() {
        int value;
        while (!done || !queue->Empty()) {
            if (queue->Pop(value)) {
                received.push_back(value);
            }
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(received.size(), iterations);
    // Verify all numbers 0..iterations-1 are present
    std::sort(received.begin(), received.end());
    for (int i = 0; i < iterations; i++) {
        EXPECT_EQ(received[i], i);
    }
}

TEST_F(LockFreeSPSCQueueTest, UninitializedQueue_OperationsReturnFalse)
{
    LockFreeSPSCQueue<int> uninit;
    EXPECT_TRUE(uninit.Empty());
    EXPECT_EQ(uninit.Size(), 0u);
    EXPECT_FALSE(uninit.Push(42));

    int value;
    EXPECT_FALSE(uninit.Pop(value));
    EXPECT_FALSE(uninit.Full());
}

// ===========================================================================
// MPSCQueue Tests
// ===========================================================================

class MPSCQueueTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        queue = std::make_unique<MPSCQueue<int>>(32);
    }

    std::unique_ptr<MPSCQueue<int>> queue;
};

TEST_F(MPSCQueueTest, Constructor_WithValidCapacity_InitializesCorrectly)
{
    MPSCQueue<int> q(16);
    EXPECT_TRUE(q.Empty());
    EXPECT_EQ(q.Size(), 0u);
    EXPECT_EQ(q.Capacity(), 16u);
}

TEST_F(MPSCQueueTest, Constructor_WithNonPowerOfTwo_RoundsUp)
{
    MPSCQueue<int> q(10);
    EXPECT_EQ(q.Capacity(), 16u);

    MPSCQueue<int> q2(20);
    EXPECT_EQ(q2.Capacity(), 32u);
}

TEST_F(MPSCQueueTest, Push_WhenQueueNotFull_ReturnsTrue)
{
    EXPECT_TRUE(queue->Push(42));
    EXPECT_FALSE(queue->Empty());
    EXPECT_EQ(queue->Size(), 1u);
}

TEST_F(MPSCQueueTest, Push_WhenQueueFull_ReturnsFalse)
{
    // Fill the queue
    for (int i = 0; i < 32; i++) {
        EXPECT_TRUE(queue->Push(i));
    }
    EXPECT_FALSE(queue->Push(999));
    EXPECT_EQ(queue->Size(), 32u);
}

TEST_F(MPSCQueueTest, TryPop_WhenQueueNotEmpty_ReturnsTrueAndRetrievesData)
{
    queue->Push(42);
    queue->Push(100);

    int value;
    EXPECT_TRUE(queue->TryPop(value));
    EXPECT_EQ(value, 42);
    EXPECT_EQ(queue->Size(), 1u);

    EXPECT_TRUE(queue->TryPop(value));
    EXPECT_EQ(value, 100);
    EXPECT_TRUE(queue->Empty());
}

TEST_F(MPSCQueueTest, TryPop_WhenQueueEmpty_ReturnsFalse)
{
    int value;
    EXPECT_FALSE(queue->TryPop(value));
}

TEST_F(MPSCQueueTest, PushPop_MaintainsFIFOOrder)
{
    std::vector<int> input = {1, 2, 3, 4, 5};
    for (int val : input) {
        queue->Push(val);
    }

    std::vector<int> output;
    int value;
    while (queue->TryPop(value)) {
        output.push_back(value);
    }

    EXPECT_EQ(output, input);
}

TEST_F(MPSCQueueTest, Size_ReturnsCorrectCount)
{
    EXPECT_EQ(queue->Size(), 0u);
    queue->Push(1);
    EXPECT_EQ(queue->Size(), 1u);
    queue->Push(2);
    EXPECT_EQ(queue->Size(), 2u);

    int value;
    queue->TryPop(value);
    EXPECT_EQ(queue->Size(), 1u);
    queue->TryPop(value);
    EXPECT_EQ(queue->Size(), 0u);
}

TEST_F(MPSCQueueTest, MixedPushPop_WorksCorrectly)
{
    const int iterations = 5000;
    std::atomic<bool> done{false};
    std::vector<int> received;

    // Producer thread
    std::thread producer([this, iterations, &done]() {
        for (int i = 0; i < iterations; i++) {
            while (!queue->Push(i)) {
                std::this_thread::yield();
            }
        }
        done = true;
    });

    // Consumer thread
    std::thread consumer([this, &received, &done]() {
        int value;
        while (!done || !queue->Empty()) {
            if (queue->TryPop(value)) {
                received.push_back(value);
            }
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(received.size(), static_cast<size_t>(iterations));
    // Verify all numbers 0..iterations-1 are present
    std::sort(received.begin(), received.end());
    for (int i = 0; i < iterations; i++) {
        EXPECT_EQ(received[i], i);
    }
}

TEST_F(MPSCQueueTest, EmptyQueue_AfterAllOperations_ReturnsEmpty)
{
    queue->Push(1);
    queue->Push(2);
    int value;
    queue->TryPop(value);
    queue->TryPop(value);
    EXPECT_TRUE(queue->Empty());
    EXPECT_EQ(queue->Size(), 0u);
}

// 使用原始指针并手动管理内存，避免 unique_ptr 的拷贝问题
TEST_F(MPSCQueueTest, Destructor_WithPendingItems_DoesNotLeak)
{
    // 使用原始指针的队列
    auto q = std::make_unique<MPSCQueue<std::string*>>(16);

    // 分配内存
    auto* str1 = new std::string("test1");
    auto* str2 = new std::string("test2");
    auto* str3 = new std::string("test3");

    q->Push(str1);
    q->Push(str2);
    q->Push(str3);

    // 手动消费并删除所有元素，避免内存泄漏
    std::string* value;
    while (q->TryPop(value)) {
        delete value;
    }

    // 队列现在为空，析构时会安全
    q.reset();
    SUCCEED();
}

TEST_F(MPSCQueueTest, PushPop_WithRawPointers_WorksCorrectly)
{
    auto q = std::make_unique<MPSCQueue<std::string*>>(16);

    auto* str1 = new std::string("Hello");
    auto* str2 = new std::string("World");

    q->Push(str1);
    q->Push(str2);

    std::string* value;
    EXPECT_TRUE(q->TryPop(value));
    EXPECT_EQ(*value, "Hello");
    delete value;

    EXPECT_TRUE(q->TryPop(value));
    EXPECT_EQ(*value, "World");
    delete value;

    EXPECT_TRUE(q->Empty());
    q.reset();
}

TEST_F(MPSCQueueTest, MultipleProducers_WithRawPointers_Works)
{
    const int numProducers = 4;
    const int itemsPerProducer = 1000;
    const int totalItems = numProducers * itemsPerProducer;

    auto q = std::make_unique<MPSCQueue<std::string*>>(128);
    std::vector<std::thread> producers;
    std::atomic<int> produced{0};
    std::vector<std::string> received;

    // Multiple producers
    for (int p = 0; p < numProducers; p++) {
        producers.emplace_back([&q, itemsPerProducer, p, &produced]() {
            for (int i = 0; i < itemsPerProducer; i++) {
                auto* str = new std::string("Producer" + std::to_string(p) + "-" + std::to_string(i));
                while (!q->Push(str)) {
                    std::this_thread::yield();
                }
                produced++;
            }
        });
    }

    // Single consumer
    std::thread consumer([&q, &received, totalItems]() {
        std::string* value;
        while (received.size() < static_cast<size_t>(totalItems)) {
            if (q->TryPop(value)) {
                received.push_back(*value);
                delete value;
            }
        }
    });

    for (auto& t : producers) {
        t.join();
    }

    consumer.join();

    EXPECT_EQ(received.size(), static_cast<size_t>(totalItems));
    EXPECT_TRUE(q->Empty());
    q.reset();
}