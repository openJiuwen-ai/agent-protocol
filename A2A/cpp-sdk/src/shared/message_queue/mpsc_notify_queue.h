/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_MPSC_NOTIFY_QUEUE_INCLUDE_H_
#define A2A_MPSC_NOTIFY_QUEUE_INCLUDE_H_

#include <atomic>
#include <functional>
#include <vector>
#include "event/event_system.h"
#include "lock_free_queue.h"

namespace A2A {

template <typename MessageType>
class MPSCNotifyQueue {
public:
    MPSCNotifyQueue(size_t queueCapacity, size_t maxBatchSize) : maxBatchSize_(maxBatchSize)
    {
        if (maxBatchSize_ == 0) {
            maxBatchSize_ = 1;
        }
        queue_ = std::make_unique<MPSCQueue<MessageType>>(queueCapacity);
    }

    ~MPSCNotifyQueue()
    {
        Cleanup();
    }

    MPSCNotifyQueue(const MPSCNotifyQueue&) = delete;
    MPSCNotifyQueue& operator=(const MPSCNotifyQueue&) = delete;
    MPSCNotifyQueue(MPSCNotifyQueue&&) = delete;
    MPSCNotifyQueue& operator=(MPSCNotifyQueue&&) = delete;

    bool Initialize(EventSystem* eventSystem, std::function<void(const MessageType&)> messageHandler)
    {
        if (initialized_.load(std::memory_order_acquire)) {
            return true;
        }
        if (eventSystem == nullptr) {
            return false;
        }
        if (!eventSystem->EventBaseValid()) {
            return false;
        }
        messageHandler_ = std::move(messageHandler);
        eventSystem_ = eventSystem;
        eventId_ = eventSystem_->CreateNotifyEventId(
            [this](int fd, short events, void* arg) { this->HandleNotify(fd, events, arg); }, this, true);
        if (eventId_ == -1) {
            messageHandler_ = nullptr;
            eventSystem_ = nullptr;
            return false;
        }
        initialized_.store(true, std::memory_order_release);
        return true;
    }

    bool Send(const MessageType& msg)
    {
        if (!initialized_.load(std::memory_order_acquire)) {
            return false;
        }
        if (!queue_->Push(msg)) {
            return false;
        }
        return eventSystem_->NotifyEventId(eventId_);
    }

    size_t GetQueueSize() const
    {
        return queue_->Size();
    }

    size_t GetQueueCapacity() const
    {
        return queue_->Capacity();
    }

    size_t GetMaxBatchSize() const
    {
        return maxBatchSize_;
    }

    bool IsInitialized() const
    {
        return initialized_.load(std::memory_order_acquire);
    }

    bool Empty() const
    {
        return queue_->Empty();
    }

    bool TryPop(MessageType& result)
    {
        return queue_->TryPop(result);
    }

    void Cleanup()
    {
        if (!initialized_.load(std::memory_order_acquire)) {
            return;
        }
        if (eventSystem_ != nullptr && eventId_ != -1) {
            eventSystem_->CloseNotifyEventId(eventId_);
            eventId_ = -1;
        }
        messageHandler_ = nullptr;
        eventSystem_ = nullptr;
        initialized_.store(false, std::memory_order_release);
    }

private:
    void HandleNotify(int fd, short /* events */, void* /* arg */)
    {
        uint64_t eventFdValue;
        while (eventSystem_->ReadEventFd(fd, eventFdValue)) {
        }
        std::vector<MessageType> localMessages;
        localMessages.reserve(maxBatchSize_);
        MessageType msg;
        while (queue_->TryPop(msg) && localMessages.size() < maxBatchSize_) {
            localMessages.push_back(std::move(msg));
        }
        for (const auto& localMsg : localMessages) {
            if (messageHandler_) {
                messageHandler_(localMsg);
            }
        }
        if (!queue_->Empty()) {
            eventSystem_->NotifyEventId(eventId_);
        }
    }

    int eventId_{-1};
    std::unique_ptr<MPSCQueue<MessageType>> queue_;
    std::function<void(const MessageType&)> messageHandler_{nullptr};
    size_t maxBatchSize_;
    EventSystem* eventSystem_{nullptr};
    std::atomic<bool> initialized_{false};
};

} // namespace A2A

#endif // A2A_MPSC_NOTIFY_QUEUE_INCLUDE_H_