/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_WORKER_THREAD_REGISTRY
#define A2A_WORKER_THREAD_REGISTRY

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace A2A::Transport {

/**
 * @brief Owns the worker threads that execute request handlers.
 *
 * A std::thread stays joinable() after it has run to completion - only join(), detach() or a
 * move clears the flag - so "remove whatever is not joinable" reclaims nothing, and every
 * request would leave a joinable handle (and its un-reaped stack and TCB) behind. Each entry
 * here therefore carries a done flag that the registry's own wrapper sets after the worker
 * body has returned; ReclaimFinished() joins and removes exactly those entries.
 *
 * Threads are joined, never detached: worker bodies capture their owner's state and must not
 * outlive it. JoinAll() (also run by the destructor) blocks until every tracked worker has
 * finished, so worker bodies must terminate on their own.
 */
class WorkerThreadRegistry {
public:
    WorkerThreadRegistry() = default;

    ~WorkerThreadRegistry()
    {
        JoinAll();
    }

    WorkerThreadRegistry(const WorkerThreadRegistry&) = delete;
    WorkerThreadRegistry& operator=(const WorkerThreadRegistry&) = delete;

    /**
     * @brief Runs fn on a new tracked worker thread.
     *
     * The done flag is set by the registry's wrapper once fn has returned, so fn itself does
     * not need to cooperate. fn must not let exceptions escape: an exception leaving a thread
     * calls std::terminate.
     */
    void Spawn(std::function<void()> fn);

    /**
     * @brief Joins and removes every entry whose worker has finished.
     *
     * Cheap by construction: the worker has already returned, join() only reaps it. Entries
     * whose workers are still running stay tracked.
     */
    void ReclaimFinished();

    /** @brief Joins and removes every entry, blocking until still-running workers finish. */
    void JoinAll();

    /** @brief Number of tracked entries; finished-but-not-yet-reclaimed workers count too. */
    std::size_t Size() const;

private:
    struct Entry {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done; // set by the wrapper after fn returned
    };

    // Joins entries with the lock NOT held: join() blocks until the worker exits, and a worker
    // never touches the registry (only its own flag), so no deadlock is possible either way;
    // keeping the wait out of the critical section just lets other threads Spawn meanwhile.
    static void Join(std::vector<Entry> entries);

    mutable std::mutex mutex_;
    std::vector<Entry> entries_;
};

} // namespace A2A::Transport

#endif
