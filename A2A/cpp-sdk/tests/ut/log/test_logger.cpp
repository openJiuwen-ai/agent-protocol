/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <vector>
#include <atomic>
#include <regex>
#include <chrono>
#include <sstream>
#include <mutex>

#include "a2a_log.h"

namespace A2A::Log::Test {

using namespace A2A::Log;
using ::testing::HasSubstr;

// Captures log callbacks for assertions. Heap-backed storage and a capture flag
// keep shutdown safe when liba2a static destructors log after tests finish.
class LogTestHelper {
public:
    struct LogEntry {
        A2A_LOG_LEVEL level;
        std::string message;
    };

    static std::mutex logMutex;
    static std::atomic<bool> captureEnabled;

    static std::vector<LogEntry>& logs()
    {
        static auto* entries = new std::vector<LogEntry>();
        return *entries;
    }

    static void DisableCapture()
    {
        captureEnabled.store(false, std::memory_order_release);
    }

    static void EnableCapture()
    {
        captureEnabled.store(true, std::memory_order_release);
    }

    static void callback(A2A_LOG_LEVEL level, std::string message)
    {
        if (!captureEnabled.load(std::memory_order_acquire)) {
            return;
        }
        std::lock_guard<std::mutex> lock(logMutex);
        logs().push_back({level, std::move(message)});
    }

    static void clear()
    {
        std::lock_guard<std::mutex> lock(logMutex);
        logs().clear();
    }

    static size_t size()
    {
        std::lock_guard<std::mutex> lock(logMutex);
        return logs().size();
    }

    static LogEntry getLast()
    {
        std::lock_guard<std::mutex> lock(logMutex);
        if (logs().empty()) {
            return {A2A_LOG_LEVEL::DEBUG, ""};
        }
        return logs().back();
    }

    static void printAll()
    {
        std::lock_guard<std::mutex> lock(logMutex);
        std::cout << "=== Captured Logs (" << logs().size() << ") ===" << std::endl;
        for (size_t i = 0; i < logs().size(); i++) {
            std::cout << "  [" << i << "] level=" << static_cast<int>(logs()[i].level) <<
                        ", msg=\"" << logs()[i].message << "\"" << std::endl;
        }
    }
};

std::mutex LogTestHelper::logMutex;
std::atomic<bool> LogTestHelper::captureEnabled{true};

class DisableLogCaptureEnvironment : public ::testing::Environment {
public:
    void TearDown() override
    {
        LogTestHelper::DisableCapture();
    }
};

const bool K_REGISTER_LOG_CAPTURE_ENVIRONMENT =
    ::testing::AddGlobalTestEnvironment(new DisableLogCaptureEnvironment()) != nullptr;

// 测试Fixture类，用于每个测试用例前后的设置和清理
class LoggerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        originalCallback = logCallback;
        originalLevel = GetLogLevel();

        LogTestHelper::EnableCapture();
        SetLogCallback(LogTestHelper::callback);
        LogTestHelper::clear();

        ASSERT_EQ(logCallback, LogTestHelper::callback) <<
            "Failed to set test callback";
    }

    void TearDown() override
    {
        LogTestHelper::clear();
        LogTestHelper::DisableCapture();
        SetLogLevel(originalLevel);
        (void)originalCallback;
    }

    // 等待异步操作完成
    void WaitForLogs()
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    A2aLogCallback originalCallback;
    A2A_LOG_LEVEL originalLevel;
};

// 测试日志级别设置和获取
TEST_F(LoggerTest, SetAndGetLogLevel)
{
    EXPECT_EQ(SetLogLevel(A2A_LOG_LEVEL::DEBUG), 0);
    EXPECT_EQ(GetLogLevel(), A2A_LOG_LEVEL::DEBUG);

    EXPECT_EQ(SetLogLevel(A2A_LOG_LEVEL::INFO), 0);
    EXPECT_EQ(GetLogLevel(), A2A_LOG_LEVEL::INFO);

    EXPECT_EQ(SetLogLevel(A2A_LOG_LEVEL::WARN), 0);
    EXPECT_EQ(GetLogLevel(), A2A_LOG_LEVEL::WARN);

    EXPECT_EQ(SetLogLevel(A2A_LOG_LEVEL::ERROR), 0);
    EXPECT_EQ(GetLogLevel(), A2A_LOG_LEVEL::ERROR);

    EXPECT_EQ(SetLogLevel(A2A_LOG_LEVEL::FATAL), 0);
    EXPECT_EQ(GetLogLevel(), A2A_LOG_LEVEL::FATAL);
}

