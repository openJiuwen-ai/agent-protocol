/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <fstream>
#include <thread>
#include <string>
#include <functional>

#include "server/mcp_server_implement.h"
#include "mcp_type.h"
#include "shared/jsonrpc.h"

using namespace Mcp;
using namespace testing;
using Json = nlohmann::json;


const char* const ECHO_TOOL_NAME = "echo";
const char* const ECHO_TOOL_TITLE = "Echo Tool";
const char* const ECHO_TOOL_DESCRIPTION = "Echoes back the input message";
const char* const PROMPT_DESCRIPTION = "Generate a personalized greeting message";
const char* const RESOURCE_DESCRIPTION = "A test resource for demonstration";
const char* const RESOURCE_MIME_TYPE = "text/plain";
const char* const RESOURCE_TEMPLATE_DESCRIPTION = "A test resource template for demonstration";
const char* const RESOURCE_TEMPLATE_MIME_TYPE = "text/plain";

static constexpr int LOOP_NUM = 3;

namespace {
    ServerTool CreateTestTool(const std::string& name = "test_tool") {
        ServerTool tool;
        auto echoFunc = [](const std::string &name, const std::string &arguments,
                           const std::optional<std::string> &ctx) -> Mcp::CallToolResult
        {
            Mcp::CallToolResult result;
            result.isError = false;
            try {
                std::string userQuery = "";
                // Parse string arguments to JSON for internal processing
                nlohmann::json argumentsJson = nlohmann::json::parse(arguments);
                if (argumentsJson.contains("user_query") && argumentsJson.at("user_query").is_string()) {
                    userQuery = argumentsJson.at("user_query").get<std::string>();
                }
                Mcp::TextContent textContent;
                textContent.text = "Echo: " + userQuery;
                result.content.push_back(textContent);
            } catch (const std::exception &e) {
                result.isError = true;
                Mcp::TextContent errorContent;
                errorContent.text = std::string("Error: ") + e.what();
                result.content.push_back(errorContent);
            }
            return result;
        };
        std::string echoInputSchema = R"({"type": "object", "properties": {"user_query": {"type": "string",
            "description": "The user query."}}, "required": ["user_query"]})";
        std::string echoOutputSchema = R"({"type": "object", "properties": {"result": {"type": "string", "description":
            "The echoed message"}}})";
        std::string echoTitle = ECHO_TOOL_TITLE;
        std::string echoDescription = ECHO_TOOL_DESCRIPTION;
        tool.name = ECHO_TOOL_NAME;
        tool.title = std::cref(echoTitle);
        tool.description = std::cref(echoDescription);
        tool.inputSchema = std::cref(echoInputSchema);
        tool.outputSchema = std::cref(echoOutputSchema);
        tool.func = echoFunc;
        return tool;
    }

    PromptInfo CreateTestPrompt(const std::string& name = "test_prompt")
    {
        PromptInfo prompt;
        prompt.name = name;
        prompt.description = "A test prompt";
        return prompt;
    }

    ResourceInfo CreateTestResource(const std::string& uri = "test://resource")
    {
        ResourceInfo resource;
        resource.uri = uri;
        resource.name = "Test Resource";
        return resource;
    }

    ResourceTemplate CreateTestResourceTemplate(const std::string& uriTemplate = "test://resource/{id}")
    {
        ResourceTemplate resTemplate;
        resTemplate.uriTemplate = uriTemplate;
        resTemplate.name = "Test Template";
        return resTemplate;
    }

    ServerConfig CreateValidStdioConfig()
    {
        return {"TestServer", "1.0.0", 1};
    }

    ServerConfig CreateValidHttpConfig()
    {
        return {"TestServer", "1.0.0", 4};
    }

    StreamableHttpServerConfig CreateValidTransportConfig()
    {
        StreamableHttpServerConfig config;
        config.endpoint = "http://localhost:8080";
        config.ioThreads = 1;
        config.tlsConfig.enabled = false;
        return config;
    }
}

TEST(McpServerImplementTest, Constructor_ValidStdioConfig)
{
    auto config = CreateValidStdioConfig();
    EXPECT_NO_THROW({
        McpServerImplement server(config);
        EXPECT_FALSE(server.IsRunning());
    });
}

TEST(McpServerImplementTest, Constructor_InvalidName)
{
    ServerConfig config;
    config.name = "";
    config.version = "1.0.0";

    EXPECT_THROW({
        McpServerImplement server(config);
    }, std::invalid_argument);
}

