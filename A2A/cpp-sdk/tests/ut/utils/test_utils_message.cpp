/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <vector>
#include <string>
#include <optional>

#include "utils_message.h"
#include "uuid.h"
#include "types.h"

namespace A2A::Utils::Test {

using namespace A2A;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::SizeIs;

class UtilsMessageTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // 每个测试前可以做一些初始化
    }

    // 辅助函数：创建测试用的 Part
    Part CreateTextPart(const std::string& text, const std::string& mediaType = "text/plain")
    {
        Part p;
        p.text = text;
        p.mediaType = mediaType;
        return p;
    }

    Part CreateDataPart(const std::string& data)
    {
        Part p;
        p.data = data;
        return p;
    }

    Part CreateFilePart(const std::string& url)
    {
        Part p;
        p.url = url;
        return p;
    }
};

// ===========================================================================
// NewAgentTextMessage 测试
// ===========================================================================
TEST_F(UtilsMessageTest, NewAgentTextMessage_Basic)
{
    std::string text = "Hello, agent!";

    Message msg = NewAgentTextMessage(text, std::nullopt, std::nullopt);

    EXPECT_EQ(msg.role, Role::AGENT);
    EXPECT_EQ(msg.parts.size(), 1);
    EXPECT_TRUE(msg.parts[0].text.has_value());
    EXPECT_EQ(msg.parts[0].text.value(), text);
    EXPECT_EQ(msg.parts[0].mediaType, "text/plain");
    EXPECT_FALSE(msg.messageId.empty());
    EXPECT_FALSE(msg.taskId.has_value());
    EXPECT_FALSE(msg.contextId.has_value());
}

TEST_F(UtilsMessageTest, NewAgentTextMessage_WithContextAndTask)
{
    std::string text = "Hello with context";
    std::string contextId = "ctx-123";
    std::string taskId = "task-456";

    Message msg = NewAgentTextMessage(text, contextId, taskId);

    EXPECT_EQ(msg.role, Role::AGENT);
    EXPECT_EQ(msg.parts.size(), 1);
    EXPECT_TRUE(msg.parts[0].text.has_value());
    EXPECT_EQ(msg.parts[0].text.value(), text);
    EXPECT_EQ(msg.parts[0].mediaType, "text/plain");
    EXPECT_FALSE(msg.messageId.empty());
    EXPECT_TRUE(msg.taskId.has_value());
    EXPECT_EQ(msg.taskId.value(), taskId);
    EXPECT_TRUE(msg.contextId.has_value());
    EXPECT_EQ(msg.contextId.value(), contextId);
}

TEST_F(UtilsMessageTest, NewAgentTextMessage_EmptyText)
{
    std::string text = "";

    Message msg = NewAgentTextMessage(text, std::nullopt, std::nullopt);

    EXPECT_EQ(msg.role, Role::AGENT);
    EXPECT_EQ(msg.parts.size(), 1);
    EXPECT_TRUE(msg.parts[0].text.has_value());
    EXPECT_EQ(msg.parts[0].text.value(), "");
    EXPECT_FALSE(msg.messageId.empty());
}

// ===========================================================================
// NewAgentPartsMessage 测试
// ===========================================================================
TEST_F(UtilsMessageTest, NewAgentPartsMessage_Basic)
{
    std::vector<Part> parts = {
        CreateTextPart("Part 1"),
        CreateTextPart("Part 2")
    };

    Message msg = NewAgentPartsMessage(parts, std::nullopt, std::nullopt);

    EXPECT_EQ(msg.role, Role::AGENT);
    EXPECT_EQ(msg.parts.size(), 2);
    EXPECT_TRUE(msg.parts[0].text.has_value());
    EXPECT_EQ(msg.parts[0].text.value(), "Part 1");
    EXPECT_TRUE(msg.parts[1].text.has_value());
    EXPECT_EQ(msg.parts[1].text.value(), "Part 2");
    EXPECT_FALSE(msg.messageId.empty());
    EXPECT_FALSE(msg.taskId.has_value());
    EXPECT_FALSE(msg.contextId.has_value());
}

TEST_F(UtilsMessageTest, NewAgentPartsMessage_WithContextAndTask)
{
    std::vector<Part> parts = {
        CreateTextPart("Hello"),
        CreateDataPart("{\"key\":\"value\"}"),
        CreateFilePart("https://example.com/file.txt")
    };
    std::string contextId = "ctx-789";
    std::string taskId = "task-012";

    Message msg = NewAgentPartsMessage(parts, contextId, taskId);

    EXPECT_EQ(msg.role, Role::AGENT);
    EXPECT_EQ(msg.parts.size(), 3);
    EXPECT_TRUE(msg.parts[0].text.has_value());
    EXPECT_EQ(msg.parts[0].text.value(), "Hello");
    EXPECT_TRUE(msg.parts[1].data.has_value());
    EXPECT_EQ(std::get<std::string>(msg.parts[1].data.value()), "{\"key\":\"value\"}");
    EXPECT_TRUE(msg.parts[2].url.has_value());
    EXPECT_EQ(msg.parts[2].url.value(), "https://example.com/file.txt");
    EXPECT_FALSE(msg.messageId.empty());
    EXPECT_TRUE(msg.taskId.has_value());
    EXPECT_EQ(msg.taskId.value(), taskId);
    EXPECT_TRUE(msg.contextId.has_value());
    EXPECT_EQ(msg.contextId.value(), contextId);
}

