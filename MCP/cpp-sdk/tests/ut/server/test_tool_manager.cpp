/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>
#include <stdexcept>
#include <thread>
#include <future>

#include "server/tool_manager.h"

namespace Mcp {

static CallToolResult TestToolFunc(const Mcp::ServerContext &ctx [[maybe_unused]], const std::string& name,
    const JsonValue& arguments)
{
    CallToolResult result;
    result.isError = false;

    TextContent textContent;
    textContent.text = R"(Tool )" + name + R"( executed with arguments: )" + arguments.dump();

    result.content.push_back(textContent);

    return result;
}

static CallToolResult TestCallbackToolFunc(const Mcp::ServerContext &ctx [[maybe_unused]], const std::string& name,
    const JsonValue& arguments [[maybe_unused]])
{
    CallToolResult result;
    result.isError = false;

    TextContent textContent;
    textContent.text = R"({"callback": "yes", "tool": ")" + name + "\"}";

    result.content.push_back(textContent);
    return result;
}

class ToolManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        toolManager = std::make_unique<ToolManager>(true);
    }

    void TearDown() override {
        toolManager.reset();
    }

    ToolInfo CreateTestTool(const std::string& name = "test_tool",
                           const std::string& description = "Test tool description") {
        ToolInfo tool;
        tool.name = name;
        tool.description = description;
        tool.inputSchema = R"({"type": "object", "properties": {}})"_json;
        tool.func = SyncToolFunc(TestToolFunc);
        return tool;
    }

    ToolInfo CreateComplexSchemaTool(const std::string& name = "complex_tool") {
        ToolInfo tool;
        tool.name = name;
        tool.description = "Tool with complex input schema";
        tool.inputSchema = R"({
            "type": "object",
            "properties": {
                "param1": {"type": "string"},
                "param2": {"type": "number"},
                "param3": {"type": "boolean"}
            },
            "required": ["param1"]
        })"_json;
        tool.func = SyncToolFunc(TestToolFunc);
        return tool;
    }

    ToolInfo CreateErrorThrowingTool(const std::string& name = "error_tool") {
        auto errorFunc = [](const Mcp::ServerContext &ctx [[maybe_unused]], const std::string& name,
            const JsonValue& arguments [[maybe_unused]]) -> CallToolResult {
            if (name == "error_tool") {
                throw std::runtime_error("Simulated tool error");
            }

            CallToolResult result;
            result.isError = false;

            TextContent textContent;
            textContent.text = "Normal execution";
            result.content.push_back(textContent);

            return result;
        };

        ToolInfo tool;
        tool.name = name;
        tool.description = "Tool that throws exceptions";
        tool.inputSchema = R"({"type": "object"})"_json;
        tool.func = SyncToolFunc(errorFunc);
        return tool;
    }

    std::unique_ptr<ToolManager> toolManager;
};

TEST_F(ToolManagerTest, ConstructorWithOverwriteTrue) {
    ToolManager manager(true);
    EXPECT_TRUE(manager.GetOverwrite());
}

TEST_F(ToolManagerTest, ConstructorWithOverwriteFalse) {
    ToolManager manager(false);
    EXPECT_FALSE(manager.GetOverwrite());
}

TEST_F(ToolManagerTest, SetOverwrite) {
    EXPECT_TRUE(toolManager->GetOverwrite());

    toolManager->SetOverwrite(false);
    EXPECT_FALSE(toolManager->GetOverwrite());

    toolManager->SetOverwrite(true);
    EXPECT_TRUE(toolManager->GetOverwrite());
}

TEST_F(ToolManagerTest, DefaultConstructor) {
    ToolManager manager; // 使用默认参数（true）
    EXPECT_TRUE(manager.GetOverwrite());
}

TEST_F(ToolManagerTest, AddToolSuccess) {
    ToolInfo tool = CreateTestTool();

    EXPECT_NO_THROW(toolManager->AddTool(tool));
}

