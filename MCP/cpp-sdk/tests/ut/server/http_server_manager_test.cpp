/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include "server/http_server_manager.h"

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <atomic>

static constexpr int WAIT_TIME = 50;
static constexpr int DEFAULT_PORT = 8081;
static constexpr int THREAD_NUM = 10;
static constexpr int LOOP_NUM = 3;

namespace Mcp::Http {


class HttpServerManagerTest : public ::testing::Test {
public:
    ~HttpServerManagerTest() {}
protected:
    void SetUp() override
    {
        RouteMap routeMap;
        routeMap["/test"] = [](const HttpRequest& req, RequestContext& ctx) {
            HttpResponse resp;
            resp.statusCode = HTTP_STATUS_OK;
            resp.body = "Test";
            ctx.httpSendFunc(resp, ctx);
        };

        // 默认配置
        defaultConfig_.host = "127.0.0.1";
        defaultConfig_.port = DEFAULT_PORT;
        defaultConfig_.ioThreadNum = 1;
        defaultConfig_.tlsConfig_.enabled = false;
        defaultConfig_.routeMap = routeMap;
    }

    void TearDown() override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME));
    }

    HttpServerManagerConfig defaultConfig_;
};

TEST_F(HttpServerManagerTest, BasicStartStop)
{
    HttpServerManager manager(defaultConfig_);

    EXPECT_NO_THROW(manager.Start());

    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME));

    EXPECT_NO_THROW(manager.Stop());

    SUCCEED();
}

TEST_F(HttpServerManagerTest, MultipleStartCalls)
{
    HttpServerManager manager(defaultConfig_);

    EXPECT_NO_THROW(manager.Start());

    EXPECT_NO_THROW(manager.Start());

    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME));

    EXPECT_NO_THROW(manager.Start());

    EXPECT_NO_THROW(manager.Stop());

    SUCCEED();
}

TEST_F(HttpServerManagerTest, MultipleStopCalls)
{
    HttpServerManager manager(defaultConfig_);

    EXPECT_NO_THROW(manager.Start());

    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME));

    EXPECT_NO_THROW(manager.Stop());

    EXPECT_NO_THROW(manager.Stop());

    EXPECT_NO_THROW(manager.Stop());

    SUCCEED();
}

TEST_F(HttpServerManagerTest, ZeroIoThreads)
{
    HttpServerManagerConfig config = defaultConfig_;
    config.ioThreadNum = 0;

    HttpServerManager manager(config);

    EXPECT_NO_THROW(manager.Start());

    EXPECT_NO_THROW(manager.Stop());

    SUCCEED();
}

// 测试多个IO线程
TEST_F(HttpServerManagerTest, MultipleIoThreads)
{
    HttpServerManagerConfig config = defaultConfig_;
    config.ioThreadNum = LOOP_NUM;  // 多个线程

    HttpServerManager manager(config);

    EXPECT_NO_THROW(manager.Start());

    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME));

    EXPECT_NO_THROW(manager.Stop());

    SUCCEED();
}

// 测试析构函数自动停止
TEST_F(HttpServerManagerTest, DestructorAutoStop)
{
    // 在独立作用域中测试
    {
        HttpServerManager manager(defaultConfig_);
        EXPECT_NO_THROW(manager.Start());
        std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME));
        // 离开作用域时析构函数会自动调用Stop
    }

    // 等待确保资源释放
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME));
    SUCCEED();
}

// 测试停止后重新启动
TEST_F(HttpServerManagerTest, RestartAfterStop)
{
    HttpServerManager manager(defaultConfig_);

    // 第一次启动停止循环
    EXPECT_NO_THROW(manager.Start());
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME));
    EXPECT_NO_THROW(manager.Stop());

    // 等待一小段时间
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME));

    // 重新启动
    EXPECT_NO_THROW(manager.Start());
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME));
    EXPECT_NO_THROW(manager.Stop());

    SUCCEED();
}

// 测试空路由
TEST_F(HttpServerManagerTest, EmptyRouteMap)
{
    HttpServerManagerConfig config = defaultConfig_;
    config.routeMap.clear();  // 空路由

    HttpServerManager manager(config);

    EXPECT_NO_THROW(manager.Start());
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME));
    EXPECT_NO_THROW(manager.Stop());

    SUCCEED();
}

// 测试不同的主机配置
TEST_F(HttpServerManagerTest, DifferentHostConfigurations)
{
    // 测试不同主机名
    std::vector<std::string> hosts = {"127.0.0.1", "0.0.0.0", "localhost", ""};

    for (const auto& host : hosts) {
        HttpServerManagerConfig config = defaultConfig_;
        config.host = host;

        HttpServerManager manager(config);

        EXPECT_NO_THROW(manager.Start());
        std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME));
        EXPECT_NO_THROW(manager.Stop());

        // 等待一小段时间避免端口冲突
        std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME));
    }

    SUCCEED();
}

// 测试不同的端口
TEST_F(HttpServerManagerTest, DifferentPorts)
{
    // 使用不同的端口避免冲突
    std::vector<uint16_t> ports = {8081, 8082, 8083, 8084};

    for (auto port : ports) {
        HttpServerManagerConfig config = defaultConfig_;
        config.port = port;

        HttpServerManager manager(config);

        EXPECT_NO_THROW(manager.Start());
        std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME));
        EXPECT_NO_THROW(manager.Stop());

        // 等待一小段时间避免端口冲突
        std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME));
    }

    SUCCEED();
}

// 测试管理器生命周期多次循环
TEST_F(HttpServerManagerTest, MultipleLifecycles)
{
    // 测试多次创建和销毁管理器
    for (int i = 0; i < LOOP_NUM; i++) {
        {
            HttpServerManager manager(defaultConfig_);
            EXPECT_NO_THROW(manager.Start());
            std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME));
            // 析构时自动停止
        }

        // 等待确保资源释放
        std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME));
    }

    SUCCEED();
}

// 测试在未启动状态下调用Stop
TEST_F(HttpServerManagerTest, StopWithoutStart)
{
    HttpServerManager manager(defaultConfig_);

    // 未启动状态下调用Stop应该安全
    EXPECT_NO_THROW(manager.Stop());

    // 再次调用Stop应该仍然安全
    EXPECT_NO_THROW(manager.Stop());

    SUCCEED();
}

// 测试配置边界值
TEST_F(HttpServerManagerTest, BoundaryConfigValues)
{
    // 测试边界值：单个IO线程
    {
        HttpServerManagerConfig config = defaultConfig_;
        config.ioThreadNum = 1;

        HttpServerManager manager(config);
        EXPECT_NO_THROW(manager.Start());
        std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME));
        EXPECT_NO_THROW(manager.Stop());
    }

    // 测试边界值：多个IO线程
    {
        HttpServerManagerConfig config = defaultConfig_;
        config.ioThreadNum = THREAD_NUM;  // 相对较多的线程

        HttpServerManager manager(config);
        EXPECT_NO_THROW(manager.Start());
        std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME));
        EXPECT_NO_THROW(manager.Stop());
    }

    SUCCEED();
}

} // namespace Mcp::Http
