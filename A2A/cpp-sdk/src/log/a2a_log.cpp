/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <atomic>
#include <cstdio>
#include <iostream>

#include "a2a_log.h"


namespace A2A::Log {

A2aLogCallback logCallback = A2aPrintfImpl;
static A2A_LOG_LEVEL g_logLevel = A2A_LOG_LEVEL::INFO;

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

const char* GetLogLevelName(A2A_LOG_LEVEL logLevel)
{
    switch (logLevel) {
        case A2A_LOG_LEVEL::DEBUG:
            return "DEBUG";
        case A2A_LOG_LEVEL::INFO:
            return "INFO";
        case A2A_LOG_LEVEL::WARN:
            return "WARN";
        case A2A_LOG_LEVEL::ERROR:
            return "ERROR";
        case A2A_LOG_LEVEL::FATAL:
            return "FATAL";
        default:
            return "UNKNOWN";
    }
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

} // namespace A2A::Log
