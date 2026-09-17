/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#define private public
#define protected public
#include "net/tcp_socket.h"
#undef private
#undef protected

#include "event_system.h"

using namespace A2A;
using namespace A2A::Server;

namespace {

class TcpSocketTest : public ::testing::Test {
protected:
    EventSystem es{false};

    void SetUp() override
    {
        ASSERT_TRUE(es.Init());
    }

    static std::pair<int, int> MakeSocketPair()
    {
        int fds[2] = {-1, -1};
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
            return {-1, -1};
        }
        return {fds[0], fds[1]};
    }

    static int CreateTcpListener(uint16_t& port)
    {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            return -1;
        }

        int on = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, static_cast<socklen_t>(sizeof(on)));

        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(0);

        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(fd);
            return -1;
        }
        if (::listen(fd, 8) != 0) {
            ::close(fd);
            return -1;
        }

        socklen_t len = sizeof(addr);
        if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
            ::close(fd);
            return -1;
        }

        port = ntohs(addr.sin_port);
        return fd;
    }

    static TcpSocketOptions MakeOpts()
    {
        TcpSocketOptions opts;
        opts.tcpNoDelay = false;
        opts.keepAlive = false;
        return opts;
    }
};

TEST_F(TcpSocketTest, BufferInitialStateIsEmpty)
{
    Buffer buf(16);

    EXPECT_EQ(buf.ReadableBytes(), 0u);
    EXPECT_EQ(buf.WritableBytes(), 16u);
}

TEST_F(TcpSocketTest, BufferEnsureWritableExpandsBuffer)
{
    Buffer buf(8);

    buf.EnsureWritable(32);

    EXPECT_GE(buf.WritableBytes(), 32u);
}

TEST_F(TcpSocketTest, BufferRetrieveAllAsStringReturnsContentAndResets)
{
    Buffer buf(16);
    auto [rfd, wfd] = MakeSocketPair();
    ASSERT_GE(rfd, 0);
    ASSERT_GE(wfd, 0);

    const std::string payload = "hello";
    ASSERT_EQ(::write(wfd, payload.data(), payload.size()), static_cast<ssize_t>(payload.size()));
    ASSERT_GT(buf.ReadFd(rfd), 0);

    EXPECT_EQ(buf.ReadableBytes(), payload.size());
    EXPECT_EQ(buf.RetrieveAllAsString(), payload);
    EXPECT_EQ(buf.ReadableBytes(), 0u);

    ::close(rfd);
    ::close(wfd);
}

TEST_F(TcpSocketTest, BufferReadFdWhenPeerClosedReturnsZero)
{
    Buffer buf(16);
    auto [rfd, wfd] = MakeSocketPair();
    ASSERT_GE(rfd, 0);
    ASSERT_GE(wfd, 0);

    ::close(wfd);
    EXPECT_EQ(buf.ReadFd(rfd), 0);

    ::close(rfd);
}

TEST_F(TcpSocketTest, BufferReadFdWhenReadableReadsBytes)
{
    Buffer buf(4);
    auto [rfd, wfd] = MakeSocketPair();
    ASSERT_GE(rfd, 0);
    ASSERT_GE(wfd, 0);

    const std::string payload = "abcdef";
    ASSERT_EQ(::write(wfd, payload.data(), payload.size()), static_cast<ssize_t>(payload.size()));

    ssize_t n = buf.ReadFd(rfd);

    ASSERT_GT(n, 0);
    EXPECT_EQ(static_cast<size_t>(n), payload.size());
    EXPECT_EQ(buf.RetrieveAllAsString(), payload);

    ::close(rfd);
    ::close(wfd);
}

TEST_F(TcpSocketTest, CreateNonblockingTcpSocketInvalidFamilyReturnsMinusOne)
{
    EXPECT_LT(TcpSocket::CreateNonblockingTcpSocket(-1), 0);
}