TEST(McpServerImplementTest, Constructor_InvalidVersion)
{
    ServerConfig config;
    config.name = "TestServer";
    config.version = "";

    EXPECT_THROW({
        McpServerImplement server(config);
    }, std::invalid_argument);
}

TEST(McpServerImplementTest, Constructor_InvalidEndpoint)
{
    ServerConfig config = CreateValidHttpConfig();
    StreamableHttpServerConfig transport;
    transport.endpoint = "";

    EXPECT_THROW({
        McpServerImplement server(config, transport);
    }, std::invalid_argument);
}

TEST(McpServerImplementTest, Lifecycle_StopWithoutStart)
{
    auto config = CreateValidStdioConfig();
    McpServerImplement server(config);

    EXPECT_NO_THROW(server.Stop());
    EXPECT_FALSE(server.IsRunning());
}

TEST(McpServerImplementTest, Lifecycle_CannotRestartAfterStop)
{
    ServerConfig config = CreateValidHttpConfig();
    StreamableHttpServerConfig transport = CreateValidTransportConfig();
    McpServerImplement server(config, transport);

    // 第一次启动应该成功
    EXPECT_TRUE(server.Run());
    EXPECT_TRUE(server.IsRunning());

    // 停止服务器
    EXPECT_NO_THROW(server.Stop());
    EXPECT_FALSE(server.IsRunning());

    // 尝试再次启动应该失败
    EXPECT_FALSE(server.Run());
    EXPECT_FALSE(server.IsRunning());
}

TEST(McpServerImplementTest, Lifecycle_CannotAddToolAfterStop)
{
    ServerConfig config = CreateValidHttpConfig();
    StreamableHttpServerConfig transport = CreateValidTransportConfig();
    McpServerImplement server(config, transport);

    // 启动服务器
    EXPECT_TRUE(server.Run());
    EXPECT_TRUE(server.IsRunning());

    // 停止服务器
    EXPECT_NO_THROW(server.Stop());
    EXPECT_FALSE(server.IsRunning());

    // 尝试添加工具应该失败
    auto tool = CreateTestTool();
    Mcp::AddToolOptionalParams toolParams;
    toolParams.description = tool.description;

    EXPECT_THROW(server.AddTool(tool.name, tool.func, toolParams), std::runtime_error);
}

TEST(McpServerImplementTest, ToolManagement_AddTool)
{
    auto config = CreateValidStdioConfig();
    McpServerImplement server(config);

    auto tool = CreateTestTool();

    Mcp::AddToolOptionalParams toolParams;
    toolParams.description = tool.description;
    toolParams.inputSchema = tool.inputSchema;
    toolParams.outputSchema = tool.outputSchema;

    EXPECT_NO_THROW(server.AddTool(tool.name, tool.func, toolParams));
}

TEST(McpServerImplementTest, ToolManagement_RemoveTool)
{
    auto config = CreateValidStdioConfig();
    McpServerImplement server(config);

    auto tool = CreateTestTool();

    Mcp::AddToolOptionalParams toolParams;
    toolParams.description = tool.description;
    toolParams.inputSchema = tool.inputSchema;
    toolParams.outputSchema = tool.outputSchema;

    EXPECT_NO_THROW(server.AddTool(tool.name, tool.func, toolParams));
    EXPECT_NO_THROW(server.RemoveTool(tool.name));
}

TEST(McpServerImplementTest, ToolManagement_AddMultipleTools) {
    auto config = CreateValidStdioConfig();
    McpServerImplement server(config);

    for (int i = 0; i < LOOP_NUM; ++i) {
        auto tool = CreateTestTool("tool_" + std::to_string(i));

        Mcp::AddToolOptionalParams toolParams;
        toolParams.description = tool.description;
        toolParams.inputSchema = tool.inputSchema;
        toolParams.outputSchema = tool.outputSchema;

        EXPECT_NO_THROW(server.AddTool(tool.name, tool.func, toolParams));
    }
}

