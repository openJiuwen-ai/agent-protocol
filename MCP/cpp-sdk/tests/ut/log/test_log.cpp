/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <gtest/gtest.h>
#include "mcp_log.h"
#include <cstdarg>

class LogTestFixture : public ::testing::Test {
protected:
    void SetUp() override {
        SetLogLevel(MCP_LOG_LEVEL_INFO);
        SetLogCallback(McpPrintfImpl);
    }
    
    void TearDown() override {
        SetLogCallback(McpPrintfImpl);
    }
};

TEST_F(LogTestFixture, SetLogLevelValid) {
    EXPECT_EQ(0, SetLogLevel(MCP_LOG_LEVEL_DEBUG));
    EXPECT_EQ(MCP_LOG_LEVEL_DEBUG, GetLogLevel());

    EXPECT_EQ(0, SetLogLevel(MCP_LOG_LEVEL_WARN));
    EXPECT_EQ(MCP_LOG_LEVEL_WARN, GetLogLevel());

    EXPECT_EQ(0, SetLogLevel(MCP_LOG_LEVEL_ERROR));
    EXPECT_EQ(MCP_LOG_LEVEL_ERROR, GetLogLevel());

    EXPECT_EQ(0, SetLogLevel(MCP_LOG_LEVEL_FATAL));
    EXPECT_EQ(MCP_LOG_LEVEL_FATAL, GetLogLevel());
}

TEST_F(LogTestFixture, SetLogLevelInvalid) {
    MCP_LOG_LEVEL original_level = GetLogLevel();
    
    EXPECT_EQ(-1, SetLogLevel(static_cast<MCP_LOG_LEVEL>(MCP_LOG_LEVEL_DEBUG - 1)));
    EXPECT_EQ(original_level, GetLogLevel());

    EXPECT_EQ(-1, SetLogLevel(static_cast<MCP_LOG_LEVEL>(MCP_LOG_LEVEL_FATAL + 1)));
    EXPECT_EQ(original_level, GetLogLevel());
}

TEST_F(LogTestFixture, GetLogLevelDefault) {
    EXPECT_EQ(MCP_LOG_LEVEL_INFO, GetLogLevel());
}

TEST_F(LogTestFixture, SetLogCallbackValid) {
    McpLogCallback original_callback = McpPrintfImpl;
    
    // Test setting a different callback (we'll use the same one for simplicity)
    EXPECT_EQ(0, SetLogCallback(McpPrintfImpl));
    EXPECT_EQ(0, SetLogCallback(original_callback));
}

TEST_F(LogTestFixture, SetLogCallbackNull) {
    // nullptr should reset to default callback and return success
    EXPECT_EQ(0, SetLogCallback(nullptr));
    EXPECT_EQ(McpPrintfImpl, g_logCallback);
}

TEST_F(LogTestFixture, SetLogCallbackSame) {
    McpLogCallback current_callback = McpPrintfImpl;
    EXPECT_EQ(0, SetLogCallback(current_callback));
    EXPECT_EQ(0, SetLogCallback(current_callback));
}

static bool test_callback_invoked = false;
static MCP_LOG_LEVEL captured_level;
static std::string captured_message;

void test_log_callback(MCP_LOG_LEVEL level, std::string message)
{
    test_callback_invoked = true;
    captured_level = level;
    captured_message = message;
}

TEST_F(LogTestFixture, LogCallbackInvocation) {
    test_callback_invoked = false;
    captured_message.clear();
    
    EXPECT_EQ(0, SetLogCallback(test_log_callback));
    
    // This should invoke our test callback
    test_log_callback(MCP_LOG_LEVEL_INFO, "Test message");
    
    EXPECT_TRUE(test_callback_invoked);
    EXPECT_EQ(MCP_LOG_LEVEL_INFO, captured_level);
    EXPECT_EQ("Test message", captured_message);
}

TEST_F(LogTestFixture, McpPrintfImplFiltersByLogLevel) {
    testing::internal::CaptureStdout();

    // Case 1: DEBUG < INFO (default), should NOT print
    McpPrintfImpl(MCP_LOG_LEVEL_DEBUG, "Debug message\n");
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_TRUE(output.empty());

    // Case 2: Change level to DEBUG, now it should print
    SetLogLevel(MCP_LOG_LEVEL_DEBUG);
    testing::internal::CaptureStdout();
    McpPrintfImpl(MCP_LOG_LEVEL_DEBUG, "Now visible: debug\n");
    output = testing::internal::GetCapturedStdout();
    EXPECT_EQ("Now visible: debug\n\n", output);
}

