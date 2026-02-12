/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <nlohmann/json.hpp>

#include "mcp_log.h"
#include "shared/common_type.h"
#include "mcp_type.h"
#include "client/mcp_client_implement.h"
#include "transport/stdio_transport.h"
#include "client/transport/streamable_http_client_transport.h"

using json = nlohmann::json;

static constexpr int TIMEOUT_MS = 1000;
static constexpr int LOOP_NUM = 3;

namespace Mcp {

// Test fixture for McpClientImplement tests
class McpClientImplementTest : public ::testing::Test {
public:
    ~McpClientImplementTest() {}
protected:
    void SetUp() override
    {
        // 创建基础配置
        config_.name = "TestClient";
        config_.version = "1.0";

        // 注意：新接口中StdioClientTransport使用ClientTransportConfig，其中包含stdioConfig字段
        stdioConfig_.command = "";
    }

    void TearDown() override
    {
        if (client_) {
            // 确保client被销毁
            client_.reset();
        }
    }

    ClientConfig config_;
    StreamableHttpClientConfig httpConfig_;
    StdioClientConfig stdioConfig_;
    std::shared_ptr<ClientTransport> transport = std::make_shared<StreamableHttpClientTransport>(
        httpConfig_.endpoint, httpConfig_.headers, httpConfig_.timeout,
        httpConfig_.sseTimeout, httpConfig_.tlsConfig);
    std::unique_ptr<McpClientImplement> client_;
};

// 构造函数测试
TEST_F(McpClientImplementTest, ConstructorWithConfig)
{
    EXPECT_NO_THROW(client_ = std::make_unique<McpClientImplement>(config_, transport));
    ASSERT_NE(client_, nullptr);
}

// Initialize方法测试
TEST_F(McpClientImplementTest, InitializeWithStdioTransport)
{
    client_ = std::make_unique<McpClientImplement>(config_, transport);
    ASSERT_NE(client_, nullptr);

    // 使用STDIO传输，应该能调用Initialize
    EXPECT_NO_THROW({
        auto future = client_->Initialize();
        EXPECT_TRUE(future.valid());
    });
}

TEST_F(McpClientImplementTest, InitializeWithStreamableHttpTransport)
{
    // 配置为STREAMABLE_HTTP传输
    httpConfig_.endpoint = "http://localhost:99999"; // 无效地址
    httpConfig_.headers = {};
    httpConfig_.timeout = std::chrono::milliseconds(TIMEOUT_MS);
    httpConfig_.sseTimeout = std::chrono::milliseconds(TIMEOUT_MS);
    httpConfig_.tlsConfig = {}; // 空TLS配置
    std::shared_ptr<ClientTransport> transport1 = std::make_shared<StreamableHttpClientTransport>(
        httpConfig_.endpoint, httpConfig_.headers, httpConfig_.timeout,
        httpConfig_.sseTimeout, httpConfig_.tlsConfig);
    client_ = std::make_unique<McpClientImplement>(config_, transport1);
    ASSERT_NE(client_, nullptr);

    // 应该能调用Initialize
    EXPECT_NO_THROW({
        auto future = client_->Initialize();
        EXPECT_TRUE(future.valid());
    });
}

TEST_F(McpClientImplementTest, DoubleInitialize)
{
    client_ = std::make_unique<McpClientImplement>(config_, transport);
    ASSERT_NE(client_, nullptr);

    // 第一次Initialize应该成功返回future
    EXPECT_NO_THROW({
        auto future1 = client_->Initialize();
        EXPECT_TRUE(future1.valid());

        // 第二次Initialize应该抛出异常
        EXPECT_THROW(client_->Initialize(), std::runtime_error);
    });
}

// 测试未初始化时调用方法
TEST_F(McpClientImplementTest, CallMethodBeforeInitialize)
{
    client_ = std::make_unique<McpClientImplement>(config_, transport);
    ASSERT_NE(client_, nullptr);

    // 在未初始化状态下调用方法应该抛出异常
    EXPECT_THROW(client_->ListTools(), std::runtime_error);
    EXPECT_THROW(client_->CallTool("test"), std::runtime_error);
    EXPECT_THROW(client_->ListPrompts(), std::runtime_error);
    EXPECT_THROW(client_->GetPrompt("test"), std::runtime_error);
    EXPECT_THROW(client_->SendRootsListChanged(), std::runtime_error);
    EXPECT_THROW(client_->SendPing(), std::runtime_error);
    EXPECT_THROW(client_->SetClientCapabilities({}), std::runtime_error);
    EXPECT_THROW(client_->GetServerCapabilities(), std::runtime_error);

    // 这些方法可能还未完全实现，但应该会抛出未初始化异常
    EXPECT_THROW(client_->SendProgressNotification("", 0, 0, ""), std::runtime_error);
    EXPECT_THROW(client_->Complete("", "", {}), std::runtime_error);

    // 资源相关方法
    EXPECT_THROW(client_->ListResources(), std::runtime_error);
    EXPECT_THROW(client_->ReadResource("test://resource"), std::runtime_error);
    EXPECT_THROW(client_->SubscribeResource("test://resource"), std::runtime_error);
    EXPECT_THROW(client_->UnsubscribeResource("test://resource"), std::runtime_error);
    EXPECT_THROW(client_->ListResourcesTemplates(), std::runtime_error);
}

// 测试资源相关方法
TEST_F(McpClientImplementTest, ResourceMethodsAfterInitialize)
{
    client_ = std::make_unique<McpClientImplement>(config_, transport);
    ASSERT_NE(client_, nullptr);

    // 先初始化
    EXPECT_NO_THROW({
        auto initFuture = client_->Initialize();
        EXPECT_TRUE(initFuture.valid());
    });

    // 资源方法应该可以调用
    EXPECT_NO_THROW({
        auto future1 = client_->ListResources();
        EXPECT_TRUE(future1.valid());

        auto future2 = client_->ReadResource("test://resource");
        EXPECT_TRUE(future2.valid());

        auto future3 = client_->SubscribeResource("test://resource");
        EXPECT_TRUE(future3.valid());

        auto future4 = client_->UnsubscribeResource("test://resource");
        EXPECT_TRUE(future4.valid());

        auto future5 = client_->ListResourcesTemplates();
        EXPECT_TRUE(future5.valid());
    });
}

// 测试CallTool的不同参数
TEST_F(McpClientImplementTest, CallToolWithArguments)
{
    client_ = std::make_unique<McpClientImplement>(config_, transport);
    ASSERT_NE(client_, nullptr);

    // 先初始化
    EXPECT_NO_THROW({
        auto initFuture = client_->Initialize();
        EXPECT_TRUE(initFuture.valid());
    });

    std::string toolName = "test_tool";
    json args = {{"param", "value"}};

    EXPECT_NO_THROW({
        // Convert JSON to string for the interface
        auto future = client_->CallTool(toolName, args.dump());
        EXPECT_TRUE(future.valid());
    });
}

TEST_F(McpClientImplementTest, CallToolWithTimeout)
{
    client_ = std::make_unique<McpClientImplement>(config_, transport);
    ASSERT_NE(client_, nullptr);

    // 先初始化
    EXPECT_NO_THROW({
        auto initFuture = client_->Initialize();
        EXPECT_TRUE(initFuture.valid());
    });

    std::string toolName = "test_tool";

    EXPECT_NO_THROW({
        auto future = client_->CallTool(toolName, std::nullopt, TIMEOUT_MS);
        EXPECT_TRUE(future.valid());
    });
}

// 测试GetPrompt的不同参数
TEST_F(McpClientImplementTest, GetPromptWithArguments)
{
    client_ = std::make_unique<McpClientImplement>(config_, transport);
    ASSERT_NE(client_, nullptr);

    // 先初始化
    EXPECT_NO_THROW({
        auto initFuture = client_->Initialize();
        EXPECT_TRUE(initFuture.valid());
    });

    std::string promptName = "test_prompt";
    json args = {{"param", "value"}};

    EXPECT_NO_THROW({
        // Convert JSON to string for the interface
        auto future = client_->GetPrompt(promptName, args.dump());
        EXPECT_TRUE(future.valid());
    });
}

// 测试空方法名
TEST_F(McpClientImplementTest, EdgeCaseEmptyToolName)
{
    client_ = std::make_unique<McpClientImplement>(config_, transport);
    ASSERT_NE(client_, nullptr);

    // 先初始化
    EXPECT_NO_THROW({
        auto initFuture = client_->Initialize();
        EXPECT_TRUE(initFuture.valid());
    });

    std::string emptyName = "";

    EXPECT_NO_THROW({
        auto future = client_->CallTool(emptyName);
        EXPECT_TRUE(future.valid());
    });
}

TEST_F(McpClientImplementTest, EdgeCaseEmptyPromptName)
{
    client_ = std::make_unique<McpClientImplement>(config_, transport);
    ASSERT_NE(client_, nullptr);

    // 先初始化
    EXPECT_NO_THROW({
        auto initFuture = client_->Initialize();
        EXPECT_TRUE(initFuture.valid());
    });

    std::string emptyName = "";

    EXPECT_NO_THROW({
        auto future = client_->GetPrompt(emptyName);
        EXPECT_TRUE(future.valid());
    });
}

// 测试SendPing方法
TEST_F(McpClientImplementTest, SendPingAfterInitialize)
{
    client_ = std::make_unique<McpClientImplement>(config_, transport);
    ASSERT_NE(client_, nullptr);

    // 先初始化
    EXPECT_NO_THROW({
        auto initFuture = client_->Initialize();
        EXPECT_TRUE(initFuture.valid());
    });

    EXPECT_NO_THROW({
        auto pingFuture = client_->SendPing();
        EXPECT_TRUE(pingFuture.valid());
    });
}

// 测试SendRootsListChanged方法
TEST_F(McpClientImplementTest, SendRootsListChangedAfterInitialize)
{
    client_ = std::make_unique<McpClientImplement>(config_, transport);
    ASSERT_NE(client_, nullptr);

    // 先初始化
    EXPECT_NO_THROW({
        auto initFuture = client_->Initialize();
        EXPECT_TRUE(initFuture.valid());
    });

    // 这个方法应该不会抛出异常
    EXPECT_NO_THROW(client_->SendRootsListChanged());
}

// 测试SetClientCapabilities方法（目前可能未完全实现）
TEST_F(McpClientImplementTest, SetClientCapabilitiesAfterInitialize)
{
    client_ = std::make_unique<McpClientImplement>(config_, transport);
    ASSERT_NE(client_, nullptr);

    // 先初始化
    EXPECT_NO_THROW({
        auto initFuture = client_->Initialize();
        EXPECT_TRUE(initFuture.valid());
    });

    McpClientCapabilities caps;
    // 目前这个方法可能只是检查初始化状态，不执行其他操作
    EXPECT_NO_THROW(client_->SetClientCapabilities(caps));
}

// 测试重复调用方法
TEST_F(McpClientImplementTest, MultipleMethodCalls)
{
    client_ = std::make_unique<McpClientImplement>(config_, transport);
    ASSERT_NE(client_, nullptr);

    // 先初始化
    EXPECT_NO_THROW({
        auto initFuture = client_->Initialize();
        EXPECT_TRUE(initFuture.valid());
    });

    // 多次调用相同方法
    EXPECT_NO_THROW({
        auto future1 = client_->ListTools();
        auto future2 = client_->ListTools();
        auto future3 = client_->ListTools();

        EXPECT_TRUE(future1.valid());
        EXPECT_TRUE(future2.valid());
        EXPECT_TRUE(future3.valid());
    });
}

// 测试快速连续调用
TEST_F(McpClientImplementTest, RapidMethodCalls)
{
    client_ = std::make_unique<McpClientImplement>(config_, transport);
    ASSERT_NE(client_, nullptr);

    // 先初始化
    EXPECT_NO_THROW({
        auto initFuture = client_->Initialize();
        EXPECT_TRUE(initFuture.valid());
    });

    // 快速连续调用多个方法
    EXPECT_NO_THROW({
        std::vector<std::future<std::shared_ptr<ListToolsResult>>> toolFutures;
        std::vector<std::future<std::shared_ptr<ListPromptsResult>>> promptFutures;

        for (int i = 0; i < LOOP_NUM; i++) {
            toolFutures.push_back(client_->ListTools());
            promptFutures.push_back(client_->ListPrompts());
        }

        for (const auto& future : toolFutures) {
            EXPECT_TRUE(future.valid());
        }

        for (const auto& future : promptFutures) {
            EXPECT_TRUE(future.valid());
        }
    });
}

// 测试子进程配置
TEST_F(McpClientImplementTest, SubprocessConfiguration)
{
    // 配置子进程命令
    stdioConfig_.command = "echo";
    stdioConfig_.args = {"hello", "world"};
    stdioConfig_.env = {{"TEST_ENV", "test_value"}};
    std::shared_ptr<ClientTransport> transport1 = std::make_shared<StdioClientTransport>(stdioConfig_);

    client_ = std::make_unique<McpClientImplement>(config_, transport1);
    ASSERT_NE(client_, nullptr);

    EXPECT_NO_THROW({
        auto initFuture = client_->Initialize();
        EXPECT_TRUE(initFuture.valid());
    });
}

} // namespace Mcp
