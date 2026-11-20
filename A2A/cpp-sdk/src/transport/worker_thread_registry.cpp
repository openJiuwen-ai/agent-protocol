/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include "worker_thread_registry.h"

namespace A2A::Transport {

void WorkerThreadRegistry::Spawn(std::function<void()> fn)
{
    auto done = std::make_shared<std::atomic<bool>>(false);
    std::thread worker([done, fun = std::move(fn)]() {
        fun();
        done->store(true); // after fn has fully returned; the last action of this thread
    });

    std::lock_guard<std::mutex> lock(mutex_);
    entries_.push_back(Entry{std::move(worker), std::move(done)});
}

void WorkerThreadRegistry::ReclaimFinished()
{
    std::vector<Entry> finished;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Entry> running;
        running.reserve(entries_.size());
        for (auto& entry : entries_) {
            if (entry.done->load()) {
                finished.push_back(std::move(entry));
            } else {
                running.push_back(std::move(entry));
            }
        }
        entries_.swap(running);
    }
    Join(std::move(finished));
}

void WorkerThreadRegistry::JoinAll()
{
    std::vector<Entry> all;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        all = std::move(entries_);
        entries_.clear();
    }
    Join(std::move(all));
}

void WorkerThreadRegistry::Join(std::vector<Entry> entries)
{
    for (auto& entry : entries) {
        if (entry.thread.joinable()) {
            entry.thread.join();
        }
    }
}

std::size_t WorkerThreadRegistry::Size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

} // namespace A2A::Transport
