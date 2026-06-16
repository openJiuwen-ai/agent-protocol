/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <iostream>
#include <chrono>
#include <atomic>

#include "a2a_log.h"


namespace A2A::Log {
// Legacy C interface global variables
A2aLogCallback logCallback = A2aPrintfImpl;
static A2A_LOG_LEVEL g_logLevel = A2A_LOG_LEVEL::INFO;

// Legacy C interface functions
int32_t SetLogLevel(const A2A_LOG_LEVEL logLevel)
{
    if (logLevel < A2A_LOG_LEVEL::DEBUG || logLevel > A2A_LOG_LEVEL::FATAL) {
        return -1;
    }
    g_logLevel = logLevel;
    return 0;
}

A2A_LOG_LEVEL GetLogLevel()
{
    return g_logLevel;
}

void A2aPrintfImpl(A2A_LOG_LEVEL logLevel, std::string message)
{
    if (logLevel < GetLogLevel()) {
        return;
    }

    printf("%s\n", message.c_str());
}

int32_t SetLogCallback(const A2aLogCallback a2aLogCallback)
{
    static std::atomic<bool> isInitialized{false};
    if (isInitialized.exchange(true)) {
        std::cout << "log callback can only be set once" << std::endl;
        return -1;
    }
    logCallback = a2aLogCallback;
    return 0;
}

// Helper function to get current timestamp
void GetCurrentTimeStamp(std::string& timestamp)
{
    // Get current time with millisecond precision using clock_gettime
    struct timespec ts;
    struct tm tmInfo;
    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tmInfo);

    // Format: YYYY-MM-DD HH:MM:SS.mmm
    char dateTimeBuf[32] = {0};
    std::strftime(dateTimeBuf, sizeof(dateTimeBuf), "%Y-%m-%d %H:%M:%S", &tmInfo);

    int32_t millis = static_cast<int32_t>(ts.tv_nsec / A2A_NANOS_PER_MILLISECOND);

    std::ostringstream oss;
    oss << dateTimeBuf << '.' <<
        std::setfill('0') << std::setw(A2A_TIMESTAMP_MILLIS_WIDTH) << millis;
    timestamp = oss.str();
}

// Internal helper to reduce macro lines and handle C++ string formatting
void LogInternal(A2A_LOG_LEVEL level, const char* file, const char* func, int line, const std::string& format)
{
    if (level < GetLogLevel() || logCallback == nullptr) {
        return;
    }

    std::string timestamp;
    GetCurrentTimeStamp(timestamp);
    const char* filename = strrchr(file, '/');
    if (filename) {
        filename = filename + 1;
    } else {
        filename = file;
    }

    std::ostringstream oss;
    oss << "[" << timestamp << "] [" << std::to_string(syscall(SYS_gettid)) << "] " <<
        filename << "::" << func << ":[" << std::to_string(line) << "] " <<
        format;
    logCallback(level, oss.str());
}

} // namespace A2A::Log