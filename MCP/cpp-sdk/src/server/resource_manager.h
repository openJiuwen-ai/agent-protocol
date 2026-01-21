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
    explicit ResourceManager(bool overwrite = true,
                             std::size_t pageSize = DEFAULT_RESOURCES_PAGE_SIZE)
        : overwrite_(overwrite), pageSize_(pageSize)
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
    // List resources with optional cursor-based pagination. When cursor is not
    // provided, listing starts from the beginning. The returned
    // ListResourcesResult may carry nextCursor to indicate more resources.
    ListResourcesResult ListResources(const std::optional<std::string>& cursor = std::nullopt);
    ReadResourceResult ReadResource(const std::string& uri);
    ListResourceTemplatesResult ListResourceTemplates() const;

    void SetPageSize(std::size_t pageSize)
    {
        pageSize_ = pageSize;
    }

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

    mutable std::mutex mutex_;
    std::size_t pageSize_;
};

} // namespace Mcp

#endif // MCP_RESOURCE_MANAGER_INCLUDE_H_