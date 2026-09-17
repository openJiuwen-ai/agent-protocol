/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "net/tcp_socket.h"

#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include "mcp_log.h"

namespace Mcp::Net {

constexpr int EXTRA_BUFF_SIZE = 65536;
constexpr int RECV_VEC_SIZE = 2;
constexpr int MAX_VEC_SIZE = 2;
constexpr int DEFAULT_VEC_SIZE = 2;
constexpr int DEFAULT_INPUT_BUFF_SIZE = 4096;

Buffer::Buffer(size_t initialCap) : buf_(initialCap), r_(0), w_(0)
{
}

size_t Buffer::ReadableBytes() const
{
    return w_ - r_;
}

size_t Buffer::WritableBytes() const
{
    return buf_.size() - w_;
}

const char* Buffer::Peek() const
{
    return buf_.data() + r_;
}

void Buffer::Retrieve(size_t n)
{
    if (n >= ReadableBytes()) {
        RetrieveAll();
    } else {
        r_ += n;
    }
}

void Buffer::RetrieveAll()
{
    r_ = w_ = 0;
}

std::string Buffer::RetrieveAllAsString()
{
    std::string s(Peek(), ReadableBytes());
    RetrieveAll();
    return s;
}

void Buffer::Append(const void* data, size_t len)
{
    EnsureWritable(len);
    std::memcpy(buf_.data() + w_, data, len);
    w_ += len;
}

void Buffer::EnsureWritable(size_t len)
{
    if (WritableBytes() >= len) {
        return;
    }
    MakeSpace(len);
}

void Buffer::MakeSpace(size_t len)
{
    if (WritableBytes() + r_ >= len) {
        // Move existing readable data to the front of the buffer so writable
        // space becomes contiguous. This avoids allocating a larger buffer
        // when there is free space at the beginning.
        size_t readable = ReadableBytes();
        if (readable > 0) {
            std::memmove(buf_.data(), buf_.data() + r_, readable);
        }
        r_ = 0;
        w_ = readable;
    } else {
        buf_.resize(w_ + len);
    }
}

ssize_t Buffer::ReadFd(int fd)
{
    char extrabuf[EXTRA_BUFF_SIZE];
    struct iovec vec[RECV_VEC_SIZE];

    size_t writable = WritableBytes();
    vec[0].iov_base = buf_.data() + w_;
    vec[0].iov_len = writable;
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);

    int iovcnt = writable > 0 ? MAX_VEC_SIZE : DEFAULT_VEC_SIZE;
    ssize_t n = ::readv(fd, vec, iovcnt);
    if (n < 0) {
        return -1;
    } else if (static_cast<size_t>(n) <= writable) {
        w_ += static_cast<size_t>(n);
    } else {
        w_ = buf_.size();
        Append(extrabuf, static_cast<size_t>(n) - writable);
    }
    return n;
}

TcpSocket::TcpSocket(Mcp::EventSystem& es, int fd, const TcpSocketOptions& opts)
    : Socket(es, fd, opts), inBuf_(DEFAULT_INPUT_BUFF_SIZE), tcpOpts_(opts)
{
    if (fd_ >= 0) {
        state_ = State::CONNECTED;
        SetTcpOptions(tcpOpts_);
    } else {
        state_ = State::CONNECTING;
    }
}

TcpSocket::~TcpSocket()
{
    TcpSocket::Close();
}

int TcpSocket::CreateNonblockingTcpSocket(int family)
{
    int fd = static_cast<int>(::socket(family, SOCK_STREAM, 0));
    if (fd < 0) {
        return -1;
    }
    if (!Socket::SetNonBlocking(fd)) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR,
                std::string("SetNonBlocking failed: ") + std::strerror(errno) + " (" + std::to_string(errno) + ")");
        ::close(fd);
        return -1;
    }
    return fd;
}

