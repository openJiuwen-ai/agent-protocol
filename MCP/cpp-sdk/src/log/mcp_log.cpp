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

const char* GetLogLevelName(MCP_LOG_LEVEL logLevel)
{
    switch (logLevel) {
        case MCP_LOG_LEVEL_DEBUG:
            return "DEBUG";
        case MCP_LOG_LEVEL_INFO:
            return "INFO";
        case MCP_LOG_LEVEL_WARN:
            return "WARN";
        case MCP_LOG_LEVEL_ERROR:
            return "ERROR";
        case MCP_LOG_LEVEL_FATAL:
            return "FATAL";
        default:
            return "UNKNOWN";
    }
}

void McpPrintfImpl(MCP_LOG_LEVEL logLevel, std::string message)
{
    if (logLevel < GetLogLevel()) {
        return;
    }
    printf("%s\n", message.c_str());
}

int32_t SetLogCallback(McpLogCallback logCallback)
{
    if (logCallback == nullptr) {
        g_logCallback = McpPrintfImpl;
        return 0;
    }
    if (logCallback == g_logCallback) {
        return 0;
    }
    g_logCallback = logCallback;
    return 0;
}