// 测试提示管理
TEST(McpServerImplementTest, PromptManagement_AddPrompt)
{
    auto config = CreateValidStdioConfig();
    McpServerImplement server(config);

    auto prompt = CreateTestPrompt();

    std::string promptDescription = PROMPT_DESCRIPTION;
    Mcp::AddPromptOptionalParams promptParams;
    promptParams.description = std::cref(promptDescription);
    const std::vector<Mcp::PromptArgument> promptArgs = {
        Mcp::PromptArgument{"name", "The name of the person to greet", true},
        Mcp::PromptArgument{"language", "Language for the greeting (default: English)", false}};
    promptParams.arguments = std::cref(promptArgs);

    EXPECT_NO_THROW(server.AddPrompt(prompt.name,
                                    [](const std::string &promptName,
                                    const std::optional<std::string> &arguments) -> Mcp::GetPromptResult {
                                    Mcp::GetPromptResult result;
                                    (void)promptName;
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
                                    }, promptParams));
}

TEST(McpServerImplementTest, PromptManagement_RemovePrompt)
{
    auto config = CreateValidStdioConfig();
    McpServerImplement server(config);

    auto prompt = CreateTestPrompt();

        std::string promptDescription = PROMPT_DESCRIPTION;
    Mcp::AddPromptOptionalParams promptParams;
    promptParams.description = std::cref(promptDescription);
    const std::vector<Mcp::PromptArgument> promptArgs = {
        Mcp::PromptArgument{"name", "The name of the person to greet", true},
        Mcp::PromptArgument{"language", "Language for the greeting (default: English)", false}};
    promptParams.arguments = std::cref(promptArgs);

    EXPECT_NO_THROW(server.AddPrompt(prompt.name,
                                    [](const std::string &promptName,
                                    const std::optional<std::string> &arguments) -> Mcp::GetPromptResult {
                                    Mcp::GetPromptResult result;
                                    (void)promptName;
                                    std::string who = "friend";
                                    std::string lang = "English";

                                    if (arguments.has_value()) {
                                        // Parse string arguments to JSON for internal processing
                                        Mcp::JsonValue j = Mcp::JsonValue::parse(arguments.value());
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
                                    }, promptParams));
    EXPECT_NO_THROW(server.RemovePrompt(prompt.name));
}

TEST(McpServerImplementTest, ResourceManagement_AddResource)
{
    auto config = CreateValidStdioConfig();
    McpServerImplement server(config);

    auto resource = CreateTestResource();

    std::string resourceDescription = RESOURCE_DESCRIPTION;
    std::string resourceMimeType = RESOURCE_MIME_TYPE;
    Mcp::AddResourceOptionalParams resourceParams;
    resourceParams.description = std::cref(resourceDescription);
    resourceParams.mimeType = std::cref(resourceMimeType);
    const std::vector<Mcp::Icon> resourceIcons{Mcp::Icon{.src = "http://example.com/icon.png",
         .mimeType = "image/png", .sizes = std::vector<std::string>{"32x32", "64x64"}, .theme = "light"}};
    resourceParams.icons = std::cref(resourceIcons);

    Mcp::ReadResourceFunc readResourceFunc = [](const std::string &uri) -> Mcp::ReadResourceResult {
        Mcp::ReadResourceResult result;
        Mcp::TextResourceContents textContents;
        return result;
    };

    EXPECT_NO_THROW(server.AddResource(resource.uri, resource.name, readResourceFunc, resourceParams));
    EXPECT_NO_THROW(server.AddResource(resource.uri, resource.name, readResourceFunc, resourceParams));
}

TEST(McpServerImplementTest, ResourceManagement_RemoveResource)
{
    auto config = CreateValidStdioConfig();
    McpServerImplement server(config);

    auto resource = CreateTestResource();

    std::string resourceDescription = RESOURCE_DESCRIPTION;
    std::string resourceMimeType = RESOURCE_MIME_TYPE;
    Mcp::AddResourceOptionalParams resourceParams;
    resourceParams.description = std::cref(resourceDescription);
    resourceParams.mimeType = std::cref(resourceMimeType);
    const std::vector<Mcp::Icon> resourceIcons{Mcp::Icon{.src = "http://example.com/icon.png",
         .mimeType = "image/png", .sizes = std::vector<std::string>{"32x32", "64x64"}, .theme = "light"}};
    resourceParams.icons = std::cref(resourceIcons);

    Mcp::ReadResourceFunc readResourceFunc = [](const std::string &uri) -> Mcp::ReadResourceResult {
        Mcp::ReadResourceResult result;
        Mcp::TextResourceContents textContents;
        return result;
    };

    EXPECT_NO_THROW(server.AddResource(resource.uri, resource.name, readResourceFunc, resourceParams));
    EXPECT_NO_THROW(server.RemoveResource(resource.uri));
}

// 测试资源模板管理
TEST(McpServerImplementTest, ResourceTemplateManagement_AddTemplate)
{
    auto config = CreateValidStdioConfig();
    McpServerImplement server(config);

    auto resTemplate = CreateTestResourceTemplate();

    std::string resourceTemplateDescription = RESOURCE_TEMPLATE_DESCRIPTION;
    std::string resourceTemplateMimeType = RESOURCE_TEMPLATE_MIME_TYPE;
    Mcp::AddResourceTemplateOptionalParams resourceTemplateParams;
    resourceTemplateParams.description = std::cref(resourceTemplateDescription);
    resourceTemplateParams.mimeType = std::cref(resourceTemplateMimeType);

    EXPECT_NO_THROW(server.AddResourceTemplate(resTemplate.uriTemplate, resTemplate.name, resourceTemplateParams));
}

TEST(McpServerImplementTest, ResourceTemplateManagement_RemoveTemplate) {
    auto config = CreateValidStdioConfig();
    McpServerImplement server(config);

    auto resTemplate = CreateTestResourceTemplate();

    std::string resourceTemplateDescription = RESOURCE_TEMPLATE_DESCRIPTION;
    std::string resourceTemplateMimeType = RESOURCE_TEMPLATE_MIME_TYPE;
    Mcp::AddResourceTemplateOptionalParams resourceTemplateParams;
    resourceTemplateParams.description = std::cref(resourceTemplateDescription);
    resourceTemplateParams.mimeType = std::cref(resourceTemplateMimeType);

    EXPECT_NO_THROW(server.AddResourceTemplate(resTemplate.uriTemplate, resTemplate.name, resourceTemplateParams));
    EXPECT_NO_THROW(server.RemoveResourceTemplate(resTemplate.uriTemplate));
}

// 测试特殊字符配置
TEST(McpServerImplementTest, SpecialCharacters_SpacesInName)
{
    ServerConfig config;
    config.name = "Test Server";
    config.version = "1.0.0";

    EXPECT_THROW({
        McpServerImplement server(config);
    }, std::invalid_argument);
}

TEST(McpServerImplementTest, SpecialCharacters_SpacesInEndpoint)
{
    ServerConfig config = CreateValidHttpConfig();
    StreamableHttpServerConfig transport;
    transport.endpoint = "http://localhost:8080 ";

    EXPECT_THROW({
        McpServerImplement server(config, transport);
    }, std::invalid_argument);
}

TEST(McpServerImplementTest, VersionFormat_ValidVersion)
{
    ServerConfig config;
    config.name = "TestServer";
    config.version = "1.2.3";

    EXPECT_NO_THROW({
        McpServerImplement server(config);
    });
}

TEST(McpServerImplementTest, VersionFormat_InvalidVersionNoDot)
{
    ServerConfig config;
    config.name = "TestServer";
    config.version = "123";

    EXPECT_THROW({
        McpServerImplement server(config);
    }, std::invalid_argument);
}

TEST(McpServerImplementTest, ThreadCount_ExceedMax)
{
    static constexpr int maxThreadNum = 65;
    ServerConfig config = CreateValidHttpConfig();
    config.workerThreads = maxThreadNum;

    auto transport = CreateValidTransportConfig();
    transport.ioThreads = maxThreadNum;

    EXPECT_THROW({
        McpServerImplement server(config, transport);
    }, std::invalid_argument);
}

TEST(McpServerImplementTest, ToolManagement_AddSameToolMultipleTimes)
{
    auto config = CreateValidStdioConfig();
    McpServerImplement server(config);

    auto tool = CreateTestTool();

    Mcp::AddToolOptionalParams toolParams;
    toolParams.description = tool.description;
    toolParams.inputSchema = tool.inputSchema;
    toolParams.outputSchema = tool.outputSchema;

    EXPECT_NO_THROW(server.AddTool(tool.name, tool.func, toolParams));
    EXPECT_NO_THROW(server.AddTool(tool.name, tool.func, toolParams));
    EXPECT_NO_THROW(server.AddTool(tool.name, tool.func, toolParams));

    EXPECT_NO_THROW(server.RemoveTool(tool.name));
}
