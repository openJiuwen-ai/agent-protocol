/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "event_system.h"

#include <event2/util.h>
#include <fcntl.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <memory>
#include <thread>
#include <type_traits>
#include <unordered_map>

#include "mcp_log.h"

#if defined(__linux__)
#include <sys/eventfd.h>
#endif

namespace Mcp {

using EventMask = std::underlying_type_t<EventType>;

constexpr long MS_PER_SECOND = 1000;
constexpr long US_PER_MS = 1000;

namespace {

int SetFdFlags(int fd)
{
    int fileStatusFlags = fcntl(fd, F_GETFL, 0);
    if (fileStatusFlags < 0) {
        return -1;
    }
    if (fcntl(fd, F_SETFL, fileStatusFlags | O_NONBLOCK) < 0) {
        return -1;
    }

    int descriptorFlags = fcntl(fd, F_GETFD, 0);
    if (descriptorFlags < 0) {
        return -1;
    }
    if (fcntl(fd, F_SETFD, descriptorFlags | FD_CLOEXEC) < 0) {
        return -1;
    }

    return 0;
}

} // namespace

// Translate the internal EventType bitmask into libevent flags.
short ToLibeventFlags(EventType events)
{
    const EventMask mask = static_cast<EventMask>(events);
    short flags = 0;

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
        int notifyWriteFd{-1};
        std::atomic<uint64_t> notifyCounter{0};

        static void eventCallbackAdapter(evutil_socket_t eventFd, short events, void* arg)
        {
            auto* data = static_cast<EventSystem::Impl::EventData*>(arg);
            if (data == nullptr) {
                MCP_LOG(MCP_LOG_LEVEL_ERROR, "EventSystem callback: data is null");
                return;
            }

            int savedId = data->id;
            bool savedPersistent = data->persistent;
            EventSystem* eventOwner = data->owner;

            if (data->callback != nullptr) {
                data->callback(static_cast<int>(eventFd), events, data->userArg);
            }

            if (!savedPersistent && eventOwner != nullptr) {
                MCP_LOG(MCP_LOG_LEVEL_DEBUG,
                        "EventSystem::eventCallbackAdapter: removing non-persistent event, eventId=%d", savedId);
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

EventSystem::EventSystem(bool enableThreadSupport) : impl_(std::make_unique<Impl>(enableThreadSupport))
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
        if (entry.second != nullptr) {
            if (entry.second->notifyWriteFd != -1 && entry.second->notifyWriteFd != entry.second->fd) {
                ::close(entry.second->notifyWriteFd);
                entry.second->notifyWriteFd = -1;
            }
            if (entry.second->fd != -1) {
                ::close(entry.second->fd);
                entry.second->fd = -1;
            }
        }
    }

    if (event_base_ != nullptr) {
        event_base_free(event_base_);
        event_base_ = nullptr;
        MCP_LOG(MCP_LOG_LEVEL_DEBUG, "EventSystem destroyed");
    }
}

bool EventSystem::Init()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (event_base_ != nullptr) {
        return true;
    }

    if (impl_->enableThreadSupport) {
        EnsureLibeventThreadSupport();
    }

    event_base_ = event_base_new();
    return event_base_ != nullptr;
}

int EventSystem::ToEventFd(int eventId)
{
    auto it = impl_->events.find(eventId);
    if (it == impl_->events.end()) {
        return -1;
    }
    return it->second->fd;
}

int EventSystem::AddEvent(int fd, EventType events, EventCallback callback, void* arg, long timeoutMs)
{
    if (event_base_ == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "EventSystem not initialized");
        return -1;
    }
    const short flags = ToLibeventFlags(events);
    if (flags == 0 || callback == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Invalid event flags or callback");
        return -1;
    }

    auto data = std::make_unique<Impl::EventData>();
    data->id = impl_->nextEventId.fetch_add(1);
    data->fd = fd;
    data->callback = std::move(callback);
    data->userArg = arg;
    data->owner = this;
    data->flags = flags;
    data->persistent = (flags & EV_PERSIST) != 0;

    event* ev = event_new(event_base_, fd, flags, &Impl::EventData::eventCallbackAdapter, data.get());
    if (ev == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Failed to create event for fd=%d", fd);
        return -1;
    }

    data->handle.reset(ev);

    timeval tv{};
    if (event_add(ev, ToTimeval(timeoutMs, tv)) != 0) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Failed to add event for fd=%d", fd);
        data->handle.reset();
        return -1;
    }

    const int assignedId = data->id;
    impl_->events.emplace(assignedId, std::move(data));
    return assignedId;
}

int EventSystem::AddTimer(long timeoutMs, EventCallback callback, void* arg, bool repeat)
{
    if (timeoutMs <= 0) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Invalid timer timeout: %ldms", timeoutMs);
        return -1;
    }

    EventType type = EventType::TIMEOUT;
    if (repeat) {
        type = static_cast<EventType>(static_cast<int>(type) | static_cast<int>(EventType::PERSIST));
    }
    // For pure timer, fd = -1 per libevent convention.
    return AddEvent(-1, type, std::move(callback), arg, timeoutMs);
}

int EventSystem::CreateNotifyEventId(EventCallback callback, void* arg, bool persist)
{
    if (event_base_ == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "The event system is not initialized.");
        return -1;
    }

    int readFd = -1;
    int writeFd = -1;