// 测试设置无效日志级别
TEST_F(LoggerTest, SetInvalidLogLevel)
{
    A2A_LOG_LEVEL originalLevel = GetLogLevel();

    // 测试小于最小值的级别
    A2A_LOG_LEVEL invalidLow = static_cast<A2A_LOG_LEVEL>(
        static_cast<int>(A2A_LOG_LEVEL::DEBUG) - 1);
    EXPECT_EQ(SetLogLevel(invalidLow), -1);
    EXPECT_EQ(GetLogLevel(), originalLevel);

    // 测试大于最大值的级别
    A2A_LOG_LEVEL invalidHigh = static_cast<A2A_LOG_LEVEL>(
        static_cast<int>(A2A_LOG_LEVEL::FATAL) + 1);
    EXPECT_EQ(SetLogLevel(invalidHigh), -1);
    EXPECT_EQ(GetLogLevel(), originalLevel);
}

TEST_F(LoggerTest, SetLogCallback)
{
    // 测试设置相同回调
    EXPECT_EQ(SetLogCallback(LogTestHelper::callback), -1);
}

// ==================== 使用 A2A_LOG_CONCAT 的测试用例 ====================
// 测试基本日志记录
TEST_F(LoggerTest, BasicLogging)
{
    SetLogLevel(A2A_LOG_LEVEL::DEBUG);
    LogTestHelper::clear();

    A2A_LOG_CONCAT(A2A_LOG_LEVEL::DEBUG, "Debug message with number: " << 42);
    A2A_LOG_CONCAT(A2A_LOG_LEVEL::INFO, "Info message with string: " << "test");
    A2A_LOG_CONCAT(A2A_LOG_LEVEL::WARN, "Warning message with float: " << 3.14159);
    A2A_LOG_CONCAT(A2A_LOG_LEVEL::ERROR, "Error message without params");
    A2A_LOG_CONCAT(A2A_LOG_LEVEL::FATAL, "Fatal message");

    WaitForLogs();

    EXPECT_EQ(LogTestHelper::size(), 5);
}

// 测试日志级别过滤
TEST_F(LoggerTest, LogLevelFiltering)
{
    // 设置日志级别为WARN
    SetLogLevel(A2A_LOG_LEVEL::WARN);
    LogTestHelper::clear();

    A2A_LOG_CONCAT(A2A_LOG_LEVEL::DEBUG, "Debug message - should be filtered");
    A2A_LOG_CONCAT(A2A_LOG_LEVEL::INFO, "Info message - should be filtered");
    A2A_LOG_CONCAT(A2A_LOG_LEVEL::WARN, "Warning message - should be logged");
    A2A_LOG_CONCAT(A2A_LOG_LEVEL::ERROR, "Error message - should be logged");
    A2A_LOG_CONCAT(A2A_LOG_LEVEL::FATAL, "Fatal message - should be logged");

    WaitForLogs();

    ASSERT_EQ(LogTestHelper::size(), 3);

    auto logs = LogTestHelper::logs();
    EXPECT_EQ(logs[0].level, A2A_LOG_LEVEL::WARN);
    EXPECT_EQ(logs[1].level, A2A_LOG_LEVEL::ERROR);
    EXPECT_EQ(logs[2].level, A2A_LOG_LEVEL::FATAL);
}

// 测试不同日志级别的内容
TEST_F(LoggerTest, LogLevelInMessage)
{
    SetLogLevel(A2A_LOG_LEVEL::DEBUG);
    LogTestHelper::clear();

    A2A_LOG_CONCAT(A2A_LOG_LEVEL::DEBUG, "Debug test");
    A2A_LOG_CONCAT(A2A_LOG_LEVEL::INFO, "Info test");
    A2A_LOG_CONCAT(A2A_LOG_LEVEL::WARN, "Warn test");
    A2A_LOG_CONCAT(A2A_LOG_LEVEL::ERROR, "Error test");
    A2A_LOG_CONCAT(A2A_LOG_LEVEL::FATAL, "Fatal test");

    WaitForLogs();

    auto logs = LogTestHelper::logs();
    EXPECT_EQ(logs.size(), 5);
    EXPECT_EQ(logs[0].level, A2A_LOG_LEVEL::DEBUG);
    EXPECT_EQ(logs[1].level, A2A_LOG_LEVEL::INFO);
    EXPECT_EQ(logs[2].level, A2A_LOG_LEVEL::WARN);
    EXPECT_EQ(logs[3].level, A2A_LOG_LEVEL::ERROR);
    EXPECT_EQ(logs[4].level, A2A_LOG_LEVEL::FATAL);
}

