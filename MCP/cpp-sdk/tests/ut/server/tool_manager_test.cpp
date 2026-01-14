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

static constexpr int LOOP_NUM = 3;
static constexpr int TOOL_NUM = 2;

namespace Mcp {

static CallToolResult TestToolFunc(const std::string& name, const JsonValue& arguments,
    std::optional<JsonValue> context)
{
    (void)context;

    CallToolResult result;
    result.isError = false;

    TextContent textContent;
    textContent.text = R"(Tool )" + name + R"( executed with arguments: )" + arguments.dump();

    result.content.push_back(textContent);

    return result;
}

static CallToolResult TestCallbackToolFunc(const std::string& name, const JsonValue& arguments,
    std::optional<JsonValue> context)
{
    (void)arguments;
    (void)context;

    CallToolResult result;
    result.isError = false;

    TextContent textContent;
    textContent.text = R"({"callback": "yes", "tool": ")" + name + "\"}";

    result.content.push_back(textContent);
    return result;
}

class ToolManagerTest : public ::testing::Test {
public:
    ~ToolManagerTest() {}
protected:
    void SetUp() override
    {
        toolManager = std::make_unique<ToolManager>(true);
    }

    void TearDown() override
    {
        toolManager.reset();
    }

    ServerTool CreateTestTool(const std::string& name = "test_tool",
                           const std::string& description = "Test tool description")
    {
        ServerTool tool;
        tool.name = name;
        tool.description = description;
        tool.inputSchema = R"({"type": "object", "properties": {}})";
        tool.func = TestToolFunc;
        return tool;
    }

    ServerTool CreateComplexSchemaTool(const std::string& name = "complex_tool")
    {
        ServerTool tool;
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
        })";
        tool.func = TestToolFunc;
        return tool;
    }

    ServerTool CreateErrorThrowingTool(const std::string& name = "error_tool")
    {
        auto errorFunc = [](const std::string& name, const JsonValue& arguments,
                           std::optional<JsonValue> context) -> CallToolResult {
            (void)arguments;
            (void)context;

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

        ServerTool tool;
        tool.name = name;
        tool.description = "Tool that throws exceptions";
        tool.inputSchema = R"({"type": "object"})";
        tool.func = errorFunc;
        return tool;
    }

    std::unique_ptr<ToolManager> toolManager;
};

TEST_F(ToolManagerTest, ConstructorWithOverwriteTrue)
{
    ToolManager manager(true);
    EXPECT_TRUE(manager.GetOverwrite());
}

TEST_F(ToolManagerTest, ConstructorWithOverwriteFalse)
{
    ToolManager manager(false);
    EXPECT_FALSE(manager.GetOverwrite());
}

TEST_F(ToolManagerTest, SetOverwrite)
{
    EXPECT_TRUE(toolManager->GetOverwrite());

    toolManager->SetOverwrite(false);
    EXPECT_FALSE(toolManager->GetOverwrite());

    toolManager->SetOverwrite(true);
    EXPECT_TRUE(toolManager->GetOverwrite());
}

TEST_F(ToolManagerTest, DefaultConstructor)
{
    ToolManager manager; // 使用默认参数（true）
    EXPECT_TRUE(manager.GetOverwrite());
}

TEST_F(ToolManagerTest, AddToolSuccess)
{
    ServerTool tool = CreateTestTool();

    EXPECT_NO_THROW(toolManager->AddTool(tool));
}

TEST_F(ToolManagerTest, AddToolWithComplexSchema)
{
    ServerTool tool = CreateComplexSchemaTool();

    EXPECT_NO_THROW(toolManager->AddTool(tool));
}

TEST_F(ToolManagerTest, AddToolEmptyName)
{
    ServerTool tool = CreateTestTool();
    tool.name = "";

    EXPECT_NO_THROW(toolManager->AddTool(tool));
}

TEST_F(ToolManagerTest, AddToolEmptyDescription)
{
    ServerTool tool = CreateTestTool();
    tool.description = "";

    EXPECT_NO_THROW(toolManager->AddTool(tool));
}

