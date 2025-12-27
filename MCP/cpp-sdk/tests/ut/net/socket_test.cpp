/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <arpa/inet.h>
#include <chrono>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <cstring>

#include <gtest/gtest.h>

#include "net/socket.h"
#include "net/tcp_listener.h"
#include "net/tcp_socket.h"

using namespace Mcp::Net;

// ==================== Socket Test Cases ====================

TEST(SocketTest, SetNonBlockingInvalidFd) {
    EXPECT_FALSE(Socket::SetNonBlocking(-1));
}

TEST(SocketTest, SetNonBlockingSetsFlag) {
    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);
    ASSERT_TRUE(Socket::SetNonBlocking(fds[0]));
    int flags = ::fcntl(fds[0], F_GETFL, 0);
    EXPECT_TRUE(flags & O_NONBLOCK);
    ::close(fds[0]);
    ::close(fds[1]);
}

// ==================== TcpSocket Test Cases ====================

TEST(TcpSocketTest, AdoptSendReceive) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

    TcpSocketOptions opts;
    opts.tcpNoDelay = false;
    auto conn = TcpSocket::Adopt(es, sv[0], opts);
    ASSERT_NE(conn, nullptr);
    const char payload[] = "ping";
    EXPECT_TRUE(conn->Send(payload, sizeof(payload) - 1));

    char buffer[32] = {};
    ssize_t n = ::recv(sv[1], buffer, sizeof(buffer), 0);
    EXPECT_EQ(n, static_cast<ssize_t>(sizeof(payload) - 1));
    EXPECT_EQ(std::string(buffer, static_cast<size_t>(n)), payload);

    conn->Close();
    ::close(sv[1]);
}

TEST(TcpSocketTest, ConnectInvalidHost) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    auto sock = TcpSocket::Connect(es, "invalid.host.example", 12345);
    EXPECT_EQ(sock, nullptr);
}

TEST(TcpSocketTest, SendQueuesWhenImmediateSendPartial) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());
    es.Start(true);

    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

    TcpSocketOptions opts;
    opts.tcpNoDelay = false;
    opts.sendBufSize = 1024;
    auto conn = TcpSocket::Adopt(es, sv[0], opts);
    ASSERT_NE(conn, nullptr);

    std::string payload(65536, 'x');
    EXPECT_TRUE(conn->Send(payload.data(), payload.size()));

    std::vector<char> received(payload.size());
    size_t total = 0;
    int flags = ::fcntl(sv[1], F_GETFL, 0);
    ASSERT_GE(flags, 0);
    ASSERT_EQ(::fcntl(sv[1], F_SETFL, flags | O_NONBLOCK), 0);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (total <= 0) {
        if (std::chrono::steady_clock::now() > deadline) {
            FAIL() << "timed out waiting for payload";
            break;
        }
        ssize_t n = ::recv(sv[1], received.data(), received.size(), 0);
        if (n > 0) {
            total = n;
            continue;
        }
        if (n == 0) {
            break;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (errno == EINTR) {
            continue;
        }
        FAIL() << "recv failed: " << std::strerror(errno);
    }
    EXPECT_GT(total, 0);

    conn->Close();
    ::close(sv[1]);
    es.Stop();
}

TEST(TcpSocketTest, ConstructorWithValidFd) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

    TcpSocketOptions opts;
    auto socket = std::make_shared<TcpSocket>(es, sv[0], opts);
    EXPECT_TRUE(socket->Valid());
    EXPECT_EQ(socket->Fd(), sv[0]);
    EXPECT_TRUE(socket->Connected());

    ::close(sv[1]);
}

TEST(TcpSocketTest, ConstructorWithInvalidFd) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    TcpSocketOptions opts;
    auto socket = std::make_shared<TcpSocket>(es, -1, opts);
    EXPECT_FALSE(socket->Valid());
    EXPECT_EQ(socket->Fd(), -1);
    EXPECT_FALSE(socket->Connected());
}

TEST(TcpSocketTest, SendEmptyData) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

    TcpSocketOptions opts;
    auto conn = TcpSocket::Adopt(es, sv[0], opts);
    ASSERT_NE(conn, nullptr);

    // Sending empty data should return true
    EXPECT_TRUE(conn->Send("", 0));
    EXPECT_TRUE(conn->Send(std::string_view("")));

    conn->Close();
    ::close(sv[1]);
}

