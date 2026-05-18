/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "prompt_manager.h"

#include <stdexcept>

namespace Mcp {

void PromptManager::AddPrompt(const PromptInfo& prompt, RenderPromptFunc handler)
{
    if (handler == nullptr) {
        throw std::invalid_argument("Prompt handler cannot be null");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (prompt.name.empty()) {
        throw std::invalid_argument("Prompt name cannot be empty");
    }
    if (!overwrite_ && prompts_.find(prompt.name) != prompts_.end()) {
        throw std::runtime_error("Prompt already exists: " + prompt.name);
    }
    prompts_[prompt.name] = PromptEntry{prompt, handler};
}

void PromptManager::RemovePrompt(const std::string& name)
{
    std::lock_guard<std::mutex> lock(mutex_);
    prompts_.erase(name);
}

ListPromptsResult PromptManager::ListPrompts()
{
    std::lock_guard<std::mutex> lock(mutex_);
    ListPromptsResult result;
    for (const auto& pair : prompts_) {
        result.prompts.push_back(pair.second.info);
    }
    return result;
}

std::optional<GetPromptResult> PromptManager::GetPrompt(const ServerContext& ctx, const std::string& name,
    const std::optional<JsonValue>& argument)
{
    RenderPromptFunc handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = prompts_.find(name);
        if (it == prompts_.end()) {
            throw std::runtime_error("Prompt not found: " + name);
        }
        handler = it->second.handler;
    }

    // Convert JsonValue to string for the RenderPromptFunc interface
    std::optional<std::string> argStr = std::nullopt;
    if (argument.has_value()) {
        argStr = argument.value().dump();
    }

    const ServerContext promptCtx{ctx.session,
        handler.IsAsync() ? ctx.responseCallback : ResponseCallback{},
        ctx.meta};
    return handler(promptCtx, name, argStr);
}

GetPromptResult PromptManager::GetPrompt(const std::string& name, const std::optional<JsonValue>& argument)
{
    auto result = GetPrompt(ServerContext{}, name, argument);
    if (!result.has_value()) {
        throw std::runtime_error("Prompt handler did not return a result");
    }
    return std::move(result.value());
}

} // namespace Mcp