TEST_F(UtilsMessageTest, NewAgentPartsMessage_EmptyParts)
{
    std::vector<Part> parts;

    Message msg = NewAgentPartsMessage(parts, std::nullopt, std::nullopt);

    EXPECT_EQ(msg.role, Role::AGENT);
    EXPECT_TRUE(msg.parts.empty());
    EXPECT_FALSE(msg.messageId.empty());
}

TEST_F(UtilsMessageTest, NewAgentPartsMessage_MixedParts)
{
    std::vector<Part> parts = {
        CreateTextPart("Text part"),
        CreateTextPart("Another text"),
        CreateDataPart("data"),
        CreateFilePart("file.txt")
    };

    Message msg = NewAgentPartsMessage(parts, std::nullopt, std::nullopt);

    EXPECT_EQ(msg.role, Role::AGENT);
    EXPECT_EQ(msg.parts.size(), 4);
}

// ===========================================================================
// GetTextParts 测试
// ===========================================================================
TEST_F(UtilsMessageTest, GetTextParts_AllText)
{
    std::vector<Part> parts = {
        CreateTextPart("First"),
        CreateTextPart("Second"),
        CreateTextPart("Third")
    };

    auto texts = GetTextParts(parts);

    EXPECT_THAT(texts, ElementsAre("First", "Second", "Third"));
}

TEST_F(UtilsMessageTest, GetTextParts_MixedParts)
{
    std::vector<Part> parts = {
        CreateTextPart("Text 1"),
        CreateDataPart("data"),
        CreateTextPart("Text 2"),
        CreateFilePart("file.txt"),
        CreateTextPart("Text 3")
    };

    auto texts = GetTextParts(parts);

    EXPECT_THAT(texts, ElementsAre("Text 1", "Text 2", "Text 3"));
}

TEST_F(UtilsMessageTest, GetTextParts_NoText)
{
    std::vector<Part> parts = {
        CreateDataPart("data1"),
        CreateDataPart("data2"),
        CreateFilePart("file1.txt")
    };

    auto texts = GetTextParts(parts);

    EXPECT_THAT(texts, IsEmpty());
}

TEST_F(UtilsMessageTest, GetTextParts_EmptyParts)
{
    std::vector<Part> parts;

    auto texts = GetTextParts(parts);

    EXPECT_THAT(texts, IsEmpty());
}

TEST_F(UtilsMessageTest, GetTextParts_TextWithEmptyString)
{
    std::vector<Part> parts = {
        CreateTextPart(""),
        CreateTextPart("Non-empty"),
        CreateTextPart("")
    };

    auto texts = GetTextParts(parts);

    EXPECT_THAT(texts, ElementsAre("", "Non-empty", ""));
}

// ===========================================================================
// GetMessageText 测试
// ===========================================================================
TEST_F(UtilsMessageTest, GetMessageText_DefaultDelimiter)
{
    Message msg;
    msg.parts = {
        CreateTextPart("Hello"),
        CreateTextPart("World")
    };

    std::string result = GetMessageText(msg);

    EXPECT_EQ(result, "Hello\nWorld");
}

TEST_F(UtilsMessageTest, GetMessageText_CustomDelimiter)
{
    Message msg;
    msg.parts = {
        CreateTextPart("Hello"),
        CreateTextPart("World"),
        CreateTextPart("!")
    };

    std::string result = GetMessageText(msg, " ");

    EXPECT_EQ(result, "Hello World !");
}

TEST_F(UtilsMessageTest, GetMessageText_CommaDelimiter)
{
    Message msg;
    msg.parts = {
        CreateTextPart("apple"),
        CreateTextPart("banana"),
        CreateTextPart("cherry")
    };

    std::string result = GetMessageText(msg, ", ");

    EXPECT_EQ(result, "apple, banana, cherry");
}

TEST_F(UtilsMessageTest, GetMessageText_NewlineDelimiter)
{
    Message msg;
    msg.parts = {
        CreateTextPart("Line 1"),
        CreateTextPart("Line 2"),
        CreateTextPart("Line 3")
    };

    std::string result = GetMessageText(msg, "\n");

    EXPECT_EQ(result, "Line 1\nLine 2\nLine 3");
}

