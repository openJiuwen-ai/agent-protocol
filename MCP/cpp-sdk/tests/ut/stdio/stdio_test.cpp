/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
*/

#include <gtest/gtest.h>

#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <nlohmann/json.hpp>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

#include "mcp_log.h"
#include "shared/jsonrpc.h"
#include "transport/stdio_transport.h"

using namespace std::chrono_literals;
using json = nlohmann::json;

static constexpr int LOOP_NUM = 3;

namespace Mcp {

// Test fixture for StdioClientTransport tests
class StdioClientTest : public ::testing::Test {
public:
    ~StdioClientTest() {}
protected:
    void SetUp() override
    {
        // Initialize a basic client transport config
        stdioConfig_.command = "";  // No subprocess by default
        // Set other config values as needed
        stdioConfig_.args = {};
        stdioConfig_.env = {};
    }

    void TearDown() override
    {
        if (clientTransport_) {
            clientTransport_->Terminate();
        }
        // Short delay to ensure cleanup
        std::this_thread::sleep_for(10ms);
    }

    StdioClientConfig stdioConfig_;
    std::unique_ptr<StdioClientTransport> clientTransport_;
};

// Test fixture for StdioServerTransport tests
class StdioServerTest : public ::testing::Test {
public:
    ~StdioServerTest() {}
protected:
    void SetUp() override
    {
        // Server transport doesn't need config
    }

    void TearDown() override
    {
        if (serverTransport_) {
            serverTransport_->Terminate();
        }
        // Short delay to ensure cleanup
        std::this_thread::sleep_for(10ms);
    }

    std::unique_ptr<StdioServerTransport> serverTransport_;
};

TEST_F(StdioServerTest, Listen)
{
    serverTransport_ = std::make_unique<StdioServerTransport>();

    EXPECT_NO_THROW(serverTransport_->Listen());

    EXPECT_NO_THROW(serverTransport_->Terminate());
}

TEST_F(StdioServerTest, DoubleListen)
{
    serverTransport_ = std::make_unique<StdioServerTransport>();

    EXPECT_NO_THROW(serverTransport_->Listen());

    EXPECT_NO_THROW(serverTransport_->Listen());

    EXPECT_NO_THROW(serverTransport_->Terminate());
}

TEST_F(StdioServerTest, DoubleTerminate)
{
    serverTransport_ = std::make_unique<StdioServerTransport>();

    EXPECT_NO_THROW(serverTransport_->Listen());
    EXPECT_NO_THROW(serverTransport_->Terminate());

    // Second terminate should be safe
    EXPECT_NO_THROW(serverTransport_->Terminate());
}

TEST_F(StdioServerTest, NullCallback)
{
    serverTransport_ = std::make_unique<StdioServerTransport>();

    // Should not crash when setting null callback
    EXPECT_NO_THROW(serverTransport_->SetCallback(nullptr));

    EXPECT_NO_THROW(serverTransport_->Listen());
    EXPECT_NO_THROW(serverTransport_->Terminate());
}

TEST_F(StdioServerTest, CallbackFlowTest)
{
    serverTransport_ = std::make_unique<StdioServerTransport>();

    class TestCallback : public TransportCallback {
    public:
        void OnMessageReceived(const JSONRPCMessage& msg, RequestContext& ctx) override
        {
            messageReceived = true;
        }

        void OnDisconnected(const std::string& reason) override
        {
            disconnected = true;
            disconnectReason = reason;
        }

        bool messageReceived = false;
        bool disconnected = false;
        std::string disconnectReason;
    };

    auto testCallback = std::make_shared<TestCallback>();
    serverTransport_->SetCallback(testCallback);

    // Listen and terminate should work without crashing
    EXPECT_NO_THROW(serverTransport_->Listen());
    EXPECT_NO_THROW(serverTransport_->Terminate());
}

TEST_F(StdioServerTest, RapidListenTerminate)
{
    for (int i = 0; i < LOOP_NUM; i++) {  // Reduce iterations for speed
        serverTransport_ = std::make_unique<StdioServerTransport>();

        // Set callback to avoid null pointer issues
        class TestCallback : public TransportCallback {
        public:
            void OnMessageReceived(const JSONRPCMessage&, RequestContext&) override {}
            void OnDisconnected(const std::string&) override {}
        };
        auto testCallback = std::make_shared<TestCallback>();
        serverTransport_->SetCallback(testCallback);

        EXPECT_NO_THROW(serverTransport_->Listen());
        EXPECT_NO_THROW(serverTransport_->Terminate());

        std::this_thread::sleep_for(5ms);
    }
}

TEST_F(StdioServerTest, SendJSONRPCResponse)
{
    serverTransport_ = std::make_unique<StdioServerTransport>();

    class TestCallback : public TransportCallback {
    public:
        void OnMessageReceived(const JSONRPCMessage&, RequestContext&) override {}
        void OnDisconnected(const std::string&) override {}
    };

    auto testCallback = std::make_shared<TestCallback>();
    serverTransport_->SetCallback(testCallback);

    EXPECT_NO_THROW(serverTransport_->Listen());

    // 创建并发送一个JSON-RPC响应
    JSONRPCResponse response;
    response.jsonrpc_ = "2.0";
    response.id_ = 1;

    RequestContext ctx;
    ctx.connectionId = 1;
    ctx.method = "test.method";

    EXPECT_NO_THROW(serverTransport_->SendMessage(response, ctx));

    std::this_thread::sleep_for(50ms);
    EXPECT_NO_THROW(serverTransport_->Terminate());
}

} // namespace Mcp