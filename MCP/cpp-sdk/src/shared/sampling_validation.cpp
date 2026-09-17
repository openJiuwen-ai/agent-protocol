/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include "shared/sampling_validation.h"

#include <set>
#include <stdexcept>

namespace Mcp {

namespace {

std::vector<Mcp::SamplingMessageContentBlock> ContentAsList(const Mcp::SamplingMessage& message)
{
    if (std::holds_alternative<Mcp::SamplingMessageContentBlock>(message.content)) {
        return {std::get<Mcp::SamplingMessageContentBlock>(message.content)};
    }
    return std::get<std::vector<Mcp::SamplingMessageContentBlock>>(message.content);
}

bool ContainsToolUse(const std::vector<Mcp::SamplingMessageContentBlock>& blocks)
{
    for (const auto& c : blocks) {
        if (std::holds_alternative<Mcp::ToolUseContent>(c)) {
            return true;
        }
    }
    return false;
}

bool ContainsToolResult(const std::vector<Mcp::SamplingMessageContentBlock>& blocks)
{
    for (const auto& c : blocks) {
        if (std::holds_alternative<Mcp::ToolResultContent>(c)) {
            return true;
        }
    }
    return false;
}

std::set<std::string> CollectToolUseIds(const std::vector<Mcp::SamplingMessageContentBlock>& blocks)
{
    std::set<std::string> ids;
    for (const auto& c : blocks) {
        if (std::holds_alternative<Mcp::ToolUseContent>(c)) {
            ids.insert(std::get<Mcp::ToolUseContent>(c).id);
        }
    }
    return ids;
}

std::set<std::string> CollectToolResultIds(const std::vector<Mcp::SamplingMessageContentBlock>& blocks)
{
    std::set<std::string> ids;
    for (const auto& c : blocks) {
        if (std::holds_alternative<Mcp::ToolResultContent>(c)) {
            ids.insert(std::get<Mcp::ToolResultContent>(c).toolUseId);
        }
    }
    return ids;
}

void EnsureToolResultMessageOnlyHasToolResults(const std::vector<Mcp::SamplingMessageContentBlock>& blocks)
{
    for (const auto& c : blocks) {
        if (!std::holds_alternative<Mcp::ToolResultContent>(c)) {
            // Spec: tool results must not be mixed with other content types.
            throw std::runtime_error("Tool results mixed with other content");
        }
    }
}

} // namespace

void ValidateToolUseResultMessages(const std::vector<Mcp::SamplingMessage>& messages)
{
    // Enforce rules across the whole message list so earlier tool loops are also well-formed.
    for (size_t i = 0; i < messages.size(); ++i) {
        const auto blocks = ContentAsList(messages[i]);
        const bool hasToolUse = ContainsToolUse(blocks);
        const bool hasToolResult = ContainsToolResult(blocks);
        if (hasToolResult) {
            EnsureToolResultMessageOnlyHasToolResults(blocks);
            if (messages[i].role != Mcp::RoleType::USER) {
                throw std::runtime_error("Tool results mixed with other content");
            }
            if (i == 0) {
                throw std::runtime_error("Tool result missing in request");
            }

            const auto prevBlocks = ContentAsList(messages[i - 1]);
            const auto toolUseIds = CollectToolUseIds(prevBlocks);
            if (toolUseIds.empty()) {
                throw std::runtime_error("Tool result missing in request");
            }

            const auto toolResultIds = CollectToolResultIds(blocks);
            if (toolUseIds != toolResultIds) {
                throw std::runtime_error("Tool result missing in request");
            }
        }

        if (hasToolUse) {
            if (messages[i].role != Mcp::RoleType::ASSISTANT) {
                throw std::runtime_error("Tool result missing in request");
            }
            // Every assistant tool_use must be followed by a user tool_result message.
            if (i + 1 >= messages.size()) {
                throw std::runtime_error("Tool result missing in request");
            }

            const auto nextBlocks = ContentAsList(messages[i + 1]);
            if (!ContainsToolResult(nextBlocks)) {
                throw std::runtime_error("Tool result missing in request");
            }
            EnsureToolResultMessageOnlyHasToolResults(nextBlocks);
            const auto toolUseIds = CollectToolUseIds(blocks);
            const auto toolResultIds = CollectToolResultIds(nextBlocks);
            if (toolUseIds != toolResultIds) {
                throw std::runtime_error("Tool result missing in request");
            }
        }
    }
}

} // namespace Mcp