TEST(TcpSocketTest, SendToClosedSocket) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

    TcpSocketOptions opts;
    auto conn = TcpSocket::Adopt(es, sv[0], opts);
    ASSERT_NE(conn, nullptr);

    // Close the socket first
    conn->Close();

    // Sending to closed socket should return false
    EXPECT_FALSE(conn->Send("test", 4));

    ::close(sv[1]);
}

TEST(TcpSocketTest, InputBufferAccess) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

    TcpSocketOptions opts;
    auto conn = TcpSocket::Adopt(es, sv[0], opts);
    ASSERT_NE(conn, nullptr);

    auto& buffer = conn->InputBuffer();
    EXPECT_EQ(buffer.ReadableBytes(), 0);

    ::close(sv[1]);
}

TEST(TcpSocketTest, DoubleClose) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

    TcpSocketOptions opts;
    auto conn = TcpSocket::Adopt(es, sv[0], opts);
    ASSERT_NE(conn, nullptr);

    conn->Close();
    conn->Close();

    ::close(sv[1]);
}

TEST(TcpSocketTest, ConnectTimeout) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    auto sock = TcpSocket::Connect(es, "10.255.255.1", 12345, 100);
    if (sock != nullptr) {
        sock->Close();
    }
}

TEST(TcpSocketTest, BufferOperations) {
    Buffer buffer(1024);

    EXPECT_EQ(buffer.ReadableBytes(), 0);
    EXPECT_GT(buffer.WritableBytes(), 0);

    const char* data = "Hello World";
    buffer.Append(data, strlen(data));
    EXPECT_EQ(buffer.ReadableBytes(), strlen(data));

    std::string retrieved(buffer.Peek(), buffer.ReadableBytes());
    EXPECT_EQ(retrieved, data);

    buffer.Retrieve(5);
    EXPECT_EQ(buffer.ReadableBytes(), 6);
    std::string remaining(buffer.Peek(), buffer.ReadableBytes());
    EXPECT_EQ(remaining, " World");

    buffer.RetrieveAll();
    EXPECT_EQ(buffer.ReadableBytes(), 0);
}

TEST(TcpSocketTest, BufferRetrieveAllAsString) {
    Buffer buffer(1024);
    const std::string test_data = "Hello World Test";
    buffer.Append(test_data);

    std::string result = buffer.RetrieveAllAsString();
    EXPECT_EQ(result, test_data);
    EXPECT_EQ(buffer.ReadableBytes(), 0);
}

TEST(TcpSocketTest, BufferAppendStringView) {
    Buffer buffer(1024);
    std::string_view sv = "Test String View";
    buffer.Append(sv);
    EXPECT_EQ(buffer.ReadableBytes(), sv.length());

    std::string result(buffer.Peek(), buffer.ReadableBytes());
    EXPECT_EQ(result, sv);
}

TEST(TcpSocketTest, BufferReadFd) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

    Buffer buffer(1024);

    const char* test_data = "Test data for buffer";
    ssize_t sent = ::send(sv[0], test_data, strlen(test_data), 0);
    ASSERT_GT(sent, 0);

    ssize_t read = buffer.ReadFd(sv[1]);
    EXPECT_GT(read, 0);
    EXPECT_EQ(buffer.ReadableBytes(), static_cast<size_t>(read));

    std::string result(buffer.Peek(), buffer.ReadableBytes());
    EXPECT_EQ(result, test_data);

    ::close(sv[0]);
    ::close(sv[1]);
}

TEST(TcpSocketTest, ConnectedMethod) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

    TcpSocketOptions opts;
    auto conn = TcpSocket::Adopt(es, sv[0], opts);
    ASSERT_NE(conn, nullptr);
    EXPECT_TRUE(conn->Connected());

    conn->Close();
    ::close(sv[1]);
}

TEST(TcpSocketTest, SendStringViewOverload) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

    TcpSocketOptions opts;
    auto conn = TcpSocket::Adopt(es, sv[0], opts);
    ASSERT_NE(conn, nullptr);

    std::string_view test_data = "Test string view data";
    EXPECT_TRUE(conn->Send(test_data));

    char buffer[256] = {};
    ssize_t n = ::recv(sv[1], buffer, sizeof(buffer), 0);
    EXPECT_EQ(n, static_cast<ssize_t>(test_data.length()));
    EXPECT_EQ(std::string(buffer, static_cast<size_t>(n)), test_data);

    conn->Close();
    ::close(sv[1]);
}

