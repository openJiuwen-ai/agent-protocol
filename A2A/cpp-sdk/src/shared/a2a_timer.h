/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_TIMER_H
#define A2A_TIMER_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "event/event_system.h"

using TimerCallback = std::function<void(void *)>;

struct TimerTask {
    int id{0};
    // Unique token of the current arming; callbacks carry it by value so stale
    // callbacks never dereference task objects.
    uint64_t generation{0};
    int eventId{-1};
    TimerCallback callback;
    void *arg{nullptr};
};

class A2ATimer {
public:
    static A2ATimer& Instance();

    int Start();
    void Stop();

    int AddTimer(int ms, TimerCallback cb, void *arg);
    void CancelTimer(int id);
    int RefreshTimer(int id, int ms);

private:
    A2ATimer() noexcept;
    ~A2ATimer() noexcept;

    A2ATimer(const A2ATimer&) = delete;
    A2ATimer& operator=(const A2ATimer&) = delete;

    bool EnsureStarted();
    void OnTimerFired(int id, uint64_t generation);

    int nextId_ = 1;
    // Monotonic arming tokens (guarded by mtx_).
    uint64_t nextGeneration_ = 1;
    int keepAliveEventId_ = -1;
    std::unordered_map<int, std::unique_ptr<TimerTask>> tasks_;

    std::mutex mtx_;
    std::atomic<bool> running_{false};
    std::unique_ptr<A2A::EventSystem> eventSystem_;
};

#endif // A2A_TIMER_H