TcpSocketPtr TcpSocket::Adopt(Mcp::EventSystem& es, int fd, const TcpSocketOptions& opts)
{
    auto sp = std::make_shared<TcpSocket>(es, fd, opts);
    sp->SetTcpOptions(opts);
    // Connections created via adopt() are treated as already connected.
    // We keep the socket enabled for reading and hold a self-reference
    // so the object stays alive while events are active.
    sp->state_ = State::CONNECTED;
    sp->selfHold_ = sp;
    sp->EnableRead();
    return sp;
}

TcpSocketPtr TcpSocket::Connect(Mcp::EventSystem& es, const std::string& host, uint16_t port, int connectTimeoutMs,
                                const TcpSocketOptions& opts)
{
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_ADDRCONFIG;

    char portStr[ListenerPortStringSize];
    std::snprintf(portStr, sizeof(portStr), "%u", static_cast<unsigned>(port));

    addrinfo* res = nullptr;
    int gai = ::getaddrinfo(host.c_str(), portStr, &hints, &res);
    if (gai != 0) {
        MCP_LOG(MCP_LOG_LEVEL_WARN, "getaddrinfo(" + host + ":" + portStr + ") failed: " + ::gai_strerror(gai));
        return nullptr;
    }

    int fd = -1;
    for (addrinfo* ai = res; ai; ai = ai->ai_next) {
        fd = CreateNonblockingTcpSocket(ai->ai_family);
        if (fd < 0) {
            MCP_LOG(MCP_LOG_LEVEL_DEBUG, "socket() create failed for family " + std::to_string(ai->ai_family));
            continue;
        }

        int ret = ::connect(fd, ai->ai_addr, static_cast<socklen_t>(ai->ai_addrlen));
        if (ret == 0) {
            // Connection succeeded immediately (synchronous connect).
            break;
        } else {
            if (errno == EINPROGRESS || errno == EALREADY) {
                break;
            }
            MCP_LOG(MCP_LOG_LEVEL_WARN,
                    std::string("connect() failed: ") + std::strerror(errno) + " (" + std::to_string(errno) + ")");
            ::close(fd);
            fd = -1;
            continue;
        }
    }

    ::freeaddrinfo(res);
    if (fd < 0) {
        return nullptr;
    }

    auto sp = std::make_shared<TcpSocket>(es, fd, opts);
    sp->selfHold_ = sp;
    sp->SetTcpOptions(opts);

    // Check whether the non-blocking connect already completed successfully
    // by inspecting the SO_ERROR socket option.
    int soerr = 0;
    socklen_t slen = sizeof(soerr);
    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &slen) != 0) {
        MCP_LOG(MCP_LOG_LEVEL_WARN,
                std::string("getsockopt(SO_ERROR) failed: ") + std::strerror(errno) +
                    " (" + std::to_string(errno) + ")");
        soerr = errno;
    }

    if (soerr == 0) {
        sp->state_ = State::CONNECTED;
        sp->EnableRead();
        return sp;
    }

    // Still connecting: register a WRITE event to detect when the
    // non-blocking connect completes (writable => connect finished).
    sp->state_ = State::CONNECTING;

    std::weak_ptr<TcpSocket> weak = sp;
    sp->evWriteId_ = es.AddEvent(
        fd, Mcp::EventType::WRITE,
        [weak](int, short, void*) {
            auto sp2 = weak.lock();
            if (sp2 == nullptr) {
                return;
            }
            sp2->HandleConnectWritable();
        },
        sp.get(), 0);

    if (connectTimeoutMs > 0) {
        sp->evConnTimeoutId_ = es.AddEvent(
            -1, Mcp::EventType::TIMEOUT,
            [weak](int, short, void*) {
                auto sp2 = weak.lock();
                if (sp2 == nullptr) {
                    return;
                }
                sp2->HandleConnectTimeout();
            },
            sp.get(), connectTimeoutMs);
    }

    return sp;
}