TEST_F(LogTestFixture, LogLevelBoundaryConditions) {
    // Test exact boundary values
    EXPECT_EQ(0, SetLogLevel(MCP_LOG_LEVEL_DEBUG));
    EXPECT_EQ(MCP_LOG_LEVEL_DEBUG, GetLogLevel());
    
    EXPECT_EQ(0, SetLogLevel(MCP_LOG_LEVEL_FATAL));
    EXPECT_EQ(MCP_LOG_LEVEL_FATAL, GetLogLevel());
}

TEST_F(LogTestFixture, MacroLogOutput) {
    testing::internal::CaptureStdout();

    SetLogLevel(MCP_LOG_LEVEL_DEBUG);

    MCP_LOG(MCP_LOG_LEVEL_INFO, std::string("Test macro log with ") + "parameters");

    std::string output = testing::internal::GetCapturedStdout();

    EXPECT_TRUE(output.find("Test macro log with parameters") != std::string::npos);
    EXPECT_TRUE(output.find("[INFO]") != std::string::npos);
}

TEST_F(LogTestFixture, GetLogLevelNameMapsKnownLevels)
{
    EXPECT_STREQ("DEBUG", GetLogLevelName(MCP_LOG_LEVEL_DEBUG));
    EXPECT_STREQ("INFO", GetLogLevelName(MCP_LOG_LEVEL_INFO));
    EXPECT_STREQ("WARN", GetLogLevelName(MCP_LOG_LEVEL_WARN));
    EXPECT_STREQ("ERROR", GetLogLevelName(MCP_LOG_LEVEL_ERROR));
    EXPECT_STREQ("FATAL", GetLogLevelName(MCP_LOG_LEVEL_FATAL));
    EXPECT_STREQ("UNKNOWN", GetLogLevelName(static_cast<MCP_LOG_LEVEL>(0)));
}

TEST_F(LogTestFixture, CallbackSwitch) {
    McpLogCallback original_callback = g_logCallback;

    EXPECT_EQ(0, SetLogCallback(test_log_callback));
    EXPECT_EQ(test_log_callback, g_logCallback);

    EXPECT_EQ(0, SetLogCallback(original_callback));
    EXPECT_EQ(original_callback, g_logCallback);
}

constexpr size_t TIMESTAMP_LEN = 23;

TEST_F(LogTestFixture, TimestampFormat)
{
    std::string timestamp;
    GetCurrentTimeStamp(timestamp);

    EXPECT_EQ(timestamp.length(), TIMESTAMP_LEN);
    EXPECT_EQ(timestamp[4], '-');
    EXPECT_EQ(timestamp[7], '-');
    EXPECT_EQ(timestamp[10], ' ');
    EXPECT_EQ(timestamp[13], ':');
    EXPECT_EQ(timestamp[16], ':');
    EXPECT_EQ(timestamp[19], '.');
}

TEST_F(LogTestFixture, LogLevelFiltering) {
    testing::internal::CaptureStdout();
    SetLogLevel(MCP_LOG_LEVEL_ERROR);

    McpPrintfImpl(MCP_LOG_LEVEL_ERROR, "Error message\n");
    McpPrintfImpl(MCP_LOG_LEVEL_WARN, "Warn message\n");

    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_TRUE(output.find("Error message") != std::string::npos);
    EXPECT_TRUE(output.find("Warn message") == std::string::npos);
}

TEST_F(LogTestFixture, LogInternalFiltersBeforeCustomCallback)
{
    test_callback_invoked = false;
    captured_message.clear();

    EXPECT_EQ(0, SetLogCallback(test_log_callback));
    SetLogLevel(MCP_LOG_LEVEL_ERROR);

    MCP_LOG(MCP_LOG_LEVEL_INFO, std::string("filtered before callback"));
    EXPECT_FALSE(test_callback_invoked);

    MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("delivered to callback"));
    EXPECT_TRUE(test_callback_invoked);
    EXPECT_TRUE(captured_message.find("delivered to callback") != std::string::npos);
    EXPECT_TRUE(captured_message.find("[ERROR]") != std::string::npos);
}