/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef MCP_LOG_INCLUDE_H_
#define MCP_LOG_INCLUDE_H_

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_BUFFER_SIZE 1024

typedef enum {
    MCP_LOG_LEVEL_DEBUG = 3,
    MCP_LOG_LEVEL_INFO = 4,
    MCP_LOG_LEVEL_WARN = 5,
    MCP_LOG_LEVEL_ERROR = 6,
    MCP_LOG_LEVEL_FATAL = 7
} MCP_LOG_LEVEL;

typedef void (*McpLogCallback)(MCP_LOG_LEVEL logLevel, const char* format, ...);
extern McpLogCallback g_logCallback;

void McpPrintfImpl(MCP_LOG_LEVEL logLevel, const char* format, ...);

int32_t SetLogCallback(McpLogCallback logCallback);
int32_t SetLogLevel(MCP_LOG_LEVEL logLevel);
MCP_LOG_LEVEL GetLogLevel(void);

// Helper function to get current timestamp
static inline void GetCurrentTimeStamp(char* buffer, size_t bufferSize)
{
    struct timespec ts;
    struct tm tmInfo;
    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tmInfo);
    strftime(buffer, bufferSize, "%Y-%m-%d %H:%M:%S", &tmInfo);

    // Add milliseconds
    int len = strlen(buffer);
    snprintf(buffer + len, bufferSize - len, ".%03ld", ts.tv_nsec / 1000000);
}

#define MCP_LOG_COMMON(logLevel, format, ...)                                                                        \
    do {                                                                                                             \
        if (g_logCallback != NULL) {                                                                                 \
            char timestamp[32];                                                                                      \
            GetCurrentTimeStamp(timestamp, sizeof(timestamp));                                                       \
            const char* filename = strrchr(__FILE__, '/');                                                           \
            filename = filename ? filename + 1 : __FILE__;                                                           \
            g_logCallback(logLevel, "[%s] [%ld] %s::%s:[%d] " format "\n", timestamp, syscall(SYS_gettid), filename, \
                          __FUNCTION__, __LINE__, ##__VA_ARGS__);                                                    \
        }                                                                                                            \
    } while (0)

#define MCP_LOG(level, format, ...) MCP_LOG_COMMON(level, format, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif
