/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "mcp_client.h"
#include "mcp_log.h"
#include "mcp_type.h"

constexpr int REQUEST_TIMEOUT = 300;
constexpr char EXAMPLE_ENDPOINT[] = "http://localhost:8000/mcp";
constexpr char EXAMPLE_TOKEN[] = "your-token";

int main()
{
    SetLogLevel(MCP_LOG_LEVEL_INFO);

    // Setup client configuration
    Mcp::ClientConfig config;
    config.name = "ResourceExampleClient";
    config.version = "1.0.0";
    Mcp::StreamableHttpClientConfig httpConfig;
    httpConfig.endpoint = EXAMPLE_ENDPOINT;
    httpConfig.tlsConfig.enabled = false;

    auto authProvider = std::make_shared<Mcp::BearerTokenProvider>(EXAMPLE_TOKEN);
    auto mcpClient = Mcp::McpClientFactory::CreateStreamableHttpClient(config, httpConfig, std::move(authProvider));

    // Initialize client
    MCP_LOG(MCP_LOG_LEVEL_INFO, "=== Example Initialize ===");
    try {
        auto initFuture = mcpClient->Initialize();
        if (initFuture.wait_for(std::chrono::seconds(REQUEST_TIMEOUT)) != std::future_status::ready) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "Initialize timeout");
            return -1;
        }
        initFuture.get();
        MCP_LOG(MCP_LOG_LEVEL_INFO, "Initialize success");
    } catch (const std::exception &e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Initialize failed: %s", e.what());
        return -1;
    }

    // Example 1: List resources
    MCP_LOG(MCP_LOG_LEVEL_INFO, "=== Example ListResources ===");
    std::string targetUri;
    try {
        auto listFuture = mcpClient->ListResources();
        if (listFuture.wait_for(std::chrono::seconds(REQUEST_TIMEOUT)) != std::future_status::ready) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "ListResources timeout");
            return -1;
        }
        auto resourceList = listFuture.get();
        MCP_LOG(MCP_LOG_LEVEL_INFO, "ListResources success, resource count: %zu", resourceList->resources.size());
        for (const auto &resource : resourceList->resources) {
            MCP_LOG(MCP_LOG_LEVEL_INFO, "  Resource: %s (uri: %s)", resource.name.c_str(), resource.uri.c_str());
        }
        if (!resourceList->resources.empty()) {
            targetUri = resourceList->resources.front().uri;
        }
    } catch (const std::exception &e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "ListResources failed: %s", e.what());
        return -1;
    }

    // Example 2: Subscribe/Unsubscribe resource
    if (!targetUri.empty()) {
        MCP_LOG(MCP_LOG_LEVEL_INFO, "=== Example Subscribe/Unsubscribe Resource ===");
        try {
            auto subFuture = mcpClient->SubscribeResource(targetUri);
            if (subFuture.wait_for(std::chrono::seconds(REQUEST_TIMEOUT)) != std::future_status::ready) {
                MCP_LOG(MCP_LOG_LEVEL_ERROR, "SubscribeResource timeout");
                return -1;
            }
            subFuture.get();
            MCP_LOG(MCP_LOG_LEVEL_INFO, "SubscribeResource success: %s", targetUri.c_str());

            auto unsubFuture = mcpClient->UnsubscribeResource(targetUri);
            if (unsubFuture.wait_for(std::chrono::seconds(REQUEST_TIMEOUT)) != std::future_status::ready) {
                MCP_LOG(MCP_LOG_LEVEL_ERROR, "UnsubscribeResource timeout");
                return -1;
            }
            unsubFuture.get();
            MCP_LOG(MCP_LOG_LEVEL_INFO, "UnsubscribeResource success: %s", targetUri.c_str());
        } catch (const std::exception &e) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "Subscribe/Unsubscribe failed: %s", e.what());
            return -1;
        }
    }

    // Example 3: Read resource
    if (!targetUri.empty()) {
        MCP_LOG(MCP_LOG_LEVEL_INFO, "=== Example ReadResource ===");
        try {
            auto readFuture = mcpClient->ReadResource(targetUri);
            if (readFuture.wait_for(std::chrono::seconds(REQUEST_TIMEOUT)) != std::future_status::ready) {
                MCP_LOG(MCP_LOG_LEVEL_ERROR, "ReadResource timeout");
                return -1;
            }
            auto readResult = readFuture.get();
            MCP_LOG(MCP_LOG_LEVEL_INFO, "ReadResource success: %s, content count: %zu", targetUri.c_str(),
                    readResult->contents.size());
        } catch (const std::exception &e) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "ReadResource failed: %s", e.what());
            return -1;
        }
    }

    // Example 4: List resource templates
    MCP_LOG(MCP_LOG_LEVEL_INFO, "=== Example ListResourcesTemplates ===");
    try {
        auto templateFuture = mcpClient->ListResourcesTemplates();
        if (templateFuture.wait_for(std::chrono::seconds(REQUEST_TIMEOUT)) != std::future_status::ready) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "ListResourcesTemplates timeout");
            return -1;
        }
        auto templates = templateFuture.get();
        MCP_LOG(MCP_LOG_LEVEL_INFO, "ListResourcesTemplates success, template count: %zu",
                templates->resourceTemplates.size());
        for (const auto &tmpl : templates->resourceTemplates) {
            MCP_LOG(MCP_LOG_LEVEL_INFO, "  Template: %s (mimeType: %s)", tmpl.uriTemplate.c_str(),
                    tmpl.mimeType.has_value() ? tmpl.mimeType.value().c_str() : "none");
        }
    } catch (const std::exception &e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "ListResourcesTemplates failed: %s", e.what());
        return -1;
    }

    MCP_LOG(MCP_LOG_LEVEL_INFO, "=== Example completed ===");
    return 0;
}