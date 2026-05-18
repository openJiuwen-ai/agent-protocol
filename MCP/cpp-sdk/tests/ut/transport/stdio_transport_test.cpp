/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
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
class StdioClientTransportTest : public ::testing::Test {
public:
    ~StdioClientTransportTest() {}
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
class StdioServerTransportTest : public ::testing::Test {
public:
    ~StdioServerTransportTest() {}
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


TEST_F(StdioClientTransportTest, ConstructorDestructor)
{
    clientTransport_ = std::make_unique<StdioClientTransport>(stdioConfig_);
    ASSERT_NE(clientTransport_, nullptr);
    EXPECT_NO_THROW(clientTransport_->Terminate());
}


TEST_F(StdioServerTransportTest, ConstructorDestructor)
{
    serverTransport_ = std::make_unique<StdioServerTransport>();
    ASSERT_NE(serverTransport_, nullptr);

    EXPECT_NO_THROW(serverTransport_->Terminate());
}

TEST_F(StdioClientTransportTest, SetCallback)
{
    clientTransport_ = std::make_unique<StdioClientTransport>(stdioConfig_);

    class TestCallback : public TransportCallback {
    public:
        void OnMessageReceived(const JSONRPCMessage&, RequestContext&) override
        {
            messageReceived++;
        }
        void OnDisconnected(const std::string&) override
        {
            disconnected++;
        }
        std::atomic<int> messageReceived{0};
        std::atomic<int> disconnected{0};
    };

    auto testCallback = std::make_shared<TestCallback>();
    clientTransport_->SetCallback(testCallback);

    SUCCEED();
}

TEST_F(StdioServerTransportTest, SetCallback)
{
    serverTransport_ = std::make_unique<StdioServerTransport>();

    class TestCallback : public TransportCallback {
    public:
        void OnMessageReceived(const JSONRPCMessage&, RequestContext&) override
        {
            messageReceived++;
        }
        void OnDisconnected(const std::string&) override
        {
            disconnected++;
        }
        std::atomic<int> messageReceived{0};
        std::atomic<int> disconnected{0};
    };

    auto testCallback = std::make_shared<TestCallback>();
    serverTransport_->SetCallback(testCallback);

    SUCCEED();
}

TEST_F(StdioClientTransportTest, ConnectWithoutCommand)
{
    stdioConfig_.command = "";
    clientTransport_ = std::make_unique<StdioClientTransport>(stdioConfig_);

    EXPECT_NO_THROW(clientTransport_->Connect());

    EXPECT_NO_THROW(clientTransport_->Terminate());
}

TEST_F(StdioClientTransportTest, DoubleConnect)
{
    clientTransport_ = std::make_unique<StdioClientTransport>(stdioConfig_);

    EXPECT_NO_THROW(clientTransport_->Connect());

    EXPECT_NO_THROW(clientTransport_->Connect());

    EXPECT_NO_THROW(clientTransport_->Terminate());
}

TEST_F(StdioClientTransportTest, DoubleTerminate)
{
    clientTransport_ = std::make_unique<StdioClientTransport>(stdioConfig_);

    EXPECT_NO_THROW(clientTransport_->Connect());
    EXPECT_NO_THROW(clientTransport_->Terminate());

    // Second terminate should be safe
    EXPECT_NO_THROW(clientTransport_->Terminate());
}

TEST_F(StdioServerTransportTest, HandleRequestEmpty)
{
    serverTransport_ = std::make_unique<StdioServerTransport>();

    Http::HttpRequest request;
    RequestContext ctx;

    // HandleRequest is empty implementation, should not crash
    EXPECT_NO_THROW(serverTransport_->HandleRequest(request, ctx));
}

// Test edge cases
TEST_F(StdioClientTransportTest, EmptyConfig)
{
    StdioClientConfig emptyConfig;
    clientTransport_ = std::make_unique<StdioClientTransport>(emptyConfig);

    EXPECT_NO_THROW(clientTransport_->Connect());
    EXPECT_NO_THROW(clientTransport_->Terminate());
}

TEST_F(StdioClientTransportTest, NullCallback)
{
    clientTransport_ = std::make_unique<StdioClientTransport>(stdioConfig_);

    // Should not crash when setting null callback
    EXPECT_NO_THROW(clientTransport_->SetCallback(nullptr));

    EXPECT_NO_THROW(clientTransport_->Connect());
    EXPECT_NO_THROW(clientTransport_->Terminate());
}

// Basic callback functionality test for client
TEST_F(StdioClientTransportTest, CallbackFlowTest)
{
    clientTransport_ = std::make_unique<StdioClientTransport>(stdioConfig_);

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
    clientTransport_->SetCallback(testCallback);

    // Connect and disconnect should work without crashing
    EXPECT_NO_THROW(clientTransport_->Connect());
    EXPECT_NO_THROW(clientTransport_->Terminate());
}

// Test rapid connect/disconnect cycles for client
TEST_F(StdioClientTransportTest, RapidConnectDisconnect)
{
    for (int i = 0; i < LOOP_NUM; i++) {  // Reduce iterations for speed
        clientTransport_ = std::make_unique<StdioClientTransport>(stdioConfig_);

        // Set callback to avoid null pointer issues
        class TestCallback : public TransportCallback {
        public:
            void OnMessageReceived(const JSONRPCMessage&, RequestContext&) override {}
            void OnDisconnected(const std::string&) override {}
        };
        auto testCallback = std::make_shared<TestCallback>();
        clientTransport_->SetCallback(testCallback);

        EXPECT_NO_THROW(clientTransport_->Connect());
        EXPECT_NO_THROW(clientTransport_->Terminate());

        std::this_thread::sleep_for(5ms);
    }
}

// Test with subprocess config for client
TEST_F(StdioClientTransportTest, SubprocessConfig)
{
    stdioConfig_.command = "echo";
    stdioConfig_.args = {"hello"};
    stdioConfig_.env = {{"TEST", "value"}};

    clientTransport_ = std::make_unique<StdioClientTransport>(stdioConfig_);

    // Set callback
    class TestCallback : public TransportCallback {
    public:
        void OnMessageReceived(const JSONRPCMessage&, RequestContext&) override {}
        void OnDisconnected(const std::string&) override {}
    };
    auto testCallback = std::make_shared<TestCallback>();
    clientTransport_->SetCallback(testCallback);

    // Connect with subprocess config (may fail but shouldn't crash)
    EXPECT_NO_THROW(clientTransport_->Connect());
    EXPECT_NO_THROW(clientTransport_->Terminate());
}

// Test JSON-RPC message serialization/deserialization integration
TEST_F(StdioClientTransportTest, JSONRPCMessageHandling)
{
    clientTransport_ = std::make_unique<StdioClientTransport>(stdioConfig_);

    class TestCallback : public TransportCallback {
    public:
        void OnMessageReceived(const JSONRPCMessage& msg, RequestContext& ctx) override
        {
            receivedMessages++;

            // Try to get method name from message
            std::string method = std::visit(
                [](auto&& arg) -> std::string {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, Mcp::JSONRPCRequest>) {
                        return arg.method_;
                    } else if constexpr (std::is_same_v<T, Mcp::JSONRPCNotification>) {
                        return arg.method_;
                    } else {
                        return "";
                    }
                },
                msg);

            lastMethod = method;
        }

        void OnDisconnected(const std::string& reason) override
        {
            disconnected = true;
        }

        std::atomic<int> receivedMessages{0};
        std::string lastMethod;
        bool disconnected = false;
    };

