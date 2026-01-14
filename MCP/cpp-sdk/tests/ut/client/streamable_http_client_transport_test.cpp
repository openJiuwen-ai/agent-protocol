/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <gtest/gtest.h>

#include <memory>
#include <atomic>
#include <string>
#include <unordered_map>
#include <chrono>
#include <thread>

#include "mcp_log.h"
#include "shared/http_common.h"
#include "client/transport/streamable_http_client_transport.h"

using namespace std::chrono_literals;

static constexpr int LOOP_NUM = 3;

namespace Mcp {

// Test fixture for StreamableHttpClientTransport tests
class StreamableHttpClientTransportTest : public ::testing::Test {
public:
    ~StreamableHttpClientTransportTest() {}
protected:
    void SetUp() override
    {
        // 使用无效URL确保快速失败
        testUrl = "http://localhost:99999"; // 本地无效端口
        headers["Test-Header"] = "Test-Value";

        // 使用极短超时
        timeout = 1ms;      // 1ms HTTP超时
        sseTimeout = 2ms;   // 2ms SSE超时
    }

    void TearDown() override
    {
        if (transport) {
            transport->Terminate();
        }
        transport.reset();
        std::this_thread::sleep_for(1ms);
    }

    std::string testUrl;
    std::unordered_map<std::string, std::string> headers;
    std::chrono::milliseconds timeout;
    std::chrono::milliseconds sseTimeout;
    std::unique_ptr<StreamableHttpClientTransport> transport;

    // 简化回调类，只记录调用次数
    class TestCallback : public TransportCallback {
    public:
        ~TestCallback() {}
        void OnMessageReceived(const JSONRPCMessage& message, RequestContext& ctx) override
        {
            messageCount++;
        }

        void OnDisconnected(const std::string& reason) override
        {
            disconnectCount++;
        }

