/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include "test_network.h"
#include "net/tcp_listener.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <condition_variable>

#include "event_system.h"
#include "net/tcp_socket.h"

using namespace A2A;
using namespace A2A::Server;

namespace {

class TcpListenerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        eventSystem_ = std::make_unique<EventSystem>(false, 0);
        ASSERT_TRUE(eventSystem_->Init());

        listener_ = std::make_unique<TcpListener>(*eventSystem_);
        testFds_.clear();
        testPort_ = 0;
    }

    void TearDown() override
    {
        listener_.reset();
        if (eventSystem_) {
            eventSystem_->Stop();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            eventSystem_.reset();
        }

        for (int fd : testFds_) {
            if (fd >= 0) close(fd);
        }
        testFds_.clear();
    }

    bool IsPortListening(uint16_t port)
    {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            return false;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port);

        int ret = connect(fd, (sockaddr*)&addr, sizeof(addr));
        close(fd);
        return ret == 0;
    }

    int ConnectToListener(uint16_t port)
    {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            return -1;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port);

        int ret = connect(fd, (sockaddr*)&addr, sizeof(addr));
        if (ret < 0) {
            close(fd);
            return -1;
        }

        testFds_.push_back(fd);
        return fd;
    }

    std::unique_ptr<EventSystem> eventSystem_;
    std::unique_ptr<TcpListener> listener_;
    std::vector<int> testFds_;
    uint16_t testPort_;
};

// 基础测试：接受连接
TEST_F(TcpListenerTest, OnNewConnectionCallbackAccepts)
{
    testPort_ = A2A::Test::GetFreeTcpPort();
    ASSERT_GT(testPort_, 0);

    std::atomic<bool> newConnectionCalled{false};
    std::shared_ptr<Socket> acceptedSocket;

    listener_->OnNewConnection([&](std::shared_ptr<Socket> socket) {
        newConnectionCalled = true;
        acceptedSocket = socket;
    });

    EXPECT_TRUE(listener_->Listen("127.0.0.1", testPort_, 5, false));
    EXPECT_TRUE(listener_->Start());

    eventSystem_->Start(true);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ASSERT_TRUE(IsPortListening(testPort_));

    int clientFd = ConnectToListener(testPort_);
    ASSERT_GE(clientFd, 0);

    for (int i = 0; i < 5; ++i) {
        if (newConnectionCalled) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    EXPECT_TRUE(newConnectionCalled);
    EXPECT_NE(acceptedSocket, nullptr);

    close(clientFd);
    eventSystem_->Stop();
    listener_->Stop();
}

// 测试：Start 时未调用 Listen
TEST_F(TcpListenerTest, StartWithoutListen)
{
    EXPECT_FALSE(listener_->Start());
}

// 测试：重复 Start
TEST_F(TcpListenerTest, StartTwice)
{
    testPort_ = A2A::Test::GetFreeTcpPort();
    ASSERT_GT(testPort_, 0);

    EXPECT_TRUE(listener_->Listen("127.0.0.1", testPort_, 5, false));
    EXPECT_TRUE(listener_->Start());
    EXPECT_FALSE(listener_->Start());
    listener_->Stop();
}

// 测试：Stop 未 Start
TEST_F(TcpListenerTest, StopWithoutStart)
{
    EXPECT_NO_THROW(listener_->Stop());
}

// 测试：重复 Listen 同一个端口
TEST_F(TcpListenerTest, ListenSamePortTwice)
{
    testPort_ = A2A::Test::GetFreeTcpPort();
    ASSERT_GT(testPort_, 0);

    EXPECT_TRUE(listener_->Listen("127.0.0.1", testPort_, 5, false));
    EXPECT_TRUE(listener_->Listen("127.0.0.1", testPort_, 5, false));
}

// 测试：使用 SO_REUSEPORT
TEST_F(TcpListenerTest, ListenWithReusePort)
{
    testPort_ = A2A::Test::GetFreeTcpPort();
    ASSERT_GT(testPort_, 0);

    EXPECT_TRUE(listener_->Listen("127.0.0.1", testPort_, 5, true));
    EXPECT_TRUE(listener_->Start());

    eventSystem_->Start(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_TRUE(IsPortListening(testPort_));

    eventSystem_->Stop();
    listener_->Stop();
}

// 测试：不同 backlog 值
TEST_F(TcpListenerTest, DifferentBacklogValues)
{
    testPort_ = A2A::Test::GetFreeTcpPort();
    ASSERT_GT(testPort_, 0);

    // 测试小的 backlog
    EXPECT_TRUE(listener_->Listen("127.0.0.1", testPort_, 1, false));
    EXPECT_TRUE(listener_->Start());
    listener_->Stop();

    // 测试大的 backlog
    testPort_ = A2A::Test::GetFreeTcpPort();
    ASSERT_GT(testPort_, 0);
    EXPECT_TRUE(listener_->Listen("127.0.0.1", testPort_, 128, false));
    EXPECT_TRUE(listener_->Start());
    listener_->Stop();

    // 测试 SOMAXCONN
    testPort_ = A2A::Test::GetFreeTcpPort();
    ASSERT_GT(testPort_, 0);
    EXPECT_TRUE(listener_->Listen("127.0.0.1", testPort_, SOMAXCONN, false));
    EXPECT_TRUE(listener_->Start());
    listener_->Stop();
}

// 测试：无效的 host
TEST_F(TcpListenerTest, InvalidHost)
{
    testPort_ = A2A::Test::GetFreeTcpPort();
    ASSERT_GT(testPort_, 0);

    EXPECT_FALSE(listener_->Listen("invalid.host.that.does.not.exist", testPort_, 5, false));
}

TEST_F(TcpListenerTest, EmptyHost)
{
    testPort_ = A2A::Test::GetFreeTcpPort();
    ASSERT_GT(testPort_, 0);

    EXPECT_TRUE(listener_->Listen("", testPort_, 5, false));
    EXPECT_TRUE(listener_->Start());
    listener_->Stop();
}

// 测试：无效的端口
TEST_F(TcpListenerTest, PortAlreadyInUse)
{
    testPort_ = A2A::Test::GetFreeTcpPort();
    ASSERT_GT(testPort_, 0);

    // 第一个监听器占用端口
    auto tempListener = std::make_unique<TcpListener>(*eventSystem_);
    EXPECT_TRUE(tempListener->Listen("127.0.0.1", testPort_, 5, false));
    EXPECT_TRUE(tempListener->Start());

    eventSystem_->Start(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT_TRUE(IsPortListening(testPort_));

    // 第二个监听器尝试监听同一个端口（不使用 SO_REUSEPORT）
    TcpListener secondListener(*eventSystem_);
    EXPECT_FALSE(secondListener.Listen("127.0.0.1", testPort_, 5, false));

    tempListener->Stop();
    eventSystem_->Stop();
}

// 测试：多次 Start/Stop 循环
TEST_F(TcpListenerTest, MultipleStartStopCycles)
{
    testPort_ = A2A::Test::GetFreeTcpPort();
    ASSERT_GT(testPort_, 0);

    EXPECT_TRUE(listener_->Listen("127.0.0.1", testPort_, 5, false));

    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(listener_->Start());
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        listener_->Stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

}