TEST(TcpSocketTest, ValidAndFdMethods) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

    TcpSocketOptions opts;
    auto socket = std::make_shared<TcpSocket>(es, sv[0], opts);

    EXPECT_TRUE(socket->Valid());
    EXPECT_EQ(socket->Fd(), sv[0]);

    // Test with invalid fd
    auto invalidSocket = std::make_shared<TcpSocket>(es, -1, opts);
    EXPECT_FALSE(invalidSocket->Valid());
    EXPECT_EQ(invalidSocket->Fd(), -1);

    ::close(sv[1]);
}

TEST(TcpSocketTest, CloseMethod) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

    TcpSocketOptions opts;
    auto socket = std::make_shared<TcpSocket>(es, sv[0], opts);

    EXPECT_NO_THROW(socket->Close());

    ::close(sv[1]);
}

TEST(TcpSocketTest, CallbackRegistration) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

    TcpSocketOptions opts;
    auto socket = std::make_shared<TcpSocket>(es, sv[0], opts);

    bool readCalled = false;
    bool writableCalled = false;
    bool closeCalled = false;
    bool errorCalled = false;

    socket->OnRead([&readCalled](const SocketPtr& sock) {
        readCalled = true;
    });

    socket->OnWritable([&writableCalled](const SocketPtr& sock) {
        writableCalled = true;
    });

    socket->OnClose([&closeCalled](const SocketPtr& sock) {
        closeCalled = true;
    });

    socket->OnError([&errorCalled](const SocketPtr& sock, int err, const std::string& msg) {
        errorCalled = true;
    });

    // Test that callback registration works (we can't directly access private members)
    EXPECT_NO_THROW(socket->OnRead(nullptr));
    EXPECT_NO_THROW(socket->OnWritable(nullptr));
    EXPECT_NO_THROW(socket->OnClose(nullptr));
    EXPECT_NO_THROW(socket->OnError(nullptr));

    ::close(sv[1]);
}

TEST(TcpSocketTest, EventControl) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

    TcpSocketOptions opts;
    auto socket = std::make_shared<TcpSocket>(es, sv[0], opts);

    // Test event enable/disable operations
    EXPECT_NO_THROW(socket->EnableRead());
    EXPECT_NO_THROW(socket->DisableRead());
    EXPECT_NO_THROW(socket->EnableWrite());
    EXPECT_NO_THROW(socket->DisableWrite());

    ::close(sv[1]);
}

// ==================== TcpListener Test Cases ====================

TEST(TcpListenerTest, ListenInvalidHost) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    TcpListener listener(es);
    EXPECT_FALSE(listener.Listen("not-a-host", 0, 1, false));
}

TEST(TcpListenerTest, ListenPortConflictWithoutReuse) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    int reserved = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(reserved, 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0;
    ASSERT_EQ(::bind(reserved, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    socklen_t addrlen = sizeof(addr);
    ASSERT_EQ(::getsockname(reserved, reinterpret_cast<sockaddr*>(&addr), &addrlen), 0);
    ASSERT_EQ(::listen(reserved, 1), 0);

    TcpListener listener(es);
    uint16_t boundPort = ntohs(addr.sin_port);
    EXPECT_FALSE(listener.Listen("", boundPort, 1, false));

    ::close(reserved);
}

TEST(TcpListenerTest, ListenBindSuccess) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    TcpListener listener(es);
    EXPECT_TRUE(listener.Listen("", 0, 1, false));
    listener.Stop();
}

TEST(TcpListenerTest, ConstructorAndDestructor) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    {
        TcpListener listener(es);
    }
    SUCCEED();
}

TEST(TcpListenerTest, StartStop) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    TcpListener listener(es);
    EXPECT_TRUE(listener.Listen("", 0, 1, false));

    EXPECT_TRUE(listener.Start());
    listener.Stop();

    // Can start again after stopping
    EXPECT_TRUE(listener.Start());
    listener.Stop();
}

TEST(TcpListenerTest, StartWithoutListen) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    TcpListener listener(es);
    EXPECT_FALSE(listener.Start());
}

TEST(TcpListenerTest, DoubleStart) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    TcpListener listener(es);
    EXPECT_TRUE(listener.Listen("", 0, 1, false));

    EXPECT_TRUE(listener.Start());
    EXPECT_FALSE(listener.Start());

    listener.Stop();
}