TEST_F(UtilsMessageTest, GetMessageText_MixedParts)
{
    Message msg;
    msg.parts = {
        CreateTextPart("Text part"),
        CreateDataPart("should be ignored"),
        CreateTextPart("Another text"),
        CreateFilePart("also ignored")
    };

    std::string result = GetMessageText(msg, " - ");

    EXPECT_EQ(result, "Text part - Another text");
}

TEST_F(UtilsMessageTest, GetMessageText_SinglePart)
{
    Message msg;
    msg.parts = {
        CreateTextPart("Only one")
    };

    std::string result = GetMessageText(msg, ", ");

    EXPECT_EQ(result, "Only one");  // 单个元素不加分隔符
}

TEST_F(UtilsMessageTest, GetMessageText_NoTextParts)
{
    Message msg;
    msg.parts = {
        CreateDataPart("data1"),
        CreateDataPart("data2")
    };

    std::string result = GetMessageText(msg, " ");

    EXPECT_EQ(result, "");  // 没有文本部分，返回空字符串
}

TEST_F(UtilsMessageTest, GetMessageText_EmptyParts)
{
    Message msg;
    msg.parts = {};

    std::string result = GetMessageText(msg, ", ");

    EXPECT_EQ(result, "");
}

TEST_F(UtilsMessageTest, GetMessageText_TextWithSpaces)
{
    Message msg;
    msg.parts = {
        CreateTextPart("Hello  World"),
        CreateTextPart("  Good  Morning  ")
    };

    std::string result = GetMessageText(msg, " ");

    EXPECT_EQ(result, "Hello  World   Good  Morning  ");  // 保留原有空格
}

// ===========================================================================
// 集成测试 - 组合使用多个函数
// ===========================================================================
TEST_F(UtilsMessageTest, CreateAndExtractText)
{
    std::string originalText = "Hello from agent";
    Message msg = NewAgentTextMessage(originalText, "ctx-001", "task-001");

    EXPECT_EQ(msg.role, Role::AGENT);
    EXPECT_EQ(msg.parts.size(), 1);

    auto texts = GetTextParts(msg.parts);
    EXPECT_THAT(texts, ElementsAre(originalText));

    std::string extractedText = GetMessageText(msg);
    EXPECT_EQ(extractedText, originalText);
}

TEST_F(UtilsMessageTest, CreateMultiPartAndExtract)
{
    std::vector<Part> parts = {
        CreateTextPart("Part A"),
        CreateTextPart("Part B"),
        CreateDataPart("Data"),
        CreateTextPart("Part C")
    };

    Message msg = NewAgentPartsMessage(parts, "ctx-002", "task-002");

    auto texts = GetTextParts(msg.parts);
    EXPECT_THAT(texts, ElementsAre("Part A", "Part B", "Part C"));

    std::string joined = GetMessageText(msg, " | ");
    EXPECT_EQ(joined, "Part A | Part B | Part C");

    EXPECT_EQ(msg.role, Role::AGENT);
    EXPECT_EQ(msg.contextId.value(), "ctx-002");
    EXPECT_EQ(msg.taskId.value(), "task-002");
    EXPECT_FALSE(msg.messageId.empty());
}

// ===========================================================================
// 边界条件测试
// ===========================================================================
TEST_F(UtilsMessageTest, VeryLongText)
{
    std::string longText(10000, 'x');

    Message msg = NewAgentTextMessage(longText, std::nullopt, std::nullopt);

    EXPECT_EQ(msg.parts.size(), 1);
    EXPECT_EQ(msg.parts[0].text.value().size(), 10000);

    std::string extracted = GetMessageText(msg);
    EXPECT_EQ(extracted.size(), 10000);
    EXPECT_EQ(extracted, longText);
}

TEST_F(UtilsMessageTest, SpecialCharacters)
{
    std::string specialText = "!@#$%^&*()_+{}[]|\\:;\"'<>,.?/~`";

    Message msg = NewAgentTextMessage(specialText, std::nullopt, std::nullopt);

    auto texts = GetTextParts(msg.parts);
    EXPECT_THAT(texts, ElementsAre(specialText));

    std::string extracted = GetMessageText(msg);
    EXPECT_EQ(extracted, specialText);
}

TEST_F(UtilsMessageTest, UnicodeText)
{
    std::string unicodeText = "Hello 世界 こんにちは";

    Message msg = NewAgentTextMessage(unicodeText, std::nullopt, std::nullopt);

    auto texts = GetTextParts(msg.parts);
    EXPECT_THAT(texts, ElementsAre(unicodeText));

    std::string extracted = GetMessageText(msg);
    EXPECT_EQ(extracted, unicodeText);
}

} // namespace A2A::Test