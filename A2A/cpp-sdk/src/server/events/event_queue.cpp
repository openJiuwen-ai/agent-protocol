/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "event_queue_impl.h"
#include "server/event_queue.h"

namespace a2a::server {

EventQueue::EventQueue(std::size_t maxSize) : impl_(std::make_unique<EventQueueImpl>(maxSize))
{
}

void EventQueue::Enqueue(const Event& ev)
{
    impl_->Enqueue(ev);
}

Event EventQueue::Dequeue(bool noWait)
{
    return impl_->Dequeue(noWait);
}

void EventQueue::TaskDone()
{
    impl_->TaskDone();
}

std::shared_ptr<EventQueue> EventQueue::Tap()
{
    return impl_->Tap();
}

void EventQueue::Close(bool immediate)
{
    impl_->Close(immediate);
}

bool EventQueue::IsClosed() const
{
    return impl_->IsClosed();
}

void EventQueue::Clear(bool clearChildren)
{
    impl_->Clear(clearChildren);
}

} // namespace a2a::server