    auto testCallback = std::make_shared<TestCallback>();
    clientTransport_->SetCallback(testCallback);

    EXPECT_NO_THROW(clientTransport_->Connect());
}

class StdioClientTransportExtendedTest : public ::testing::Test {
public:
    ~StdioClientTransportExtendedTest() {}
protected:
    void SetUp() override
    {
        stdioConfig_.command = "";
        stdioConfig_.args = {};
        stdioConfig_.env = {};

        // 创建测试echo程序
        CreateTestEchoProgram();
    }

    void TearDown() override
    {
        if (clientTransport_) {
            clientTransport_->Terminate();
        }
        // 清理测试程序
        if (access(echoProgramPath_.c_str(), F_OK) == 0) {
            unlink(echoProgramPath_.c_str());
        }
        std::this_thread::sleep_for(10ms);
    }

    void CreateTestEchoProgram()
    {
        echoProgramPath_ = "/tmp/mcp_test_echo_" + std::to_string(getpid()) + ".sh";
        FILE* fp = fopen(echoProgramPath_.c_str(), "w");
        if (fp) {
            fprintf(fp, "#!/bin/bash\n");
            fprintf(fp, "while IFS= read -r line; do\n");
            fprintf(fp, "    echo \"$line\"\n");
            fprintf(fp, "done\n");
            fclose(fp);
            chmod(echoProgramPath_.c_str(), 0755); // 0755 is file mod
        }
    }

