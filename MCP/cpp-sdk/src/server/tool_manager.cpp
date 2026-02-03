/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "tool_manager.h"

#include <stdexcept>

namespace Mcp {

void ToolManager::AddTool(const ToolInfo& tool)
{
    // Validate required fields
    if (tool.name.empty()) {
        throw std::invalid_argument("Tool name cannot be empty");
    }
    if (tool.description.empty()) {
        throw std::invalid_argument("Tool description cannot be empty");
    }
    if (!tool.func) {
        throw std::invalid_argument("Tool function implementation cannot be null");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tools_.find(tool.name);
    if (it != tools_.end() && !overwrite_) {
        throw std::runtime_error("Tool '" + tool.name + "' already exists");
    }
    tools_[tool.name] = tool;
}

void ToolManager::RemoveTool(const std::string& name)
{
    // Validate required fields
    if (name.empty()) {
        throw std::invalid_argument("Tool name cannot be empty");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tools_.find(name);
    if (it == tools_.end()) {
        throw std::runtime_error("Tool '" + name + "' not found");
    }
    tools_.erase(it);
}

ListToolsResult ToolManager::ListTools() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    ListToolsResult result;
    result.tools.reserve(tools_.size());

    for (const auto& [_, toolInfo] : tools_) {
        Tool tool;
        tool.name = toolInfo.name;
        tool.description = toolInfo.description;
        tool.inputSchema = toolInfo.inputSchema;

        result.tools.push_back(std::move(tool));
    }

    return result;
}

CallToolResult ToolManager::CallTool(const ServerContext& ctx, const std::string& name,
    const std::string& arguments) const
{
    ToolFunc func;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = tools_.find(name);
        if (it == tools_.end()) {
            throw std::runtime_error("Tool not found: " + name);
        }
        func = it->second.func;
    }

    try {
        JsonValue args = JsonValue::parse(arguments);
        return func(ctx, name, args);
    } catch (const std::exception& e) {
        throw std::runtime_error("Tool execution failed: " + std::string(e.what()));
    }
}

} // namespace Mcp