TEST_F(ToolManagerTest, AddToolNullFunction)
{
    ServerTool tool = CreateTestTool();
    tool.func = nullptr;

    EXPECT_NO_THROW(toolManager->AddTool(tool));
}

TEST_F(ToolManagerTest, AddToolOverwriteWhenOverwriteTrue)
{
    ServerTool tool1 = CreateTestTool("same_tool", "First description");
    ServerTool tool2 = CreateTestTool("same_tool", "Second description");
    tool2.func = TestCallbackToolFunc;

    EXPECT_NO_THROW(toolManager->AddTool(tool1));

    EXPECT_NO_THROW(toolManager->AddTool(tool2));
}

TEST_F(ToolManagerTest, AddToolNoOverwriteWhenOverwriteFalse)
{
    toolManager->SetOverwrite(false);

    ServerTool tool1 = CreateTestTool("same_tool", "First description");
    ServerTool tool2 = CreateTestTool("same_tool", "Second description");

    EXPECT_NO_THROW(toolManager->AddTool(tool1));

    EXPECT_THROW(toolManager->AddTool(tool2), std::runtime_error);
}

TEST_F(ToolManagerTest, AddMultipleTools)
{
    std::vector<ServerTool> tools;

    for (int i = 0; i < LOOP_NUM; i++) {
        ServerTool tool = CreateTestTool("tool_" + std::to_string(i), "Tool " + std::to_string(i));
        EXPECT_NO_THROW(toolManager->AddTool(tool));
    }
}

TEST_F(ToolManagerTest, RemoveToolSuccess)
{
    ServerTool tool = CreateTestTool("tool_to_remove");

    toolManager->AddTool(tool);
    EXPECT_NO_THROW(toolManager->RemoveTool("tool_to_remove"));
}

TEST_F(ToolManagerTest, RemoveToolEmptyName)
{
    EXPECT_THROW(toolManager->RemoveTool(""), std::invalid_argument);
}

TEST_F(ToolManagerTest, RemoveNonExistentTool)
{
    EXPECT_THROW(toolManager->RemoveTool("non_existent_tool"), std::runtime_error);
}

TEST_F(ToolManagerTest, RemoveToolMultipleTimes)
{
    ServerTool tool = CreateTestTool("tool_to_remove");

    toolManager->AddTool(tool);
    EXPECT_NO_THROW(toolManager->RemoveTool("tool_to_remove"));

    EXPECT_THROW(toolManager->RemoveTool("tool_to_remove"), std::runtime_error);
}

TEST_F(ToolManagerTest, ListToolsEmpty)
{
    ListToolsResult result = toolManager->ListTools();
    EXPECT_EQ(result.tools.size(), 0);
}

TEST_F(ToolManagerTest, ListToolsSingleTool)
{
    ServerTool tool = CreateTestTool("tool1", "First tool");
    toolManager->AddTool(tool);

    ListToolsResult result = toolManager->ListTools();
    ASSERT_EQ(result.tools.size(), 1);
    EXPECT_EQ(result.tools[0].name, "tool1");
    EXPECT_EQ(result.tools[0].description, "First tool");
    EXPECT_TRUE(result.tools[0].inputSchema.has_value());
}

