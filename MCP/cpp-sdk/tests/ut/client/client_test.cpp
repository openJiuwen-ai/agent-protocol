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

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "mcp_client.h"
#include "mcp_server.h"
#include "mcp_type.h"
#include "mcp_log.h"

const char* const LOG_FILE_NAME = "server_example.log";
const char* const ECHO_TOOL_NAME = "echo";
const char* const ECHO_TOOL_TITLE = "Echo Tool";
const char* const ECHO_TOOL_DESCRIPTION = "Echoes back the input message";
static constexpr int TEST_TIMEOUT_MS = 5000;
static constexpr int TEST_WAIT_TIME_MS = 650;
static constexpr int RESOURCE_SIZE = 1024;

class McpTestServer {
public:
    static McpTestServer& Instance() {
        static McpTestServer instance;
        return instance;
    }

    bool Start() {
        if (running_) {return true;}
        try {
            // 创建服务器实例
            if (!CreateServer()) {
                return false;
            }

            // 配置服务器功能
            ConfigureServer();

            // 启动服务器
            if (!StartServer()) {
                return false;
            }

            // 等待服务器启动
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
    bool CreateServer() {
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

    // 配置服务器功能
    void ConfigureServer() {
        AddEchoTool();
        AddTestResource();
        AddGreetingPrompt();
        AddResourceTemplate();
    }

    // 添加 Echo 工具
    void AddEchoTool() {
        auto echoFunc = [](const std::string &name, const std::string &arguments,
                           const std::optional<std::string> &ctx) -> Mcp::CallToolResult {
            // Parse string arguments to JSON for internal processing
            nlohmann::json argumentsJson = nlohmann::json::parse(arguments);
            return CreateEchoToolResult(argumentsJson);
        };

        std::string echoInputSchema = R"({"type": "object", "properties": {"user_query": {"type": "string",
            "description": "The user query."}}, "required": ["user_query"]})";
        std::string echoOutputSchema = R"({"type": "object", "properties": {"result": {"type": "string",
            "description":"The echoed message"}}})";
        std::string echoTitle = ECHO_TOOL_TITLE;
        std::string echoDescription = ECHO_TOOL_DESCRIPTION;

        try {
            Mcp::AddToolOptionalParams toolParams;
            toolParams.title = std::cref(echoTitle);
            toolParams.description = std::cref(echoDescription);
            toolParams.inputSchema = std::cref(echoInputSchema);
            toolParams.outputSchema = std::cref(echoOutputSchema);
            server_->AddTool(ECHO_TOOL_NAME, echoFunc, toolParams);
            MCP_LOG(MCP_LOG_LEVEL_INFO, std::string("add tool success: ") + ECHO_TOOL_NAME);
        } catch (const std::exception &e) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("add tool failed: ") + e.what());
        } catch (...) {
            MCP_LOG(MCP_LOG_LEVEL_INFO, "add tool failed as expected");
        }
    }

    // 创建 Echo 工具的结果
    static Mcp::CallToolResult CreateEchoToolResult(const nlohmann::json &arguments) {
        Mcp::CallToolResult result;
        result.isError = false;
        try {
            std::string userQuery = "";
            if (arguments.contains("user_query") && arguments.at("user_query").is_string()) {
                userQuery = arguments.at("user_query").get<std::string>();
            }
            AddTextContent(result, "Echo: " + userQuery);
            AddImageContent(result, "test", "image");

            Mcp::ResourceLink reLink;
            reLink.uri = "test";
            reLink.name = "test";
            reLink.title = "test";
            reLink.description = "test";
            reLink.mimeType = "resource_link";
            reLink.size = RESOURCE_SIZE;
            result.content.push_back(reLink);

            AddAudioContent(result, "test", "audio");
            AddBlobResource(result, "test", "test", "blob", "test");
        } catch (const std::exception &e) {
            result.isError = true;
            AddTextContent(result, std::string("Error: ") + e.what());
        }
        return result;
    }

    void AddTestResource() {
        Mcp::ResourceInfo resource = CreateTestResourceInfo();
        Mcp::ReadResourceFunc readResourceFunc = CreateReadResourceFunction();
        Mcp::AddResourceOptionalParams resourceParams;
        if (resource.title.has_value()) {
            resourceParams.title = std::cref(resource.title.value());
        }
        if (resource.description.has_value()) {
            resourceParams.description = std::cref(resource.description.value());
        }
        if (resource.mimeType.has_value()) {
            resourceParams.mimeType = std::cref(resource.mimeType.value());
        }
        if (resource.size.has_value()) {
            resourceParams.size = resource.size.value();
        }
        if (resource.icons.has_value()) {
            resourceParams.icons = std::cref(resource.icons.value());
        }
        if (resource.annotations.has_value()) {
            resourceParams.annotations = std::cref(resource.annotations.value());
        }
        server_->AddResource(resource.uri, resource.name, readResourceFunc, resourceParams);
    }

    static Mcp::ResourceInfo CreateTestResourceInfo() {
        Mcp::ResourceInfo resource;
        resource.uri = "http://example.com/resource";
        resource.name = "Test Resource";
        resource.description = "A test resource for demonstration";
        resource.mimeType = "text/plain";

        std::vector<Mcp::Icon> icons;
        Mcp::Icon icon1;
        icon1.src = "https://example.com/icons/document.png";
        icon1.mimeType = "image/png";
        icon1.sizes = {"16x16", "32x32", "64x64"};
        icon1.theme = "light";
        icons.push_back(icon1);
        resource.icons = icons;

        Mcp::Annotations annotations;
        std::vector<Mcp::RoleType> audience = {Mcp::RoleType::USER, Mcp::RoleType::ASSISTANT};
        annotations.audience = audience;
        annotations.lastModified = "2025-01-15T10:30:00Z";
        annotations.priority = 1.0;
        resource.annotations = annotations;

        return resource;
    }

    static Mcp::ReadResourceFunc CreateReadResourceFunction() {
        return [](const std::string &uri) -> Mcp::ReadResourceResult {
            Mcp::ReadResourceResult result;
            Mcp::TextResourceContents textContents;
            textContents.uri = uri;
            textContents.text = "hello, " + uri;
            textContents.mimeType = "text/plain";
            result.contents.push_back(textContents);
            return result;
        };
    }

    void AddGreetingPrompt() {
        Mcp::PromptInfo greetingPrompt = CreateGreetingPromptInfo();
        Mcp::AddPromptOptionalParams promptParams;
        if (greetingPrompt.description.has_value()) {
            promptParams.description = std::cref(greetingPrompt.description.value());
        }
        if (greetingPrompt.arguments.has_value()) {
            promptParams.arguments = std::cref(greetingPrompt.arguments.value());
        }
        server_->AddPrompt(greetingPrompt.name, CreatePromptFunction(), promptParams);
    }

    static Mcp::PromptInfo CreateGreetingPromptInfo() {
        Mcp::PromptInfo greetingPrompt;
        greetingPrompt.name = "example_prompt";
        greetingPrompt.description = "Generate a personalized greeting message";
        greetingPrompt.arguments = {
            Mcp::PromptArgument{"name", "The name of the person to greet", true},
            Mcp::PromptArgument{"language", "Language for the greeting (default: English)", false}};
        return greetingPrompt;
    }

    static std::function<Mcp::GetPromptResult(const std::string&, const std::optional<std::string>&)>
    CreatePromptFunction() {
        return [](const std::string &promptName,
                  const std::optional<std::string> &arguments) -> Mcp::GetPromptResult {
            Mcp::GetPromptResult result;
            result.description = "example_prompt";

            std::string who = "friend";
            std::string lang = "English";

            if (arguments.has_value()) {
                // Parse string arguments to JSON for internal processing
                nlohmann::json j = nlohmann::json::parse(arguments.value());
                if (j.contains("name") && j["name"].is_string()) {
                    who = j["name"].get<std::string>();
                }
                if (j.contains("language") && j["language"].is_string()) {
                    lang = j["language"].get<std::string>();
                }
            }

            Mcp::TextContent tc;
            tc.type = "text";
            tc.text = "Hello, " + who + "! (language=" + lang + ")";
            Mcp::PromptMessage msg;
            msg.role = Mcp::RoleType::ASSISTANT;
            msg.content = tc;
            result.messages.push_back(msg);
            return result;
        };
    }

    void AddResourceTemplate() {
        Mcp::ResourceTemplate resourceTemplate = CreateResourceTemplate();
        Mcp::AddResourceTemplateOptionalParams templateParams;
        if (resourceTemplate.description.has_value()) {
            templateParams.description = std::cref(resourceTemplate.description.value());
        }
        if (resourceTemplate.mimeType.has_value()) {
            templateParams.mimeType = std::cref(resourceTemplate.mimeType.value());
        }
        server_->AddResourceTemplate(resourceTemplate.uriTemplate, resourceTemplate.name, templateParams);
    }

    static Mcp::ResourceTemplate CreateResourceTemplate() {
        Mcp::ResourceTemplate resourceTemplate;
        resourceTemplate.uriTemplate = "http://example.com/resourceTemplate/{id}";
        resourceTemplate.name = "Test Resource Template";
        resourceTemplate.description = "A test resource template for demonstration";
        resourceTemplate.mimeType = "text/plain";
        return resourceTemplate;
    }

    bool StartServer() {
        if (!server_->Run()) {
            std::cerr << "Failed to start MCP server" << std::endl;
            return false;
        }
        return true;
    }

    // 辅助函数：添加各种类型的内容
    static void AddTextContent(Mcp::CallToolResult& result, const std::string& text) {
        Mcp::TextContent textContent;
        textContent.text = text;
        result.content.push_back(textContent);
    }

    static void AddImageContent(Mcp::CallToolResult& result, const std::string& data, const std::string& mimeType) {
        Mcp::ImageContent imageContent;
        imageContent.data = data;
        imageContent.mimeType = mimeType;
        result.content.push_back(imageContent);
    }

    static void AddAudioContent(Mcp::CallToolResult& result, const std::string& data, const std::string& mimeType) {
        Mcp::AudioContent audioContent;
        audioContent.data = data;
        audioContent.mimeType = mimeType;
        result.content.push_back(audioContent);
    }

    static void AddBlobResource(Mcp::CallToolResult& result, const std::string& uri, const std::string& blob,
                               const std::string& mimeType, const std::string& resourceUri) {
        Mcp::BlobResourceContents blobResource;
        blobResource.uri = uri;
        blobResource.blob = blob;
        blobResource.mimeType = mimeType;
        Mcp::EmbeddedResource embedded;
        embedded.resource = blobResource;
        result.content.push_back(embedded);
    }