TEST_F(TcpSocketTest, CreateNonblockingTcpSocketValidFamilyReturnsNonBlockingFd)
{
    int fd = TcpSocket::CreateNonblockingTcpSocket(AF_INET);
    ASSERT_GE(fd, 0);

    int flags = ::fcntl(fd, F_GETFL, 0);
    ASSERT_GE(flags, 0);
    EXPECT_TRUE((flags & O_NONBLOCK) != 0);

    ::close(fd);
}

TEST_F(TcpSocketTest, ConstructorWithNegativeFdStartsConnecting)
{
    TcpSocket sock(es, -1, MakeOpts());

    EXPECT_FALSE(sock.Connected());
    EXPECT_EQ(sock.state_, TcpSocket::State::CONNECTING);
}

TEST_F(TcpSocketTest, AdoptReturnsConnectedSocket)
{
    auto [fd1, fd2] = MakeSocketPair();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    auto sock = TcpSocket::Adopt(es, fd1, MakeOpts());

    ASSERT_NE(sock, nullptr);
    EXPECT_TRUE(sock->Connected());
    EXPECT_EQ(sock->state_, TcpSocket::State::CONNECTED);

    ::close(fd2);
    sock->Close();
}

TEST_F(TcpSocketTest, InputBufferReturnsInternalBufferReference)
{
    auto [fd1, fd2] = MakeSocketPair();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    auto sock = TcpSocket::Adopt(es, fd1, MakeOpts());

    Buffer& buf = sock->InputBuffer();
    EXPECT_EQ(&buf, &sock->inBuf_);

    ::close(fd2);
    sock->Close();
}

TEST_F(TcpSocketTest, SendWhenNotConnectedReturnsFalse)
{
    TcpSocket sock(es, -1, MakeOpts());

    EXPECT_FALSE(sock.Send("abc", 3));
}

TEST_F(TcpSocketTest, SendZeroLengthReturnsTrue)
{
    auto [fd1, fd2] = MakeSocketPair();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    auto sock = TcpSocket::Adopt(es, fd1, MakeOpts());

    EXPECT_TRUE(sock->Send("", 0));

    ::close(fd2);
    sock->Close();
}

TEST_F(TcpSocketTest, SendStringViewSendsDataImmediately)
{
    auto [fd1, fd2] = MakeSocketPair();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    auto sock = TcpSocket::Adopt(es, fd1, MakeOpts());

    ASSERT_TRUE(sock->Send(std::string_view("hello")));

    char buf[16] = {};
    ssize_t n = ::read(fd2, buf, sizeof(buf));
    ASSERT_EQ(n, 5);
    EXPECT_EQ(std::string(buf, static_cast<size_t>(n)), "hello");

    ::close(fd2);
    sock->Close();
}

TEST_F(TcpSocketTest, HandleReadableReadsIntoInputBuffer)
{
    auto [fd1, fd2] = MakeSocketPair();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    auto sock = TcpSocket::Adopt(es, fd1, MakeOpts());

    const std::string payload = "readable-data";
    ASSERT_EQ(::write(fd2, payload.data(), payload.size()), static_cast<ssize_t>(payload.size()));

    sock->HandleReadable();

    EXPECT_EQ(sock->InputBuffer().RetrieveAllAsString(), payload);

    ::close(fd2);
    sock->Close();
}

TEST_F(TcpSocketTest, HandleReadableWhenPeerClosedClosesSocket)
{
    auto [fd1, fd2] = MakeSocketPair();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    auto sock = TcpSocket::Adopt(es, fd1, MakeOpts());

    ::close(fd2);
    sock->HandleReadable();

    EXPECT_EQ(sock->state_, TcpSocket::State::CLOSED);
}

TEST_F(TcpSocketTest, HandleWritableSendsQueuedDataAndClearsOutputBuffer)
{
    auto [fd1, fd2] = MakeSocketPair();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    auto sock = TcpSocket::Adopt(es, fd1, MakeOpts());

    const std::string payload = "queued-output";
    sock->outBuf_.assign(payload.begin(), payload.end());
    sock->outBufSent_ = 0;
    sock->outBufSize_ = sock->outBuf_.size();

    sock->HandleWritable();

    char buf[64] = {};
    ssize_t n = ::read(fd2, buf, sizeof(buf));
    ASSERT_EQ(n, static_cast<ssize_t>(payload.size()));
    EXPECT_EQ(std::string(buf, static_cast<size_t>(n)), payload);

    EXPECT_EQ(sock->outBufSize_, 0u);
    EXPECT_EQ(sock->outBufSent_, 0u);
    EXPECT_TRUE(sock->outBuf_.empty());

    ::close(fd2);
    sock->Close();
}

