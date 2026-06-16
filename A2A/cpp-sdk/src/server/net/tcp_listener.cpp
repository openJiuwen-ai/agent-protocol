/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include "net/tcp_listener.h"

#include <cerrno>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

#include "a2a_log.h"
#include "event_system.h"

namespace A2A::Server {

static bool SetReusePort(int fd, bool on)
{
    int v = on ? 1 : 0;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &v, static_cast<socklen_t>(sizeof(v))) != 0) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "SO_REUSEPORT failed, errno: " + std::to_string(errno));
        return false;
    }
    return true;
}

TcpListener::TcpListener(EventSystem& es) : es_(es)
{
}

TcpListener::~TcpListener()
{
    Stop();
    if (listenFd_ >= 0) {
        ::close(listenFd_);
        listenFd_ = -1;
    }
}

bool TcpListener::Listen(const std::string& host, uint16_t port, int backlog, bool reusePort)
{
    if (listenFd_ >= 0) {
        return true;
    }

    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_UNSPEC;

    std::string portStr = std::to_string(port);
    addrinfo* res = nullptr;
    int gai = ::getaddrinfo(host.empty() ? nullptr : host.c_str(), portStr.c_str(), &hints, &res);
    if (gai != 0) {
        return false;
    }

    int fd = -1;
    for (addrinfo* ai = res; ai; ai = ai->ai_next) {
        fd = static_cast<int>(::socket(ai->ai_family, SOCK_STREAM, 0));
        if (fd < 0) {
            A2A_LOG(A2A_LOG_LEVEL_ERROR, "socket() failed, errno: " + std::to_string(errno));
            continue;
        }

        if (reusePort) {
            if (!SetReusePort(fd, true)) {
                A2A_LOG(A2A_LOG_LEVEL_INFO, "requested SO_REUSEPORT not available on this platform");
            }
        }

        if (::bind(fd, ai->ai_addr, static_cast<socklen_t>(ai->ai_addrlen)) != 0) {
            A2A_LOG(A2A_LOG_LEVEL_ERROR, "bind() failed on fd " + std::to_string(fd) +
                " errno: " + std::to_string(errno));
            ::close(fd);
            fd = -1;
            continue;
        }

        if (::listen(fd, backlog) != 0) {
            A2A_LOG(A2A_LOG_LEVEL_ERROR, "listen() failed on fd " + std::to_string(fd) +
                " errno: " + std::to_string(errno));
            ::close(fd);
            fd = -1;
            continue;
        }
        // success: bound and listening
        break;
    }

    ::freeaddrinfo(res);
    if (fd < 0) {
        return false;
    }

    if (!Socket::SetNonBlocking(fd)) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "SetNonBlocking failed on fd " + std::to_string(fd) +
                " errno: " + std::to_string(errno));
        ::close(fd);
        return false;
    }
    listenFd_ = fd;
    return true;
}

bool TcpListener::Start()
{
    if (listenFd_ < 0 || evAcceptId_ != 0) {
        return false;
    }

    evAcceptId_ = es_.AddEvent(listenFd_, EventType::READ, [this](int, short, void*) { HandleAccept(); }, this, 0);
    return evAcceptId_ != 0;
}

void TcpListener::Stop()
{
    if (evAcceptId_ != 0) {
        es_.RemoveEvent(evAcceptId_);
        evAcceptId_ = 0;
    }
}

void TcpListener::HandleAccept()
{
    if (listenFd_ < 0) {
        return;
    }

    while (true) {
        sockaddr_storage ss{};
        socklen_t slen = sizeof(ss);
        int cfd = static_cast<int>(::accept(listenFd_, reinterpret_cast<sockaddr*>(&ss), &slen));
        if (cfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            if (onError_ != nullptr) {
                onError_(nullptr, errno, std::string("accept: ") + std::strerror(errno));
            }
            break;
        }

        if (!Socket::SetNonBlocking(cfd)) {
            ::close(cfd);
            continue;
        }

        TcpSocketOptions opts;
        auto conn = TcpSocket::Adopt(es_, cfd, opts);
        if (onNewConn_ != nullptr) {
            onNewConn_(conn);
        } else {
            // No user accepted the new connection; close it immediately to avoid leaks
            conn->Close();
        }
    }
}

void TcpListener::OnNewConnection(NewConnectionCallback cb)
{
    onNewConn_ = std::move(cb);
}

void TcpListener::OnError(ErrorCallback cb)
{
    onError_ = std::move(cb);
}

} // namespace A2A::Server