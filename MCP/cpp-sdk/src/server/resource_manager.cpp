/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "resource_manager.h"

#include <stdexcept>

namespace Mcp {

void ResourceManager::AddResource(const ResourceInfo& resource, ReadResourceFunc readFunc)
{
    // Validate required fields
    if (resource.uri.empty()) {
        throw std::invalid_argument("Resource URI cannot be empty");
    }
    if (resource.name.empty()) {
        throw std::invalid_argument("Resource name cannot be empty");
    }
    if (!readFunc) {
        throw std::invalid_argument("ReadResourceFunc cannot be null");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = resources_.find(resource.uri);
    if (it != resources_.end() && !overwrite_) {
        throw std::runtime_error("Resource '" + resource.uri + "' already exists");
    }

    ResourceEntry entry;
    entry.info = resource;
    entry.readFunc = std::move(readFunc);
    resources_[resource.uri] = std::move(entry);
}

void ResourceManager::RemoveResource(const std::string& uri)
{
    // Validate required fields
    if (uri.empty()) {
        throw std::invalid_argument("Resource URI cannot be empty");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = resources_.find(uri);
    if (it == resources_.end()) {
        throw std::runtime_error("Resource '" + uri + "' not found");
    }
    resources_.erase(it);
}

void ResourceManager::AddResourceTemplate(const ResourceTemplate& resourceTemplate)
{
    // Validate required fields
    if (resourceTemplate.uriTemplate.empty()) {
        throw std::invalid_argument("Resource template URI cannot be empty");
    }
    if (resourceTemplate.name.empty()) {
        throw std::invalid_argument("Resource template name cannot be empty");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = resourceTemplates_.find(resourceTemplate.uriTemplate);
    if (it != resourceTemplates_.end() && !overwrite_) {
        throw std::runtime_error("Resource template '" + resourceTemplate.uriTemplate + "' already exists");
    }

    resourceTemplates_[resourceTemplate.uriTemplate] = resourceTemplate;
}

void ResourceManager::RemoveResourceTemplate(const std::string& uriTemplate)
{
    // Validate required fields
    if (uriTemplate.empty()) {
        throw std::invalid_argument("Resource template URI cannot be empty");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = resourceTemplates_.find(uriTemplate);
    if (it == resourceTemplates_.end()) {
        throw std::runtime_error("Resource template '" + uriTemplate + "' not found");
    }
    resourceTemplates_.erase(it);
}

ListResourcesResult ResourceManager::ListResources()
{
    std::lock_guard<std::mutex> lock(mutex_);
    ListResourcesResult result;
    result.resources.reserve(resources_.size());

    for (const auto& [uri, entry] : resources_) {
        result.resources.push_back(entry.info);
    }

    return result;
}

ReadResourceResult ResourceManager::ReadResource(const ServerContext& ctx, const std::string& uri)
{
    ReadResourceFunc readFunc;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = resources_.find(uri);
        if (it == resources_.end()) {
            throw std::runtime_error("Resource not found:" + uri);
        }
        readFunc = it->second.readFunc;
    }

    return readFunc(ctx, uri);
}

ListResourceTemplatesResult ResourceManager::ListResourceTemplates()
{
    std::lock_guard<std::mutex> lock(mutex_);
    ListResourceTemplatesResult result;
    result.resourceTemplates.reserve(resourceTemplates_.size());

    for (const auto& [uriTemplate, resourceTemplate] : resourceTemplates_) {
        result.resourceTemplates.push_back(resourceTemplate);
    }

    return result;
}

void ResourceManager::SubscribeResource(const std::string& uri)
{
    // Subscription notification mechanism to be implemented here.
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = resources_.find(uri);
    if (it == resources_.end()) {
        throw std::runtime_error("Resource not found:" + uri);
    }
}

void ResourceManager::UnsubscribeResource(const std::string& uri)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = resources_.find(uri);
    if (it == resources_.end()) {
        throw std::runtime_error("Resource not found:" + uri);
    }
}

} // namespace Mcp