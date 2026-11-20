/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */
#ifndef MCP_PLATFORM_COMPAT_H
#define MCP_PLATFORM_COMPAT_H

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/socket.h>
#include <type_traits>
#include <unistd.h>

#include <atomic>
#include <string>

#if defined(__linux__)
#include <sys/eventfd.h>
#include <sys/syscall.h>
#endif

namespace Mcp::Platform {

// Converts a pthread handle into a uint64_t tag for log correlation. The
// pthread_t type is a pointer on some platforms and an integer of varying
// width on others (e.g. illumos uses unsigned int); dispatch on the actual
// representation so the fallback compiles on any of them.
template <typename T>
inline uint64_t PthreadToTag(const T& value)
{
    static_assert(std::is_integral_v<T> || std::is_pointer_v<T>,
        "Unsupported pthread_t representation on this platform; extend the "
        "GetThreadId fallback for it.");
    if constexpr (std::is_pointer_v<T>) {
        return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(value));
    } else {
        return static_cast<uint64_t>(value);
    }
}

// pthread_setname_np accepts at most 15 characters plus the terminating '\0'.
constexpr size_t THREAD_NAME_MAX_LEN = 15;
// A pipe provides exactly two fds: the read end and the write end.
constexpr int PIPE_FD_COUNT = 2;
// Drain buffer used to read wake records out of the notify pipe.
constexpr size_t NOTIFY_DRAIN_BUF_SIZE = 64;

/**
 * @brief Get the current OS thread id, matching what platform tooling reports.
 * @return Kernel thread id (Linux `gettid`, macOS 64-bit global thread id).
 */
inline uint64_t GetThreadId()
{
#if defined(__linux__)
    return static_cast<uint64_t>(syscall(SYS_gettid));
#elif defined(__APPLE__)
    uint64_t tid = 0;
    pthread_threadid_np(nullptr, &tid);
    return tid;
#else
    // No kernel tid API here: use the pthread handle as a per-thread tag so
    // at least threads within one process remain distinguishable in logs.
    return PthreadToTag(pthread_self());
#endif
}

/**
 * @brief Set the name of the current thread (max 15 characters + '\0').
 * @param name Thread name; longer names are truncated before being set.
 */
inline void SetCurrentThreadName(const std::string& name)
{
    const std::string truncatedName = name.substr(0, THREAD_NAME_MAX_LEN);
#if defined(__APPLE__)
    (void)pthread_setname_np(truncatedName.c_str());
#else
    (void)pthread_setname_np(pthread_self(), truncatedName.c_str());
#endif
}

/**
 * @brief Set the TCP keepalive idle time (seconds). Linux uses TCP_KEEPIDLE,
 *        macOS exposes the same knob as TCP_KEEPALIVE.
 * @return setsockopt result; 0 on success.
 */
inline int SetKeepAliveIdle(int fd, int sec)
{
#if defined(__APPLE__)
    return ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPALIVE, &sec, sizeof(sec));
#else
    return ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &sec, sizeof(sec));
#endif
}

/**
 * @brief Set the TCP keepalive retransmit interval (seconds).
 *        Silently no-ops (returns 0) on platforms without the option.
 * @return setsockopt result; 0 on success.
 */
inline int SetKeepAliveIntvl(int fd, int sec)
{
#if defined(TCP_KEEPINTVL)
    return ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &sec, sizeof(sec));
#else
    (void)fd;
    (void)sec;
    return 0;
#endif
}

/**
 * @brief Set the TCP keepalive probe count.
 *        Silently no-ops (returns 0) on platforms without the option.
 * @return setsockopt result; 0 on success.
 */
inline int SetKeepAliveCnt(int fd, int cnt)
{
#if defined(TCP_KEEPCNT)
    return ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
#else
    (void)fd;
    (void)cnt;
    return 0;
#endif
}

/**
 * @brief Cross-platform event notification channel backing EventSystem alerting.
 *        Linux: a single eventfd whose counter carries the accumulated value.
 *        Other platforms (e.g. macOS): a non-blocking pipe plus a software
 *        counter; a wake record is written only on the empty->non-empty edge so
 *        the "coalescing counter" semantics match the eventfd path.
 *
 * @note CONTRACT: both fds are ALWAYS created non-blocking by
 *       CreateNotifyChannel(), and NotifyChannelNotify() relies on that: the
 *       write must never block, because callers may hold the EventSystem mutex.
 *       Do not assign externally created blocking fds into this struct.
 */
struct NotifyChannel {
    int readFd{-1};
    int writeFd{-1};
    std::atomic<uint64_t> counter{0};
};

/**
 * @brief Create the notification channel fds (non-blocking + close-on-exec).
 * @param[out] ch Channel to initialize.
 * @return 0 on success; -1 on failure (channel left untouched).
 */
inline int CreateNotifyChannel(NotifyChannel& ch)
{
#if defined(__linux__)
    const int efd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (efd < 0) {
        return -1;
    }
    ch.readFd = efd;
    ch.writeFd = efd;
    ch.counter.store(0, std::memory_order_relaxed);
    return 0;
#else
    int pipeFds[PIPE_FD_COUNT] = {-1, -1};
    if (::pipe(pipeFds) != 0) {
        return -1;
    }
    for (int i = 0; i < PIPE_FD_COUNT; ++i) {
        const int statusFlags = ::fcntl(pipeFds[i], F_GETFL, 0);
        if (statusFlags < 0 || ::fcntl(pipeFds[i], F_SETFL, statusFlags | O_NONBLOCK) < 0 ||
            ::fcntl(pipeFds[i], F_SETFD, FD_CLOEXEC) < 0) {
            ::close(pipeFds[0]);
            ::close(pipeFds[1]);
            return -1;
        }
    }
    ch.readFd = pipeFds[0];
    ch.writeFd = pipeFds[1];
    ch.counter.store(0, std::memory_order_relaxed);
    return 0;
#endif
}

