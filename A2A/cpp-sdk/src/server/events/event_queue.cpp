/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <algorithm>
#include <stdexcept>

#include "event_queue.h"

namespace A2A::Server {

EventQueue::EventQueue(std::size_t maxSize) : maxSize_(maxSize)
{
    if (maxSize_ <= 0) {
        throw std::invalid_argument("maxSize must be > 0");
    }
}

void EventQueue::Enqueue(const Event& ev)
{
    std::unique_lock<std::mutex> lk(mutex_);
    if (closed_) {
        return; // drop if closed
    }
    notFull_.wait(lk, [&] { return queue_.size() < maxSize_ || closed_; });
    if (closed_) {
        return;
    }
    queue_.push(ev);
    lk.unlock();
    notEmpty_.notify_one();
    // fan-out to children
    std::vector<std::shared_ptr<EventQueue>> children;
    {
        std::lock_guard<std::mutex> g(cmutex_);
        children = children_;
    }
    for (auto& c : children)
        c->Enqueue(ev);
}

Event EventQueue::Dequeue(bool no_wait)
{
    std::unique_lock<std::mutex> lk(mutex_);
    if (no_wait) {
        if (queue_.empty()) {
            throw std::runtime_error("queue empty");
        }
    } else {
        notEmpty_.wait(lk, [&] { return !queue_.empty() || closed_; });
        if (queue_.empty() && closed_) {
            throw std::runtime_error("queue closed");
        }
    }
    auto ev = queue_.front();
    queue_.pop();
    lk.unlock();
    notFull_.notify_one();
    return ev;
}

void EventQueue::TaskDone()
{
    // no-op: provided for API symmetry; could track in-flight
}

std::shared_ptr<EventQueue> EventQueue::Tap()
{
    auto child = std::make_shared<EventQueue>(maxSize_);
    std::lock_guard<std::mutex> g(cmutex_);
    children_.push_back(child);
    return child;
}

void EventQueue::Close(bool immediate)
{
    {
        std::lock_guard<std::mutex> lk(mutex_);
        closed_ = true;
        if (immediate) {
            std::queue<Event> empty;
            std::swap(queue_, empty);
        }
    }
    notEmpty_.notify_all();
    notFull_.notify_all();
    // close children
    std::vector<std::shared_ptr<EventQueue>> children;
    {
        std::lock_guard<std::mutex> g(cmutex_);
        children = children_;
    }
    for (auto& c : children)
        c->Close(immediate);
}

void EventQueue::Clear(bool clearChildren)
{
    {
        std::lock_guard<std::mutex> lk(mutex_);
        std::queue<Event> empty;
        std::swap(queue_, empty);
    }
    if (clearChildren) {
        std::lock_guard<std::mutex> g(cmutex_);
        for (auto& c : children_)
            c->Clear(true);
    }
}

bool EventQueue::IsEmpty() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return queue_.empty();
}

} // namespace A2A::Server