TEST_F(ToolManagerTest, AddToolWithComplexSchema) {
    ToolInfo tool = CreateComplexSchemaTool();

    EXPECT_NO_THROW(toolManager->AddTool(tool));
}

TEST_F(ToolManagerTest, AddToolEmptyName) {
    ToolInfo tool = CreateTestTool();
    tool.name = "";

    EXPECT_THROW(toolManager->AddTool(tool), std::invalid_argument);
}

TEST_F(ToolManagerTest, AddToolEmptyDescription) {
    ToolInfo tool = CreateTestTool();
    tool.description = "";

    EXPECT_THROW(toolManager->AddTool(tool), std::invalid_argument);
}

TEST_F(ToolManagerTest, AddToolNullFunction) {
    ToolInfo tool = CreateTestTool();
    tool.func = SyncToolFunc{};  // 空的 function 对象

    EXPECT_THROW(toolManager->AddTool(tool), std::invalid_argument);
}

TEST_F(ToolManagerTest, AddToolOverwriteWhenOverwriteTrue) {
    ToolInfo tool1 = CreateTestTool("same_tool", "First description");
    ToolInfo tool2 = CreateTestTool("same_tool", "Second description");
    tool2.func = SyncToolFunc(TestCallbackToolFunc);

    EXPECT_NO_THROW(toolManager->AddTool(tool1));

    EXPECT_NO_THROW(toolManager->AddTool(tool2));
}

TEST_F(ToolManagerTest, AddToolNoOverwriteWhenOverwriteFalse) {
    toolManager->SetOverwrite(false);

    ToolInfo tool1 = CreateTestTool("same_tool", "First description");
    ToolInfo tool2 = CreateTestTool("same_tool", "Second description");

    EXPECT_NO_THROW(toolManager->AddTool(tool1));

    EXPECT_THROW(toolManager->AddTool(tool2), std::runtime_error);
}

TEST_F(ToolManagerTest, AddMultipleTools) {
    std::vector<ToolInfo> tools;

    for (int i = 0; i < 5; i++) {
        ToolInfo tool = CreateTestTool("tool_" + std::to_string(i),
                                      "Tool " + std::to_string(i));
        EXPECT_NO_THROW(toolManager->AddTool(tool));
    }
}

TEST_F(ToolManagerTest, RemoveToolSuccess) {
    ToolInfo tool = CreateTestTool("tool_to_remove");

    toolManager->AddTool(tool);
    EXPECT_NO_THROW(toolManager->RemoveTool("tool_to_remove"));
}

TEST_F(ToolManagerTest, RemoveToolEmptyName) {
    EXPECT_THROW(toolManager->RemoveTool(""), std::invalid_argument);
}

TEST_F(ToolManagerTest, RemoveNonExistentTool) {
    EXPECT_THROW(toolManager->RemoveTool("non_existent_tool"), std::runtime_error);
}

TEST_F(ToolManagerTest, RemoveToolMultipleTimes) {
    ToolInfo tool = CreateTestTool("tool_to_remove");

    toolManager->AddTool(tool);
    EXPECT_NO_THROW(toolManager->RemoveTool("tool_to_remove"));

    EXPECT_THROW(toolManager->RemoveTool("tool_to_remove"), std::runtime_error);
}

TEST_F(ToolManagerTest, ListToolsEmpty) {
    ListToolsResult result = toolManager->ListTools();
    EXPECT_EQ(result.tools.size(), 0);
}

TEST_F(ToolManagerTest, ListToolsSingleTool) {
    ToolInfo tool = CreateTestTool("tool1", "First tool");
    toolManager->AddTool(tool);

    ListToolsResult result = toolManager->ListTools();
    ASSERT_EQ(result.tools.size(), 1);
    EXPECT_EQ(result.tools[0].name, "tool1");
    EXPECT_EQ(result.tools[0].description, "First tool");
    EXPECT_FALSE(result.tools[0].inputSchema.is_null());
}