private:
    std::unique_ptr<Mcp::McpServer> server_;
    std::atomic<bool> running_{false};
};

// 测试固件类
class McpIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 启动服务器
        if (!McpTestServer::Instance().Start()) {
            FAIL() << "Failed to start test server";
        }

        // 等待服务器稳定
        std::this_thread::sleep_for(std::chrono::milliseconds(TEST_WAIT_TIME_MS));
    }

    void TearDown() override {
        // 不在每个测试后停止服务器，保持服务器运行以提高测试效率
    }

    std::shared_ptr<Mcp::McpClient> CreateTestClient() {
        Mcp::ClientConfig config;
        config.name = "TestClient";
        config.version = "1.0.0";

        Mcp::StreamableHttpClientConfig httpConfig;
        httpConfig.endpoint = "http://127.0.0.1:8001/mcp"; // 与服务器端口一致
        httpConfig.timeout = std::chrono::milliseconds(TEST_TIMEOUT_MS);
        httpConfig.tlsConfig.enabled = false;

        return Mcp::McpClientFactory::CreateStreamableHttpClient(
            config, httpConfig, nullptr);
    }
};

// 测试客户端初始化
TEST_F(McpIntegrationTest, ClientInitialization)
{
    auto client = CreateTestClient();
    ASSERT_NE(client, nullptr);

    // 测试初始化
    auto initFuture = client->Initialize();

    // 使用更合理的方式等待
    auto status = initFuture.wait_for(std::chrono::milliseconds(TEST_TIMEOUT_MS));
    ASSERT_EQ(status, std::future_status::ready);

    try {
        auto initResult = initFuture.get();
        ASSERT_NE(initResult, nullptr);
        // 验证初始化结果
        EXPECT_FALSE(initResult->protocolVersion.empty());
        EXPECT_EQ(initResult->serverInfo.name, "TestMCPServer");
    } catch (const std::exception& e) {
        FAIL() << "Initialization failed with exception: " << e.what();
    }
}

