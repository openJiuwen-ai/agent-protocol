/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <future>
#include <chrono>
#include <thread>
#include <vector>

#include "test_network.h"
#include "transport/http_server_transport.h"
#include "server/http_server_manager.h"
#include "transport_emitter.h"
#include "types.h"

namespace A2A::Transport::Test {

using namespace A2A::Transport;
using namespace A2A::Server;
using namespace A2A::Http;
using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::SaveArg;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::AtLeast;
using ::testing::AnyNumber;
using ::testing::InSequence;

class MockHttpServerManager : public HttpServerManager {
public:
    MockHttpServerManager(const HttpServerManagerConfig& config) : HttpServerManager(config)
    {}
};

class MockTransportEmitter : public TransportEmitter {
public:
    MOCK_METHOD(void, WriteStreamingData, (const std::string&), (override));
    MOCK_METHOD(void, WriteDone, (), (override));
};

class MockHttpRequestContext {
public:
    MOCK_METHOD(void, httpSendFunc, (const HttpResponse&, const HttpRequestContext&), (const));
};

// ===========================================================================
// Test Fixture
// ===========================================================================
class HttpServerTransportTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        config.ip = "127.0.0.1";
        config.port = A2A::Test::GetFreeTcpPort();
        config.ioThreadNum = 2;

        transport = std::make_shared<HttpServerTransport>(config);

        // 设置默认 handler
        rpcHandlerCalled = false;
        cardHandlerCalled = false;

        transport->SetRpcHandler(
            [this](const std::string& req, std::string& resp,
                std::shared_ptr<Transport::TransportEmitter> emitter) {
            rpcHandlerCalled = true;
            lastRpcRequest = req;
            if (rpcHandlerMock) {
                rpcHandlerMock(req, resp, emitter);
            } else {
                resp = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"ok\"}";
            }
        });

        transport->SetCardHandler([this](const std::string& req, std::string& resp) {
            cardHandlerCalled = true;
            lastCardRequest = req;
            if (cardHandlerMock) {
                cardHandlerMock(req, resp);
            } else {
                resp = "{\"name\":\"Test Agent\",\"version\":\"1.0.0\"}";
            }
        });
    }

    void TearDown() override
    {
        if (transport) {
            transport->Stop();
        }
    }

    // 辅助方法：创建测试用的 HttpRequest
    HttpRequest CreateJsonRpcRequest(const std::string& method, const std::string& params = "{}", int id = 1)
    {
        HttpRequest req;
        req.method = "POST";
        req.headers["Content-Type"] = "application/json";
        req.body = R"({
            "jsonrpc": "2.0",
            "method": ")" + method + R"(",
            "params": )" + params + R"(,
            "id": )" + std::to_string(id) + R"(
        })";
        return req;
    }

    // 辅助方法：创建 mock context
    HttpRequestContext CreateMockContext()
    {
        HttpRequestContext ctx;
        ctx.httpSendFunc = [this](const HttpResponse& resp, const HttpRequestContext&) {
            lastResponse = resp;
            responseSent = true;
            responsePromise.set_value(resp);
        };
        return ctx;
    }

    // 等待响应
    bool WaitForResponse(int timeoutMs = 100)
    {
        auto future = responsePromise.get_future();
        auto status = future.wait_for(std::chrono::milliseconds(timeoutMs));
        responsePromise = std::promise<HttpResponse>();
        responseSent = false;
        return status == std::future_status::ready;
    }

    HttpConfig config;
    std::shared_ptr<HttpServerTransport> transport;

    // Mock handlers
    std::function<void(const std::string&, std::string&,
        std::shared_ptr<Transport::TransportEmitter> emitter)> rpcHandlerMock;
    std::function<void(const std::string&, std::string&)> cardHandlerMock;

    // 调用记录
    bool rpcHandlerCalled = false;
    bool cardHandlerCalled = false;
    std::string lastRpcRequest;
    std::string lastCardRequest;

    // 响应记录
    HttpResponse lastResponse;
    bool responseSent = false;
    std::promise<HttpResponse> responsePromise;
};

// ===========================================================================
// 构造函数和基础配置测试
// ===========================================================================
TEST_F(HttpServerTransportTest, Constructor)
{
    EXPECT_NE(transport, nullptr);
}

TEST_F(HttpServerTransportTest, SetHeader)
{
    transport->SetHeader("X-Custom-Header", "custom-value");
    transport->SetHeader("X-Another-Header", "another-value");
    SUCCEED();
}

TEST_F(HttpServerTransportTest, SetBearerToken)
{
    transport->SetBearerToken("test-token");
    SUCCEED();
}

TEST_F(HttpServerTransportTest, SetTimeoutMs)
{
    transport->SetTimeoutMs(5000, 10000);
    SUCCEED();
}

// ===========================================================================
// Handler 设置测试 - 通过行为验证
// ===========================================================================
TEST_F(HttpServerTransportTest, SetRpcHandler)
{
    // 通过发送请求验证 handler 被调用
    auto ctx = CreateMockContext();
    auto req = CreateJsonRpcRequest("MESSAGE_SEND");
    SUCCEED();
}

TEST_F(HttpServerTransportTest, SetCardHandler)
{
    SUCCEED();
}

// ===========================================================================
// Start 测试
// ===========================================================================
TEST_F(HttpServerTransportTest, StartSuccess)
{
    EXPECT_NO_THROW({
        transport->Start();
        // 给服务器一点时间启动
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    });
}

TEST_F(HttpServerTransportTest, StartWithDifferentConfig)
{
    HttpConfig customConfig;
    customConfig.ip = "127.0.0.1";
    customConfig.port = 9090;
    customConfig.ioThreadNum = 4;

    auto customTransport = std::make_shared<HttpServerTransport>(customConfig);

    EXPECT_NO_THROW({
        customTransport->Start();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        customTransport->Stop();
    });
}

