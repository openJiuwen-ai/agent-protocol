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

/** @brief Nanoseconds per millisecond, used for timestamp formatting. */
constexpr int32_t A2A_NANOS_PER_MILLISECOND = 1000000;

/** @brief Width of the millisecond field in log timestamps. */
constexpr int32_t A2A_TIMESTAMP_MILLIS_WIDTH = 3;

/**
 * @brief Log severity levels (ordered from most to least verbose).
 * @note 日志级别，数值越大表示越严重。
 */
enum class A2A_LOG_LEVEL {
    DEBUG = 3,
    INFO = 4,
    WARN = 5,
    ERROR = 6,
    FATAL = 7
};

/**
 * @brief User-provided callback invoked for each log line.
 * @param logLevel Severity of the message.
 * @param message  Fully formatted log line.
 */
using A2aLogCallback = void (*)(A2A_LOG_LEVEL logLevel, std::string message);

/** @brief Active log sink; defaults to @ref A2aPrintfImpl. nullptr suppresses all output. */
extern A2aLogCallback logCallback;

/**
 * @brief Default log sink: writes to stdout via @c printf (appends newline).
 * @param logLevel Severity of the message (filtered against @ref GetLogLevel).
 * @param message  Fully formatted log line without trailing newline.
 */
void A2aPrintfImpl(A2A_LOG_LEVEL logLevel, std::string message);

/**
 * @brief Register a custom log callback (process-wide, once only).
 * @param[in] a2aLogCallback Callback to receive formatted log lines.
 * @return 0 on success, @c -1 if a callback was already registered.
 */
int32_t SetLogCallback(const A2aLogCallback a2aLogCallback);

/**
 * @brief Set the minimum severity that is emitted.
 * @param[in] logLevel Minimum level to log.
 * @return 0 on success, non-zero on failure.
 */
int32_t SetLogLevel(const A2A_LOG_LEVEL logLevel);

/**
 * @brief Get the current minimum log level.
 * @return Active minimum severity.
 */
A2A_LOG_LEVEL GetLogLevel(void);

/**
 * @brief Format the current wall-clock time into a string.
 * @param[out] timestamp Receives "YYYY-MM-DD HH:MM:SS.mmm".
 */
void GetCurrentTimeStamp(std::string& timestamp);

/**
 * @brief Internal helper used by A2A_LOG / A2A_LOG_CONCAT macros.
 * @param[in] level  Severity.
 * @param[in] file   Source file name.
 * @param[in] func   Function name.
 * @param[in] line   Source line number.
 * @param[in] format Message body.
 */
void LogInternal(A2A_LOG_LEVEL level, const char* file, const char* func, int line, const std::string& format);

} // namespace A2A::Log

/**
 * @brief Log a pre-formatted message via @ref LogInternal (legacy).
 * @param level  One of @c A2A::Log::A2A_LOG_LEVEL values.
 * @param format Message body; **not** printf-interpolated (passed as-is).
 */
#define A2A_LOG_COMMON(logLevel, format, ...) \
    ::A2A::Log::LogInternal(::A2A::Log::logLevel, __FILE__, __FUNCTION__, __LINE__, format)

#define A2A_LOG(level, format, ...) A2A_LOG_COMMON(level, format, ##__VA_ARGS__)

/**
 * @brief Log with stream concatenation; formats timestamp, tid, and location.
 * @param level   One of @c A2A::Log::A2A_LOG_LEVEL values.
 * @param message Expression streamed with @c << (e.g. @c "port " << port).
 * @note Filtered by @ref GetLogLevel; requires non-null @ref logCallback.
 */
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