TEST_F(ToolManagerTest, ListToolsMultipleTools)
{
    const int numTools = LOOP_NUM;

    for (int i = 0; i < numTools; i++) {
        ServerTool tool = CreateTestTool("tool_" + std::to_string(i), "Tool " + std::to_string(i));
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

TEST_F(ToolManagerTest, ListToolsAfterRemove)
{
    for (int i = 0; i < LOOP_NUM; i++) {
        ServerTool tool = CreateTestTool("tool_" + std::to_string(i));
        toolManager->AddTool(tool);
    }

    toolManager->RemoveTool("tool_1");

    ListToolsResult result = toolManager->ListTools();
    EXPECT_EQ(result.tools.size(), TOOL_NUM);

    std::set<std::string> expectedNames = {"tool_0", "tool_2"};
    std::set<std::string> actualNames;
    for (const auto& tool : result.tools) {
        actualNames.insert(tool.name);
    }

    EXPECT_EQ(expectedNames, actualNames);
}

TEST_F(ToolManagerTest, CallToolSuccess)
{
    ServerTool tool = CreateTestTool("test_tool");
    toolManager->AddTool(tool);

    JsonValue arguments = R"({"param": "value"})"_json;
    CallToolResult result = toolManager->CallTool("test_tool", arguments.dump());

    EXPECT_FALSE(result.isError);
    EXPECT_FALSE(result.content.empty());
    EXPECT_TRUE(std::holds_alternative<TextContent>(result.content[0]));

    auto& textContent = std::get<TextContent>(result.content[0]);
    EXPECT_EQ(textContent.type, "text");
    EXPECT_FALSE(textContent.text.empty());
}

TEST_F(ToolManagerTest, CallToolWithJsonArguments)
{
    ServerTool tool = CreateTestTool("json_tool");
    toolManager->AddTool(tool);

    std::string arguments = R"({"key1": "value1", "key2": 123, "key3": true})";
    CallToolResult result = toolManager->CallTool("json_tool", arguments);

    EXPECT_FALSE(result.isError);
    EXPECT_FALSE(result.content.empty());
}

TEST_F(ToolManagerTest, CallNonExistentTool)
{
    EXPECT_THROW(toolManager->CallTool("non_existent_tool", R"({})"), std::runtime_error);
}

TEST_F(ToolManagerTest, CallToolEmptyName)
{
    EXPECT_THROW(toolManager->CallTool("", R"({})"), std::runtime_error);
}

TEST_F(ToolManagerTest, CallToolThrowsException)
{
    ServerTool tool = CreateErrorThrowingTool("error_tool");
    toolManager->AddTool(tool);

    EXPECT_THROW(toolManager->CallTool("error_tool", R"({})"), std::runtime_error);
}

TEST_F(ToolManagerTest, CallToolAfterRemove)
{
    ServerTool tool = CreateTestTool("temp_tool");
    toolManager->AddTool(tool);

    EXPECT_NO_THROW(toolManager->CallTool("temp_tool", R"({})"));

    toolManager->RemoveTool("temp_tool");

    EXPECT_THROW(toolManager->CallTool("temp_tool", R"({})"), std::runtime_error);
}

TEST_F(ToolManagerTest, CallToolAfterOverwrite)
{
    ServerTool tool1 = CreateTestTool("tool", "First tool");
    ServerTool tool2 = CreateTestTool("tool", "Second tool");
    tool2.func = TestCallbackToolFunc;

    toolManager->AddTool(tool1);
    toolManager->AddTool(tool2);

    CallToolResult result = toolManager->CallTool("tool", R"({})");

    EXPECT_FALSE(result.isError);
    EXPECT_FALSE(result.content.empty());
}

TEST_F(ToolManagerTest, AddToolWithVeryLongName)
{
    std::string longName(1000, 'a'); // 1000个'a'
    ServerTool tool = CreateTestTool(longName);

    EXPECT_NO_THROW(toolManager->AddTool(tool));

    ListToolsResult result = toolManager->ListTools();
    ASSERT_EQ(result.tools.size(), 1);
    EXPECT_EQ(result.tools[0].name, longName);

    EXPECT_NO_THROW(toolManager->CallTool(longName, R"({})"));
}

TEST_F(ToolManagerTest, AddManyTools)
{
    const int numTools = 1000;

    for (int i = 0; i < numTools; i++) {
        ServerTool tool = CreateTestTool("tool_" + std::to_string(i));
        EXPECT_NO_THROW(toolManager->AddTool(tool));
    }

    ListToolsResult result = toolManager->ListTools();
    EXPECT_EQ(result.tools.size(), numTools);
}

TEST_F(ToolManagerTest, RemoveToolExceptionMessages)
{
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
    try {
        toolManager->CallTool("non_existent", R"({})");
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "Tool not found: non_existent");
    }

    ServerTool tool = CreateErrorThrowingTool("error_tool");
    toolManager->AddTool(tool);

    try {
        toolManager->CallTool("error_tool", R"({})");
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error& e) {
        EXPECT_TRUE(std::string(e.what()).find("Tool execution failed") != std::string::npos);
    }
}

} // namespace Mcp