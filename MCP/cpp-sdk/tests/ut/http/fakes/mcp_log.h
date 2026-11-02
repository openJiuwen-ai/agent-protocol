/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */
#pragma once

// Logging is irrelevant to the scripted transport; type-check messages.
#define MCP_LOG_LEVEL_ERROR 0
#define MCP_LOG(level, message) static_cast<void>(message)