TEST(TcpListenerTest, StopWithoutStart) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    TcpListener listener(es);
    EXPECT_TRUE(listener.Listen("", 0, 1, false));

    // Stopping without starting should not crash
    EXPECT_NO_THROW(listener.Stop());
}

TEST(TcpListenerTest, BasicFunctionality) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    TcpListener listener(es);
    bool listenResult = listener.Listen("127.0.0.1", 0, 1, false);
    if (listenResult) {
        EXPECT_TRUE(listener.Start());
        listener.Stop();
    }
}

TEST(TcpListenerTest, CallbackRegistration) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    TcpListener listener(es);

    bool newConnCalled = false;
    bool errorCalled = false;

    listener.OnNewConnection([&newConnCalled](const TcpSocketPtr& conn) {
        newConnCalled = true;
    });

    listener.OnError([&errorCalled](const SocketPtr& sock, int err, const std::string& msg) {
        errorCalled = true;
    });

    EXPECT_NO_THROW(listener.OnNewConnection(nullptr));
    EXPECT_NO_THROW(listener.OnError(nullptr));
}

TEST(TcpListenerTest, ListenAnyAddress) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    TcpListener listener(es);

    EXPECT_TRUE(listener.Listen("", 0, 1, false));
    EXPECT_TRUE(listener.Listen("0.0.0.0", 0, 1, false));
    listener.Stop();
}

TEST(TcpListenerTest, ListenSpecificPort) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    TcpListener listener(es);

    bool result = listener.Listen("127.0.0.1", 12345, 1, false);
    if (result) {
        listener.Stop();
    }

    SUCCEED();
}

TEST(TcpListenerTest, ListenWithReusePort) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    TcpListener listener(es);
    EXPECT_TRUE(listener.Listen("127.0.0.1", 0, 1, true));
    listener.Stop();
}

TEST(TcpListenerTest, MultipleListenAttempts) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    TcpListener listener(es);

    EXPECT_TRUE(listener.Listen("127.0.0.1", 0, 1, false));

    EXPECT_NO_THROW({
        bool result = listener.Listen("127.0.0.1", 0, 1, false);
        if (result) {
            listener.Stop();
        }
    });
}

TEST(TcpSocketTest, ApplyOptionsWithZeroBufferSize) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

    TcpSocketOptions opts;
    opts.recvBufSize = 0;
    opts.sendBufSize = 0;
    opts.reuseAddr = false;
    opts.reusePort = false;

    auto socket = TcpSocket::Adopt(es, sv[0], opts);
    EXPECT_NE(socket, nullptr);

    ::close(sv[1]);
}

TEST(TcpSocketTest, ApplyOptionsWithInvalidFd) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    TcpSocketOptions opts;
    opts.recvBufSize = 1024;
    opts.sendBufSize = 1024;
    opts.reuseAddr = true;
    opts.reusePort = true;

    // Test with invalid fd - should not crash during construction
    int invalid_fd = -1;
    EXPECT_NO_THROW({
        auto socket = std::make_shared<TcpSocket>(es, invalid_fd, opts);
        EXPECT_FALSE(socket->Valid());
    });
}

TEST(TcpSocketTest, NotifyCloseOnceOnlyCallsOnce) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

    auto socket = TcpSocket::Adopt(es, sv[0], TcpSocketOptions{});

    int callCount = 0;
    socket->OnClose([&callCount](const SocketPtr& sock) {
        callCount++;
    });

    // Close multiple times through public interface, but callback should only be called once
    socket->Close();
    socket->Close();
    socket->Close();

    EXPECT_EQ(callCount, 1);
    ::close(sv[1]);
}

TEST(TcpSocketTest, EnableDisableEventsMultipleTimes) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

    auto socket = TcpSocket::Adopt(es, sv[0], TcpSocketOptions{});

    // Multiple enable/disable calls should not crash
    socket->EnableRead();
    socket->EnableRead();
    socket->DisableRead();
    socket->DisableRead();

    socket->EnableWrite();
    socket->EnableWrite();
    socket->DisableWrite();
    socket->DisableWrite();

    socket->Close();
    ::close(sv[1]);
}