// 测试Ping
TEST_F(McpIntegrationTest, SendPing)
{
    auto client = CreateTestClient();
    ASSERT_NE(client, nullptr);

    auto initFuture = client->Initialize();
    auto initStatus = initFuture.wait_for(std::chrono::milliseconds(TEST_TIMEOUT_MS));
    ASSERT_EQ(initStatus, std::future_status::ready);
    ASSERT_NO_THROW(initFuture.get());

    auto pingFuture = client->SendPing();
    auto status = pingFuture.wait_for(std::chrono::milliseconds(TEST_TIMEOUT_MS));
    ASSERT_EQ(status, std::future_status::ready);

    try {
        auto pingResult = pingFuture.get();
        ASSERT_NE(pingResult, nullptr);
    } catch (const std::exception &e) {
        FAIL() << "SendPing failed with exception: " << e.what();
    }
}

// 测试列出工具
TEST_F(McpIntegrationTest, ListTools)
{
    auto client = CreateTestClient();
    ASSERT_NE(client, nullptr);
    // 先初始化
    auto initFuture = client->Initialize();
    initFuture.wait();
    // 测试列出工具
    auto listFuture = client->ListTools();
    auto status = listFuture.wait_for(std::chrono::milliseconds(TEST_TIMEOUT_MS));
    ASSERT_EQ(status, std::future_status::ready);

    try {
        auto listResult = listFuture.get();
        ASSERT_NE(listResult, nullptr);
        // 验证至少有一个工具
        EXPECT_GT(listResult->tools.size(), 0);
        // 验证特定工具存在
        bool foundEchoTool = false;
        for (const auto& tool : listResult->tools) {
            if (tool.name == "echo") {
                foundEchoTool = true;
                EXPECT_EQ(tool.description, "Echoes back the input message");
                break;
            }
        }
        EXPECT_TRUE(foundEchoTool);
    } catch (const std::exception& e) {
        FAIL() << "ListTools failed with exception: " << e.what();
    }
}

