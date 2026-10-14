/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

#include "worker_thread_registry.h"

namespace A2A::Transport::Test {

using namespace std::chrono_literals;

// Regression for the original leak: the old cleanup removed entries with !joinable(), but a
// thread that has run to completion without being joined is still joinable, so nothing was
// ever removed and every request left a joinable handle behind. Finished entries must be
// joined and removed once reclaimed.
TEST(WorkerThreadRegistryTest, ReclaimRemovesFinishedWorkers)
{
    WorkerThreadRegistry registry;
    constexpr int workerCount = 8;
    std::atomic<int> finished{0};
    for (int i = 0; i < workerCount; ++i) {
        registry.Spawn([&finished]() { ++finished; });
    }

    // Reclaim until every entry is gone. The deadline only guards the test against a
    // regression; workers finish within microseconds, so it never trips in practice.
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (registry.Size() > 0 && std::chrono::steady_clock::now() < deadline) {
        registry.ReclaimFinished();
        std::this_thread::sleep_for(5ms);
    }

    EXPECT_EQ(registry.Size(), 0);
    EXPECT_EQ(finished.load(), workerCount);
}

TEST(WorkerThreadRegistryTest, ReclaimKeepsRunningWorkers)
{
    WorkerThreadRegistry registry;
    std::promise<void> release;
    auto released = release.get_future().share();
    registry.Spawn([&released]() { released.get(); });

    registry.ReclaimFinished();
    // The worker is still running (blocked on the future), so its entry must stay tracked -
    // reclaiming it here would drop the only joinable handle and leak the thread.
    EXPECT_EQ(registry.Size(), 1);

    release.set_value();
    registry.JoinAll();
    EXPECT_EQ(registry.Size(), 0);
}

TEST(WorkerThreadRegistryTest, JoinAllBlocksUntilWorkersFinish)
{
    WorkerThreadRegistry registry;
    std::atomic<int> finished{0};
    std::promise<void> release;
    auto released = release.get_future().share();
    for (int i = 0; i < 4; ++i) {
        registry.Spawn([&finished, released]() {
            released.get();
            ++finished;
        });
    }

    // Released only while JoinAll is already blocking, so a JoinAll that skips the wait (or
    // returns before every worker exits) observes finished < 4.
    std::thread releaser([&release]() {
        std::this_thread::sleep_for(50ms);
        release.set_value();
    });

    registry.JoinAll();
    EXPECT_EQ(finished.load(), 4);
    EXPECT_EQ(registry.Size(), 0);

    releaser.join();
}

TEST(WorkerThreadRegistryTest, DestructorJoinsOutstandingWorkers)
{
    std::atomic<int> finished{0};
    std::promise<void> release;
    auto released = release.get_future().share();
    {
        WorkerThreadRegistry registry;
        for (int i = 0; i < 4; ++i) {
            registry.Spawn([&finished, released]() {
                released.get();
                ++finished;
            });
        }
        // Workers may finish now; the destructor must join them (not detach, not leak) before
        // member destruction begins.
        release.set_value();
    }
    EXPECT_EQ(finished.load(), 4);
}

TEST(WorkerThreadRegistryTest, SpawnRunsTheWorkerBody)
{
    WorkerThreadRegistry registry;
    std::promise<int> result;
    registry.Spawn([&result]() { result.set_value(42); });

    EXPECT_EQ(result.get_future().get(), 42);

    registry.JoinAll();
    EXPECT_EQ(registry.Size(), 0);
}

// Spawn and ReclaimFinished run on different threads in the transport (IO threads vs each
// other); the registry must stay consistent under that interleaving.
TEST(WorkerThreadRegistryTest, ConcurrentSpawnAndReclaim)
{
    WorkerThreadRegistry registry;
    std::atomic<bool> stop{false};

    std::thread spawner([&registry, &stop]() {
        while (!stop.load()) {
            registry.Spawn([]() {});
            std::this_thread::sleep_for(1ms);
        }
    });
    std::thread reclaimer([&registry, &stop]() {
        while (!stop.load()) {
            registry.ReclaimFinished();
            std::this_thread::sleep_for(1ms);
        }
    });

    std::this_thread::sleep_for(200ms);
    stop = true;
    spawner.join();
    reclaimer.join();

    registry.JoinAll();
    EXPECT_EQ(registry.Size(), 0);
}

} // namespace A2A::Transport::Test
