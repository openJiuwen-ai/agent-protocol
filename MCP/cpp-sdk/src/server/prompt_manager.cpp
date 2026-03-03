/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "prompt_manager.h"

#include <stdexcept>

namespace Mcp {

void PromptManager::AddPrompt(const PromptInfo& prompt, RenderPromptFunc handler)
{
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

    try {
        // Use std::visit to handle the variant
        return std::visit([&](auto&& f) -> std::optional<GetPromptResult> {
            using T = std::decay_t<decltype(f)>;
            if constexpr (std::is_same_v<T, SyncRenderPromptFunc>) {
                // Synchronous function, create context without callback but keep meta
                ServerContext syncCtx = {ctx.session, nullptr, ctx.meta};
                return f(syncCtx, name, argument);
            } else if constexpr (std::is_same_v<T, AsyncRenderPromptFunc>) {
                // Asynchronous function, use original context with callback
                f(ctx, name, argument);
                return std::nullopt;
            }
        }, handler);
    } catch (const std::exception& e) {
        throw std::runtime_error("Prompt execution failed: " + std::string(e.what()));
    }
}

} // namespace Mcp