    StdioClientConfig stdioConfig_;
    std::unique_ptr<StdioClientTransport> clientTransport_;
    std::string echoProgramPath_;
};

class StdioServerTransportExtendedTest : public ::testing::Test {
public:
    ~StdioServerTransportExtendedTest() {}
protected:
    void SetUp() override
    {
        // Server transport不需要特殊配置
    }

    void TearDown() override
    {
        if (serverTransport_) {
            serverTransport_->Terminate();
        }
        std::this_thread::sleep_for(10ms);
    }

    std::unique_ptr<StdioServerTransport> serverTransport_;
};

TEST_F(StdioClientTransportExtendedTest, ConnectWithEchoProgram)
{
    if (access(echoProgramPath_.c_str(), F_OK) != 0) {
        GTEST_SKIP() << "Test echo program not created";
    }

    stdioConfig_.command = echoProgramPath_;
    clientTransport_ = std::make_unique<StdioClientTransport>(stdioConfig_);

    class TestCallback : public TransportCallback {
    public:
        void OnMessageReceived(const JSONRPCMessage&, RequestContext&) override
        {
            messageReceived = true;
        }
        void OnDisconnected(const std::string&) override
        {
            disconnected = true;
        }
        std::atomic<bool> messageReceived{false};
        std::atomic<bool> disconnected{false};
    };

    auto testCallback = std::make_shared<TestCallback>();
    clientTransport_->SetCallback(testCallback);

    EXPECT_NO_THROW(clientTransport_->Connect());
    std::this_thread::sleep_for(100ms);
    EXPECT_NO_THROW(clientTransport_->Terminate());
}

TEST_F(StdioClientTransportExtendedTest, ConnectionLifecycle)
{
    clientTransport_ = std::make_unique<StdioClientTransport>(stdioConfig_);

    class TestCallback : public TransportCallback {
    public:
        void OnMessageReceived(const JSONRPCMessage&, RequestContext&) override {}
        void OnDisconnected(const std::string&) override {}
    };

    auto testCallback = std::make_shared<TestCallback>();
    clientTransport_->SetCallback(testCallback);

    EXPECT_NO_THROW(clientTransport_->Connect());
    std::this_thread::sleep_for(50ms);
    EXPECT_NO_THROW(clientTransport_->Terminate());
}

TEST_F(StdioClientTransportExtendedTest, SendJSONRPCRequest)
{
    clientTransport_ = std::make_unique<StdioClientTransport>(stdioConfig_);

    class TestCallback : public TransportCallback {
    public:
        void OnMessageReceived(const JSONRPCMessage&, RequestContext&) override {}
        void OnDisconnected(const std::string&) override {}
    };

    auto testCallback = std::make_shared<TestCallback>();
    clientTransport_->SetCallback(testCallback);
    EXPECT_NO_THROW(clientTransport_->Connect());
}

TEST_F(StdioClientTransportExtendedTest, DisconnectCallback)
{
    clientTransport_ = std::make_unique<StdioClientTransport>(stdioConfig_);

    class TestCallback : public TransportCallback {
    public:
        void OnMessageReceived(const JSONRPCMessage&, RequestContext&) override {}
        void OnDisconnected(const std::string& reason) override
        {
            disconnectCalled = true;
            disconnectReason = reason;
        }

        std::atomic<bool> disconnectCalled{false};
        std::string disconnectReason;
    };

    auto testCallback = std::make_shared<TestCallback>();
    clientTransport_->SetCallback(testCallback);

    EXPECT_NO_THROW(clientTransport_->Connect());
    std::this_thread::sleep_for(50ms);
    EXPECT_NO_THROW(clientTransport_->Terminate());

    // 给回调一些时间执行
    std::this_thread::sleep_for(50ms);

    // 主要测试不崩溃
    SUCCEED();
}

TEST_F(StdioClientTransportExtendedTest, MessageSerializationIntegration)
{
    clientTransport_ = std::make_unique<StdioClientTransport>(stdioConfig_);

    class TestCallback : public TransportCallback {
    public:
        void OnMessageReceived(const JSONRPCMessage& msg, RequestContext& ctx) override
        {
            // 尝试序列化接收到的消息
            try {
                std::string serialized = SerializeJSONRPCMessage(msg);
                lastSerializedMessage = serialized;
            } catch (const std::exception& e) {
                serializationError = e.what();
            }
        }
        void OnDisconnected(const std::string&) override {}

        std::string lastSerializedMessage;
        std::string serializationError;
    };

    auto testCallback = std::make_shared<TestCallback>();
    clientTransport_->SetCallback(testCallback);

    EXPECT_NO_THROW(clientTransport_->Connect());
    std::this_thread::sleep_for(50ms);
    EXPECT_NO_THROW(clientTransport_->Terminate());
}

TEST_F(StdioClientTransportExtendedTest, VariousConfigCombinations)
{
    // 测试1: 只有命令
    {
        StdioClientConfig config;
        config.command = "ls";
        auto transport = std::make_unique<StdioClientTransport>(config);
        EXPECT_NO_THROW(transport->Connect());
        EXPECT_NO_THROW(transport->Terminate());
    }
    // 测试2: 命令+参数
    {
        StdioClientConfig config;
        config.command = "ls";
        config.args = {"-l", "-a"};
        auto transport = std::make_unique<StdioClientTransport>(config);
        EXPECT_NO_THROW(transport->Connect());
        EXPECT_NO_THROW(transport->Terminate());
    }
    // 测试3: 命令+环境变量
    {
        StdioClientConfig config;
        config.command = "env";
        config.env = {{"PATH", "/usr/bin"}};
        auto transport = std::make_unique<StdioClientTransport>(config);
        EXPECT_NO_THROW(transport->Connect());
        EXPECT_NO_THROW(transport->Terminate());
    }
    // 测试4: 完整配置
    {
        StdioClientConfig config;
        config.command = "bash";
        config.args = {"-c", "echo hello"};
        config.env = {{"TEST", "value"}};
        auto transport = std::make_unique<StdioClientTransport>(config);
        EXPECT_NO_THROW(transport->Connect());
        EXPECT_NO_THROW(transport->Terminate());
    }
}

TEST_F(StdioClientTransportExtendedTest, MessageReceivedCallback)
{
    clientTransport_ = std::make_unique<StdioClientTransport>(stdioConfig_);

    class TestCallback : public TransportCallback {
    public:
        void OnMessageReceived(const JSONRPCMessage& msg, RequestContext& ctx) override
        {
            messageCount++;

            // 记录消息类型
            std::visit([this](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, Mcp::JSONRPCRequest>) {
                    requestCount++;
                } else if constexpr (std::is_same_v<T, Mcp::JSONRPCResponse>) {
                    responseCount++;
                } else if constexpr (std::is_same_v<T, Mcp::JSONRPCNotification>) {
                    notificationCount++;
                }
                },
                msg);
        }

        void OnDisconnected(const std::string& reason) override
        {
            disconnected = true;
            disconnectReason = reason;
        }

        std::atomic<int> messageCount{0};
        std::atomic<int> requestCount{0};
        std::atomic<int> responseCount{0};
        std::atomic<int> notificationCount{0};
        std::atomic<bool> disconnected{false};
        std::string disconnectReason;
    };