#if defined(__linux__)
    readFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    writeFd = readFd;
    if (readFd < 0) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Create eventfd failed.");
        return -1;
    }
#else
    int pipeFds[2] = {-1, -1};
    if (pipe(pipeFds) != 0) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Create notify pipe failed.");
        return -1;
    }

    readFd = pipeFds[0];
    writeFd = pipeFds[1];
    if (SetFdFlags(readFd) != 0 || SetFdFlags(writeFd) != 0) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Configure notify pipe failed.");
        ::close(readFd);
        ::close(writeFd);
        return -1;
    }
#endif

    // Register the eventfd for read events. Make it persistent by default unless caller says otherwise.
    EventType type = EventType::READ;
    if (persist) {
        type = static_cast<EventType>(static_cast<int>(type) | static_cast<int>(EventType::PERSIST));
    }

    int eventId = AddEvent(readFd, type, std::move(callback), arg, 0);
    if (eventId < 0) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Failed to add eventFd to the event system.");
        ::close(readFd);
        if (writeFd != readFd) {
            ::close(writeFd);
        }
        return -1;
    }

    auto it = impl_->events.find(eventId);
    if (it != impl_->events.end() && it->second != nullptr) {
        it->second->notifyWriteFd = writeFd;
    }

    return eventId;
}

bool EventSystem::NotifyEventId(int eventId, uint64_t increment)
{
    auto it = impl_->events.find(eventId);
    if (it == impl_->events.end() || it->second == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "The eventId not found.");
        return false;
    }

    ssize_t written = 0;

    while (true) {
#if defined(__linux__)
        const uint64_t val = increment;
        written = ::write(it->second->fd, &val, sizeof(val));
        if (written == static_cast<ssize_t>(sizeof(val))) {
            return true;
        }
#else
        const uint64_t previous = it->second->notifyCounter.fetch_add(increment, std::memory_order_acq_rel);
        if (previous > 0) {
            return true;
        }

        const uint8_t wakeByte = 1;
        written = ::write(it->second->notifyWriteFd, &wakeByte, sizeof(wakeByte));
        if (written == static_cast<ssize_t>(sizeof(wakeByte))) {
            return true;
        }
#endif
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

bool EventSystem::ReadEventFd(int fd, uint64_t& outValue)
{
#if defined(__linux__)
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
#else
    Impl::EventData* notifyEvent = nullptr;
    for (auto& entry : impl_->events) {
        if (entry.second != nullptr && entry.second->fd == fd && entry.second->notifyWriteFd != -1) {
            notifyEvent = entry.second.get();
            break;
        }
    }

    if (notifyEvent == nullptr) {
        return false;
    }

    uint8_t buffer[64];
    while (true) {
        const ssize_t rd = ::read(fd, buffer, sizeof(buffer));
        if (rd > 0) {
            continue;
        }
        if (rd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            return false;
        }
        break;
    }

    const uint64_t count = notifyEvent->notifyCounter.exchange(0, std::memory_order_acq_rel);
    if (count == 0) {
        errno = EAGAIN;
        return false;
    }

    outValue = count;
    return true;
#endif
}

bool EventSystem::CloseNotifyEventId(int eventId)
{
    if (eventId < 0) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "The eventId is invalid.");
        return false;
    }

    int eventFd = ToEventFd(eventId);
    if (eventFd == -1) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "The eventId not found.");
        return false;
    }

    int writeFd = -1;
    auto it = impl_->events.find(eventId);
    if (it != impl_->events.end() && it->second != nullptr) {
        writeFd = it->second->notifyWriteFd;
    }

    // Remove the libevent watcher and close the fd.
    RemoveEvent(eventId);
    ::close(eventFd);
    if (writeFd != -1 && writeFd != eventFd) {
        ::close(writeFd);
    }
    return true;
}

bool EventSystem::RemoveEvent(int eventId)
{
    std::unique_ptr<Impl::EventData> data;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = impl_->events.find(eventId);
        if (it == impl_->events.end()) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "The eventId not found.");
            return false;
        }
        data = std::move(it->second);
        impl_->events.erase(it);
    }

    if (data != nullptr && data->handle != nullptr) {
        event_del(data->handle.get());
        data->handle.reset();
    }

    return true;
}

void EventSystem::Start(bool run_in_background)
{
    if (event_base_ == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "The event system is not initialized.");
        return;
    }

    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return; // already running
    }

    if (run_in_background) {
        if (impl_->eventThread.joinable()) {
            impl_->eventThread.join();
        }

        impl_->eventThread = std::thread([this]() {
            event_base_dispatch(event_base_);
            running_.store(false);
        });
    } else {
        event_base_dispatch(event_base_);
        running_.store(false);
    }
}

void EventSystem::Stop()
{
    if (event_base_ == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "The event system is not initialized.");
        return;
    }

    if (!running_.exchange(false)) {
        // already stopped
        return;
    }

    event_base_loopbreak(event_base_);

    if (impl_->eventThread.joinable()) {
        impl_->eventThread.join();
    }
}

struct event_base* EventSystem::GetEventBase() const
{
    return event_base_;
}

} // namespace Mcp
