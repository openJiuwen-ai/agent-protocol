/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "tool_manager.h"

#include <stdexcept>

namespace Mcp {

void ToolManager::AddTool(const ServerTool& tool)
{
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

    for (const auto& [_, item] : tools_) {
        Tool tool;
        tool.name = item.name;
        tool.description = item.description;
        tool.inputSchema = item.inputSchema;
        tool.outputSchema = item.outputSchema;
        tool.annotations = item.annotations;
        tool.icons = item.icons;
        result.tools.push_back(std::move(tool));
    }

    return result;
}

CallToolResult ToolManager::CallTool(const std::string& name, const std::string& arguments) const
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
        return func(name, args, std::nullopt);
    } catch (const std::exception& e) {
        throw std::runtime_error("Tool execution failed: " + std::string(e.what()));
    }
}

} // namespace Mcp
