/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef MCP_PROMPT_MANAGER_INCLUDE_H_
#define MCP_PROMPT_MANAGER_INCLUDE_H_

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "mcp_server.h"
#include "mcp_type.h"
#include "shared/jsonrpc.h"

namespace Mcp {

class PromptManager {
public:
    explicit PromptManager(bool overwrite = true) : overwrite_(overwrite)
    {
    }

    ~PromptManager() = default;

    void AddPrompt(const PromptInfo& prompt, RenderPromptFunc handler);
    void RemovePrompt(const std::string& name);
    ListPromptsResult ListPrompts();
    std::optional<GetPromptResult> GetPrompt(const ServerContext& ctx, const std::string& name,
        const std::optional<JsonValue>& argument = std::nullopt);
    // Backward-compat overload without ServerContext.
    GetPromptResult GetPrompt(const std::string& name,
        const std::optional<JsonValue>& argument = std::nullopt);

private:
    struct PromptEntry {
        PromptInfo info;
        RenderPromptFunc handler;
    };

    bool overwrite_;
    std::unordered_map<std::string, PromptEntry> prompts_;
    std::mutex mutex_;
};

} // namespace Mcp

#endif // MCP_PROMPT_MANAGER_INCLUDE_H_