// 测试多参数日志 - 使用 << 拼接
TEST_F(LoggerTest, MultipleParameters)
{
    SetLogLevel(A2A_LOG_LEVEL::DEBUG);
    LogTestHelper::clear();

    int a = 10;
    double b = 3.14159;
    std::string c = "hello";

    A2A_LOG_CONCAT(A2A_LOG_LEVEL::INFO, "Parameters: int=" << a << ", float=" << b << ", string=" << c);

    WaitForLogs();

    ASSERT_EQ(LogTestHelper::size(), 1);
    std::string message = LogTestHelper::getLast().message;

    EXPECT_THAT(message, HasSubstr("int=10"));
    EXPECT_THAT(message, HasSubstr("string=hello"));
    EXPECT_THAT(message, HasSubstr("float="));
    EXPECT_THAT(message, HasSubstr("3.14159"));
}

// 测试多线程环境
TEST_F(LoggerTest, MultiThreadedLogging)
{
    SetLogLevel(A2A_LOG_LEVEL::DEBUG);
    LogTestHelper::clear();

    constexpr int THREAD_COUNT = 5;
    constexpr int LOGS_PER_THREAD = 10;
    std::vector<std::thread> threads;

    for (int i = 0; i < THREAD_COUNT; ++i) {
        threads.emplace_back([i]() {
            for (int j = 0; j < LOGS_PER_THREAD; ++j) {
                A2A_LOG_CONCAT(A2A_LOG_LEVEL::INFO, "Thread " << i << " logging message " << j);
                std::this_thread::sleep_for(std::chrono::microseconds(rand() % 100));
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    WaitForLogs();
    EXPECT_EQ(LogTestHelper::size(), THREAD_COUNT * LOGS_PER_THREAD);
}

// 测试宏扩展（复用 LoggerTest fixture，确保 capture 在单进程跑全量 UT 时已启用）
TEST_F(LoggerTest, MacroExpansion)
{
    SetLogLevel(A2A_LOG_LEVEL::DEBUG);
    LogTestHelper::clear();

    EXPECT_NO_THROW({
        A2A_LOG_CONCAT(A2A_LOG_LEVEL::DEBUG, "No params");
        A2A_LOG_CONCAT(A2A_LOG_LEVEL::INFO, "One param: " << 1);
        A2A_LOG_CONCAT(A2A_LOG_LEVEL::WARN, "Two params: " << 2 << " and " << "test");
        A2A_LOG_CONCAT(A2A_LOG_LEVEL::ERROR, "Three params: " << 3 << ", " << "test" << ", " << 3.14);
    });

    WaitForLogs();
    EXPECT_EQ(LogTestHelper::size(), 4);
}

// 测试枚举值范围
TEST(LogLevelTest, EnumValues)
{
    EXPECT_LT(static_cast<int>(A2A_LOG_LEVEL::DEBUG),
                static_cast<int>(A2A_LOG_LEVEL::INFO));
    EXPECT_LT(static_cast<int>(A2A_LOG_LEVEL::INFO),
                static_cast<int>(A2A_LOG_LEVEL::WARN));
    EXPECT_LT(static_cast<int>(A2A_LOG_LEVEL::WARN),
                static_cast<int>(A2A_LOG_LEVEL::ERROR));
    EXPECT_LT(static_cast<int>(A2A_LOG_LEVEL::ERROR),
                static_cast<int>(A2A_LOG_LEVEL::FATAL));
}

// 测试连续日志记录
TEST_F(LoggerTest, SequentialLogging)
{
    SetLogLevel(A2A_LOG_LEVEL::DEBUG);
    LogTestHelper::clear();

    for (int i = 0; i < 10; ++i) {
        A2A_LOG_CONCAT(A2A_LOG_LEVEL::INFO, "Sequence number: " << i);
    }

    WaitForLogs();
    EXPECT_EQ(LogTestHelper::size(), 10);

    auto logs = LogTestHelper::logs();
    for (int i = 0; i < 10; ++i) {
        std::string expected = "Sequence number: " + std::to_string(i);
        EXPECT_THAT(logs[i].message, HasSubstr(expected));
    }
}

// 测试特殊字符处理
TEST_F(LoggerTest, SpecialCharacters)
{
    SetLogLevel(A2A_LOG_LEVEL::DEBUG);
    LogTestHelper::clear();

    A2A_LOG_CONCAT(A2A_LOG_LEVEL::INFO, "Special chars: !@#$%^&*()_+");

    WaitForLogs();
    ASSERT_EQ(LogTestHelper::size(), 1);
    std::string message = LogTestHelper::getLast().message;

    EXPECT_THAT(message, HasSubstr("Special chars: !@#$%^&*()_+"));
}

} // namespace A2A::Log::Test
