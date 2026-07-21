/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_TIMER_H
#define A2A_TIMER_H

#include <vector>
#include <chrono>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <sys/epoll.h>

using TimerCallback = std::function<void(void *)>;

struct TimerTask {
    int id;
    std::chrono::steady_clock::time_point expire;
    TimerCallback callback;
    void *arg;
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

    int Init();
    void RunLoop();
    int GetNextTimeout();
    void HandleExpired();

    int nextId_ = 1;
    int epfd_ = -1;
    int wakeFd_ = -1;
    std::unordered_map<int, TimerTask> tasks_;

    std::mutex mtx_;
    std::thread loopThread_;
    std::atomic<bool> running_;
};

#endif // A2A_TIMER_H