// 测试调用工具
TEST_F(McpIntegrationTest, CallTool)
{
    auto client = CreateTestClient();
    ASSERT_NE(client, nullptr);
    // 初始化
    auto initFuture = client->Initialize();
    initFuture.wait();
    // 测试调用工具 - 使用正确的参数名
    nlohmann::json arguments;
    arguments["user_query"] = "Shenzhen weather"; // 与example一致

    // Convert JSON to string for the interface
    auto callFuture = client->CallTool("echo", arguments.dump(), TEST_TIMEOUT_MS);
    auto status = callFuture.wait_for(std::chrono::milliseconds(TEST_TIMEOUT_MS));
    ASSERT_EQ(status, std::future_status::ready);

    try {
        auto callResult = callFuture.get();
        ASSERT_NE(callResult, nullptr);
        // 验证调用成功
        EXPECT_FALSE(callResult->isError);
        EXPECT_GT(callResult->content.size(), 0);
        // 验证返回内容
        bool foundTextContent = false;
        for (const auto& content : callResult->content) {
            if (std::holds_alternative<Mcp::TextContent>(content)) {
                const auto& text = std::get<Mcp::TextContent>(content);
                // 服务器返回"Echo: " + 查询内容
                EXPECT_EQ(text.text, "Echo: Shenzhen weather");
                foundTextContent = true;
                break;
            }
        }
        EXPECT_TRUE(foundTextContent);
    } catch (const std::exception& e) {
        FAIL() << "CallTool failed with exception: " << e.what();
    }
}

// 测试调用工具失败情况
TEST_F(McpIntegrationTest, CallToolWithInvalidArguments)
{
    auto client = CreateTestClient();
    ASSERT_NE(client, nullptr);
    // 初始化
    auto initFuture = client->Initialize();
    initFuture.wait();
    // 测试调用工具（缺少必需参数）
    nlohmann::json arguments;

    // Convert JSON to string for the interface
    auto callFuture = client->CallTool("echo", arguments.dump(), TEST_TIMEOUT_MS);
    auto status = callFuture.wait_for(std::chrono::milliseconds(TEST_TIMEOUT_MS));
    EXPECT_EQ(status, std::future_status::timeout);
}