void TcpSocket::SetTcpOptions(const TcpSocketOptions& opts)
{
    if (fd_ < 0) {
        return;
    }

    if (opts.tcpNoDelay) {
        int val = 1;
        int rc = ::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &val, static_cast<socklen_t>(sizeof(val)));
        if (rc != 0) {
            MCP_LOG(MCP_LOG_LEVEL_WARN,
                    std::string("setsockopt(TCP_NODELAY) failed: ") + std::strerror(errno) +
                        " (" + std::to_string(errno) + ")");
        }
    }

    if (opts.keepAlive) {
        int on = 1;
        int rc = ::setsockopt(fd_, SOL_SOCKET, SO_KEEPALIVE, &on, static_cast<socklen_t>(sizeof(on)));
        if (rc != 0) {
            MCP_LOG(MCP_LOG_LEVEL_WARN,
                    std::string("setsockopt(SO_KEEPALIVE) failed: ") + std::strerror(errno) +
                        " (" + std::to_string(errno) + ")");
        }
        if (opts.keepAliveIdleSec > 0) {
            rc = ::setsockopt(fd_, IPPROTO_TCP, TCP_KEEPIDLE, &opts.keepAliveIdleSec,
                              static_cast<socklen_t>(sizeof(opts.keepAliveIdleSec)));
            if (rc != 0) {
                MCP_LOG(MCP_LOG_LEVEL_WARN,
                        std::string("setsockopt(TCP_KEEPIDLE) failed: ") + std::strerror(errno) +
                            " (" + std::to_string(errno) + ")");
            }
        }
        if (opts.keepAliveIntvlSec > 0) {
            rc = ::setsockopt(fd_, IPPROTO_TCP, TCP_KEEPINTVL, &opts.keepAliveIntvlSec,
                              static_cast<socklen_t>(sizeof(opts.keepAliveIntvlSec)));
            if (rc != 0) {
                MCP_LOG(MCP_LOG_LEVEL_WARN,
                        std::string("setsockopt(TCP_KEEPINTVL) failed: ") + std::strerror(errno) +
                            " (" + std::to_string(errno) + ")");
            }
        }
        if (opts.keepAliveCnt > 0) {
            rc = ::setsockopt(fd_, IPPROTO_TCP, TCP_KEEPCNT, &opts.keepAliveCnt,
                              static_cast<socklen_t>(sizeof(opts.keepAliveCnt)));
            if (rc != 0) {
                MCP_LOG(MCP_LOG_LEVEL_WARN,
                        std::string("setsockopt(TCP_KEEPCNT) failed: ") + std::strerror(errno) +
                            " (" + std::to_string(errno) + ")");
            }
        }
    }
}

void TcpSocket::TransitionTo(State s)
{
    state_ = s;
}

bool TcpSocket::Connected() const
{
    return state_ == State::CONNECTED;
}

// Non-inline wrappers moved from header
bool TcpSocket::Send(std::string_view sv)
{
    return Send(sv.data(), sv.size());
}

Buffer& TcpSocket::InputBuffer()
{
    return inBuf_;
}

// Buffer convenience overload moved from header
void Buffer::Append(std::string_view sv)
{
    Append(sv.data(), sv.size());
}

void TcpSocket::HandleConnectWritable()
{
    if (state_ != State::CONNECTING) {
        return;
    }

    int err = 0;
    socklen_t len = sizeof(err);
    if (::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &err, &len) != 0) {
        err = errno;
    }

    // Cancel the connection timeout event if it was set.
    if (evConnTimeoutId_ != 0) {
        es_.RemoveEvent(evConnTimeoutId_);
        evConnTimeoutId_ = 0;
    }

    // Cancel the WRITE event used for connection completion.
    if (evWriteId_ != 0) {
        es_.RemoveEvent(evWriteId_);
        evWriteId_ = 0;
    }

    if (err != 0) {
        MCP_LOG(MCP_LOG_LEVEL_WARN,
                std::string("connection failed: ") + std::strerror(err) + " (" + std::to_string(err) + ")");
        NotifyError(err, "connect");
        Close();
        return;
    }

    // Connected: update state and enable reading from the socket.
    TransitionTo(State::CONNECTED);
    EnableRead();
}

