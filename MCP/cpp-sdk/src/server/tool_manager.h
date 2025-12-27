/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef MCP_TOOL_MANAGER_INCLUDE_H_
#define MCP_TOOL_MANAGER_INCLUDE_H_

#include <mutex>
#include <string>
#include <unordered_map>

#include "mcp_type.h"
#include "shared/jsonrpc.h"

namespace Mcp {

class ToolManager {
public:
    explicit ToolManager(bool overwrite = true) : overwrite_(overwrite)
    {
    }

    void AddTool(const ToolInfo& tool);
    void RemoveTool(const std::string& name);

    ListToolsResult ListTools() const;
    CallToolResult CallTool(const std::string& name, const std::string& arguments) const;

    void SetOverwrite(bool overwrite)
    {
        overwrite_ = overwrite;
    }
    bool GetOverwrite() const
    {
        return overwrite_;
    }

private:
    bool overwrite_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, ToolInfo> tools_;
};

} // namespace Mcp

#endif // MCP_TOOL_MANAGER_INCLUDE_H_