/**
 * @brief Close both ends of the channel; safe on already-valid/empty channels.
 * @param[in,out] ch Channel to close; fds are reset to -1.
 */
inline void CloseNotifyChannel(NotifyChannel& ch)
{
    if (ch.writeFd >= 0 && ch.writeFd != ch.readFd) {
        ::close(ch.writeFd);
    }
    if (ch.readFd >= 0) {
        ::close(ch.readFd);
    }
    ch.readFd = -1;
    ch.writeFd = -1;
}

/**
 * @brief Raw 8-byte eventfd write (Linux baseline semantics).
 * @return true on a complete write; false on error (EAGAIN included).
 */
inline bool WriteChannelFdRaw(int fd, uint64_t value)
{
#if defined(__linux__)
    const uint64_t val = value;
    while (true) {
        const ssize_t written = ::write(fd, &val, sizeof(val));
        if (written == static_cast<ssize_t>(sizeof(val))) {
            return true;
        }
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        return false;
    }
#else
    (void)fd;
    (void)value;
    return false;
#endif
}

/**
 * @brief Increment the notification counter and provide a wake record.
 * @return true when the increment is committed (a full pipe still counts as
 *         success: a wake record is already pending); false otherwise.
 * @note Callers must serialize Notify against Read for the same channel.
 */
inline bool NotifyChannelNotify(NotifyChannel& ch, uint64_t value)
{
#if defined(__linux__)
    return WriteChannelFdRaw(ch.writeFd, value);
#else
    // eventfd parity: a zero increment is a successful no-op.
    if (value == 0) {
        return true;
    }

    // Same counter bound as eventfd; reject invalid increments before the
    // subtraction below can underflow.
    constexpr uint64_t MAX_COUNTER = UINT64_MAX - 1;
    if (value > MAX_COUNTER) {
        return false;
    }

    uint64_t current = ch.counter.load(std::memory_order_relaxed);
    while (current <= MAX_COUNTER - value) {
        if (ch.counter.compare_exchange_weak(
            current, current + value,
            std::memory_order_acq_rel, std::memory_order_relaxed)) {
            break;
        }
    }
    if (current > MAX_COUNTER - value) {
        return false;
    }
    if (current > 0) {
        // Coalesce into the pending wake record.
        return true;
    }

    // Only the empty->non-empty edge writes a wake byte, so the pipe holds at
    // most one byte and a drain needs at most two reads. Keep this invariant.
    const uint8_t wakeByte = 1;
    while (true) {
        const ssize_t written = ::write(ch.writeFd, &wakeByte, sizeof(wakeByte));
        if (written == static_cast<ssize_t>(sizeof(wakeByte))) {
            return true;
        }
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Count committed and a wake record is pending: deliverable.
                return true;
            }
            // Undeliverable: roll back the committed increment.
            ch.counter.fetch_sub(value, std::memory_order_acq_rel);
            return false;
        }
        ch.counter.fetch_sub(value, std::memory_order_acq_rel);
        return false;
    }
#endif
}

/**
 * @brief Read the accumulated counter value from the channel and reset it.
 *        Linux reads the eventfd; pipe reads drain the wake records and then
 *        return the software counter.
 * @return true and @p outValue set on success; false when empty / on error.
 */
inline bool NotifyChannelRead(NotifyChannel& ch, int readFd, uint64_t& outValue)
{
#if defined(__linux__)
    uint64_t val = 0;
    while (true) {
        const ssize_t rd = ::read(readFd, &val, sizeof(val));
        if (rd == static_cast<ssize_t>(sizeof(val))) {
            outValue = val;
            return true;
        }
        if (rd < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        return false;
    }
#else
    // At most one wake byte is pending (see NotifyChannelNotify); in the
    // normal path (EINTR retries aside) this loop reads at most twice.
    uint8_t buffer[NOTIFY_DRAIN_BUF_SIZE];
    while (true) {
        const ssize_t rd = ::read(readFd, buffer, sizeof(buffer));
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

    const uint64_t count = ch.counter.exchange(0, std::memory_order_acq_rel);
    if (count == 0) {
        errno = EAGAIN;
        return false;
    }

    outValue = count;
    return true;
#endif
}

/**
 * @brief Fallback raw read for fds without a registered notify channel.
 *        Linux keeps the legacy 8-byte read semantics; other platforms do not
 *        support it (always false), matching previous behavior.
 * @return true and @p outValue set on success; false otherwise.
 */
inline bool ReadChannelFdRaw(int fd, uint64_t& outValue)
{
#if defined(__linux__)
    uint64_t val = 0;
    while (true) {
        const ssize_t rd = ::read(fd, &val, sizeof(val));
        if (rd == static_cast<ssize_t>(sizeof(val))) {
            outValue = val;
            return true;
        }
        if (rd < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        return false;
    }
#else
    (void)fd;
    (void)outValue;
    return false;
#endif
}

} // namespace Mcp::Platform
#endif