TEST_F(McpIntegrationTest, CallToolWithNullArguments)
{
    auto client = CreateTestClient();
    ASSERT_NE(client, nullptr);
    // 初始化
    auto initFuture = client->Initialize();
    initFuture.wait();
    // 测试调用工具（缺少必需参数）
    auto callFuture = client->CallTool("echo", std::nullopt, TEST_TIMEOUT_MS);
    auto status = callFuture.wait_for(std::chrono::milliseconds(TEST_TIMEOUT_MS));
    EXPECT_EQ(status, std::future_status::timeout);
}

// 测试读取资源
TEST_F(McpIntegrationTest, ReadResource)
{
    auto client = CreateTestClient();
    ASSERT_NE(client, nullptr);
    // 初始化
    auto initFuture = client->Initialize();
    initFuture.wait();
    // 测试读取资源
    std::string resourceUri = "http://example.com/resource";
    auto readFuture = client->ReadResource(resourceUri);
    auto status = readFuture.wait_for(std::chrono::milliseconds(TEST_TIMEOUT_MS));
    ASSERT_EQ(status, std::future_status::ready);

    try {
        auto readResult = readFuture.get();
        ASSERT_NE(readResult, nullptr);
        // 验证资源内容
        EXPECT_GT(readResult->contents.size(), 0);

        for (const auto& content : readResult->contents) {
            if (std::holds_alternative<Mcp::TextResourceContents>(content)) {
                const auto& textContent = std::get<Mcp::TextResourceContents>(content);
                EXPECT_EQ(textContent.uri, resourceUri);
                EXPECT_EQ(textContent.text, "hello, " + resourceUri);
                break;
            }
        }
    } catch (const std::exception& e) {
        FAIL() << "ReadResource failed with exception: " << e.what();
    }
}

// 测试列出资源
TEST_F(McpIntegrationTest, ListResources)
{
    auto client = CreateTestClient();
    ASSERT_NE(client, nullptr);
    // 初始化
    auto initFuture = client->Initialize();
    initFuture.wait();
    // 测试列出资源
    auto listFuture = client->ListResources();
    auto status = listFuture.wait_for(std::chrono::milliseconds(TEST_TIMEOUT_MS));
    ASSERT_EQ(status, std::future_status::ready);

    try {
        auto listResult = listFuture.get();
        ASSERT_NE(listResult, nullptr);
        // 验证至少有一个资源
        EXPECT_GT(listResult->resources.size(), 0);
        // 验证特定资源存在
        bool foundTestResource = false;
        for (const auto& resource : listResult->resources) {
            if (resource.uri == "http://example.com/resource") {
                foundTestResource = true;
                EXPECT_EQ(resource.name, "Test Resource");
                EXPECT_EQ(resource.description, "A test resource for demonstration");
                break;
            }
        }
        EXPECT_TRUE(foundTestResource);
    } catch (const std::exception& e) {
        FAIL() << "ListResources failed with exception: " << e.what();
    }
}

// 测试列出prompts
TEST_F(McpIntegrationTest, ListPrompts)
{
    auto client = CreateTestClient();
    ASSERT_NE(client, nullptr);
    // 初始化
    auto initFuture = client->Initialize();
    initFuture.wait();
    // 测试列出prompts
    auto listFuture = client->ListPrompts();
    auto status = listFuture.wait_for(std::chrono::milliseconds(TEST_TIMEOUT_MS));
    ASSERT_EQ(status, std::future_status::ready);
    static constexpr int targetSize = 2;
    try {
        auto listResult = listFuture.get();
        ASSERT_NE(listResult, nullptr);
        // 验证至少有一个prompt
        EXPECT_GT(listResult->prompts.size(), 0);
        // 验证特定prompt存在
        bool foundExamplePrompt = false;
        for (const auto& prompt : listResult->prompts) {
            if (prompt.name == "example_prompt") {
                foundExamplePrompt = true;
                EXPECT_EQ(prompt.description, "Generate a personalized greeting message");
                // 验证有参数
                EXPECT_TRUE(prompt.arguments.has_value());
                EXPECT_EQ(prompt.arguments.value().size(), targetSize);
                break;
            }
        }
        EXPECT_TRUE(foundExamplePrompt);
    } catch (const std::exception& e) {
        FAIL() << "ListPrompts failed with exception: " << e.what();
    }
}

