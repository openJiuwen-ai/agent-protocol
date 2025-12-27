/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <algorithm>
#include <stdexcept>

#include "event_queue_impl.h"
#include "server/event_queue.h"

namespace a2a::server {

EventQueueImpl::EventQueueImpl(std::size_t max_size) : maxSize_(max_size)
{
    if (maxSize_ <= 0) {
        throw std::invalid_argument("max_size must be > 0");
    }
}

void EventQueueImpl::Enqueue(const Event& ev)
{
    std::unique_lock<std::mutex> lk(m_);
    if (closed_) {
        return; // drop if closed
    }
    notFull_.wait(lk, [&] { return q_.size() < maxSize_ || closed_; });
    if (closed_) {
        return;
    }
    q_.push(ev);
    lk.unlock();
    notEmpty_.notify_one();
    // fan-out to children
    std::vector<std::shared_ptr<EventQueue>> children;
    {
        std::lock_guard<std::mutex> g(cm_);
        children = children_;
    }
    for (auto& c : children)
        c->Enqueue(ev);
}

Event EventQueueImpl::Dequeue(bool no_wait)
{
    std::unique_lock<std::mutex> lk(m_);
    if (no_wait) {
        if (q_.empty())
            throw std::runtime_error("queue empty");
    } else {
        notEmpty_.wait(lk, [&] { return !q_.empty() || closed_; });
        if (q_.empty() && closed_)
            throw std::runtime_error("queue closed");
    }
    auto ev = q_.front();
    q_.pop();
    lk.unlock();
    notFull_.notify_one();
    return ev;
}

void EventQueueImpl::TaskDone()
{
    // no-op: provided for API symmetry; could track in-flight
}

std::shared_ptr<EventQueue> EventQueueImpl::Tap()
{
    auto child = std::make_shared<EventQueue>(maxSize_);
    std::lock_guard<std::mutex> g(cm_);
    children_.push_back(child);
    return child;
}

void EventQueueImpl::Close(bool immediate)
{
    {
        std::lock_guard<std::mutex> lk(m_);
        closed_ = true;
        if (immediate) {
            std::queue<Event> empty;
            std::swap(q_, empty);
        }
    }
    notEmpty_.notify_all();
    notFull_.notify_all();
    // close children
    std::vector<std::shared_ptr<EventQueue>> children;
    {
        std::lock_guard<std::mutex> g(cm_);
        children = children_;
    }
    for (auto& c : children)
        c->Close(immediate);
}

void EventQueueImpl::Clear(bool clear_children)
{
    {
        std::lock_guard<std::mutex> lk(m_);
        std::queue<Event> empty;
        std::swap(q_, empty);
    }
    if (clear_children) {
        std::lock_guard<std::mutex> g(cm_);
        for (auto& c : children_)
            c->Clear(true);
    }
}

} // namespace a2a::server