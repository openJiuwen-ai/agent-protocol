/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#include <chrono>
#include <future>
#include <functional>
#include <iostream>
#include <memory>
#include <thread>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <optional>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "mcp_client.h"
#include "mcp_server.h"
#include "mcp_type.h"
#include "mcp_log.h"

static constexpr int TEST_TIMEOUT_MS = 5000;
static constexpr int TEST_WAIT_TIME_MS = 300;

std::string g_loggingLevel = "null";

class McpTestServer {
public:
    static McpTestServer& Instance()
    {
        static McpTestServer instance;
        return instance;
    }

    bool Start()
    {
        if (running_) {return true;}
        try {
            if (!CreateServer()) {
                return false;
            }

        server_->RegisterSetLoggingLevelHandler(
            [](const std::string& lvl) {
                g_loggingLevel = lvl;
                return;
            }
        );

            if (!StartServer()) {
                return false;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(TEST_WAIT_TIME_MS));
            running_ = true;
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Server setup exception: " << e.what() << std::endl;
            return false;
        }
    }

    void Stop()
    {
        if (server_ && running_) {
            server_->Stop();
            running_ = false;
        }
    }

    bool IsRunning() const { return running_; }

private:
    McpTestServer() = default;
    ~McpTestServer() { Stop(); }

    // 创建服务器实例
    bool CreateServer()
    {
        // 创建服务器配置
        Mcp::ServerConfig config;
        config.name = "TestMCPServer";
        config.version = "1.0.0";
        config.workerThreads = 1;
        Mcp::StreamableHttpServerConfig streamableHttpConfig;
        streamableHttpConfig.ioThreads = 1;
        streamableHttpConfig.endpoint = "http://127.0.0.1:8001/mcp"; // 使用不同端口避免冲突
        streamableHttpConfig.isJsonResponseEnabled = true;
        streamableHttpConfig.tlsConfig.enabled = false;

        // 创建服务器实例
        server_ = Mcp::McpServerFactory::CreateStreamableHttpServer(config, streamableHttpConfig);
        if (server_ == nullptr) {
            std::cerr << "Failed to create MCP server instance" << std::endl;
            return false;
        }
        return true;
    }

    bool StartServer()
    {
        if (!server_->Run()) {
            std::cerr << "Failed to start MCP server" << std::endl;
            return false;
        }
        return true;
    }

private:
    std::unique_ptr<Mcp::McpServer> server_;
    std::atomic<bool> running_{false};
};

class McpIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        if (!McpTestServer::Instance().Start()) {
            FAIL() << "Failed to start test server";
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(TEST_WAIT_TIME_MS));
    }

    void TearDown() override {}

    std::shared_ptr<Mcp::McpClient> CreateTestClient()
    {
        Mcp::ClientConfig config;
        config.name = "TestClient";
        config.version = "1.0.0";

        Mcp::StreamableHttpClientConfig httpConfig;
        httpConfig.endpoint = "http://127.0.0.1:8001/mcp";
        httpConfig.timeout = std::chrono::milliseconds(TEST_TIMEOUT_MS);
        httpConfig.tlsConfig.enabled = false;

        return Mcp::McpClientFactory::CreateStreamableHttpClient(
            config, httpConfig, nullptr);
    }
};

TEST_F(McpIntegrationTest, SetLoggingLevel)
{
    auto client = CreateTestClient();
    ASSERT_NE(client, nullptr);
    // 先初始化
    auto initFuture = client->Initialize();
    initFuture.wait();
    // 测试设置日志级别
    auto lvFuture = client->SetLoggingLevel(Mcp::LoggingLevel::Debug);

    try {
        lvFuture.get();
        // 添加：等待服务器处理
        std::this_thread::sleep_for(std::chrono::milliseconds(TEST_WAIT_TIME_MS));
        ASSERT_EQ(g_loggingLevel, "debug");
    } catch (const std::exception& e) {
        FAIL() << "SetLoggingLevel failed with exception: " << e.what();
    }
}