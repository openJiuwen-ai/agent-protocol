/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>
#include <chrono>
#include <future>
#include <thread>
#include <regex>

#include "server/server_manager.h"

static constexpr int TEST_ID = 123;
static constexpr int THREAD_NUM = 2;
static constexpr int LOOP_NUM = 3;
static constexpr int WAIT_TIME = 50;
static constexpr int DEFAULT_PORT = 9100;

namespace Mcp {

// 创建测试请求的辅助函数
HttpRequest CreateTestRequest()
{
    HttpRequest request;
    request.method = "POST";
    request.url = "/mcp";
    request.version = "HTTP/1.1";
    request.headers["Content-Type"] = "application/json";
    request.headers[Http::MCP_SESSION_ID_HEADER] = "test-session-123";
    request.body = R"({"jsonrpc": "2.0", "id": 1, "method": "initialize"})";
    return request;
}

// 创建测试上下文的辅助函数
RequestContext CreateTestContext()
{
    RequestContext ctx;
    ctx.connectionId = TEST_ID;
    ctx.sessionId = "test-session-123";
    return ctx;
}

// ServerManager的测试夹具
class ServerManagerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // 每个测试前的通用设置
    }

    void TearDown() override
    {
        // 每个测试后的清理
    }

    // 创建带Stdio配置的ServerManager
    std::unique_ptr<ServerManager> CreateStdioServerManager()
    {
        ServerConfig config;
        config.workerThreads = 1;
        return std::make_unique<ServerManager>(config);
    }

    // 创建带HTTP配置的ServerManager
    std::unique_ptr<ServerManager> CreateHttpServerManager()
    {
        ServerConfig config;
        config.workerThreads = 1;

        StreamableHttpServerConfig transportConfig;
        transportConfig.endpoint = "http://localhost:8080";
        transportConfig.ioThreads = 1;

        return std::make_unique<ServerManager>(config, transportConfig);
    }

    // 创建带自定义端口的ServerManager以避免端口冲突
    std::unique_ptr<ServerManager> CreateHttpServerManagerWithPort(int port)
    {
        ServerConfig config;
        config.workerThreads = 1;

        StreamableHttpServerConfig transportConfig;
        transportConfig.endpoint = "http://localhost:" + std::to_string(port);
        transportConfig.ioThreads = 1;

        return std::make_unique<ServerManager>(config, transportConfig);
    }
};

// 测试基本构造
TEST_F(ServerManagerTest, ConstructStdio)
{
    auto manager = CreateStdioServerManager();
    ASSERT_NE(manager, nullptr);
}

// 测试设置请求回调
TEST_F(ServerManagerTest, SetIncomingRequestCallback)
{
    auto manager = CreateStdioServerManager();

    bool callbackCalled = false;
    auto callback = [&callbackCalled](const RequestId&, const Request&, RequestContext&) {
        callbackCalled = true;
    };

    // 应该能成功设置回调
    manager->SetIncomingRequestCallback(callback);

    // 再次设置回调不应抛出异常
    EXPECT_NO_THROW(manager->SetIncomingRequestCallback(callback));
}

// 测试会话管理（获取不存在的会话）
TEST_F(ServerManagerTest, GetNonExistentSession)
{
    auto manager = CreateStdioServerManager();

    // 获取不存在的会话 - 应返回nullptr
    auto session = manager->GetSession("non-existent-session-id");
    EXPECT_EQ(session, nullptr);
}

// 测试停止未启动的manager
TEST_F(ServerManagerTest, StopWithoutStart)
{
    auto manager = CreateStdioServerManager();
    EXPECT_NO_THROW(manager->Stop());
}

