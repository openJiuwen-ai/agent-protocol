/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <event2/util.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <memory>
#include <thread>
#include <type_traits>
#include <unordered_map>

#include "a2a_log.h"
#include "shared/thread_utils.h"
#include "event_system.h"

namespace A2A {

using EventMask = std::underlying_type_t<EventType>;

constexpr long MS_PER_SECOND = 1000;
constexpr long US_PER_MS = 1000;

// Translate the internal EventType bitmask into libevent flags.
int ToLibeventFlags(EventType events)
{
    const EventMask mask = static_cast<EventMask>(events);
    int flags = 0;

    if ((static_cast<uint32_t>(mask) & static_cast<EventMask>(EventType::READ)) != 0) {
        flags |= EV_READ;
    }
    if ((static_cast<uint32_t>(mask) & static_cast<EventMask>(EventType::WRITE)) != 0) {
        flags |= EV_WRITE;
    }
    if ((static_cast<uint32_t>(mask) & static_cast<EventMask>(EventType::TIMEOUT)) != 0) {
        flags |= EV_TIMEOUT;
    }
    if ((static_cast<uint32_t>(mask) & static_cast<EventMask>(EventType::SIGNAL)) != 0) {
        flags |= EV_SIGNAL;
    }

    // Persist by default for I/O and signal watchers (backward compatible),
    // and also honor explicit PERSIST for any event (notably timers).
    const bool hasIoOrSignal = (static_cast<unsigned>(flags) &
        static_cast<unsigned>(EV_READ | EV_WRITE | EV_SIGNAL)) != 0u;
    const bool hasPersist = (static_cast<uint32_t>(mask) & static_cast<uint32_t>(EventType::PERSIST)) != 0u;

    if (hasIoOrSignal || hasPersist) {
        flags |= EV_PERSIST;
    }
    
    return flags;
}

std::once_flag& ThreadInitOnceFlag()
{
    static std::once_flag flag;
    return flag;
}

void EnsureLibeventThreadSupport()
{
    std::call_once(ThreadInitOnceFlag(), []() { evthread_use_pthreads(); });
}

// Convert a timeout in milliseconds to timeval.
// Returns pointer to tv when timeoutMs > 0, otherwise nullptr.
const timeval* ToTimeval(long timeoutMs, timeval& tv)
{
    if (timeoutMs <= 0) {
        return nullptr;
    }

    tv.tv_sec = timeoutMs / MS_PER_SECOND;
    tv.tv_usec = static_cast<long>((timeoutMs % MS_PER_SECOND) * US_PER_MS);
    return &tv;
}

struct EventSystem::Impl {
    struct EventData {
        int id{0};
        int fd{0};
        EventCallback callback;
        void* userArg{nullptr};
        EventSystem* owner{nullptr};
        std::unique_ptr<event, decltype(&event_free)> handle{nullptr, &event_free};
        short flags{0};
        bool persistent{false};

        static void eventCallbackAdapter(evutil_socket_t eventFd, short events, void* arg)
        {
            auto* data = static_cast<EventSystem::Impl::EventData*>(arg);
            if (data == nullptr) {
                A2A_LOG(A2A_LOG_LEVEL::ERROR, "EventSystem callback: data is null");
                return;
            }

            int savedId = data->id;
            bool savedPersistent = data->persistent;
            EventSystem* eventOwner = data->owner;

            try {
                if (data->callback != nullptr) {
                    data->callback(static_cast<int>(eventFd), events, data->userArg);
                }
            } catch (const std::exception& e) {
                A2A_LOG(A2A_LOG_LEVEL::ERROR, std::string("Exception in event callback: ") + e.what());
            } catch (...) {
                A2A_LOG(A2A_LOG_LEVEL::ERROR, "Unknown exception in event callback");
            }

            if (!savedPersistent && eventOwner != nullptr) {
                A2A_LOG(A2A_LOG_LEVEL::DEBUG,
                    std::string("EventSystem::eventCallbackAdapter: removing non-persistent event, eventId=") +
                    std::to_string(savedId));
                eventOwner->RemoveEvent(savedId);
            }
        }
    };

