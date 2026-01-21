/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef MCP_TOOL_MANAGER_INCLUDE_H_
#define MCP_TOOL_MANAGER_INCLUDE_H_

#include <mutex>
#include <string>
#include <unordered_map>

#include "mcp_server.h"
#include "shared/jsonrpc.h"

namespace Mcp {

struct ServerTool {
    std::string name;
    ToolFunc func;
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::optional<std::string> inputSchema;
    std::optional<std::string> outputSchema;
    bool structuredOutput = false;
    std::optional<ToolAnnotations> annotations;
    std::optional<std::vector<Icon>> icons;

    ServerTool() = default;
    
    ServerTool(std::string name, ToolFunc func,
               std::optional<std::reference_wrapper<const std::string>> title = std::nullopt,
               std::optional<std::reference_wrapper<const std::string>> description = std::nullopt,
               std::optional<std::reference_wrapper<const std::string>> inputSchema = std::nullopt,
               std::optional<std::reference_wrapper<const std::string>> outputSchema = std::nullopt,
               bool structuredOutput = false,
               std::optional<std::reference_wrapper<const ToolAnnotations>> annotations = std::nullopt,
               std::optional<std::reference_wrapper<const std::vector<Icon>>> icons = std::nullopt)
        : name(std::move(name)), func(std::move(func)),
        title(title ? std::optional<std::string>(title->get()) : std::nullopt),
        description(description ? std::optional<std::string>(description->get()) : std::nullopt),
        inputSchema(inputSchema ? std::optional<std::string>(inputSchema->get()) : std::nullopt),
        outputSchema(outputSchema ? std::optional<std::string>(outputSchema->get()) : std::nullopt),
        structuredOutput(structuredOutput),
        annotations(annotations ? std::optional<ToolAnnotations>(annotations->get()) : std::nullopt),
        icons(icons ? std::optional<std::vector<Icon>>(icons->get()) : std::nullopt) {}
};

class ToolManager {
public:
    explicit ToolManager(bool overwrite = true,
                         std::size_t pageSize = DEFAULT_TOOLS_PAGE_SIZE)
        : overwrite_(overwrite), pageSize_(pageSize)
    {
    }

    void AddTool(const ServerTool& tool);
    void RemoveTool(const std::string& name);

    // List tools with optional cursor-based pagination. When cursor is not set,
    // listing starts from the beginning. The returned ListToolsResult may
    // carry nextCursor to indicate there are more tools to fetch.
    ListToolsResult ListTools(const std::optional<std::string>& cursor = std::nullopt) const;
    CallToolResult CallTool(const std::string& name, const std::string& arguments) const;

    void SetOverwrite(bool overwrite)
    {
        overwrite_ = overwrite;
    }
    bool GetOverwrite() const
    {
        return overwrite_;
    }

    void SetPageSize(std::size_t pageSize)
    {
        pageSize_ = pageSize;
    }

private:
    bool overwrite_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, ServerTool> tools_;
    std::size_t pageSize_;
};

} // namespace Mcp

#endif // MCP_TOOL_MANAGER_INCLUDE_H_