void TcpSocket::HandleConnectTimeout()
{
    if (state_ != State::CONNECTING) {
        return;
    }
    int err = ETIMEDOUT;
    NotifyError(err, "connect-timeout");
    Close();
}

void TcpSocket::HandleReadable()
{
    if (!Connected()) {
        return;
    }

    while (true) {
        ssize_t n = inBuf_.ReadFd(fd_);
        if (n > 0) {
            // Only responsible for reading data into the Buffer; higher
            // layers are expected to consume data from `inBuf_`.
            continue;
        } else if (n == 0) {
            // Peer closed the connection.
            Close();
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            NotifyError(errno, "read");
            Close();
            return;
        }
    }
}

void TcpSocket::HandleWritable()
{
    if (!Connected()) {
        return;
    }

    // Try to send queued data until EAGAIN/EWOULDBLOCK or the buffer is empty.
    // This loop attempts to drain `outBuf_` by calling send() repeatedly.
    while (outBufSize_ > 0) {
        const char* base = outBuf_.data() + outBufSent_;
        size_t len = outBuf_.size() - outBufSent_;

        ssize_t n = ::send(fd_, base, len, 0);
        if (n > 0) {
            outBufSent_ += static_cast<size_t>(n);
            outBufSize_ = outBuf_.size() - outBufSent_;
            if (outBufSize_ == 0) {
                outBuf_.clear();
                outBuf_.shrink_to_fit();
                outBufSent_ = 0;
                DisableWrite();
                break;
            }
            continue;
        } else if (n == 0) {
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            NotifyError(errno, "send");
            Close();
            return;
        }
    }
}

bool TcpSocket::Send(const void* data, size_t len)
{
    if (!Connected()) {
        return false;
    }
    if (len == 0) {
        return true;
    }

    const char* p = static_cast<const char*>(data);

    // If there is no queued data, try an immediate non-blocking send to
    // avoid copying into the send queue. On EAGAIN/EWOULDBLOCK we fall
    // back to queueing the data and enabling write notifications.
    if (outBufSize_ == 0) {
        ssize_t n = ::send(fd_, p, len, 0);
        if (n >= 0) {
            size_t sent = static_cast<size_t>(n);
            if (sent == len) {
                return true;
            } else {
                outBuf_.assign(p + sent, p + len);
                outBufSent_ = 0;
                outBufSize_ = outBuf_.size();
                EnableWrite();
                return true;
            }
        } else {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                NotifyError(errno, "send");
                Close();
                return false;
            }
            outBuf_.assign(p, p + len);
            outBufSent_ = 0;
            outBufSize_ = outBuf_.size();
            EnableWrite();
            return true;
        }
    } else {
        // There is already queued data: append new data to the end of the
        // output buffer and ensure WRITE events are enabled to drain it.
        outBuf_.insert(outBuf_.end(), p, p + len);
        outBufSize_ = outBuf_.size() - outBufSent_;
        EnableWrite();
        return true;
    }
}

void TcpSocket::ShutdownWrite()
{
    if (fd_ < 0) {
        return;
    }
    // Perform a half-close: signal we're done sending, but can still read.
    ::shutdown(fd_, SHUT_WR);
}

void TcpSocket::Close()
{
    if (state_ == State::CLOSED) {
        return;
    }
    state_ = State::CLOSING;

    if (evConnTimeoutId_ != 0) {
        es_.RemoveEvent(evConnTimeoutId_);
        evConnTimeoutId_ = 0;
    }

    Socket::Close();
    state_ = State::CLOSED;
}

} // namespace Mcp::Net
