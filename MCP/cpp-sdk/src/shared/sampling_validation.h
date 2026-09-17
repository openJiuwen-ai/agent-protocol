/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef MCP_SAMPLING_VALIDATION_INCLUDE_H_
#define MCP_SAMPLING_VALIDATION_INCLUDE_H_

#include <vector>

#include "mcp_type.h"

namespace Mcp {

/**
 * @brief Validate tool_use/tool_result message structure for sampling.
 *
 * Constraints (from MCP Sampling spec):
 * 1) Any message containing tool_result blocks MUST contain ONLY tool_result blocks.
 * 2) tool_result messages MUST be preceded by an assistant message containing tool_use blocks.
 * 3) tool_result.toolUseId set MUST match the previous message tool_use.id set.
 *
 * @param messages The sampling messages to validate.
 * @throw std::runtime_error If validation fails.
 */
void ValidateToolUseResultMessages(const std::vector<SamplingMessage>& messages);

} // namespace Mcp

#endif // MCP_SAMPLING_VALIDATION_INCLUDE_H_
