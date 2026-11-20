/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef MCP_THREAD_UTILS_H
#define MCP_THREAD_UTILS_H

#include <string>

#include "platform_compat.h"

namespace Mcp {

/**
 * @brief Set the name of the current thread.
 * @param name The thread name (max 15 characters + null terminator)
 */
inline void SetCurrentThreadName(const std::string& name)
{
    Platform::SetCurrentThreadName(name);
}

} // namespace Mcp

#endif // MCP_THREAD_UTILS_H