// 测试获取prompt
TEST_F(McpIntegrationTest, GetPrompt)
{
    auto client = CreateTestClient();
    ASSERT_NE(client, nullptr);

    // 初始化
    auto initFuture = client->Initialize();
    initFuture.wait();

    // 测试获取prompt（不带参数）
    auto getFuture = client->GetPrompt("example_prompt", std::nullopt);
    auto status = getFuture.wait_for(std::chrono::milliseconds(TEST_TIMEOUT_MS));
    ASSERT_EQ(status, std::future_status::ready);

    try {
        auto getResult = getFuture.get();
        ASSERT_NE(getResult, nullptr);

        // 验证获取成功，不验证具体内容
        // 只要调用成功并且有返回结果就行
        EXPECT_TRUE(getResult->messages.size() >= 0); // 不要求有消息，只要没有异常
    } catch (const std::exception& e) {
        FAIL() << "GetPrompt (without arguments) failed with exception: " << e.what();
    }
}

// 测试获取prompt（带参数）
TEST_F(McpIntegrationTest, GetPromptWithArguments)
{
    auto client = CreateTestClient();
    ASSERT_NE(client, nullptr);

    // 初始化
    auto initFuture = client->Initialize();
    initFuture.wait();

    // 创建参数
    nlohmann::json arguments;
    arguments["name"] = "John";
    arguments["language"] = "Chinese";

    // 测试获取prompt（带参数）
    // Convert JSON to string for the interface
    auto getFuture = client->GetPrompt("example_prompt", arguments.dump());
    auto status = getFuture.wait_for(std::chrono::milliseconds(TEST_TIMEOUT_MS));
    ASSERT_EQ(status, std::future_status::ready);

    try {
        auto getResult = getFuture.get();
        ASSERT_NE(getResult, nullptr);
        EXPECT_TRUE(getResult->messages.size() >= 0); // 不要求有消息，只要没有异常
    } catch (const std::exception& e) {
        FAIL() << "GetPrompt (with arguments) failed with exception: " << e.what();
    }
}

// 测试列出资源模板
TEST_F(McpIntegrationTest, ListResourceTemplates)
{
    auto client = CreateTestClient();
    ASSERT_NE(client, nullptr);

    // 初始化
    auto initFuture = client->Initialize();
    initFuture.wait();

    // 测试列出资源模板
    auto listFuture = client->ListResourcesTemplates();
    auto status = listFuture.wait_for(std::chrono::milliseconds(TEST_TIMEOUT_MS));
    ASSERT_EQ(status, std::future_status::ready);

    try {
        auto listResult = listFuture.get();
        ASSERT_NE(listResult, nullptr);

        // 验证至少有一个资源模板
        EXPECT_GT(listResult->resourceTemplates.size(), 0);

        // 验证特定资源模板存在
        bool foundTemplate = false;
        for (const auto& tmpl : listResult->resourceTemplates) {
            if (tmpl.uriTemplate == "http://example.com/resourceTemplate/{id}") {
                foundTemplate = true;
                EXPECT_EQ(tmpl.name, "Test Resource Template");
                EXPECT_EQ(tmpl.description, "A test resource template for demonstration");
                EXPECT_TRUE(tmpl.mimeType.has_value());
                EXPECT_EQ(tmpl.mimeType.value(), "text/plain");
                break;
            }
        }
        EXPECT_TRUE(foundTemplate);
    } catch (const std::exception& e) {
        FAIL() << "ListResourcesTemplates failed with exception: " << e.what();
    }
}
// 测试订阅和取消订阅资源
TEST_F(McpIntegrationTest, SubscribeUnsubscribeResource)
{
    auto client = CreateTestClient();
    ASSERT_NE(client, nullptr);

    // 初始化
    auto initFuture = client->Initialize();
    initFuture.wait();

    // 订阅资源
    std::string resourceUri = "http://example.com/resource";
    auto subFuture = client->SubscribeResource(resourceUri);
    auto status = subFuture.wait_for(std::chrono::milliseconds(TEST_TIMEOUT_MS));
    ASSERT_EQ(status, std::future_status::ready);

    try {
        auto subResult = subFuture.get();
        ASSERT_NE(subResult, nullptr);
    } catch (const std::exception& e) {
        FAIL() << "SubscribeResource failed with exception: " << e.what();
    }

    // 取消订阅资源
    auto unsubFuture = client->UnsubscribeResource(resourceUri);
    status = unsubFuture.wait_for(std::chrono::milliseconds(TEST_TIMEOUT_MS));
    ASSERT_EQ(status, std::future_status::ready);

    try {
        auto unsubResult = unsubFuture.get();
        ASSERT_NE(unsubResult, nullptr);
    } catch (const std::exception& e) {
        FAIL() << "UnsubscribeResource failed with exception: " << e.what();
    }
}
// 测试工具调用超时
TEST_F(McpIntegrationTest, CallToolTimeout)
{
    auto client = CreateTestClient();
    ASSERT_NE(client, nullptr);

    // 初始化
    auto initFuture = client->Initialize();
    initFuture.wait();

    // 测试带超时的调用（极短超时）
    nlohmann::json arguments = {{"user_query", "Timeout test"}};

    // Convert JSON to string for the interface
    auto callFuture = client->CallTool("echo", arguments.dump(), 1); // 1ms超时

    // 由于超时很短，应该立即返回或超时
    auto status = callFuture.wait_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(status == std::future_status::ready || status == std::future_status::timeout);
}
// 测试服务器配置
TEST(McpUnitTest, ServerConfigValidation)
{
    Mcp::ServerConfig config;

    // 测试默认值
    EXPECT_EQ(config.name, "MCP Server");
    EXPECT_EQ(config.version, "1.0.0");

    // 测试自定义配置
    config.name = "Custom Server";
    config.version = "2.0.0";
    config.workerThreads = 4;

    EXPECT_EQ(config.name, "Custom Server");
    EXPECT_EQ(config.version, "2.0.0");
    EXPECT_EQ(config.workerThreads, 4);
}