// 测试不同线程数配置
TEST_F(ServerManagerTest, DifferentWorkerThreadCounts)
{
    // 测试1个线程
    {
        ServerConfig config;
        config.workerThreads = 1;

        StreamableHttpServerConfig transportConfig;
        transportConfig.endpoint = "http://localhost:8081";
        transportConfig.ioThreads = 1;

        ServerManager manager(config, transportConfig);
        EXPECT_NO_THROW(manager.Start());
        std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME));
        EXPECT_NO_THROW(manager.Stop());
    }

    // 测试3个线程
    {
        ServerConfig config;
        config.workerThreads = LOOP_NUM;

        StreamableHttpServerConfig transportConfig;
        transportConfig.endpoint = "http://localhost:8082";
        transportConfig.ioThreads = 1;

        ServerManager manager(config, transportConfig);
        EXPECT_NO_THROW(manager.Start());
        std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME));
        EXPECT_NO_THROW(manager.Stop());
    }
}

// 测试配置默认值
TEST_F(ServerManagerTest, DefaultConfigValues)
{
    ServerConfig config;

    // 验证默认值
    EXPECT_EQ(config.name, "MCP Server");
    EXPECT_EQ(config.version, "1.0.0");
    EXPECT_EQ(config.workerThreads, 1);
}

// 测试流式HTTP服务器配置默认值
TEST_F(ServerManagerTest, StreamableHttpServerConfigDefaults)
{
    StreamableHttpServerConfig config;

    // 验证默认值
    EXPECT_TRUE(config.endpoint.empty());
    EXPECT_FALSE(config.isJsonResponseEnabled);
    EXPECT_EQ(config.ioThreads, 1);
    EXPECT_FALSE(config.tlsConfig.enabled);
}

// 测试端点解析中的特殊字符
TEST_F(ServerManagerTest, EndpointWithSpecialChars)
{
    ServerConfig config;
    StreamableHttpServerConfig transportConfig;
    transportConfig.endpoint = "http://local#host:8080"; // 主机名中有特殊字符
    transportConfig.ioThreads = 1;

    // 这可能会解析失败，我们测试不会崩溃
    ServerManager manager(config, transportConfig);
    // 启动可能会抛出异常
    EXPECT_THROW(manager.Start(), std::runtime_error);
}

// 测试HTTP模式下的快速启动停止
TEST_F(ServerManagerTest, QuickStartStopHttp)
{
    ServerConfig config;
    config.workerThreads = 1;

    StreamableHttpServerConfig transportConfig;
    transportConfig.endpoint = "http://localhost:8092";
    transportConfig.ioThreads = 1;

    ServerManager manager(config, transportConfig);

    // 快速启动并立即停止
    EXPECT_NO_THROW(manager.Start());
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME));
    EXPECT_NO_THROW(manager.Stop());
}

// 测试并发启动停止（使用不同端口避免冲突）
TEST_F(ServerManagerTest, ConcurrentStartStopDifferentPorts)
{
    const int numThreads = LOOP_NUM;
    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};

    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&, i]() {
            try {
                ServerConfig config;
                config.workerThreads = 1;

                StreamableHttpServerConfig transportConfig;
                transportConfig.endpoint = "http://localhost:" + std::to_string(DEFAULT_PORT + i);
                transportConfig.ioThreads = 1;

                ServerManager manager(config, transportConfig);

                manager.Start();
                std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME));
                manager.Stop();

                successCount++;
            } catch (...) {
                // 记录失败但不增加计数
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // 所有线程应成功
    EXPECT_EQ(successCount.load(), numThreads);
}

// 测试带IPv6地址的端点
TEST_F(ServerManagerTest, EndpointWithIPv6)
{
    ServerConfig config;
    config.workerThreads = 1;

    StreamableHttpServerConfig transportConfig;
    transportConfig.endpoint = "http://[::1]:8080"; // IPv6地址
    transportConfig.ioThreads = 1;

    ServerManager manager(config, transportConfig);

    // IPv6地址应该能解析
    EXPECT_NO_THROW(manager.Start());
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME));
    EXPECT_NO_THROW(manager.Stop());
}

} // namespace Mcp