TEST_F(TcpSocketTest, ShutdownWriteDoesNotCrash)
{
    auto [fd1, fd2] = MakeSocketPair();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    auto sock = TcpSocket::Adopt(es, fd1, MakeOpts());

    sock->ShutdownWrite();

    ::close(fd2);
    sock->Close();
}

TEST_F(TcpSocketTest, CloseIsIdempotent)
{
    auto [fd1, fd2] = MakeSocketPair();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    auto sock = TcpSocket::Adopt(es, fd1, MakeOpts());

    sock->Close();
    EXPECT_EQ(sock->state_, TcpSocket::State::CLOSED);

    EXPECT_NO_THROW(sock->Close());

    ::close(fd2);
}

TEST_F(TcpSocketTest, HandleConnectWritableWhenSoErrorZeroBecomesConnected)
{
    auto [fd1, fd2] = MakeSocketPair();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    auto sock = std::make_shared<TcpSocket>(es, fd1, MakeOpts());
    sock->state_ = TcpSocket::State::CONNECTING;

    sock->HandleConnectWritable();

    EXPECT_EQ(sock->state_, TcpSocket::State::CONNECTED);

    ::close(fd2);
    sock->Close();
}

TEST_F(TcpSocketTest, HandleConnectTimeoutWhenConnectingClosesSocket)
{
    auto [fd1, fd2] = MakeSocketPair();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    auto sock = std::make_shared<TcpSocket>(es, fd1, MakeOpts());
    sock->state_ = TcpSocket::State::CONNECTING;

    sock->HandleConnectTimeout();

    EXPECT_EQ(sock->state_, TcpSocket::State::CLOSED);

    ::close(fd2);
}

TEST_F(TcpSocketTest, SetTcpOptionsWithInvalidFdDoesNothing)
{
    TcpSocket sock(es, -1, MakeOpts());

    EXPECT_NO_THROW(sock.SetTcpOptions(MakeOpts()));
}

TEST_F(TcpSocketTest, ConnectInvalidHostReturnsNull)
{
    auto sock = TcpSocket::Connect(es, "nonexistent.invalid.test.host", 6553, 10, MakeOpts());
    EXPECT_EQ(sock, nullptr);
}

TEST_F(TcpSocketTest, ConnectLocalListenerReturnsSocket)
{
    uint16_t port = 0;
    int listenFd = CreateTcpListener(port);
    ASSERT_GE(listenFd, 0);

    auto client = TcpSocket::Connect(es, "127.0.0.1", port, 100, MakeOpts());
    ASSERT_NE(client, nullptr);

    sockaddr_in peer {};
    socklen_t len = sizeof(peer);
    int acceptedFd = ::accept(listenFd, reinterpret_cast<sockaddr*>(&peer), &len);
    ASSERT_GE(acceptedFd, 0);

    if (!client->Connected()) {
        client->HandleConnectWritable();
    }

    EXPECT_TRUE(client->Connected());

    ASSERT_TRUE(client->Send(std::string_view("ping")));
    char buf[8] = {};
    ssize_t n = ::read(acceptedFd, buf, sizeof(buf));
    ASSERT_EQ(n, 4);
    EXPECT_EQ(std::string(buf, static_cast<size_t>(n)), "ping");

    ::close(acceptedFd);
    ::close(listenFd);
    client->Close();
}