    explicit Impl(bool threadEnabled) : enableThreadSupport(threadEnabled)
    {
    }

    ~Impl()
    {
        if (eventThread.joinable()) {
            eventThread.join();
        }
    }

    bool enableThreadSupport{false};
    std::atomic<int> nextEventId{1};
    std::unordered_map<int, std::unique_ptr<EventData>> events;
    std::thread eventThread;
};

EventSystem::EventSystem(bool enableThreadSupport, int eventThreadIndex)
    : impl_(std::make_unique<Impl>(enableThreadSupport)), eventThreadIndex_(eventThreadIndex)
{
}

EventSystem::~EventSystem()
{
    Stop();

    std::unordered_map<int, std::unique_ptr<Impl::EventData>> events;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        events.swap(impl_->events);
    }

    for (auto& entry : events) {
        if (entry.second != nullptr && entry.second->handle != nullptr) {
            event_del(entry.second->handle.get());
            entry.second->handle.reset();
        }
    }

    if (eventBase_ != nullptr) {
        event_base_free(eventBase_);
        eventBase_ = nullptr;
        A2A_LOG(A2A_LOG_LEVEL::INFO, "EventSystem destroyed");
    }
}

bool EventSystem::Init()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (eventBase_ != nullptr) {
        return true;
    }

    if (impl_->enableThreadSupport) {
        EnsureLibeventThreadSupport();
    }

    eventBase_ = event_base_new();
    return eventBase_ != nullptr;
}

int EventSystem::ToEventFd(int eventId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = impl_->events.find(eventId);
    if (it == impl_->events.end()) {
        return -1;
    }
    return it->second->fd;
}

int EventSystem::AddEvent(int fd, EventType events, EventCallback callback, void* arg, long timeoutMs)
{
    if (eventBase_ == nullptr) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "EventSystem not initialized");
        return -1;
    }
    const short flags = static_cast<short>(ToLibeventFlags(events));
    if (flags == 0 || callback == nullptr) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "Invalid event flags or callback");
        return -1;
    }

    auto data = std::make_unique<Impl::EventData>();
    data->id = impl_->nextEventId.fetch_add(1);
    data->fd = fd;
    data->callback = std::move(callback);
    data->userArg = arg;
    data->owner = this;
    data->flags = flags;
    data->persistent = (static_cast<unsigned short>(flags) & EV_PERSIST) != 0;

    event* ev = event_new(eventBase_, fd, flags, &Impl::EventData::eventCallbackAdapter, data.get());
    if (ev == nullptr) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, std::string("Failed to create event for fd=") + std::to_string(fd));
        return -1;
    }

    data->handle.reset(ev);
    const int assignedId = data->id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        impl_->events.emplace(assignedId, std::move(data));
    }

    timeval tv{};
    if (event_add(ev, ToTimeval(timeoutMs, tv)) != 0) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "Failed to add event for fd=" + std::to_string(fd));
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = impl_->events.find(assignedId);
        if (it != impl_->events.end()) {
            if (it->second != nullptr && it->second->handle != nullptr) {
                it->second->handle.reset();
            }
            impl_->events.erase(it);
        }
        return -1;
    }

    return assignedId;
}

int EventSystem::AddTimer(long timeoutMs, EventCallback callback, void* arg, bool repeat)
{
    if (timeoutMs <= 0) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "Invalid timer timeout: " +  std::to_string(timeoutMs) + " ms");
        return -1;
    }

    EventType type = EventType::TIMEOUT;
    if (repeat) {
        type = static_cast<EventType>(static_cast<unsigned int>(type) | static_cast<unsigned int>(EventType::PERSIST));
    }
    // For pure timer, fd = -1 per libevent convention.
    return AddEvent(-1, type, std::move(callback), arg, timeoutMs);
}

int EventSystem::CreateNotifyEventId(EventCallback callback, void* arg, bool persist)
{
    if (eventBase_ == nullptr) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "The event system is not initialized.");
        return -1;
    }

    // Create an eventfd with non-blocking and close-on-exec flags.
    int efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (efd < 0) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "Create eventfd failed.");
        return -1;
    }

    // Register the eventfd for read events. Make it persistent by default unless caller says otherwise.
    EventType type = EventType::READ;
    if (persist) {
        type = static_cast<EventType>(static_cast<unsigned int>(type) | static_cast<unsigned int>(EventType::PERSIST));
    }

    int eventId = AddEvent(efd, type, std::move(callback), arg, 0);
    if (eventId < 0) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "Failed to add eventFd to the event system.");
        ::close(efd);
        return -1;
    }

    return eventId;
}

