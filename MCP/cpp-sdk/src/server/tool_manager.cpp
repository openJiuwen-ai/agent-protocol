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

ListToolsResult ToolManager::ListTools(const std::optional<std::string>& cursor) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    // Build a stable ordering of tool names.
    std::vector<std::string> names;
    names.reserve(tools_.size());
    for (const auto& entry : tools_) {
        names.push_back(entry.first);
    }
    std::sort(names.begin(), names.end());

    // Decode cursor as starting index; invalid values fall back to 0.
    std::size_t startIndex = 0;
    if (cursor.has_value()) {
        try {
            startIndex = static_cast<std::size_t>(std::stoll(cursor.value()));
        } catch (...) {
            startIndex = 0;
        }
        if (startIndex > names.size()) {
            startIndex = names.size();
        }
    }

    ListToolsResult result;

    const std::size_t endIndex = std::min(startIndex + pageSize_, names.size());
    result.tools.reserve(endIndex - startIndex);

    for (std::size_t i = startIndex; i < endIndex; ++i) {
        const auto& name = names[i];
        const auto it = tools_.find(name);
        if (it == tools_.end()) {
            continue;
        }
        const auto& item = it->second;

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

    if (endIndex < names.size()) {
        result.nextCursor = std::to_string(endIndex);
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
        // Convert JsonValue to string for the ToolFunc interface
        rawResult = tool.func(name, args.dump(), std::nullopt);
    } catch (const std::exception& e) {
        CallToolResult errorResult;
        errorResult.isError = true;

        TextContent textContent;
        textContent.type = "text";
        textContent.text = e.what();
        errorResult.content.push_back(std::move(textContent));

        rawResult = std::move(errorResult);
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
