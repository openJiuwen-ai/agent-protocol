/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef MCP_LOG_INCLUDE_H_
#define MCP_LOG_INCLUDE_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

// Time-related constants for logging
constexpr int32_t MCP_NANOS_PER_MILLISECOND = 1000000;
constexpr int32_t MCP_TIMESTAMP_MILLIS_WIDTH = 3;

enum MCP_LOG_LEVEL {
    MCP_LOG_LEVEL_DEBUG = 3,
    MCP_LOG_LEVEL_INFO = 4,
    MCP_LOG_LEVEL_WARN = 5,
    MCP_LOG_LEVEL_ERROR = 6,
    MCP_LOG_LEVEL_FATAL = 7
};

typedef void (*McpLogCallback)(MCP_LOG_LEVEL logLevel, std::string message);
extern McpLogCallback g_logCallback;

void McpPrintfImpl(MCP_LOG_LEVEL logLevel, std::string message);

int32_t SetLogCallback(McpLogCallback logCallback);
int32_t SetLogLevel(MCP_LOG_LEVEL logLevel);
MCP_LOG_LEVEL GetLogLevel(void);

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

    int32_t millis = static_cast<int32_t>(ts.tv_nsec / MCP_NANOS_PER_MILLISECOND);

    std::ostringstream oss;
    oss << dateTimeBuf << '.'
        << std::setfill('0') << std::setw(MCP_TIMESTAMP_MILLIS_WIDTH) << millis;
    timestamp = oss.str();
}

namespace Mcp::Log {

// Internal helper to reduce macro lines and handle C++ string formatting
template<typename... Args>
inline void LogInternal(MCP_LOG_LEVEL level, const char* file, const char* func, int line,
                        const std::string& format, Args&&... args)
{
    if (g_logCallback == nullptr) {
        return;
    }
    if (level < GetLogLevel()) {
        return;
    }

    std::string timestamp;
    GetCurrentTimeStamp(timestamp);
    const char* filename = strrchr(file, '/');
    filename = filename ? filename + 1 : file;

    std::string prefix = "[" + timestamp + "] [" + std::to_string(syscall(SYS_gettid)) + "] " +
                         std::string(filename) + "::" + std::string(func) + ":[" + std::to_string(line) + "] ";

    static_assert(sizeof...(Args) == 0,
                  "MCP_LOG expects C++-style string composition");
    g_logCallback(level, prefix + format);
}

} // namespace Mcp::Log

#define MCP_LOG_COMMON(logLevel, format, ...) \
    ::Mcp::Log::LogInternal(logLevel, __FILE__, __FUNCTION__, __LINE__, format, ##__VA_ARGS__)

#define MCP_LOG(level, format, ...) MCP_LOG_COMMON(level, format, ##__VA_ARGS__)

#endif