bool EventSystem::NotifyEventId(int eventId, uint64_t increment)
{
    // Use eventfd write to increment the counter atomically.
    ssize_t written = 0;
    const uint64_t val = increment;

    int eventFd = ToEventFd(eventId);
    if (eventFd == -1) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "The eventId not found.");
        return false;
    }
    while (true) {
        written = ::write(eventFd, &val, sizeof(val));
        if (written == static_cast<ssize_t>(sizeof(val))) {
            return true;
        }
        if (written < 0) {
            if (errno == EINTR) {
                continue; // retry
            }
            // EAGAIN/EWOULDBLOCK means the fd would block; treat as failure for notify.
            return false;
        }
        // Partial write, treat as failure.
        return false;
    }
}

bool EventSystem::ReadEventFd(int fd, uint64_t& outValue) const
{
    uint64_t val = 0;
    ssize_t rd = 0;
    while (true) {
        rd = ::read(fd, &val, sizeof(val));
        if (rd == static_cast<ssize_t>(sizeof(val))) {
            outValue = val;
            return true;
        }
        if (rd < 0) {
            if (errno == EINTR) {
                continue; // retry
            }
            return false;
        }
        // Partial read or EOF
        return false;
    }
}

bool EventSystem::CloseNotifyEventId(int eventId)
{
    if (eventId < 0) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "The eventId is invalid.");
        return false;
    }

    int eventFd = ToEventFd(eventId);
    if (eventFd == -1) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "The eventId not found.");
        return false;
    }

    // Remove the libevent watcher and close the fd.
    return RemoveEventInternal(eventId, true);
}

bool EventSystem::RemoveEvent(int eventId)
{
    return RemoveEventInternal(eventId, false);
}

bool EventSystem::RemoveEventInternal(int eventId, bool closeFd)
{
    std::unique_ptr<Impl::EventData> data;
    {
        // Remove from map first so concurrent Notify fails immediately.
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = impl_->events.find(eventId);
        if (it == impl_->events.end()) {
            A2A_LOG(A2A_LOG_LEVEL::ERROR, "The eventId not found.");
            return false;
        }
        data = std::move(it->second);
        impl_->events.erase(it);
    }

    if (data != nullptr && data->handle != nullptr) {
        event_del(data->handle.get());
        data->handle.reset();
    }

    if (closeFd && data != nullptr && data->fd >= 0) {
        ::close(data->fd);
    }

    return true;
}

void EventSystem::Start(bool runInBackground)
{
    if (eventBase_ == nullptr) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "The event system is not initialized.");
        return;
    }

    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return; // already running
    }

    if (runInBackground) {
        if (impl_->eventThread.joinable()) {
            impl_->eventThread.join();
        }

        impl_->eventThread = std::thread([this]() {
            SetCurrentThreadName("A2A-Event-" + std::to_string(eventThreadIndex_));
            try {
                event_base_dispatch(eventBase_);
            } catch (const std::exception& e) {
                A2A_LOG(A2A_LOG_LEVEL::ERROR, std::string("Event loop exception: ") + e.what());
            } catch (...) {
                A2A_LOG(A2A_LOG_LEVEL::ERROR, "Unknown exception in event loop");
            }
            running_.store(false);
        });
    } else {
        event_base_dispatch(eventBase_);
        running_.store(false);
    }
}

void EventSystem::Stop()
{
    if (eventBase_ == nullptr) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "The event system is not initialized.");
        return;
    }

    if (!running_.exchange(false)) {
        // already stopped
        return;
    }

    event_base_loopbreak(eventBase_);

    if (impl_->eventThread.joinable()) {
        impl_->eventThread.join();
    }
}

bool EventSystem::EventBaseValid() const
{
    return eventBase_ != nullptr;
}

} // namespace A2A