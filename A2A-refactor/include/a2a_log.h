/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_LOG_INCLUDE_H_
#define A2A_LOG_INCLUDE_H_

#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <iomanip>
#include <sstream>
#include <string>

// Time-related constants for logging
constexpr int32_t A2A_NANOS_PER_MILLISECOND = 1000000;
constexpr int32_t A2A_TIMESTAMP_MILLIS_WIDTH = 3;

enum A2A_LOG_LEVEL {
    A2A_LOG_LEVEL_DEBUG = 3,
    A2A_LOG_LEVEL_INFO = 4,
    A2A_LOG_LEVEL_WARN = 5,
    A2A_LOG_LEVEL_ERROR = 6,
    A2A_LOG_LEVEL_FATAL = 7
};

typedef void (*A2aLogCallback)(A2A_LOG_LEVEL logLevel, std::string message);
extern A2aLogCallback g_logCallback;

void A2aPrintfImpl(A2A_LOG_LEVEL logLevel, std::string message);

int32_t SetLogCallback(A2aLogCallback logCallback);
int32_t SetLogLevel(A2A_LOG_LEVEL logLevel);
A2A_LOG_LEVEL GetLogLevel(void);

// Helper function to get current timestamp
static inline void GetCurrentTimeStamp(std::string& timestamp)
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
    oss << dateTimeBuf << '.'
        << std::setfill('0') << std::setw(A2A_TIMESTAMP_MILLIS_WIDTH) << millis;
    timestamp = oss.str();
}

namespace A2A::Log {

// Internal helper to reduce macro lines and handle C++ string formatting
template<typename... Args>
inline void LogInternal(A2A_LOG_LEVEL level, const char* file, const char* func, int line,
                        const std::string& format, Args&&... args)
{
    if (g_logCallback == nullptr) {
        return;
    }

    std::string timestamp;
    GetCurrentTimeStamp(timestamp);
    const char* filename = strrchr(file, '/');
    filename = filename ? filename + 1 : file;

    std::string prefix = "[" + timestamp + "] [" + std::to_string(syscall(SYS_gettid)) + "] " +
                         std::string(filename) + "::" + std::string(func) + ":[" + std::to_string(line) + "] ";

    static_assert(sizeof...(Args) == 0, "A2A_LOG expects C++-style string composition");
    g_logCallback(level, prefix + format);
}

} // namespace A2A::Log

#define A2A_LOG_COMMON(logLevel, format, ...) \
    ::A2A::Log::LogInternal(logLevel, __FILE__, __FUNCTION__, __LINE__, format, ##__VA_ARGS__)

#define A2A_LOG(level, format, ...) A2A_LOG_COMMON(level, format, ##__VA_ARGS__)

#endif
