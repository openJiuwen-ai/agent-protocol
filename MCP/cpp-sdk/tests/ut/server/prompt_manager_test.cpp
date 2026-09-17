/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>
#include <atomic>
#include <future>
#include <thread>
#include "prompt_manager.h"

static constexpr int THREAD_NUM = 2;
static constexpr int LOOP_NUM = 3;
static constexpr int REQ_ID = 12345;

namespace Mcp {

// Helper function to create a simple PromptInfo
PromptInfo CreatePromptInfo(const std::string& name, const std::optional<std::string>& description = std::nullopt)
{
    PromptInfo info;
    info.name = name;
    info.description = description;
    return info;
}

// Test fixture for PromptManager
class PromptManagerTest : public ::testing::Test {
public:
    ~PromptManagerTest() {}
protected:
    void SetUp() override
    {
        // Create test prompts
        prompt1 = CreatePromptInfo("prompt1", "First prompt");
        prompt2 = CreatePromptInfo("prompt2", "Second prompt");
        prompt3 = CreatePromptInfo("prompt3", "Third prompt");
    }

    PromptInfo prompt1;
    PromptInfo prompt2;
    PromptInfo prompt3;
};

// Test cases for PromptManager with overwrite = true
TEST_F(PromptManagerTest, AddPrompt_Success)
{
    PromptManager manager(true);

    bool handlerCalled = false;
    auto handler = [&handlerCalled](const ServerContext& /* ctx */, const std::string& name,
        const std::optional<std::string>&) {
        handlerCalled = true;
        return GetPromptResult{};
    };

    EXPECT_NO_THROW(manager.AddPrompt(prompt1, handler));
    EXPECT_FALSE(handlerCalled); // Handler not called yet
}

TEST_F(PromptManagerTest, AddPrompt_EmptyNameThrows)
{
    PromptManager manager(true);
    PromptInfo emptyNamePrompt;
    emptyNamePrompt.name = "";

    auto handler = [](const ServerContext& /* ctx */, const std::string&,
        const std::optional<std::string>&) {
        return GetPromptResult{};
    };

    EXPECT_THROW(manager.AddPrompt(emptyNamePrompt, handler), std::invalid_argument);
}

TEST_F(PromptManagerTest, AddPrompt_NullHandlerThrows)
{
    PromptManager manager(true);

    EXPECT_THROW(manager.AddPrompt(prompt1, nullptr), std::invalid_argument);
}

TEST_F(PromptManagerTest, AddPrompt_OverwriteExisting)
{
    PromptManager manager(true);

    // Track which handler was called
    std::string calledHandler;

    auto handler1 = [&calledHandler](const ServerContext& /* ctx */, const std::string&,
        const std::optional<std::string>&) {
        calledHandler = "first";
        GetPromptResult result;
        result.description = "First handler";
        return result;
    };

    auto handler2 = [&calledHandler](const ServerContext& /* ctx */, const std::string&,
        const std::optional<std::string>&) {
        calledHandler = "second";
        GetPromptResult result;
        result.description = "Second handler";
        return result;
    };

    // First add
    EXPECT_NO_THROW(manager.AddPrompt(prompt1, handler1));

    // Overwrite with different handler
    EXPECT_NO_THROW(manager.AddPrompt(prompt1, handler2));

    // Verify the second handler is used
    GetPromptResult result = manager.GetPrompt("prompt1");
    EXPECT_EQ(result.description, "Second handler");
    EXPECT_EQ(calledHandler, "second");
}

TEST_F(PromptManagerTest, AddPrompt_NoOverwriteThrowsWhenExists)
{
    PromptManager manager(false);

    auto handler = [](const ServerContext& /* ctx */, const std::string&,
        const std::optional<std::string>&) {
        return GetPromptResult{};
    };

    EXPECT_NO_THROW(manager.AddPrompt(prompt1, handler));

    // Try to add again - should throw
    EXPECT_THROW(manager.AddPrompt(prompt1, handler), std::runtime_error);
}

TEST_F(PromptManagerTest, RemovePrompt_ExistingPrompt)
{
    PromptManager manager(true);

    auto handler = [](const ServerContext& /* ctx */, const std::string&,
        const std::optional<std::string>&) {
        return GetPromptResult{};
    };

    manager.AddPrompt(prompt1, handler);
    manager.AddPrompt(prompt2, handler);

    // Remove prompt1
    EXPECT_NO_THROW(manager.RemovePrompt("prompt1"));

    // Verify prompt1 is removed, prompt2 still exists
    ListPromptsResult listResult = manager.ListPrompts();
    EXPECT_EQ(listResult.prompts.size(), 1);
    EXPECT_EQ(listResult.prompts[0].name, "prompt2");
}

TEST_F(PromptManagerTest, RemovePrompt_NonExistentPrompt)
{
    PromptManager manager(true);

    auto handler = [](const ServerContext& /* ctx */, const std::string&,
        const std::optional<std::string>&) {
        return GetPromptResult{};
    };

    manager.AddPrompt(prompt1, handler);

    // Remove non-existent prompt - should not throw
    EXPECT_NO_THROW(manager.RemovePrompt("nonexistent"));

    // Verify prompt1 still exists
    ListPromptsResult listResult = manager.ListPrompts();
    EXPECT_EQ(listResult.prompts.size(), 1);
    EXPECT_EQ(listResult.prompts[0].name, "prompt1");
}

TEST_F(PromptManagerTest, ListPrompts_Empty)
{
    PromptManager manager(true);

    ListPromptsResult result = manager.ListPrompts();
    EXPECT_TRUE(result.prompts.empty());
}

TEST_F(PromptManagerTest, ListPrompts_MultiplePrompts)
{
    PromptManager manager(true);

    auto handler = [](const ServerContext& /* ctx */, const std::string&,
        const std::optional<std::string>&) {
        return GetPromptResult{};
    };

    manager.AddPrompt(prompt1, handler);
    manager.AddPrompt(prompt2, handler);
    manager.AddPrompt(prompt3, handler);

    ListPromptsResult result = manager.ListPrompts();
    EXPECT_EQ(result.prompts.size(), LOOP_NUM);

    // Check all prompts are present
    std::set<std::string> expectedNames = {"prompt1", "prompt2", "prompt3"};
    std::set<std::string> actualNames;
    for (const auto& prompt : result.prompts) {
        actualNames.insert(prompt.name);
    }
    EXPECT_EQ(actualNames, expectedNames);
}

TEST_F(PromptManagerTest, ListPrompts_OrderMayVary)
{
    PromptManager manager(true);

    auto handler = [](const ServerContext& /* ctx */, const std::string&,
        const std::optional<std::string>&) {
        return GetPromptResult{};
    };

    // Add prompts in reverse order
    manager.AddPrompt(prompt3, handler);
    manager.AddPrompt(prompt1, handler);
    manager.AddPrompt(prompt2, handler);

    ListPromptsResult result = manager.ListPrompts();
    EXPECT_EQ(result.prompts.size(), LOOP_NUM);

    // Just verify all three are present, order doesn't matter
    std::set<std::string> expectedNames = {"prompt1", "prompt2", "prompt3"};
    std::set<std::string> actualNames;
    for (const auto& prompt : result.prompts) {
        actualNames.insert(prompt.name);
    }
    EXPECT_EQ(actualNames, expectedNames);
}

TEST_F(PromptManagerTest, GetPrompt_ExistingPrompt)
{
    PromptManager manager(true);

    auto expectedResult = GetPromptResult{};
    expectedResult.description = "Test description";
    PromptMessage message;
    message.role = RoleType::USER;
    TextContent textContent;
    textContent.text = "Hello, world!";
    message.content = textContent;
    expectedResult.messages.push_back(message);

    auto handler = [expectedResult](const ServerContext& /* ctx */, const std::string& name,
        const std::optional<std::string>& argument) {
        return expectedResult;
    };

    manager.AddPrompt(prompt1, handler);

    GetPromptResult result = manager.GetPrompt("prompt1");
    EXPECT_EQ(result.description, "Test description");
    EXPECT_EQ(result.messages.size(), 1);

    // Check message content
    const auto& content = std::get<TextContent>(result.messages[0].content);
    EXPECT_EQ(content.text, "Hello, world!");
}

TEST_F(PromptManagerTest, GetPrompt_WithArguments)
{
    PromptManager manager(true);

    auto handler = [](const ServerContext& /* ctx */, const std::string& /* name */,
                      const std::optional<std::string>& argument) {
        GetPromptResult result;
        if (argument.has_value()) {
            nlohmann::json j = nlohmann::json::parse(argument.value());
            if (j.contains("username")) {
                std::string username = j["username"];
                result.description = "Prompt for " + username;
            } else {
                result.description = "Default prompt";
            }
        } else {
            result.description = "Default prompt";
        }
        return result;
    };

    manager.AddPrompt(prompt1, handler);

    // Get prompt with arguments
    nlohmann::json args = {{"username", "Alice"}};
    GetPromptResult result = manager.GetPrompt("prompt1", args);
    EXPECT_EQ(result.description, "Prompt for Alice");

    // Get prompt without arguments
    GetPromptResult result2 = manager.GetPrompt("prompt1");
    EXPECT_EQ(result2.description, "Default prompt");
}

TEST_F(PromptManagerTest, GetPrompt_ComplexArguments)
{
    PromptManager manager(true);

    auto handler = [](const ServerContext& /* ctx */, const std::string& /* name */,
                      const std::optional<std::string>& argument) {
        GetPromptResult result;
        if (argument.has_value()) {
            JsonValue j = JsonValue::parse(argument.value());
            std::string argsStr = j.dump();
            result.description = "Prompt with args: " + argsStr;
        }
        return result;
    };

    manager.AddPrompt(prompt1, handler);

    // Test with complex JSON arguments
    JsonValue args = {
        {"string", "value"},
        {"number", 42},
        {"array", {1, 2, 3}},
        {"object", {{"nested", "value"}}}
    };

    GetPromptResult result = manager.GetPrompt("prompt1", args);
    EXPECT_NE(result.description, std::nullopt);
    EXPECT_TRUE(result.description->find("Prompt with args:") != std::string::npos);
}

TEST_F(PromptManagerTest, GetPrompt_NonExistentPromptThrows)
{
    PromptManager manager(true);

    EXPECT_THROW(manager.GetPrompt("nonexistent"), std::runtime_error);
}

TEST_F(PromptManagerTest, Concurrency_Basic)
{
    PromptManager manager(true);

    // Test that we can add and get prompts in sequence
    auto handler1 = [](const ServerContext& /* ctx */, const std::string&,
        const std::optional<std::string>&) {
        GetPromptResult result;
        result.description = "Handler 1";
        return result;
    };

    auto handler2 = [](const ServerContext& /* ctx */, const std::string&,
        const std::optional<std::string>&) {
        GetPromptResult result;
        result.description = "Handler 2";
        return result;
    };

    manager.AddPrompt(prompt1, handler1);
    manager.AddPrompt(prompt2, handler2);

    GetPromptResult result1 = manager.GetPrompt("prompt1");
    EXPECT_EQ(result1.description, "Handler 1");

    GetPromptResult result2 = manager.GetPrompt("prompt2");
    EXPECT_EQ(result2.description, "Handler 2");

    manager.RemovePrompt("prompt1");

    EXPECT_THROW(manager.GetPrompt("prompt1"), std::runtime_error);
    EXPECT_NO_THROW(manager.GetPrompt("prompt2"));
}

TEST_F(PromptManagerTest, Concurrency_MultiThreadAccess)
{
    PromptManager manager(true);

    std::atomic<int> handlerCallCount{0};
    auto handler = [&handlerCallCount](const ServerContext& /* ctx */, const std::string&,
        const std::optional<std::string>&) {
        handlerCallCount++;
        return GetPromptResult{};
    };

    // Add a prompt
    manager.AddPrompt(prompt1, handler);

    // Multiple threads getting the same prompt
    constexpr int kNumThreads = 10;
    std::vector<std::future<void>> futures;

    for (int i = 0; i < kNumThreads; i++) {
        futures.push_back(std::async(std::launch::async, [&manager]() {
            EXPECT_NO_THROW(manager.GetPrompt("prompt1"));
        }));
    }

    // Wait for all threads
    for (auto& future : futures) {
        future.get();
    }

    EXPECT_EQ(handlerCallCount, kNumThreads);
}

TEST_F(PromptManagerTest, Concurrency_MultiThreadAddAndGet)
{
    PromptManager manager(true);

    std::atomic<int> addedCount{0};
    std::atomic<int> gotCount{0};

    // Thread function to add prompts
    auto addThreadFunc = [&manager, &addedCount](int threadId) {
        for (int i = 0; i < 10; i++) {
            std::string promptName = "thread" + std::to_string(threadId) + "_prompt" + std::to_string(i);
            PromptInfo info;
            info.name = promptName;

            auto handler = [promptName](const ServerContext& /* ctx */, const std::string&,
                const std::optional<std::string>&) {
                GetPromptResult result;
                result.description = "From " + promptName;
                return result;
            };

            try {
                manager.AddPrompt(info, handler);
                addedCount++;
            } catch (const std::exception& e) {
                // Might get duplicate if overwrite=false, but we're using overwrite=true
            }
        }
    };

    // Thread function to get prompts (will fail for non-existent prompts)
    auto getThreadFunc = [&manager, &gotCount]() {
        for (int i = 0; i < 20; i++) {
            try {
                // Try to get a random prompt name
                std::string promptName = "thread" + std::to_string(i % LOOP_NUM) + "_prompt" + std::to_string(i % 5);
                manager.GetPrompt(promptName);
                gotCount++;
            } catch (const std::runtime_error&) {
                // Expected for non-existent prompts
            } catch (const std::exception& e) {
                // Should not happen
                FAIL() << "Unexpected exception: " << e.what();
            }
        }
    };

    // Run threads
    std::vector<std::thread> threads;
    for (int i = 0; i < LOOP_NUM; i++) {
        threads.emplace_back(addThreadFunc, i);
    }
    for (int i = 0; i < THREAD_NUM; i++) {
        threads.emplace_back(getThreadFunc);
    }

    // Wait for all threads
    for (auto& thread : threads) {
        thread.join();
    }

    // Verify no deadlocks occurred
    EXPECT_GE(addedCount, 0);
    EXPECT_GE(gotCount, 0);
}

TEST_F(PromptManagerTest, PromptInfo_FieldsPreserved)
{
    PromptManager manager(true);

    PromptInfo detailedPrompt;
    detailedPrompt.name = "detailed_prompt";
    detailedPrompt.description = "A detailed prompt description";
    detailedPrompt.title = "Detailed Prompt Title";

    Icon icon;
    icon.src = "icon.png";
    icon.mimeType = "image/png";
    detailedPrompt.icons = std::vector<Icon>{icon};

    PromptArgument arg;
    arg.name = "param1";
    arg.description = "First parameter";
    arg.required = true;
    detailedPrompt.arguments = std::vector<PromptArgument>{arg};

    auto handler = [](const ServerContext& /* ctx */, const std::string&,
        const std::optional<std::string>&) {
        return GetPromptResult{};
    };

    manager.AddPrompt(detailedPrompt, handler);

    ListPromptsResult result = manager.ListPrompts();
    ASSERT_EQ(result.prompts.size(), 1);

    const auto& retrievedPrompt = result.prompts[0];
    EXPECT_EQ(retrievedPrompt.name, "detailed_prompt");
    EXPECT_EQ(retrievedPrompt.description, "A detailed prompt description");
    EXPECT_EQ(retrievedPrompt.title, "Detailed Prompt Title");
    ASSERT_TRUE(retrievedPrompt.icons.has_value());
    EXPECT_EQ(retrievedPrompt.icons->size(), 1);
    EXPECT_EQ(retrievedPrompt.icons->at(0).src, "icon.png");
    ASSERT_TRUE(retrievedPrompt.arguments.has_value());
    EXPECT_EQ(retrievedPrompt.arguments->size(), 1);
    EXPECT_EQ(retrievedPrompt.arguments->at(0).name, "param1");
}

TEST_F(PromptManagerTest, HandlerCalledWithCorrectParameters)
{
    PromptManager manager(true);

    std::string receivedName;
    std::optional<JsonValue> receivedArgs;

    auto handler = [&receivedName, &receivedArgs](const ServerContext& /* ctx */, const std::string& name,
        const std::optional<std::string>& args) {
        receivedName = name;
        if (args.has_value()) {
            receivedArgs = JsonValue::parse(args.value());
        }
        return GetPromptResult{};
    };

    manager.AddPrompt(prompt1, handler);

    JsonValue args = {{"key", "value"}};
    manager.GetPrompt("prompt1", args);

    EXPECT_EQ(receivedName, "prompt1");
    ASSERT_TRUE(receivedArgs.has_value());
    EXPECT_EQ((*receivedArgs)["key"], "value");
}

TEST_F(PromptManagerTest, GetPrompt_HandlerThrowsException)
{
    PromptManager manager(true);

    auto handler = [](const ServerContext& /* ctx */, const std::string&,
        const std::optional<std::string>&) -> GetPromptResult {
        throw std::runtime_error("Handler error");
    };

    manager.AddPrompt(prompt1, handler);

    // Exception from handler should propagate
    EXPECT_THROW(manager.GetPrompt("prompt1"), std::runtime_error);
}

TEST_F(PromptManagerTest, PromptManager_DestructorDoesNotThrow)
{
    {
        PromptManager manager(true);

        // Add some prompts
        auto handler = [](const ServerContext& /* ctx */, const std::string&,
            const std::optional<std::string>&) {
            return GetPromptResult{};
        };

        manager.AddPrompt(prompt1, handler);
        manager.AddPrompt(prompt2, handler);

        // Destructor should not throw
    }
    SUCCEED(); // If we get here, destructor didn't throw
}

TEST_F(PromptManagerTest, LargeNumberOfPrompts)
{
    PromptManager manager(true);

    auto handler = [](const ServerContext& /* ctx */, const std::string&,
        const std::optional<std::string>&) {
        return GetPromptResult{};
    };

    constexpr int kNumPrompts = 1000;
    for (int i = 0; i < kNumPrompts; i++) {
        PromptInfo info;
        info.name = "prompt_" + std::to_string(i);
        info.description = "Prompt " + std::to_string(i);

        EXPECT_NO_THROW(manager.AddPrompt(info, handler));
    }

    ListPromptsResult result = manager.ListPrompts();
    EXPECT_EQ(result.prompts.size(), kNumPrompts);

    // Verify we can get a prompt from the middle
    GetPromptResult getResult = manager.GetPrompt("prompt_500");
    EXPECT_NO_THROW(manager.GetPrompt("prompt_999")); // Last one
}

} // namespace Mcp
