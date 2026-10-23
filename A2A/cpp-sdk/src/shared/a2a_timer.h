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
    // Bumped on every refresh: identifies this particular arming. Callbacks
    // carry (id, generation) by value, so a stale callback can detect that the
    // timer was re-armed or removed without dereferencing task objects.
    uint64_t generation{1};
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
    int keepAliveEventId_ = -1;
    std::unordered_map<int, std::unique_ptr<TimerTask>> tasks_;

    std::mutex mtx_;
    std::atomic<bool> running_{false};
    std::unique_ptr<A2A::EventSystem> eventSystem_;
};

#endif // A2A_TIMER_H