TEST_F(ToolManagerTest, ListToolsMultipleTools) {
    const int numTools = 5;

    for (int i = 0; i < numTools; i++) {
        ToolInfo tool = CreateTestTool("tool_" + std::to_string(i),
                                      "Tool " + std::to_string(i));
        toolManager->AddTool(tool);
    }

    ListToolsResult result = toolManager->ListTools();
    EXPECT_EQ(result.tools.size(), numTools);

    // 检查所有工具都包含在列表中
    std::set<std::string> expectedNames;
    for (int i = 0; i < numTools; i++) {
        expectedNames.insert("tool_" + std::to_string(i));
    }

    std::set<std::string> actualNames;
    for (const auto& tool : result.tools) {
        actualNames.insert(tool.name);
    }

    EXPECT_EQ(expectedNames, actualNames);
}

TEST_F(ToolManagerTest, ListToolsAfterRemove) {
    for (int i = 0; i < 3; i++) {
        ToolInfo tool = CreateTestTool("tool_" + std::to_string(i));
        toolManager->AddTool(tool);
    }

    toolManager->RemoveTool("tool_1");

    ListToolsResult result = toolManager->ListTools();
    EXPECT_EQ(result.tools.size(), 2);

    std::set<std::string> expectedNames = {"tool_0", "tool_2"};
    std::set<std::string> actualNames;
    for (const auto& tool : result.tools) {
        actualNames.insert(tool.name);
    }

    EXPECT_EQ(expectedNames, actualNames);
}

TEST_F(ToolManagerTest, CallToolSuccess) {
    ToolInfo tool = CreateTestTool("test_tool");
    toolManager->AddTool(tool);

    JsonValue arguments = R"({"param": "value"})"_json;
    const Mcp::ServerContext context = {};
    auto optResult = toolManager->CallTool(context, "test_tool", arguments.dump());
    ASSERT_TRUE(optResult.has_value());
    CallToolResult result = optResult.value();

    EXPECT_FALSE(result.isError);
    EXPECT_FALSE(result.content.empty());
    EXPECT_TRUE(std::holds_alternative<TextContent>(result.content[0]));

    auto& textContent = std::get<TextContent>(result.content[0]);
    EXPECT_EQ(textContent.type, "text");
    EXPECT_FALSE(textContent.text.empty());
}

TEST_F(ToolManagerTest, CallToolWithJsonArguments) {
    ToolInfo tool = CreateTestTool("json_tool");
    toolManager->AddTool(tool);

    std::string arguments = R"({"key1": "value1", "key2": 123, "key3": true})";
    const Mcp::ServerContext context = {};
    auto optResult = toolManager->CallTool(context, "json_tool", arguments);
    ASSERT_TRUE(optResult.has_value());
    CallToolResult result = optResult.value();

    EXPECT_FALSE(result.isError);
    EXPECT_FALSE(result.content.empty());
}

TEST_F(ToolManagerTest, CallNonExistentTool) {
    const Mcp::ServerContext context = {};
    EXPECT_THROW(toolManager->CallTool(context, "non_existent_tool", R"({})"), std::runtime_error);
}

TEST_F(ToolManagerTest, CallToolEmptyName) {
    const Mcp::ServerContext context = {};
    EXPECT_THROW(toolManager->CallTool(context, "", R"({})"), std::runtime_error);
}

TEST_F(ToolManagerTest, CallToolThrowsException) {
    ToolInfo tool = CreateErrorThrowingTool("error_tool");
    toolManager->AddTool(tool);

    const Mcp::ServerContext context = {};
    EXPECT_THROW(toolManager->CallTool(context, "error_tool", R"({})"), std::runtime_error);
}

TEST_F(ToolManagerTest, CallToolAfterRemove) {
    ToolInfo tool = CreateTestTool("temp_tool");
    toolManager->AddTool(tool);

    const Mcp::ServerContext context = {};
    EXPECT_NO_THROW(toolManager->CallTool(context, "temp_tool", R"({})"));

    toolManager->RemoveTool("temp_tool");

    EXPECT_THROW(toolManager->CallTool(context, "temp_tool", R"({})"), std::runtime_error);
}

