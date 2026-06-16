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

namespace A2A::Log {
// Time-related constants for logging
constexpr int32_t A2A_NANOS_PER_MILLISECOND = 1000000;
constexpr int32_t A2A_TIMESTAMP_MILLIS_WIDTH = 3;

enum class A2A_LOG_LEVEL {
    DEBUG = 3,
    INFO = 4,
    WARN = 5,
    ERROR = 6,
    FATAL = 7
};

using A2aLogCallback = void (*)(A2A_LOG_LEVEL logLevel, std::string message);
extern A2aLogCallback logCallback;

void A2aPrintfImpl(A2A_LOG_LEVEL logLevel, std::string message);

int32_t SetLogCallback(const A2aLogCallback a2aLogCallback);
int32_t SetLogLevel(const A2A_LOG_LEVEL logLevel);
A2A_LOG_LEVEL GetLogLevel(void);

// Helper function to get current timestamp
void GetCurrentTimeStamp(std::string& timestamp);

// Internal helper to reduce macro lines and handle C++ string formatting
void LogInternal(A2A_LOG_LEVEL level, const char* file, const char* func, int line, const std::string& format);

} // namespace A2A::Log

#define A2A_LOG_COMMON(logLevel, format, ...) \
    ::A2A::Log::LogInternal(::A2A::Log::logLevel, __FILE__, __FUNCTION__, __LINE__, format)

#define A2A_LOG(level, format, ...) A2A_LOG_COMMON(level, format, ##__VA_ARGS__)

#define A2A_LOG_CONCAT(level, message) \
    do { \
        if ((::A2A::Log::level) >= ::A2A::Log::GetLogLevel() && ::A2A::Log::logCallback) { \
            std::ostringstream oss; \
            std::string ts; \
            ::A2A::Log::GetCurrentTimeStamp(ts); \
            oss << "[" << ts << "] [" << syscall(SYS_gettid) << "] " << \
                __FILE__ << "::" << __FUNCTION__ << ":[" << __LINE__ << "] " << \
                message; \
            ::A2A::Log::logCallback((::A2A::Log::level), oss.str()); \
        } \
    } while (0)

#endif