    auto testCallback = std::make_shared<TestCallback>();
    clientTransport_->SetCallback(testCallback);

    EXPECT_NO_THROW(clientTransport_->Connect());
    std::this_thread::sleep_for(50ms);
    EXPECT_NO_THROW(clientTransport_->Terminate());
}

TEST_F(StdioClientTransportExtendedTest, MultipleCallbackSettings)
{
    clientTransport_ = std::make_unique<StdioClientTransport>(stdioConfig_);

    class TestCallback1 : public TransportCallback {
    public:
        void OnMessageReceived(const JSONRPCMessage&, RequestContext&) override
        {
            callback1Called = true;
        }
        void OnDisconnected(const std::string&) override {}
        bool callback1Called = false;
    };

    class TestCallback2 : public TransportCallback {
    public:
        void OnMessageReceived(const JSONRPCMessage&, RequestContext&) override
        {
            callback2Called = true;
        }
        void OnDisconnected(const std::string&) override {}
        bool callback2Called = false;
    };

    auto callback1 = std::make_shared<TestCallback1>();
    auto callback2 = std::make_shared<TestCallback2>();

    // 设置第一个回调
    EXPECT_NO_THROW(clientTransport_->SetCallback(callback1));

    // 设置第二个回调（应该替换第一个）
    EXPECT_NO_THROW(clientTransport_->SetCallback(callback2));

    EXPECT_NO_THROW(clientTransport_->Connect());
    std::this_thread::sleep_for(50ms);
    EXPECT_NO_THROW(clientTransport_->Terminate());
}

