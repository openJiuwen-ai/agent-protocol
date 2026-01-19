/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "tool_manager.h"
#include "mcp_log.h"

#include <nlohmann/json.hpp>
#include <nlohmann/json-schema.hpp>
#include <stdexcept>

namespace Mcp {

CallToolResult NormalizeToolReturn(const ToolReturn& raw)
{
    if (std::holds_alternative<CallToolResult>(raw)) {
        return std::get<CallToolResult>(raw);
    } else if (std::holds_alternative<std::string>(raw)) {
        CallToolResult result;
        auto& structured = std::get<std::string>(raw);
        result.structuredContent = structured;
        
        TextContent textContent;
        textContent.type = "text";
        textContent.text = structured;
        result.content.push_back(textContent);
        
        return result;
    }
    
    return CallToolResult();
}

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
        tool.title = item.title;
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
    ServerTool tool;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = tools_.find(name);
        if (it == tools_.end()) {
            throw std::runtime_error("Tool not found: " + name);
        }
        tool = it->second;
    }

    JsonValue args;
    try {
        args = JsonValue::parse(arguments);
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to parse arguments: " + std::string(e.what()));
    }

    if (tool.inputSchema.has_value()) {
        try {
            JsonValue schemaJson = JsonValue::parse(tool.inputSchema.value());
            
            nlohmann::json_schema::json_validator validator;
            validator.set_root_schema(schemaJson);
            validator.validate(args);
        } catch (const std::exception& e) {
            throw std::runtime_error("Input validation failed for tool '" + name + "': " + std::string(e.what()));
        }
    }
    ToolReturn rawResult;
    try {
        rawResult = tool.func(name, args, std::nullopt);
    } catch (const std::exception& e) {
        throw std::runtime_error("Tool execution failed: " + std::string(e.what()));
    }

    CallToolResult result = NormalizeToolReturn(rawResult);
    if (tool.outputSchema.has_value() && result.isError == false) {
        if (result.structuredContent.has_value() == false) {
            return result;
        }
        try {
            JsonValue outputJson = JsonValue::parse(result.structuredContent.value());
            if (outputJson.is_object() == false) {
                throw std::runtime_error("structuredContent must be a JSON object for tool '" + name + "'");
            }
            JsonValue schemaJson = JsonValue::parse(tool.outputSchema.value());
            nlohmann::json_schema::json_validator validator;
            validator.set_root_schema(schemaJson);
            validator.validate(outputJson);
        } catch (const std::exception& e) {
            throw std::runtime_error("Output validation failed for tool '" + name + "': " + std::string(e.what()));
        }
    }

    return result;
}

} // namespace Mcp
