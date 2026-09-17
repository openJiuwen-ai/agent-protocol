/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

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
#include <thread>
#include <chrono>

#include "net/socket.h"
#include "event_system.h"

namespace A2A::Server::Test {

using namespace A2A::Server;
using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;

class TestableSocket : public Socket {
public:
    TestableSocket(EventSystem& es, int fd, const SocketOptions& opts)
        : Socket(es, fd, opts)
    {}

    // 实现纯虚函数
    bool Send(const char* data, size_t len) override
    {
        // 简单的测试实现
        if (fd_ < 0) {
            return false;
        }
        ssize_t ret = ::write(fd_, data, len);
        return ret == static_cast<ssize_t>(len);
    }

    // 暴露 protected 方法供测试
    void PublicHandleReadable()
    {
        HandleReadable();
    }

    void PublicHandleWritable()
    {
        HandleWritable();
    }

    void PublicNotifyError(int err, const char* where)
    {
        NotifyError(err, where);
    }

    void PublicNotifyCloseOnce()
    {
        NotifyCloseOnce();
    }

    // 获取内部状态用于验证
    int GetReadEventId() const
    {
        return evReadId_;
    }
    int GetWriteEventId() const
    {
        return evWriteId_;
    }
    bool HasReadCallback() const
    {
        return onRead_ != nullptr;
    }
    bool HasWriteCallback() const
    {
        return onWritable_ != nullptr;
    }
    bool HasCloseCallback() const
    {
        return onClose_ != nullptr;
    }
    bool HasErrorCallback() const
    {
        return onError_ != nullptr;
    }
};

class SocketTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // 初始化 EventSystem
        ASSERT_TRUE(es.Init());

        // 创建 socket pair 用于测试
        int fds[2] = {-1, -1};
        ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
        fd1_ = fds[0];
        fd2_ = fds[1];

        // 设置非阻塞模式
        SetNonBlocking(fd1_);
        SetNonBlocking(fd2_);
    }

    void TearDown() override
    {
        // 先停止事件循环，确保所有事件被清理
        es.Stop();

        // 给系统一点时间处理待完成的事件
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // 关闭所有文件描述符
        CloseFd(fd1_);
        CloseFd(fd2_);

        // 清理所有持有的 socket 对象
        sockets_.clear();
    }

    // 辅助方法：安全关闭文件描述符
    void CloseFd(int& fd)
    {
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }

    // 辅助方法：设置非阻塞
    void SetNonBlocking(int fd)
    {
        if (fd < 0) {
            return;
        }
        int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags >= 0) {
            ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }
    }

    // 辅助方法：创建测试 socket
    std::shared_ptr<TestableSocket> CreateTestSocket(int fd, const SocketOptions& opts = SocketOptions())
    {
        auto socket = std::make_shared<TestableSocket>(es, fd, opts);
        sockets_.push_back(socket);  // 保存到 vector 中便于清理
        return socket;
    }

    // 辅助方法：创建带默认选项的测试 socket
    std::shared_ptr<TestableSocket> CreateDefaultTestSocket(int fd)
    {
        return CreateTestSocket(fd, CreateDefaultOptions());
    }

    EventSystem es{true, 0};
    int fd1_ = -1;
    int fd2_ = -1;

    // 保存所有创建的 socket 对象，便于统一清理
    std::vector<std::shared_ptr<TestableSocket>> sockets_;

    SocketOptions CreateDefaultOptions()
    {
        SocketOptions opts;
        opts.nonBlocking = true;
        opts.recvBufSize = 4096;
        opts.sendBufSize = 4096;
        opts.reusePort = false;
        return opts;
    }

    // 等待条件变量或超时
    template<typename Predicate>
    bool WaitForCondition(Predicate pred, int timeoutMs = 100)
    {
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start).count() < timeoutMs) {
            if (pred()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return pred();
    }
};

// ===========================================================================
// 构造函数测试
// ===========================================================================
TEST_F(SocketTest, ConstructorWithValidFd)
{
    auto opts = CreateDefaultOptions();
    int originalFd = fd1_;  // 保存原始值
    auto socket = CreateTestSocket(fd1_, opts);
    fd1_ = -1;  // 标记为已转移

    EXPECT_EQ(socket->Fd(), originalFd);
    EXPECT_TRUE(socket->Valid());
}

TEST_F(SocketTest, ConstructorWithInvalidFd)
{
    auto opts = CreateDefaultOptions();
    auto socket = CreateTestSocket(-1, opts);

    EXPECT_EQ(socket->Fd(), -1);
    EXPECT_FALSE(socket->Valid());
}

