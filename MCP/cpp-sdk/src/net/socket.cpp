/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "net/socket.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <string>

#include "mcp_log.h"

namespace Mcp::Net {

Socket::Socket(Mcp::EventSystem& es, int fd, const SocketOptions& opts) : es_(es), fd_(fd), options_(opts)
{
    // If we were given a valid file descriptor, apply the provided
    // socket options right away (non-blocking, buffer sizes, reuse flags).
    if (fd_ >= 0) {
        ApplyOptions(options_);
    }
}

bool Socket::SetNonBlocking(int fd)
{
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        MCP_LOG(MCP_LOG_LEVEL_WARN, "F_GETFL failed: %s (%d)", std::strerror(errno), errno);
        return false;
    }
    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        MCP_LOG(MCP_LOG_LEVEL_WARN, "F_SETFL failed: %s (%d)", std::strerror(errno), errno);
        return false;
    }
    return true;
}

void Socket::ApplyOptions(const SocketOptions& opts)
{
    if (fd_ < 0) {
        return;
    }

    // Configure non-blocking and close-on-exec according to options.
    if (opts.nonBlocking) {
        if (!SetNonBlocking(fd_)) {
            MCP_LOG(MCP_LOG_LEVEL_WARN, "SetNonBlocking failed: %s (%d)", std::strerror(errno), errno);
        }
    }

    if (opts.recvBufSize > 0) {
        if (::setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &opts.recvBufSize,
                         static_cast<socklen_t>(sizeof(opts.recvBufSize))) != 0) {
            MCP_LOG(MCP_LOG_LEVEL_WARN, "setsockopt(SO_RCVBUF) failed: %s (%d)", std::strerror(errno), errno);
        }
    }
    if (opts.sendBufSize > 0) {
        if (::setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &opts.sendBufSize,
                         static_cast<socklen_t>(sizeof(opts.sendBufSize))) != 0) {
            MCP_LOG(MCP_LOG_LEVEL_WARN, "setsockopt(SO_SNDBUF) failed: %s (%d)", std::strerror(errno), errno);
        }
    }

    if (opts.reuseAddr) {
        int on = 1;
        if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &on, static_cast<socklen_t>(sizeof(on))) != 0) {
            MCP_LOG(MCP_LOG_LEVEL_WARN, "setsockopt(SO_REUSEADDR) failed: %s (%d)", std::strerror(errno), errno);
        }
    }

    if (opts.reusePort) {
        int on = 1;
        if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &on, static_cast<socklen_t>(sizeof(on))) != 0) {
            MCP_LOG(MCP_LOG_LEVEL_WARN, "setsockopt(SO_REUSEPORT) failed: %s (%d)", std::strerror(errno), errno);
        }
    }
}

int Socket::Fd() const
{
    return fd_;
}

bool Socket::Valid() const
{
    return fd_ >= 0;
}

// Callback registration moved out of header to avoid inline implementation.
void Socket::OnRead(ReadCallback cb)
{
    onRead_ = std::move(cb);
}

void Socket::OnWritable(WritableCallback cb)
{
    onWritable_ = std::move(cb);
}

void Socket::OnClose(CloseCallback cb)
{
    onClose_ = std::move(cb);
}

void Socket::OnError(ErrorCallback cb)
{
    onError_ = std::move(cb);
}

void Socket::EnableRead()
{
    if (fd_ < 0 || evReadId_ != 0) {
        return;
    }

    std::weak_ptr<Socket> weak = shared_from_this();
    evReadId_ = es_.AddEvent(
        fd_, Mcp::EventType::READ,
        [weak](int, short, void*) {
            auto sp = weak.lock();
            if (sp == nullptr) {
                return;
            }

            sp->HandleReadable();
            if (sp->onRead_ != nullptr) {
                sp->onRead_(sp);
            }
        },
        this, 0);
}

void Socket::DisableRead()
{
    if (evReadId_ != 0) {
        es_.RemoveEvent(evReadId_);
        evReadId_ = 0;
    }
}

void Socket::EnableWrite()
{
    if (fd_ < 0 || evWriteId_ != 0) {
        return;
    }

    std::weak_ptr<Socket> weak = shared_from_this();
    evWriteId_ = es_.AddEvent(
        fd_, Mcp::EventType::WRITE,
        [weak](int, short, void*) {
            auto sp = weak.lock();
            if (sp == nullptr) {
                return;
            }

            sp->HandleWritable();
            if (sp->onWritable_ != nullptr) {
                sp->onWritable_(sp);
            }
        },
        this, 0);
}

void Socket::DisableWrite()
{
    if (evWriteId_ != 0) {
        es_.RemoveEvent(evWriteId_);
        evWriteId_ = 0;
    }
}

void Socket::NotifyError(int err, const char* where)
{
    if (onError_ != nullptr) {
        onError_(shared_from_this(), err, std::string(where) + ": " + std::string(std::strerror(err)));
    }
}

void Socket::NotifyCloseOnce()
{
    if (onClose_ != nullptr) {
        auto cb = std::move(onClose_);
        onClose_ = nullptr;
        cb(shared_from_this());
    }
}

void Socket::Close()
{
    if (fd_ < 0) {
        return;
    }
    // First unregister/disable any registered events
    if (evReadId_ != 0) {
        es_.RemoveEvent(evReadId_);
        evReadId_ = 0;
    }
    if (evWriteId_ != 0) {
        es_.RemoveEvent(evWriteId_);
        evWriteId_ = 0;
    }
    // Close the file descriptor
    ::close(fd_);
    fd_ = -1;
    // Notify close callback (once)
    NotifyCloseOnce();
    // Release self-hold
    selfHold_.reset();
}

} // namespace Mcp::Net