TEST_F(StdioClientTransportExtendedTest, DirectStdioConnection)
{
    // 使用空命令，直接使用标准输入输出
    stdioConfig_.command = "";
    clientTransport_ = std::make_unique<StdioClientTransport>(stdioConfig_);

    class TestCallback : public TransportCallback {
    public:
        void OnMessageReceived(const JSONRPCMessage&, RequestContext&) override {}
        void OnDisconnected(const std::string&) override {}
    };

    auto testCallback = std::make_shared<TestCallback>();
    clientTransport_->SetCallback(testCallback);

    EXPECT_NO_THROW(clientTransport_->Connect());
    std::this_thread::sleep_for(50ms);
    EXPECT_NO_THROW(clientTransport_->Terminate());
}

TEST_F(StdioServerTransportExtendedTest, SendMessageWithoutConnection)
{
    serverTransport_ = std::make_unique<StdioServerTransport>();

    // 不调用Listen，直接发送消息
    JSONRPCResponse response;
    response.jsonrpc_ = "2.0";
    response.id_ = 1;

    RequestContext ctx;
    ctx.connectionId = 1;
    ctx.method = "test.method";

    // 应该不会崩溃
    EXPECT_NO_THROW(serverTransport_->SendMessage(response, ctx));
}

TEST_F(StdioClientTransportExtendedTest, ConnectAndImmediateTerminate)
{
    clientTransport_ = std::make_unique<StdioClientTransport>(stdioConfig_);

    class TestCallback : public TransportCallback {
    public:
        void OnMessageReceived(const JSONRPCMessage&, RequestContext&) override {}
        void OnDisconnected(const std::string&) override {}
    };

    auto testCallback = std::make_shared<TestCallback>();
    clientTransport_->SetCallback(testCallback);

    EXPECT_NO_THROW(clientTransport_->Connect());
    // 立即断开
    EXPECT_NO_THROW(clientTransport_->Terminate());
}

TEST_F(StdioClientTransportExtendedTest, RepeatedTerminate)
{
    clientTransport_ = std::make_unique<StdioClientTransport>(stdioConfig_);

    class TestCallback : public TransportCallback {
    public:
        void OnMessageReceived(const JSONRPCMessage&, RequestContext&) override {}
        void OnDisconnected(const std::string&) override {}
    };

    auto testCallback = std::make_shared<TestCallback>();
    clientTransport_->SetCallback(testCallback);

    EXPECT_NO_THROW(clientTransport_->Connect());
    std::this_thread::sleep_for(50ms);
    // 多次断开
    EXPECT_NO_THROW(clientTransport_->Terminate());
    EXPECT_NO_THROW(clientTransport_->Terminate());
    EXPECT_NO_THROW(clientTransport_->Terminate());
}

} // namespace Mcp