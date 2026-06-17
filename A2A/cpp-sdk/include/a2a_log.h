/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_LOG_INCLUDE_H_
#define A2A_LOG_INCLUDE_H_

#include <stdint.h>
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
 * @brief Get the string name for a log level (e.g. @c "INFO").
 * @param[in] level Log severity.
 * @return @c "DEBUG" / @c "INFO" / @c "WARN" / @c "ERROR" / @c "FATAL", or @c "UNKNOWN".
 */
const char* GetLogLevelName(A2A_LOG_LEVEL level);

/**
 * @brief Format the current wall-clock time into a string.
 * @param[out] timestamp Receives "YYYY-MM-DD HH:MM:SS.mmm".
 */
static inline void GetCurrentTimeStamp(std::string& timestamp)
{
    struct timespec ts;
    struct tm tmInfo;
    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tmInfo);

    char dateTimeBuf[32] = {0};
    std::strftime(dateTimeBuf, sizeof(dateTimeBuf), "%Y-%m-%d %H:%M:%S", &tmInfo);

    int32_t millis = static_cast<int32_t>(ts.tv_nsec / A2A_NANOS_PER_MILLISECOND);

    std::ostringstream oss;
    oss << dateTimeBuf << '.' <<
        std::setfill('0') << std::setw(A2A_TIMESTAMP_MILLIS_WIDTH) << millis;
    timestamp = oss.str();
}

// Internal helper to reduce macro lines and handle C++ string formatting.
template<typename... Args>
inline void LogInternal(A2A_LOG_LEVEL level, const char* file, const char* func, int line,
                        const std::string& format, Args&&... args)
{
    if (logCallback == nullptr) {
        return;
    }
    if (level < GetLogLevel()) {
        return;
    }

    std::string timestamp;
    GetCurrentTimeStamp(timestamp);
    const char* filename = strrchr(file, '/');
    filename = filename ? filename + 1 : file;

    std::string prefix = "[" + timestamp + "] [" + std::to_string(syscall(SYS_gettid)) + "] [" +
                         std::string(GetLogLevelName(level)) + "] " + std::string(filename) + "::" +
                         std::string(func) + ":[" + std::to_string(line) + "] ";

    static_assert(sizeof...(Args) == 0,
                  "A2A_LOG expects C++-style string composition");
    logCallback(level, prefix + format);
}

} // namespace A2A::Log

/**
 * @brief Log a diagnostic message via @ref LogInternal.
 * @param level  One of @c A2A::Log::A2A_LOG_LEVEL values.
 * @param format Message body as a pre-composed @c std::string (not printf-style).
 */
#define A2A_LOG_COMMON(logLevel, format, ...) \
    ::A2A::Log::LogInternal(::A2A::Log::logLevel, __FILE__, __FUNCTION__, __LINE__, format, ##__VA_ARGS__)

#define A2A_LOG(level, format, ...) A2A_LOG_COMMON(level, format, ##__VA_ARGS__)

#endif