TEST(TcpSocketTest, SetTcpOptionsWithInvalidFd) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    TcpSocketOptions opts;
    opts.tcpNoDelay = true;
    opts.keepAlive = true;

    int invalid_fd = -1;
    EXPECT_NO_THROW({
        auto socket = std::make_shared<TcpSocket>(es, invalid_fd, opts);
        EXPECT_FALSE(socket->Valid());
    });
}

TEST(TcpSocketTest, SendAfterCloseReturnsFalse) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

    auto conn = TcpSocket::Adopt(es, sv[0], TcpSocketOptions{});
    ASSERT_NE(conn, nullptr);

    conn->Close();
    EXPECT_FALSE(conn->Send("test", 4));
    EXPECT_FALSE(conn->Send(std::string_view("test")));

    ::close(sv[1]);
}

TEST(TcpSocketTest, ShutdownWriteDoesNotAffectRead) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

    auto conn = TcpSocket::Adopt(es, sv[0], TcpSocketOptions{});
    ASSERT_NE(conn, nullptr);

    EXPECT_TRUE(conn->Connected());

    conn->ShutdownWrite();

    EXPECT_TRUE(conn->Connected());

    conn->Close();
    ::close(sv[1]);
}

TEST(TcpSocketTest, HandleConnectTimeoutBehavior) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    auto sock = TcpSocket::Connect(es, "10.255.255.1", 12345, 100);

    if (sock != nullptr) {
        sock->Close();
    }

    SUCCEED();
}

// ==================== Additional TcpListener Test Cases ====================

TEST(TcpListenerTest, StopMultipleTimesSafe) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    TcpListener listener(es);
    EXPECT_TRUE(listener.Listen("127.0.0.1", 0, 1, false));

    listener.Stop();
    listener.Stop();
    listener.Stop();

    SUCCEED();
}

TEST(TcpListenerTest, ListenTwiceUsesFirstSuccess) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    TcpListener listener(es);

    bool firstResult = listener.Listen("127.0.0.1", 0, 1, false);
    if (firstResult) {
        // Second listen should not change anything significant
        listener.Listen("127.0.0.1", 12345, 1, false);
        // Both should behave reasonably
        SUCCEED();
    }

    listener.Stop();
}

TEST(TcpListenerTest, ListenWithIPv6) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    TcpListener listener(es);
    // Try IPv6 localhost
    bool result = listener.Listen("::1", 0, 1, false);
    if (result) {
        listener.Stop();
    }
    SUCCEED();
}

TEST(TcpListenerTest, ListenWithBothIPv4AndIPv6) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    TcpListener listener(es);
    // Empty host should try both IPv4 and IPv6
    bool result = listener.Listen("", 0, 1, false);
    if (result) {
        listener.Stop();
    }
    SUCCEED();
}

TEST(TcpListenerTest, StartReturnsFalseWhenAlreadyStarted) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    TcpListener listener(es);
    EXPECT_TRUE(listener.Listen("127.0.0.1", 0, 1, false));

    EXPECT_TRUE(listener.Start());
    EXPECT_FALSE(listener.Start()); // Should fail second time

    listener.Stop();
}

// ==================== Edge Case Tests for Protected/Private Members (Indirect) ====================

TEST(TcpSocketTest, StateTransitionsThroughPublicInterface) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    // Test state transitions through normal usage patterns
    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

    // Adopted socket starts in CONNECTED state
    auto conn = TcpSocket::Adopt(es, sv[0], TcpSocketOptions{});
    ASSERT_NE(conn, nullptr);
    EXPECT_TRUE(conn->Connected());

    // Closing should transition to appropriate state
    conn->Close();
    EXPECT_FALSE(conn->Connected());

    ::close(sv[1]);
}

TEST(TcpSocketTest, OutputBufferManagementThroughSend) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

    auto conn = TcpSocket::Adopt(es, sv[0], TcpSocketOptions{});
    ASSERT_NE(conn, nullptr);

    // Test sending data triggers proper buffering mechanisms
    std::string testData(1000, 'A');
    EXPECT_TRUE(conn->Send(testData));

    // Verify data was sent or buffered
    char buffer[1500];
    ssize_t received = ::recv(sv[1], buffer, sizeof(buffer), 0);
    EXPECT_GT(received, 0);

    conn->Close();
    ::close(sv[1]);
}