TEST_F(TcpSocketTest, BufferPeekReturnsReadablePointer)
{
    Buffer buf(16);
    auto [rfd, wfd] = MakeSocketPair();
    ASSERT_GE(rfd, 0);
    ASSERT_GE(wfd, 0);

    const std::string payload = "peek-data";
    ASSERT_EQ(::write(wfd, payload.data(), payload.size()), static_cast<ssize_t>(payload.size()));
    ASSERT_GT(buf.ReadFd(rfd), 0);

    ASSERT_NE(buf.Peek(), nullptr);
    EXPECT_EQ(std::string(buf.Peek(), buf.ReadableBytes()), payload);

    ::close(rfd);
    ::close(wfd);
}

TEST_F(TcpSocketTest, BufferEnsureWritableWhenEnoughSpaceDoesNotShrink)
{
    Buffer buf(32);
    const auto oldCap = buf.buf_.size();

    buf.EnsureWritable(8);

    EXPECT_EQ(buf.buf_.size(), oldCap);
}

TEST_F(TcpSocketTest, BufferMakeSpaceResizesWhenNeeded)
{
    Buffer buf(8);
    buf.r_ = 1;
    buf.w_ = 8;
    const auto oldCap = buf.buf_.size();

    buf.EnsureWritable(16);

    EXPECT_GT(buf.buf_.size(), oldCap);
    EXPECT_GE(buf.WritableBytes(), 16u);
}

TEST_F(TcpSocketTest, BufferReadFdWithInvalidFdReturnsMinusOne)
{
    Buffer buf(16);
    EXPECT_EQ(buf.ReadFd(-1), -1);
}

TEST_F(TcpSocketTest, HandleReadableWhenNotConnectedReturnsImmediately)
{
    auto [fd1, fd2] = MakeSocketPair();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    auto sock = TcpSocket::Adopt(es, fd1, MakeOpts());
    sock->state_ = TcpSocket::State::CONNECTING;

    EXPECT_NO_THROW(sock->HandleReadable());

    ::close(fd2);
    sock->Close();
}

TEST_F(TcpSocketTest, HandleReadableWhenWouldBlockKeepsConnected)
{
    auto [fd1, fd2] = MakeSocketPair();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    ASSERT_TRUE(Socket::SetNonBlocking(fd1));
    auto sock = TcpSocket::Adopt(es, fd1, MakeOpts());

    sock->HandleReadable();

    EXPECT_TRUE(sock->Connected());

    ::close(fd2);
    sock->Close();
}

TEST_F(TcpSocketTest, HandleWritableWhenNotConnectedReturnsImmediately)
{
    auto [fd1, fd2] = MakeSocketPair();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    auto sock = TcpSocket::Adopt(es, fd1, MakeOpts());
    sock->state_ = TcpSocket::State::CONNECTING;
    sock->outBuf_ = {'a', 'b', 'c'};
    sock->outBufSent_ = 0;
    sock->outBufSize_ = 3;

    EXPECT_NO_THROW(sock->HandleWritable());
    EXPECT_EQ(sock->outBufSize_, 3u);

    ::close(fd2);
    sock->Close();
}

TEST_F(TcpSocketTest, SendWhenQueuedAlreadyAppendsData)
{
    auto [fd1, fd2] = MakeSocketPair();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    auto sock = TcpSocket::Adopt(es, fd1, MakeOpts());
    sock->outBuf_ = {'a', 'b'};
    sock->outBufSent_ = 0;
    sock->outBufSize_ = 2;

    ASSERT_TRUE(sock->Send("cd", 2));

    EXPECT_EQ(sock->outBufSize_, 4u);
    EXPECT_EQ(std::string(sock->outBuf_.begin(), sock->outBuf_.end()), "abcd");

    ::close(fd2);
    sock->Close();
}

TEST_F(TcpSocketTest, SendWhenWouldBlockQueuesData)
{
    auto [fd1, fd2] = MakeSocketPair();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    ASSERT_TRUE(Socket::SetNonBlocking(fd1));
    ASSERT_TRUE(Socket::SetNonBlocking(fd2));

    int sndbuf = 4096;
    ::setsockopt(fd1, SOL_SOCKET, SO_SNDBUF, &sndbuf, static_cast<socklen_t>(sizeof(sndbuf)));

    auto sock = TcpSocket::Adopt(es, fd1, MakeOpts());

    std::string big(1 << 20, 'x');
    bool queued = false;
    for (int i = 0; i < 16; ++i) {
        ASSERT_TRUE(sock->Send(big.data(), big.size()));
        if (sock->outBufSize_ > 0) {
            queued = true;
            break;
        }
    }

    EXPECT_TRUE(queued);
    EXPECT_GT(sock->outBufSize_, 0u);

    ::close(fd2);
    sock->Close();
}