        std::atomic<int> messageCount{0};
        std::atomic<int> disconnectCount{0};
    };
};

// 基础构造函数测试
TEST_F(StreamableHttpClientTransportTest, ConstructorSuccess)
{
    EXPECT_NO_THROW(transport = std::make_unique<StreamableHttpClientTransport>(
        testUrl, headers, timeout, sseTimeout));
    ASSERT_NE(transport, nullptr);
}

TEST_F(StreamableHttpClientTransportTest, ConstructorWithDefaultHeaders)
{
    EXPECT_NO_THROW(transport = std::make_unique<StreamableHttpClientTransport>(
        testUrl, std::unordered_map<std::string, std::string>{}, timeout, sseTimeout));
    ASSERT_NE(transport, nullptr);
}

// 析构函数测试
TEST_F(StreamableHttpClientTransportTest, DestructorWithoutTerminate)
{
    transport = std::make_unique<StreamableHttpClientTransport>(testUrl, headers, timeout, sseTimeout);
    ASSERT_NE(transport, nullptr);
    EXPECT_NO_THROW(transport.reset());
}

// SetCallback测试
TEST_F(StreamableHttpClientTransportTest, SetCallback)
{
    transport = std::make_unique<StreamableHttpClientTransport>(testUrl, headers, timeout, sseTimeout);
    ASSERT_NE(transport, nullptr);

    auto testCallback = std::make_shared<TestCallback>();
    EXPECT_NO_THROW(transport->SetCallback(testCallback));
}

TEST_F(StreamableHttpClientTransportTest, SetNullCallback)
{
    transport = std::make_unique<StreamableHttpClientTransport>(testUrl, headers, timeout, sseTimeout);
    ASSERT_NE(transport, nullptr);
    EXPECT_NO_THROW(transport->SetCallback(nullptr));
}

TEST_F(StreamableHttpClientTransportTest, SetCallbackMultipleTimes)
{
    transport = std::make_unique<StreamableHttpClientTransport>(testUrl, headers, timeout, sseTimeout);
    ASSERT_NE(transport, nullptr);

    auto callback1 = std::make_shared<TestCallback>();
    auto callback2 = std::make_shared<TestCallback>();

    EXPECT_NO_THROW(transport->SetCallback(callback1));
    EXPECT_NO_THROW(transport->SetCallback(callback2));
    EXPECT_NO_THROW(transport->SetCallback(nullptr));
    EXPECT_NO_THROW(transport->SetCallback(callback1));
}

// Connect测试
TEST_F(StreamableHttpClientTransportTest, Connect)
{
    transport = std::make_unique<StreamableHttpClientTransport>(testUrl, headers, timeout, sseTimeout);
    ASSERT_NE(transport, nullptr);
    EXPECT_NO_THROW(transport->Connect());
}

TEST_F(StreamableHttpClientTransportTest, ConnectMultipleTimes)
{
    transport = std::make_unique<StreamableHttpClientTransport>(testUrl, headers, timeout, sseTimeout);
    ASSERT_NE(transport, nullptr);

    EXPECT_NO_THROW(transport->Connect());
    EXPECT_NO_THROW(transport->Connect());
    EXPECT_NO_THROW(transport->Connect());
}

TEST_F(StreamableHttpClientTransportTest, ConnectAfterSetCallback)
{
    transport = std::make_unique<StreamableHttpClientTransport>(testUrl, headers, timeout, sseTimeout);
    ASSERT_NE(transport, nullptr);

    auto callback = std::make_shared<TestCallback>();
    transport->SetCallback(callback);
    EXPECT_NO_THROW(transport->Connect());
}

TEST_F(StreamableHttpClientTransportTest, ConnectBeforeSetCallback)
{
    transport = std::make_unique<StreamableHttpClientTransport>(testUrl, headers, timeout, sseTimeout);
    ASSERT_NE(transport, nullptr);

    EXPECT_NO_THROW(transport->Connect());
    auto callback = std::make_shared<TestCallback>();
    EXPECT_NO_THROW(transport->SetCallback(callback));
}

// Terminate测试
TEST_F(StreamableHttpClientTransportTest, Terminate)
{
    transport = std::make_unique<StreamableHttpClientTransport>(testUrl, headers, timeout, sseTimeout);
    ASSERT_NE(transport, nullptr);
    EXPECT_NO_THROW(transport->Terminate());
}

TEST_F(StreamableHttpClientTransportTest, DoubleTerminate)
{
    transport = std::make_unique<StreamableHttpClientTransport>(testUrl, headers, timeout, sseTimeout);
    ASSERT_NE(transport, nullptr);

    EXPECT_NO_THROW(transport->Terminate());
    EXPECT_NO_THROW(transport->Terminate());
    EXPECT_NO_THROW(transport->Terminate());
}

TEST_F(StreamableHttpClientTransportTest, TerminateAfterConnect)
{
    transport = std::make_unique<StreamableHttpClientTransport>(testUrl, headers, timeout, sseTimeout);
    ASSERT_NE(transport, nullptr);

    transport->Connect();
    EXPECT_NO_THROW(transport->Terminate());
}

TEST_F(StreamableHttpClientTransportTest, TerminateAfterSetCallback)
{
    transport = std::make_unique<StreamableHttpClientTransport>(testUrl, headers, timeout, sseTimeout);
    ASSERT_NE(transport, nullptr);

    auto callback = std::make_shared<TestCallback>();
    transport->SetCallback(callback);
    EXPECT_NO_THROW(transport->Terminate());
}

TEST_F(StreamableHttpClientTransportTest, TerminateWithoutConnect)
{
    transport = std::make_unique<StreamableHttpClientTransport>(testUrl, headers, timeout, sseTimeout);
    ASSERT_NE(transport, nullptr);

    EXPECT_NO_THROW(transport->Terminate());
}

// URL边界测试
TEST_F(StreamableHttpClientTransportTest, EmptyUrl)
{
    std::string emptyUrl = "";
    EXPECT_NO_THROW(transport = std::make_unique<StreamableHttpClientTransport>(
        emptyUrl, headers, timeout, sseTimeout));
    ASSERT_NE(transport, nullptr);
}

TEST_F(StreamableHttpClientTransportTest, VeryLongUrl)
{
    std::string longUrl = "http://" + std::string(1000, 'a') + ".com";
    EXPECT_NO_THROW(transport = std::make_unique<StreamableHttpClientTransport>(
        longUrl, headers, timeout, sseTimeout));
    ASSERT_NE(transport, nullptr);
}

TEST_F(StreamableHttpClientTransportTest, HttpsUrl)
{
    std::string httpsUrl = "https://example.com";
    EXPECT_NO_THROW(transport = std::make_unique<StreamableHttpClientTransport>(
        httpsUrl, headers, timeout, sseTimeout));
    ASSERT_NE(transport, nullptr);
}

// 超时边界测试
TEST_F(StreamableHttpClientTransportTest, LargeTimeoutValues)
{
    auto largeTimeout = 150ms;
    EXPECT_NO_THROW(transport = std::make_unique<StreamableHttpClientTransport>(
        testUrl, headers, largeTimeout, largeTimeout));
    ASSERT_NE(transport, nullptr);
}

TEST_F(StreamableHttpClientTransportTest, DifferentHttpAndSseTimeouts)
{
    auto httpTimeout = 10ms;
    auto sseTimeout = 100ms;
    EXPECT_NO_THROW(transport = std::make_unique<StreamableHttpClientTransport>(
        testUrl, headers, httpTimeout, sseTimeout));
    ASSERT_NE(transport, nullptr);
}

// Headers边界测试
TEST_F(StreamableHttpClientTransportTest, EmptyHeaders)
{
    std::unordered_map<std::string, std::string> emptyHeaders;
    EXPECT_NO_THROW(transport = std::make_unique<StreamableHttpClientTransport>(
        testUrl, emptyHeaders, timeout, sseTimeout));
    ASSERT_NE(transport, nullptr);
}

TEST_F(StreamableHttpClientTransportTest, MultipleHeaders)
{
    std::unordered_map<std::string, std::string> multiHeaders = {
        {"Header1", "Value1"},
        {"Header2", "Value2"},
        {"Header3", "Value3"},
        {"Header4", "Value4"},
        {"Header5", "Value5"}
    };
    EXPECT_NO_THROW(transport = std::make_unique<StreamableHttpClientTransport>(
        testUrl, multiHeaders, timeout, sseTimeout));
    ASSERT_NE(transport, nullptr);
}

// 生命周期管理测试
TEST_F(StreamableHttpClientTransportTest, CompleteLifecycle)
{
    transport = std::make_unique<StreamableHttpClientTransport>(testUrl, headers, timeout, sseTimeout);
    ASSERT_NE(transport, nullptr);

    auto callback = std::make_shared<TestCallback>();
    transport->SetCallback(callback);
    transport->Connect();
    EXPECT_NO_THROW(transport->Terminate());
}

TEST_F(StreamableHttpClientTransportTest, MultipleLifecycles)
{
    // 测试重复创建和销毁
    for (int i = 0; i < LOOP_NUM; ++i) {
        EXPECT_NO_THROW(transport = std::make_unique<StreamableHttpClientTransport>(
            testUrl, headers, timeout, sseTimeout));
        ASSERT_NE(transport, nullptr);

        auto callback = std::make_shared<TestCallback>();
        transport->SetCallback(callback);
        transport->Connect();
        transport->Terminate();
        transport.reset();
        std::this_thread::sleep_for(1ms);
    }
}

TEST_F(StreamableHttpClientTransportTest, ConcurrentSetCallback)
{
    transport = std::make_unique<StreamableHttpClientTransport>(testUrl, headers, timeout, sseTimeout);
    ASSERT_NE(transport, nullptr);

    std::thread t1([this]() {
        auto callback = std::make_shared<TestCallback>();
        EXPECT_NO_THROW(transport->SetCallback(callback));
    });

    std::thread t2([this]() {
        auto callback = std::make_shared<TestCallback>();
        EXPECT_NO_THROW(transport->SetCallback(callback));
    });

    t1.join();
    t2.join();

    EXPECT_NO_THROW(transport->Terminate());
}

TEST_F(StreamableHttpClientTransportTest, StateQueriesThroughBehavior)
{
    transport = std::make_unique<StreamableHttpClientTransport>(testUrl, headers, timeout, sseTimeout);
    ASSERT_NE(transport, nullptr);

    // 测试所有方法都存在且可调用
    auto testCallback = std::make_shared<TestCallback>();
    EXPECT_NO_THROW(transport->SetCallback(testCallback));
    EXPECT_NO_THROW(transport->Connect());
    EXPECT_NO_THROW(transport->Terminate());

    SUCCEED(); // 如果没有崩溃，测试通过
}

// 性能测试（快速执行）
TEST_F(StreamableHttpClientTransportTest, RapidConstructionDestruction)
{
    const int iterations = 10;
    for (int i = 0; i < iterations; ++i) {
        EXPECT_NO_THROW(transport = std::make_unique<StreamableHttpClientTransport>(
            testUrl, headers, 1ms, 1ms));
        ASSERT_NE(transport, nullptr);

        transport->Terminate();
        transport.reset();
    }
}

// 内存泄漏测试（通过多次创建销毁）
TEST_F(StreamableHttpClientTransportTest, MemoryLeakCheck)
{
    for (int i = 0; i < LOOP_NUM; ++i) {
        transport = std::make_unique<StreamableHttpClientTransport>(testUrl, headers, timeout, sseTimeout);
        ASSERT_NE(transport, nullptr);

        auto callback = std::make_shared<TestCallback>();
        transport->SetCallback(callback);
        transport->Connect();
        transport->Terminate();
        transport.reset();

        // 短暂休眠让资源释放
        std::this_thread::sleep_for(2ms);
    }
}

// TLS配置测试
TEST_F(StreamableHttpClientTransportTest, ConstructorWithTlsConfig)
{
    TlsConfig tlsConfig;
    tlsConfig.enabled = false; // 测试关闭TLS

    EXPECT_NO_THROW(transport = std::make_unique<StreamableHttpClientTransport>(
        testUrl, headers, timeout, sseTimeout, tlsConfig));
    ASSERT_NE(transport, nullptr);
}

} // namespace Mcp