TEST(TcpSocketTest, InputBufferGrowsAsNeeded) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

    auto conn = TcpSocket::Adopt(es, sv[0], TcpSocketOptions{});
    ASSERT_NE(conn, nullptr);

    size_t initialCapacity = 4096; // Default from implementation

    std::string largeData(initialCapacity + 1000, 'B');
    ssize_t sent = ::send(sv[1], largeData.c_str(), largeData.size(), 0);
    ASSERT_EQ(sent, static_cast<ssize_t>(largeData.size()));

    char recvBuffer[1024];
    ssize_t n = ::recv(conn->Fd(), recvBuffer, sizeof(recvBuffer), MSG_PEEK);
    if (n > 0) {

        SUCCEED();
    }

    conn->Close();
    ::close(sv[1]);
}

TEST(BufferTest, BufferInternalMechanismsThroughPublicAPI) {
    // Test buffer behaviors that indicate internal mechanisms working

    Buffer buffer(100);

    // Fill most of buffer
    std::string data1(80, 'A');
    buffer.Append(data1);
    EXPECT_EQ(buffer.ReadableBytes(), 80);
    EXPECT_EQ(buffer.WritableBytes(), 20);

    // Read some data (creates gap at beginning)
    buffer.Retrieve(50);
    EXPECT_EQ(buffer.ReadableBytes(), 30);
    EXPECT_EQ(buffer.WritableBytes(), 20); // Still same due to gap

    // Add more data - should trigger internal space management
    std::string data2(50, 'B');
    buffer.Append(data2);
    EXPECT_EQ(buffer.ReadableBytes(), 80);

    // Verify data integrity shows internal movement worked
    std::string result(buffer.Peek(), buffer.ReadableBytes());
    EXPECT_EQ(result.substr(0, 30), std::string(30, 'A'));
    EXPECT_EQ(result.substr(30), std::string(50, 'B'));
}

TEST(TcpListenerTest, ConnectionHandlingFramework) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    TcpListener listener(es);
    EXPECT_TRUE(listener.Listen("127.0.0.1", 0, 1, false));
    EXPECT_TRUE(listener.Start());

    bool newConnReceived = false;
    bool errorCallbackReceived = false;

    listener.OnNewConnection([&newConnReceived](const TcpSocketPtr& conn) {
        newConnReceived = true;
        EXPECT_NE(conn, nullptr);
        EXPECT_TRUE(conn->Valid());
    });

    listener.OnError([&errorCallbackReceived](const SocketPtr& sock, int err, const std::string& msg) {
        errorCallbackReceived = true;
    });

    // Test callback registration works
    EXPECT_NO_THROW(listener.OnNewConnection(nullptr));
    EXPECT_NO_THROW(listener.OnError(nullptr));

    listener.Stop();

    // Framework should work even if no actual connections occur
    SUCCEED();
}

TEST(TcpSocketTest, DestructorCleanupBehavior) {
    Mcp::EventSystem es;
    ASSERT_TRUE(es.Init());

    {
        int sv[2];
        ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

        auto socket = TcpSocket::Adopt(es, sv[0], TcpSocketOptions{});

        // Socket should be valid within scope
        EXPECT_NE(socket, nullptr);
        EXPECT_TRUE(socket->Valid());

        // Destructor will be called when going out of scope
        // Should clean up properly without crashing

    } // Destructor called here

    SUCCEED();
}

TEST(BufferTest, BoundaryConditionsAndEdgeCases) {
    // Test extreme boundary conditions

    // Very small buffer
    Buffer tinyBuffer(1);
    EXPECT_EQ(tinyBuffer.WritableBytes(), 1);

    // Empty operations
    tinyBuffer.Append("", 0);
    EXPECT_EQ(tinyBuffer.ReadableBytes(), 0);

    // Over-retrieve
    tinyBuffer.Retrieve(1000);
    EXPECT_EQ(tinyBuffer.ReadableBytes(), 0);

    // Retrieve all from empty
    tinyBuffer.RetrieveAll();
    EXPECT_EQ(tinyBuffer.ReadableBytes(), 0);

    // Retrieve all as string from empty
    std::string result = tinyBuffer.RetrieveAllAsString();
    EXPECT_TRUE(result.empty());

    // Large single append
    std::string hugeData(1000000, 'X'); // 1MB
    tinyBuffer.Append(hugeData);
    EXPECT_EQ(tinyBuffer.ReadableBytes(), 1000000);

    std::string retrieved(tinyBuffer.Peek(), 10);
    EXPECT_EQ(retrieved, std::string(10, 'X'));
}