TEST_F(ToolManagerTest, CallToolAfterOverwrite) {
    ToolInfo tool1 = CreateTestTool("tool", "First tool");
    ToolInfo tool2 = CreateTestTool("tool", "Second tool");
    tool2.func = SyncToolFunc(TestCallbackToolFunc);

    toolManager->AddTool(tool1);
    toolManager->AddTool(tool2);

    const Mcp::ServerContext context = {};
    auto optResult = toolManager->CallTool(context, "tool", R"({})");
    ASSERT_TRUE(optResult.has_value());
    CallToolResult result = optResult.value();

    EXPECT_FALSE(result.isError);
    EXPECT_FALSE(result.content.empty());
}

TEST_F(ToolManagerTest, AddToolWithVeryLongName) {
    std::string longName(1000, 'a'); // 1000个'a'
    ToolInfo tool = CreateTestTool(longName);

    EXPECT_NO_THROW(toolManager->AddTool(tool));

    ListToolsResult result = toolManager->ListTools();
    ASSERT_EQ(result.tools.size(), 1);
    EXPECT_EQ(result.tools[0].name, longName);

    const Mcp::ServerContext context = {};
    EXPECT_NO_THROW(toolManager->CallTool(context, longName, R"({})"));
}

TEST_F(ToolManagerTest, AddManyTools) {
    const int numTools = 1000;

    for (int i = 0; i < numTools; i++) {
        ToolInfo tool = CreateTestTool("tool_" + std::to_string(i));
        EXPECT_NO_THROW(toolManager->AddTool(tool));
    }

    ListToolsResult result = toolManager->ListTools();
    EXPECT_EQ(result.tools.size(), numTools);
}

TEST_F(ToolManagerTest, AddToolExceptionMessages) {
    ToolInfo tool;

    tool.name = "";
    tool.description = "Test";
    tool.func = SyncToolFunc(TestToolFunc);

    try {
        toolManager->AddTool(tool);
        FAIL() << "Expected std::invalid_argument";
    } catch (const std::invalid_argument& e) {
        EXPECT_STREQ(e.what(), "Tool name cannot be empty");
    }

    // 测试空描述
    tool.name = "test";
    tool.description = "";

    try {
        toolManager->AddTool(tool);
        FAIL() << "Expected std::invalid_argument";
    } catch (const std::invalid_argument& e) {
        EXPECT_STREQ(e.what(), "Tool description cannot be empty");
    }

    tool.description = "Test";
    tool.func = SyncToolFunc{};

    try {
        toolManager->AddTool(tool);
        FAIL() << "Expected std::invalid_argument";
    } catch (const std::invalid_argument& e) {
        EXPECT_STREQ(e.what(), "Tool function implementation cannot be null");
    }
}

TEST_F(ToolManagerTest, RemoveToolExceptionMessages) {
    try {
        toolManager->RemoveTool("");
        FAIL() << "Expected std::invalid_argument";
    } catch (const std::invalid_argument& e) {
        EXPECT_STREQ(e.what(), "Tool name cannot be empty");
    }

    try {
        toolManager->RemoveTool("non_existent");
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "Tool 'non_existent' not found");
    }
}

TEST_F(ToolManagerTest, CallToolExceptionMessages)
{
    const Mcp::ServerContext context = {};
    try {
        toolManager->CallTool(context, "non_existent", R"({})");
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "Tool not found: non_existent");
    }

    ToolInfo tool = CreateErrorThrowingTool("error_tool");
    toolManager->AddTool(tool);

    try {
        toolManager->CallTool(context, "error_tool", R"({})");
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error& e) {
        EXPECT_TRUE(std::string(e.what()).find("Tool execution failed") != std::string::npos);
    }
}

} // namespace Mcp