TEST_F(TcpSocketTest, HandleConnectWritableWhenNotConnectingReturnsImmediately)
{
    auto [fd1, fd2] = MakeSocketPair();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    auto sock = TcpSocket::Adopt(es, fd1, MakeOpts());
    sock->state_ = TcpSocket::State::CONNECTED;

    EXPECT_NO_THROW(sock->HandleConnectWritable());
    EXPECT_EQ(sock->state_, TcpSocket::State::CONNECTED);

    ::close(fd2);
    sock->Close();
}

TEST_F(TcpSocketTest, HandleConnectWritableWhenGetSockoptFailsClosesSocket)
{
    auto [fd1, fd2] = MakeSocketPair();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    auto sock = std::make_shared<TcpSocket>(es, fd1, MakeOpts());
    int capturedErr = 0;
    bool errorCalled = false;

    sock->OnError([&](const SocketPtr&, int err, const std::string&) {
        errorCalled = true;
        capturedErr = err;
    });

    sock->state_ = TcpSocket::State::CONNECTING;
    sock->fd_ = -1;

    sock->HandleConnectWritable();

    EXPECT_TRUE(errorCalled);
    EXPECT_NE(capturedErr, 0);
    EXPECT_EQ(sock->state_, TcpSocket::State::CLOSED);

    ::close(fd2);
}

TEST_F(TcpSocketTest, HandleConnectTimeoutWhenNotConnectingReturnsImmediately)
{
    auto [fd1, fd2] = MakeSocketPair();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    auto sock = TcpSocket::Adopt(es, fd1, MakeOpts());
    sock->state_ = TcpSocket::State::CONNECTED;

    EXPECT_NO_THROW(sock->HandleConnectTimeout());
    EXPECT_EQ(sock->state_, TcpSocket::State::CONNECTED);

    ::close(fd2);
    sock->Close();
}

TEST_F(TcpSocketTest, SetTcpOptionsWithKeepAliveAndNoDelayDoesNotCrash)
{
    auto [fd1, fd2] = MakeSocketPair();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    auto sock = TcpSocket::Adopt(es, fd1, MakeOpts());

    TcpSocketOptions opts;
    opts.tcpNoDelay = true;
    opts.keepAlive = true;
    opts.keepAliveIdleSec = 1;
    opts.keepAliveIntvlSec = 1;
    opts.keepAliveCnt = 1;

    EXPECT_NO_THROW(sock->SetTcpOptions(opts));

    ::close(fd2);
    sock->Close();
}

TEST_F(TcpSocketTest, TransitionToChangesState)
{
    auto [fd1, fd2] = MakeSocketPair();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    auto sock = TcpSocket::Adopt(es, fd1, MakeOpts());

    sock->TransitionTo(TcpSocket::State::CLOSING);
    EXPECT_EQ(sock->state_, TcpSocket::State::CLOSING);

    ::close(fd2);
    sock->Close();
}

TEST_F(TcpSocketTest, CloseWithPendingConnectTimeoutEventResetsEventId)
{
    auto [fd1, fd2] = MakeSocketPair();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    auto sock = TcpSocket::Adopt(es, fd1, MakeOpts());
    sock->evConnTimeoutId_ = es.AddTimer(
        1000,
        [](int, short, void*) {},
        nullptr,
        false);

    ASSERT_GT(sock->evConnTimeoutId_, 0);

    sock->Close();

    EXPECT_EQ(sock->state_, TcpSocket::State::CLOSED);
    EXPECT_EQ(sock->evConnTimeoutId_, 0);

    ::close(fd2);
}

} // namespace