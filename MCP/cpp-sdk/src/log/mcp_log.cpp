/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "mcp_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

McpLogCallback g_logCallback = McpPrintfImpl;

static MCP_LOG_LEVEL g_logLevel = MCP_LOG_LEVEL_INFO;

int32_t SetLogLevel(MCP_LOG_LEVEL logLevel)
{
    if (logLevel < MCP_LOG_LEVEL_DEBUG || logLevel > MCP_LOG_LEVEL_FATAL) {
        return -1;
    }
    g_logLevel = logLevel;
    return 0;
}

MCP_LOG_LEVEL GetLogLevel(void)
{
    return g_logLevel;
}

void McpPrintfImpl(MCP_LOG_LEVEL logLevel, const char* format, ...)
{
    if (logLevel < GetLogLevel()) {
        return;
    }
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

int32_t SetLogCallback(McpLogCallback logCallback)
{
    if (logCallback == nullptr) {
        printf("log callback is null");
        return -1;
    }
    if (logCallback == g_logCallback) {
        printf("log callback is the same");
        return 0;
    }
    printf("log callback changed\n");
    g_logCallback = logCallback;
    return 0;
}