// ===========================================================================
// SetNonBlocking 测试
// ===========================================================================
TEST_F(SocketTest, SetNonBlockingSuccess)
{
    int fd = ::dup(fd1_);
    ASSERT_GE(fd, 0);

    // 先清除非阻塞标志
    int flags = ::fcntl(fd, F_GETFL, 0);
    ASSERT_GE(flags, 0);
    ::fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    EXPECT_TRUE(TestableSocket::SetNonBlocking(fd));

    flags = ::fcntl(fd, F_GETFL, 0);
    EXPECT_TRUE((flags & O_NONBLOCK) != 0);

    ::close(fd);
}

TEST_F(SocketTest, SetNonBlockingWithInvalidFd)
{
    EXPECT_FALSE(TestableSocket::SetNonBlocking(-1));
}

// ===========================================================================
// ApplyOptions 测试
// ===========================================================================
TEST_F(SocketTest, ApplyOptionsNonBlocking)
{
    auto opts = CreateDefaultOptions();
    opts.nonBlocking = true;

    int fd = ::dup(fd1_);
    ASSERT_GE(fd, 0);

    // 先清除非阻塞标志
    int flags = ::fcntl(fd, F_GETFL, 0);
    ASSERT_GE(flags, 0);
    ::fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    TestableSocket socket(es, fd, opts);

    flags = ::fcntl(fd, F_GETFL, 0);
    EXPECT_TRUE((flags & O_NONBLOCK) != 0);
}

TEST_F(SocketTest, ApplyOptionsRecvBufSize)
{
    auto opts = CreateDefaultOptions();
    opts.recvBufSize = 8192;

    int fd = ::dup(fd1_);
    ASSERT_GE(fd, 0);

    TestableSocket socket(es, fd, opts);

    int actualSize = 0;
    socklen_t len = sizeof(actualSize);
    EXPECT_EQ(::getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &actualSize, &len), 0);
    EXPECT_GE(actualSize, opts.recvBufSize);
}

TEST_F(SocketTest, ApplyOptionsSendBufSize)
{
    auto opts = CreateDefaultOptions();
    opts.sendBufSize = 8192;

    int fd = ::dup(fd1_);
    ASSERT_GE(fd, 0);

    TestableSocket socket(es, fd, opts);

    int actualSize = 0;
    socklen_t len = sizeof(actualSize);
    EXPECT_EQ(::getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &actualSize, &len), 0);
    EXPECT_GE(actualSize, opts.sendBufSize);
}

TEST_F(SocketTest, ApplyOptionsReusePort)
{
    auto opts = CreateDefaultOptions();
    opts.reusePort = true;

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(fd, 0);

    TestableSocket socket(es, fd, opts);

    int reuse = 0;
    socklen_t len = sizeof(reuse);
    EXPECT_EQ(::getsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &reuse, &len), 0);
    EXPECT_EQ(reuse, 1);
}

// ===========================================================================
// Send 方法测试
// ===========================================================================
TEST_F(SocketTest, SendSuccess)
{
    auto socket = std::make_shared<TestableSocket>(es, fd1_, CreateDefaultOptions());
    fd1_ = -1;

    const char* data = "hello";
    EXPECT_TRUE(socket->Send(data, strlen(data)));

    char buf[16] = {0};
    ssize_t n = ::read(fd2_, buf, sizeof(buf));
    EXPECT_EQ(n, static_cast<ssize_t>(strlen(data)));
    EXPECT_STREQ(buf, data);
}

TEST_F(SocketTest, SendWithInvalidFd)
{
    auto socket = std::make_shared<TestableSocket>(es, -1, CreateDefaultOptions());

    const char* data = "hello";
    EXPECT_FALSE(socket->Send(data, strlen(data)));
}

// ===========================================================================
// 回调注册测试
// ===========================================================================
TEST_F(SocketTest, OnReadCallback)
{
    auto socket = std::make_shared<TestableSocket>(es, fd1_, CreateDefaultOptions());
    fd1_ = -1;

    bool called = false;
    socket->OnRead([&called](std::shared_ptr<Socket>) {
        called = true;
    });

    socket->EnableRead();

    // 启动事件循环
    std::thread t([this]() { es.Start(false); });

    // 写入数据触发读事件
    const char* data = "test";
    ASSERT_EQ(::write(fd2_, data, strlen(data)), static_cast<ssize_t>(strlen(data)));

    EXPECT_TRUE(WaitForCondition([&]() { return called; }, 100));

    es.Stop();
    t.join();
}

TEST_F(SocketTest, OnWritableCallback)
{
    auto socket = std::make_shared<TestableSocket>(es, fd1_, CreateDefaultOptions());
    fd1_ = -1;

    bool called = false;
    socket->OnWritable([&called](std::shared_ptr<Socket>) {
        called = true;
    });

    socket->EnableWrite();

    std::thread t([this]() { es.Start(false); });

    EXPECT_TRUE(WaitForCondition([&]() { return called; }, 100));

    es.Stop();
    t.join();
}

TEST_F(SocketTest, OnCloseCallback)
{
    auto socket = std::make_shared<TestableSocket>(es, fd1_, CreateDefaultOptions());
    fd1_ = -1;

    bool called = false;
    socket->OnClose([&called](std::shared_ptr<Socket>) {
        called = true;
    });

    socket->Close();

    EXPECT_TRUE(called);
    EXPECT_FALSE(socket->Valid());
}

TEST_F(SocketTest, OnErrorCallback)
{
    auto socket = std::make_shared<TestableSocket>(es, fd1_, CreateDefaultOptions());
    fd1_ = -1;

    bool called = false;
    int capturedErr = 0;
    std::string capturedMsg;

    socket->OnError([&](std::shared_ptr<Socket>, int err, const std::string& msg) {
        called = true;
        capturedErr = err;
        capturedMsg = msg;
    });

    socket->PublicNotifyError(EINVAL, "test error");

    EXPECT_TRUE(called);
    EXPECT_EQ(capturedErr, EINVAL);
    EXPECT_TRUE(capturedMsg.find("test error") != std::string::npos);
}

// ===========================================================================
// 事件启用/禁用测试
// ===========================================================================
TEST_F(SocketTest, EnableDisableRead)
{
    auto socket = CreateDefaultTestSocket(fd1_);
    fd1_ = -1;  // 所有权转移

    std::atomic<int> readCount{0};
    char buffer[1024];

    // 使用 weak_ptr 避免循环引用
    std::weak_ptr<TestableSocket> weakSocket = socket;

    socket->OnRead([weakSocket, &readCount, &buffer](std::shared_ptr<Socket>) {
        auto sock = weakSocket.lock();
        if (!sock) return;

        // 使用 ssize_t 避免警告
        ssize_t n = read(sock->Fd(), buffer, sizeof(buffer));
        if (n > 0) {
            readCount++;
        }
    });

    socket->EnableRead();
    EXPECT_NE(socket->GetReadEventId(), 0);

    // 启动事件循环
    std::thread t([this]() { es.Start(false); });

    // 写入数据
    const char* data = "test";
    ASSERT_EQ(::write(fd2_, data, strlen(data)), static_cast<ssize_t>(strlen(data)));

    EXPECT_TRUE(WaitForCondition([&]() { return readCount >= 1; }, 100));

    // 禁用读事件
    socket->DisableRead();
    EXPECT_EQ(socket->GetReadEventId(), 0);

    es.Stop();
    t.join();

    // socket 会在 TearDown 中被自动清理
}

TEST_F(SocketTest, EnableDisableWrite)
{
    auto socket = std::make_shared<TestableSocket>(es, fd1_, CreateDefaultOptions());
    fd1_ = -1;

    int writeCount = 0;
    socket->OnWritable([&writeCount](std::shared_ptr<Socket>) {
        writeCount++;
    });

    socket->EnableWrite();
    EXPECT_NE(socket->GetWriteEventId(), 0);

    std::thread t([this]() { es.Start(false); });

    EXPECT_TRUE(WaitForCondition([&]() { return writeCount >= 1; }, 100));

    socket->DisableWrite();
    EXPECT_EQ(socket->GetWriteEventId(), 0);
    int currentCount = writeCount;

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(writeCount, currentCount);  // 不应该增加

    es.Stop();
    t.join();
}

// ===========================================================================
// Close 测试
// ===========================================================================
TEST_F(SocketTest, CloseWithEvents)
{
    auto socket = std::make_shared<TestableSocket>(es, fd1_, CreateDefaultOptions());
    fd1_ = -1;

    socket->EnableRead();
    socket->EnableWrite();

    bool closeCalled = false;
    socket->OnClose([&closeCalled](std::shared_ptr<Socket>) {
        closeCalled = true;
    });

    socket->Close();

    EXPECT_TRUE(closeCalled);
    EXPECT_FALSE(socket->Valid());
    EXPECT_EQ(socket->Fd(), -1);
    EXPECT_EQ(socket->GetReadEventId(), 0);
    EXPECT_EQ(socket->GetWriteEventId(), 0);

    // 再次关闭应该无效果
    EXPECT_NO_THROW(socket->Close());
}