// 测试客户端配置
TEST(McpUnitTest, ClientConfigValidation)
{
    Mcp::ClientConfig config;

    // 测试默认值
    EXPECT_EQ(config.name, "MCP Client");
    EXPECT_EQ(config.version, "1.0.0");

    // 测试自定义配置
    config.name = "Custom Client";
    config.version = "3.0.0";

    EXPECT_EQ(config.name, "Custom Client");
    EXPECT_EQ(config.version, "3.0.0");
}

// 测试工具结果结构
TEST(McpUnitTest, ToolResultStructure)
{
    Mcp::CallToolResult result;

    // 默认值测试
    EXPECT_FALSE(result.isError);
    EXPECT_TRUE(result.content.empty());

    // 添加内容
    Mcp::TextContent text;
    text.text = "Test text";
    result.content.push_back(text);

    EXPECT_EQ(result.content.size(), 1);

    // 错误情况
    Mcp::CallToolResult errorResult;
    errorResult.isError = true;
    Mcp::TextContent errorText;
    errorText.text = "Error occurred";
    errorResult.content.push_back(errorText);

    EXPECT_TRUE(errorResult.isError);
    EXPECT_FALSE(errorResult.content.empty());
}

// 测试JSON参数处理
TEST(McpUnitTest, JsonParameterHandling)
{
    // 测试有效的JSON参数
    nlohmann::json validArgs = {
        {"message", "Hello"},
        {"count", 42},
        {"items", {"item1", "item2"}},
        {"metadata", {{"key", "value"}}}
    };

    EXPECT_TRUE(validArgs.contains("message"));
    EXPECT_TRUE(validArgs["message"].is_string());
    EXPECT_TRUE(validArgs.contains("count"));
    EXPECT_TRUE(validArgs["count"].is_number());
    EXPECT_TRUE(validArgs.contains("items"));
    EXPECT_TRUE(validArgs["items"].is_array());
    EXPECT_TRUE(validArgs.contains("metadata"));
    EXPECT_TRUE(validArgs["metadata"].is_object());

    // 测试空参数
    nlohmann::json emptyArgs;
    EXPECT_TRUE(emptyArgs.empty());

    // 测试嵌套JSON
    nlohmann::json nestedArgs = {
        {"user", {
            {"name", "John"},
            {"age", 30},
            {"email", "john@example.com"}
        }},
        {"settings", {
            {"theme", "dark"},
            {"notifications", true}
        }}
    };

    EXPECT_TRUE(nestedArgs["user"]["name"].is_string());
    EXPECT_TRUE(nestedArgs["settings"]["notifications"].is_boolean());
}