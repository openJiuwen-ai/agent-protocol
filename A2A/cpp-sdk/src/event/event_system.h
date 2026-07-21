/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_EVENT_SYSTEM_INCLUDE_H_
#define A2A_EVENT_SYSTEM_INCLUDE_H_

#include <event2/event.h>
#include <event2/thread.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

namespace A2A {
/**
 * @brief Event types supported by the EventSystem.
 */
enum class EventType {
    READ = EV_READ,
    WRITE = EV_WRITE,
    TIMEOUT = EV_TIMEOUT,
    SIGNAL = EV_SIGNAL,
    PERSIST = EV_PERSIST,
};

// Enable bitwise ops for EventType (strong enum) so callers can combine flags.
inline constexpr EventType operator|(EventType lhs, EventType rhs) noexcept
{
    return static_cast<EventType>(static_cast<unsigned int>(lhs) | static_cast<unsigned int>(rhs));
}

inline constexpr EventType operator&(EventType lhs, EventType rhs) noexcept
{
    return static_cast<EventType>(static_cast<unsigned int>(lhs) & static_cast<unsigned int>(rhs));
}

inline constexpr EventType operator~(EventType v) noexcept
{
    return static_cast<EventType>(~static_cast<unsigned int>(v));
}

inline EventType& operator|=(EventType& lhs, EventType rhs) noexcept
{
    lhs = lhs | rhs;
    return lhs;
}

inline EventType& operator&=(EventType& lhs, EventType rhs) noexcept
{
    lhs = lhs & rhs;
    return lhs;
}

/**
 * @brief Callback function type for event notifications
 */
using EventCallback = std::function<void(int fd, short events, void* arg)>;

/**
 * @brief EventSystem class for managing events using libevent.
 */
class EventSystem {
public:
    explicit EventSystem(bool enableThreadSupport = true, int eventThreadIndex = 0);
    ~EventSystem();

    // Non-copyable, non-movable
    EventSystem(const EventSystem&) = delete;
    EventSystem& operator=(const EventSystem&) = delete;

    /**
    * @brief Initialize the event system.
    * @return true if initialization is successful, false otherwise.
    */
    bool Init();

    /**
    * @brief Add an event (I/O, signal, or timeout).
    * @param fd File descriptor to monitor (use -1 for pure timer events).
    * @param events Bitmask of EventType flags. Use EventType::PERSIST to make the event persistent.
    * @callback Callback function to invoke on event.
    * @param arg User-defined argument passed to the callback.
    * @param timeoutMs Optional timeout in milliseconds. For TIMEOUT events, sets the (re)trigger interval.
    * @return Event ID if successful, -1 otherwise.
    */
    int AddEvent(int fd, EventType events, EventCallback callback, void* arg, long timeoutMs = 0);

    /**
    * @brief Add a timer-only event.
    * @param timeoutMs Timeout interval in milliseconds (>0).
    * @param callback Callback invoked when the timer fires.
    * @param arg User-defined argument passed to the callback.
    * @param repeat If true, the timer is periodic (persistent); otherwise one-shot.
    * @return Event ID if successful, -1 otherwise.
    */
    int AddTimer(long timeoutMs, EventCallback callback, void* arg, bool repeat = false);

    /**
    * @brief Remove an event by its ID.
    * @param eventId ID of the event to remove.
    * @return true if successful, false otherwise.
    */
    bool RemoveEvent(int eventId);

    /**
    * @brief Create an eventfd and register it with the EventSystem so it can be
    * monitored via the normal libevent callbacks.
    * @param callback Callback invoked when the eventfd becomes readable.
    *                 The callback will receive the eventfd as the fd parameter.
    * @param arg User-defined argument passed to the callback.
    * @param persist If true, the registered libevent will be persistent.
    * @return The created notify eventId on success, or -1 on failure.
    */
    int CreateNotifyEventId(EventCallback callback, void* arg, bool persist = true);

    /**
    * @brief Notify (write) to a notify eventId to wake up listeners. By default writes the value 1.
    * @param eventId The eventId returned by CreateNotifyEventId.
    * @param increment The 64-bit value to add to the eventfd counter (default 1).
    * @return true on success, false on failure.
    */
    bool NotifyEventId(int eventId, uint64_t increment = 1);

    /**
    * @brief Read the current counter value from a file descriptor.
    * @param fd The fd.
    * @param outValue On success set to the value read from the eventfd.
    * @return true on success, false on failure.
    */
    bool ReadEventFd(int fd, uint64_t& outValue) const;

    /**
    * @brief Close a notify eventId and remove any libevent watchers associated with it.
    * @param eventId The eventId to close.
    * @return true on success, false on failure.
    */
    bool CloseNotifyEventId(int eventId);

    /**
    * @brief Start the event loop.
    * @param runInBackground If true, runs the loop in a background thread.
    */
    void Start(bool runInBackground = false);

    /**
    * @brief Stop the event loop.
    */
    void Stop();

    /**
    * @brief check whether event base is valid
    * @return whether event base is valid
    */
    bool EventBaseValid() const;

private:
    int ToEventFd(int eventId);
    bool RemoveEventInternal(int eventId, bool closeFd);

    struct Impl;
    std::unique_ptr<Impl> impl_;
    // Protects access to impl_ state that is shared across threads (primarily events map).
    // Do not hold this lock while invoking callbacks or calling event_del/close to avoid deadlocks.
    std::mutex mutex_;
    std::atomic<bool> running_{false};
    struct event_base* eventBase_{nullptr};
    int eventThreadIndex_{0};
};

} // namespace A2A

#endif // A2A_EVENT_SYSTEM_INCLUDE_H_