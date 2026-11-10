/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */
#pragma once

#include <string>

// Preserve the logging call shape without a function-like macro.
inline constexpr int MCP_LOG_LEVEL_ERROR = 0;
inline void MCP_LOG(int /* level */, const std::string& message)
{
    static_cast<void>(message);
}
