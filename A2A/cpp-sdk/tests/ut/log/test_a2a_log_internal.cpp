/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>

#include <regex>
#include <string>

#include "a2a_log.h"

namespace A2A::Log::Test {

namespace {

std::string g_capturedLogMessage;

void CaptureLogCallback(A2A_LOG_LEVEL, std::string message)
{
    g_capturedLogMessage = std::move(message);
}

} // namespace

TEST(A2aLogInternalTest, GetCurrentTimeStamp_Format)
{
    std::string timestamp;
    GetCurrentTimeStamp(timestamp);

    EXPECT_FALSE(timestamp.empty());
    // YYYY-MM-DD HH:MM:SS.mmm
    EXPECT_TRUE(std::regex_match(timestamp, std::regex(R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})")));
}

TEST(A2aLogInternalTest, LogInternal_InvokesCallbackWithMetadata)
{
    const auto previousLevel = GetLogLevel();
    SetLogLevel(A2A_LOG_LEVEL::DEBUG);

    const auto previousCallback = logCallback;
    g_capturedLogMessage.clear();
    logCallback = CaptureLogCallback;

    LogInternal(A2A_LOG_LEVEL::INFO, __FILE__, __FUNCTION__, __LINE__, "hello log");

    EXPECT_NE(g_capturedLogMessage.find("hello log"), std::string::npos);
    EXPECT_NE(g_capturedLogMessage.find("::"), std::string::npos);

    logCallback = previousCallback;
    SetLogLevel(previousLevel);
}

TEST(A2aLogInternalTest, A2aPrintfImpl_FiltersBelowLogLevel)
{
    const auto previousLevel = GetLogLevel();
    SetLogLevel(A2A_LOG_LEVEL::ERROR);

    testing::internal::CaptureStdout();
    A2aPrintfImpl(A2A_LOG_LEVEL::INFO, "should not print");
    const std::string output = testing::internal::GetCapturedStdout();

    EXPECT_TRUE(output.empty());

    SetLogLevel(previousLevel);
}

} // namespace A2A::Log::Test
