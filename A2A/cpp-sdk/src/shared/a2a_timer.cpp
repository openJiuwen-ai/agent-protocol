/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <string>
#include <utility>

#include "a2a_log.h"
#include "event/event_system.h"
#include "a2a_timer.h"

namespace {

// Keeps the internal event loop from exiting while no timers are pending,
// so timers added later (from any thread) fire as expected.
constexpr long KEEP_ALIVE_INTERVAL_MS = 60 * 60 * 1000;

} // namespace

A2ATimer::A2ATimer() noexcept
{
}

A2ATimer::~A2ATimer() noexcept
{
    Stop();
}

A2ATimer& A2ATimer::Instance()
{
    static A2ATimer instance;
    return instance;
}

int A2ATimer::Start()
{
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "a2a timer already started");
        return 0;
    }

    if (eventSystem_ == nullptr) {
        eventSystem_ = std::make_unique<A2A::EventSystem>();
    }
    if (!eventSystem_->Init()) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "a2a timer init event system failed");
        running_.store(false);
        return -1;
    }

    if (keepAliveEventId_ == -1) {
        const int id = eventSystem_->AddTimer(
            KEEP_ALIVE_INTERVAL_MS, [](int, short, void*) {}, nullptr, true);
        if (id < 0) {
            A2A_LOG(A2A_LOG_LEVEL::ERROR, "a2a timer create keep-alive event failed");
            running_.store(false);
            return -1;
        }
        keepAliveEventId_ = id;
    }

    eventSystem_->Start(true);
    A2A_LOG(A2A_LOG_LEVEL::INFO, "start a2a timer succeed");
    return 0;
}

void A2ATimer::Stop()
{
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        A2A_LOG(A2A_LOG_LEVEL::WARN, "a2a timer already stopped");
        return;
    }

    if (eventSystem_ != nullptr) {
        eventSystem_->Stop();
    }
    A2A_LOG(A2A_LOG_LEVEL::INFO, "stop a2a timer succeed");
}

int A2ATimer::AddTimer(int ms, TimerCallback cb, void *arg)
{
    if (ms <= 0) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "invalid timer timeout: " + std::to_string(ms));
        return -1;
    }
    if (!this->EnsureStarted()) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "a2a timer not started");
        return -1;
    }

    std::lock_guard<std::mutex> lock(mtx_);
    auto task = std::make_unique<TimerTask>();
    task->id = nextId_++;
    task->callback = std::move(cb);
    task->arg = arg;
    const int id = task->id;
    const uint64_t generation = nextGeneration_++;
    task->generation = generation;
    task->eventId = eventSystem_->AddTimer(
        static_cast<long>(ms),
        [this, id, generation](int, short, void*) {
            this->OnTimerFired(id, generation);
        },
        nullptr, false);
    if (task->eventId < 0) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "a2a timer add task failed, id: " + std::to_string(task->id));
        return -1;
    }
    tasks_.emplace(id, std::move(task));

    A2A_LOG(A2A_LOG_LEVEL::DEBUG, "timer added, id: " + std::to_string(id) + ", timeout: " + std::to_string(ms));
    return id;
}

void A2ATimer::CancelTimer(int id)
{
    if (!running_.load()) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "a2a timer not started");
        return;
    }

    std::unique_ptr<TimerTask> task;
    int eventId = -1;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = tasks_.find(id);
        if (it == tasks_.end()) {
            return;
        }
        eventId = it->second->eventId;
        task = std::move(it->second);
        tasks_.erase(it);
    }

    // RemoveEvent may wait for a currently running callback (libevent defers
    // event_del while the callback executes), so it must run WITHOUT holding
    // mtx_: the callback itself needs mtx_ and would otherwise deadlock.
    // Callbacks carry (id, generation) by value only, so freeing the task here
    // is safe even if a stale callback is still pending in libevent.
    if (eventSystem_ != nullptr && eventId >= 0) {
        eventSystem_->RemoveEvent(eventId);
    }
}

int A2ATimer::RefreshTimer(int id, int ms)
{
    if (!running_.load()) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "a2a timer not started");
        return -1;
    }
    if (ms <= 0) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "invalid timer timeout: " + std::to_string(ms));
        return -1;
    }

    int oldEventId = -1;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = tasks_.find(id);
        if (it == tasks_.end()) {
            A2A_LOG(A2A_LOG_LEVEL::ERROR, "timer not found, id: " + std::to_string(id));
            return -1;
        }
        oldEventId = it->second->eventId;
        const uint64_t generation = nextGeneration_++;
        const int newEventId = eventSystem_->AddTimer(
            static_cast<long>(ms),
            [this, id, generation](int, short, void*) {
                this->OnTimerFired(id, generation);
            },
            nullptr, false);
        if (newEventId < 0) {
            // Keep the current arming: a failed refresh must not lose the timer.
            A2A_LOG(A2A_LOG_LEVEL::ERROR, "a2a timer refresh failed, id: " + std::to_string(id));
            return -1;
        }
        // Publish under the same lock so the replacement callback (which must
        // take mtx_) cannot run before the new generation is visible.
        it->second->generation = generation;
        it->second->eventId = newEventId;
    }

    // RemoveEvent may block on a running callback: call it outside mtx_.
    if (oldEventId >= 0) {
        (void)eventSystem_->RemoveEvent(oldEventId);
    }

    A2A_LOG(A2A_LOG_LEVEL::DEBUG,
        "timer refreshed, id: " + std::to_string(id) +
        ", timeout: " + std::to_string(ms));
    return 0;
}

bool A2ATimer::EnsureStarted()
{
    if (running_.load()) {
        return true;
    }
    return this->Start() == 0;
}

void A2ATimer::OnTimerFired(int id, uint64_t generation)
{
    TimerCallback cb;
    void *userArg = nullptr;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = tasks_.find(id);
        if (it == tasks_.end() || it->second->generation != generation) {
            // Removed, fired, or re-armed: this arming is stale.
            return;
        }
        cb = std::move(it->second->callback);
        userArg = it->second->arg;
        tasks_.erase(it);
    }

    // Run callbacks without holding the lock so other threads can add/cancel timers.
    if (cb) {
        cb(userArg);
    }
}

struct A2ATimerInitializer {
    A2ATimerInitializer() noexcept
    {
    }

    ~A2ATimerInitializer()
    {
        A2ATimer::Instance().Stop();
    }
};

static A2ATimerInitializer g_timerInitializer;
