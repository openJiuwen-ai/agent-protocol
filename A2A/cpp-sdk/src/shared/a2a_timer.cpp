/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <unistd.h>
#include <iostream>
#include <sys/eventfd.h>

#include "a2a_log.h"
#include "a2a_timer.h"

constexpr int MAX_EPOLL_CNT = 16;

A2ATimer::A2ATimer() noexcept
    : running_(false)
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

int A2ATimer::Init()
{
    epfd_ = epoll_create1(0);
    if (epfd_ == -1) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "a2a timer epoll_create1 failed");
        return -1;
    }

    wakeFd_ = eventfd(0, EFD_NONBLOCK);
    if (wakeFd_ == -1) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "a2a timer eventfd failed");
        close(epfd_);
        epfd_ = -1;
        return -1;
    }

    epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = wakeFd_;
    epoll_ctl(epfd_, EPOLL_CTL_ADD, wakeFd_, &ev);
    if (epfd_ == -1) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "a2a timer epoll_ctl failed");
        close(epfd_);
        epfd_ = -1;
        close(wakeFd_);
        wakeFd_ = -1;
        return -1;
    }

    return 0;
}

int A2ATimer::Start()
{
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "a2a timer already started");
        return 0;
    }

    if (this->Init() != 0) {
        running_.store(false);
        return -1;
    }

    loopThread_ = std::thread(&A2ATimer::RunLoop, this);
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

    {
        std::lock_guard<std::mutex> lock(mtx_);
        uint64_t one = 1;
        if (write(wakeFd_, &one, sizeof(one)) < 0) {
            A2A_LOG(A2A_LOG_LEVEL::WARN, "write failed");
        }
    }

    if (loopThread_.joinable()) {
        loopThread_.join();
    }

    if (epfd_ > 0) {
        close(epfd_);
        epfd_ = -1;
    }
    if (wakeFd_ > 0) {
        close(wakeFd_);
        wakeFd_ = -1;
    }
    A2A_LOG(A2A_LOG_LEVEL::INFO, "stop a2a timer succeed");
}

int A2ATimer::AddTimer(int ms, TimerCallback cb, void *arg)
{
    if (!running_.load() && this->Start() != 0) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "a2a timer not started");
        return -1;
    }

    std::lock_guard<std::mutex> lock(mtx_);
    TimerTask t;
    t.id = nextId_++;
    t.expire = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    t.callback = cb;
    t.arg = arg;
    tasks_[t.id] = t;

    // 写一个字节唤醒epoll
    uint64_t one = 1;
    if (write(wakeFd_, &one, sizeof(one)) < 0) {
        A2A_LOG(A2A_LOG_LEVEL::WARN, "write failed");
    }
    return t.id;
}

void A2ATimer::CancelTimer(int id)
{
    if (!running_.load()) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "a2a timer not started");
        return;
    }

    std::lock_guard<std::mutex> lock(mtx_);
    tasks_.erase(id);
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
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = tasks_.find(id);
    if (it == tasks_.end()) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "timer not found, id: " + std::to_string(id));
        return -1;
    }
    it->second.expire = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    A2A_LOG(A2A_LOG_LEVEL::DEBUG,
        "timer refreshed, id: " + std::to_string(id) +
        ", timeout: " + std::to_string(ms));
    uint64_t one = 1;
    if (write(wakeFd_, &one, sizeof(one)) < 0) {
        A2A_LOG(A2A_LOG_LEVEL::WARN, "write failed");
        return -1;
    }
    return 0;
}

int A2ATimer::GetNextTimeout()
{
    std::lock_guard<std::mutex> lock(mtx_);
    if (tasks_.empty()) {
        return -1;
    }

    auto now = std::chrono::steady_clock::now();
    auto nearest = tasks_.begin()->second.expire;
    for (const auto& item : tasks_) {
        if (item.second.expire < nearest) {
            nearest = item.second.expire;
        }
    }
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(nearest - now).count();
    return diff > 0 ? static_cast<int>(diff) : 0;
}

void A2ATimer::HandleExpired()
{
    std::vector<TimerTask> expired;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto now = std::chrono::steady_clock::now();

        for (auto it = tasks_.begin(); it != tasks_.end();) {
            if (it->second.expire < now + std::chrono::milliseconds(1)) {
                expired.emplace_back(it->second);
                it = tasks_.erase(it);
            } else {
                ++it;
            }
        }
    }

    for (auto& e : expired) {
        if (e.callback) {
            e.callback(e.arg);
        }
    }
}

void A2ATimer::RunLoop()
{
    while (running_) {
        int timeout = GetNextTimeout();
        epoll_event events[MAX_EPOLL_CNT];
        int nfds = epoll_wait(epfd_, events, MAX_EPOLL_CNT, timeout);
        if (nfds == 0) {
            HandleExpired();
            continue;
        } else if (nfds < 0) {
            continue;
        }

        for (int i = 0; i < nfds; i++) {
            A2A_LOG(A2A_LOG_LEVEL::INFO, "Got I/O event on fd " + std::to_string(events[i].data.fd));
            if (events[i].data.fd != wakeFd_) {
                continue;
            }
            uint64_t val;
            if (read(wakeFd_, &val, sizeof(val)) < 0) {
                A2A_LOG(A2A_LOG_LEVEL::WARN, "read failed");
            }
            HandleExpired();
        }
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