/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef MCP_THREAD_UTILS_H
#define MCP_THREAD_UTILS_H

#include <pthread.h>
#include <string>

namespace Mcp {

/**
 * @brief Set the name of the current thread (Linux only)
 * @param name The thread name (max 15 characters + null terminator)
 */
inline void SetCurrentThreadName(const std::string& name)
{
    // pthread_setname_np limits thread names to 16 chars including null terminator
    std::string truncatedName = name.substr(0, 15);
    pthread_setname_np(pthread_self(), truncatedName.c_str());
}

} // namespace Mcp

#endif // MCP_THREAD_UTILS_H