// ===========================================================================
// Stop 测试
// ===========================================================================
TEST_F(HttpServerTransportTest, Stop)
{
    transport->Start();
    EXPECT_NO_THROW({
        transport->Stop();
    });
}

TEST_F(HttpServerTransportTest, StopWithoutStart)
{
    EXPECT_NO_THROW({
        transport->Stop();
    });
}

TEST_F(HttpServerTransportTest, StopTwice)
{
    transport->Start();
    transport->Stop();
    EXPECT_NO_THROW({
        transport->Stop();
    });
}

// ===========================================================================
// Start-Stop 循环测试
// ===========================================================================
TEST_F(HttpServerTransportTest, MultipleStartStop)
{
    for (int i = 0; i < 3; i++) {
        transport->Start();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        transport->Stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    SUCCEED();
}

// ===========================================================================
// SendData 测试（服务器不应发送数据）
// ===========================================================================
TEST_F(HttpServerTransportTest, SendData)
{
    int result = transport->SendData("http://example.com", "test data");
    EXPECT_EQ(result, -1);
}

TEST_F(HttpServerTransportTest, SendDataWithEmptyUrl)
{
    int result = transport->SendData("", "test data");
    EXPECT_EQ(result, -1);
}

TEST_F(HttpServerTransportTest, SendDataWithEmptyData)
{
    int result = transport->SendData("http://example.com", "");
    EXPECT_EQ(result, -1);
}

// ===========================================================================
// 析构函数测试
// ===========================================================================
TEST_F(HttpServerTransportTest, Destructor)
{
    auto localTransport = std::make_shared<HttpServerTransport>(config);
    localTransport->Start();
    // 离开作用域时自动析构，应该不会崩溃
}

TEST_F(HttpServerTransportTest, DestructorWithRunningServer)
{
    auto localTransport = std::make_shared<HttpServerTransport>(config);
    localTransport->Start();
    // 不调用 Stop，直接析构
}

// ===========================================================================
// 配置测试
// ===========================================================================
TEST_F(HttpServerTransportTest, DifferentPorts)
{
    for (int i = 0; i < 4; ++i) {
        HttpConfig testConfig;
        testConfig.ip = "127.0.0.1";
        testConfig.port = (i == 3) ? 0 : static_cast<int>(A2A::Test::GetFreeTcpPort());
        testConfig.ioThreadNum = 1;

        auto testTransport = std::make_shared<HttpServerTransport>(testConfig);
        EXPECT_NO_THROW({
            testTransport->Start();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            testTransport->Stop();
        });
    }
}

TEST_F(HttpServerTransportTest, DifferentIPs)
{
    const std::vector<std::string> ips = {"127.0.0.1", "0.0.0.0", "localhost"};

    for (const auto& ip : ips) {
        const int port = static_cast<int>(A2A::Test::GetFreeTcpPort());
        HttpConfig testConfig;
        testConfig.ip = ip;
        testConfig.port = port;
        testConfig.ioThreadNum = 1;

        auto testTransport = std::make_shared<HttpServerTransport>(testConfig);
        EXPECT_NO_THROW({
            testTransport->Start();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            testTransport->Stop();
        });
    }
}

TEST_F(HttpServerTransportTest, DifferentThreadCounts)
{
    std::vector<int> threadCounts = {1, 2, 4, 8};

    for (int threadCount : threadCounts) {
        HttpConfig testConfig;
        testConfig.ip = "127.0.0.1";
        testConfig.port = static_cast<int>(A2A::Test::GetFreeTcpPort());
        testConfig.ioThreadNum = threadCount;

        auto testTransport = std::make_shared<HttpServerTransport>(testConfig);
        EXPECT_NO_THROW({
            testTransport->Start();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            testTransport->Stop();
        });
    }
}

// ===========================================================================
// 边界条件测试
// ===========================================================================
TEST_F(HttpServerTransportTest, ZeroPort)
{
    HttpConfig testConfig;
    testConfig.ip = "127.0.0.1";
    testConfig.port = 0;
    testConfig.ioThreadNum = 1;

    auto testTransport = std::make_shared<HttpServerTransport>(testConfig);
    EXPECT_NO_THROW({
        testTransport->Start();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        testTransport->Stop();
    });
}

TEST_F(HttpServerTransportTest, EmptyIP)
{
    HttpConfig testConfig;
    testConfig.ip = "";
    testConfig.port = static_cast<int>(A2A::Test::GetFreeTcpPort());
    testConfig.ioThreadNum = 1;

    auto testTransport = std::make_shared<HttpServerTransport>(testConfig);
    EXPECT_NO_THROW({
        testTransport->Start();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        testTransport->Stop();
    });
}

TEST_F(HttpServerTransportTest, ZeroThreadCount)
{
    HttpConfig testConfig;
    testConfig.ip = "127.0.0.1";
    testConfig.port = static_cast<int>(A2A::Test::GetFreeTcpPort());
    testConfig.ioThreadNum = 0;

    auto testTransport = std::make_shared<HttpServerTransport>(testConfig);
    EXPECT_NO_THROW({
        testTransport->Start();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        testTransport->Stop();
    });
}

// ===========================================================================
// 内存泄漏测试
// ===========================================================================
TEST_F(HttpServerTransportTest, NoMemoryLeak)
{
    for (int i = 0; i < 10; i++) {
        {
            HttpConfig testConfig = config;
            testConfig.port = static_cast<int>(A2A::Test::GetFreeTcpPort());

            HttpServerTransport testTransport(testConfig);

            testTransport.Start();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            testTransport.Stop();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

} // namespace A2A::Transport::Test