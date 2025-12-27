/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef MCP_RESOURCE_MANAGER_INCLUDE_H_
#define MCP_RESOURCE_MANAGER_INCLUDE_H_

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "mcp_type.h"
#include "shared/jsonrpc.h"

namespace Mcp {

class ResourceManager {
public:
    explicit ResourceManager(bool overwrite = true) : overwrite_(overwrite)
    {
    }

    ~ResourceManager() = default;

    // Resource operations
    void AddResource(const ResourceInfo& resource, ReadResourceFunc readFunc);
    void RemoveResource(const std::string& uri);

    // Resource template operations
    void AddResourceTemplate(const ResourceTemplate& resourceTemplate);
    void RemoveResourceTemplate(const std::string& uriTemplate);

    // Get operations
    ListResourcesResult ListResources();
    ReadResourceResult ReadResource(const std::string& uri);
    ListResourceTemplatesResult ListResourceTemplates();

    // Subscription operations
    void SubscribeResource(const std::string& uri);
    void UnsubscribeResource(const std::string& uri);

private:
    struct ResourceEntry {
        ResourceInfo info;
        ReadResourceFunc readFunc;
    };

    bool overwrite_;
    std::unordered_map<std::string, ResourceEntry> resources_;
    std::unordered_map<std::string, ResourceTemplate> resourceTemplates_;

    std::mutex mutex_;
};

} // namespace Mcp

#endif // MCP_RESOURCE_MANAGER_INCLUDE_H_