/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include "a2a_log.h"
#include <iostream>
#include <chrono>
#include <cstdarg>

// Legacy C interface global variables
A2aLogCallback g_logCallback = A2aPrintfImpl;
static A2A_LOG_LEVEL g_logLevel = A2A_LOG_LEVEL_INFO;

// Legacy C interface functions
int32_t SetLogLevel(const A2A_LOG_LEVEL logLevel)
{
    if (logLevel < A2A_LOG_LEVEL_DEBUG || logLevel > A2A_LOG_LEVEL_FATAL) {
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

int32_t SetLogCallback(const A2aLogCallback logCallback)
{
    if (logCallback == nullptr) {
        std::cout << "log callback is null" << std::endl;
        return -1;
    }
    if (logCallback == g_logCallback) {
        return 0;
    }
    g_logCallback = logCallback;
    return 0;
}