// ===========================================================================
// HandleReadable 测试
// ===========================================================================
TEST_F(SocketTest, HandleReadableWithData)
{
    auto socket = std::make_shared<TestableSocket>(es, fd1_, CreateDefaultOptions());
    fd1_ = -1;

    bool readCalled = false;
    socket->OnRead([&readCalled](std::shared_ptr<Socket>) {
        readCalled = true;
    });

    socket->EnableRead();

    // 启动事件循环
    std::thread t([this]() { es.Start(false); });

    // 写入数据触发读事件
    const char* data = "readable";
    ASSERT_EQ(::write(fd2_, data, strlen(data)), static_cast<ssize_t>(strlen(data)));

    // 等待事件被处理
    EXPECT_TRUE(WaitForCondition([&]() { return readCalled; }, 100));

    es.Stop();
    t.join();
}

TEST_F(SocketTest, HandleReadableWithoutCallback)
{
    auto socket = std::make_shared<TestableSocket>(es, fd1_, CreateDefaultOptions());
    fd1_ = -1;

    // 没有注册回调，不应该崩溃
    EXPECT_NO_THROW(socket->PublicHandleReadable());
}

// ===========================================================================
// HandleWritable 测试
// ===========================================================================
TEST_F(SocketTest, HandleWritableWithCallback)
{
    auto socket = std::make_shared<TestableSocket>(es, fd1_, CreateDefaultOptions());
    fd1_ = -1;

    bool writeCalled = false;
    socket->OnWritable([&writeCalled](std::shared_ptr<Socket>) {
        writeCalled = true;
    });

    socket->EnableWrite();

    // 启动事件循环
    std::thread t([this]() { es.Start(false); });

    // 等待写事件被触发（通常立即触发）
    EXPECT_TRUE(WaitForCondition([&]() { return writeCalled; }, 100));

    es.Stop();
    t.join();
}

TEST_F(SocketTest, HandleWritableWithoutCallback)
{
    auto socket = std::make_shared<TestableSocket>(es, fd1_, CreateDefaultOptions());
    fd1_ = -1;

    EXPECT_NO_THROW(socket->PublicHandleWritable());
}

// ===========================================================================
// NotifyCloseOnce 测试
// ===========================================================================
TEST_F(SocketTest, NotifyCloseOnce)
{
    auto socket = std::make_shared<TestableSocket>(es, fd1_, CreateDefaultOptions());
    fd1_ = -1;

    int callCount = 0;
    socket->OnClose([&callCount](std::shared_ptr<Socket>) {
        callCount++;
    });

    socket->PublicNotifyCloseOnce();
    socket->PublicNotifyCloseOnce();
    socket->PublicNotifyCloseOnce();

    EXPECT_EQ(callCount, 1);
}

// ===========================================================================
// NotifyError 测试
// ===========================================================================
TEST_F(SocketTest, NotifyErrorWithoutCallback)
{
    auto socket = std::make_shared<TestableSocket>(es, fd1_, CreateDefaultOptions());
    fd1_ = -1;

    EXPECT_NO_THROW(socket->PublicNotifyError(ECONNRESET, "test error"));
}

// ===========================================================================
// 边界条件测试
// ===========================================================================
TEST_F(SocketTest, EnableReadWithInvalidFd)
{
    auto socket = std::make_shared<TestableSocket>(es, -1, CreateDefaultOptions());

    // 不应该崩溃
    socket->EnableRead();
    EXPECT_EQ(socket->GetReadEventId(), 0);
}

TEST_F(SocketTest, EnableWriteWithInvalidFd)
{
    auto socket = std::make_shared<TestableSocket>(es, -1, CreateDefaultOptions());

    socket->EnableWrite();
    EXPECT_EQ(socket->GetWriteEventId(), 0);
}

TEST_F(SocketTest, DisableReadWithNoEvent)
{
    auto socket = std::make_shared<TestableSocket>(es, fd1_, CreateDefaultOptions());
    fd1_ = -1;

    socket->DisableRead();  // 没有启用，不应该崩溃
}

TEST_F(SocketTest, DisableWriteWithNoEvent)
{
    auto socket = std::make_shared<TestableSocket>(es, fd1_, CreateDefaultOptions());
    fd1_ = -1;

    socket->DisableWrite();
}

// ===========================================================================
// 多线程并发测试
// ===========================================================================
TEST_F(SocketTest, ConcurrentEnableDisable)
{
    auto socket = std::make_shared<TestableSocket>(es, fd1_, CreateDefaultOptions());
    fd1_ = -1;

    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};

    for (int i = 0; i < 4; i++) {
        threads.emplace_back([socket, &successCount]() {
            for (int j = 0; j < 10; j++) {
                socket->EnableRead();
                socket->DisableRead();
                socket->EnableWrite();
                socket->DisableWrite();
                successCount++;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(successCount.load(), 40);
}

} // namespace A2A::Server::Test