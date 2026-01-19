/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <variant>

#include <nlohmann/json.hpp>

#include "mcp_client.h"
#include "mcp_log.h"
#include "mcp_type.h"

constexpr int REQUEST_TIMEOUT = 300;
constexpr char EXAMPLE_ENDPOINT[] = "http://localhost:8000/mcp";
constexpr char EXAMPLE_TOKEN[] = "your-token";
constexpr char EXAMPLE_TOOL_NAME[] = "echo";

int main()
{
    SetLogLevel(MCP_LOG_LEVEL_INFO);

    // Setup client configuration
    Mcp::ClientConfig config;
    config.name = "ToolExampleClient";
    config.version = "1.0.0";
    Mcp::StreamableHttpClientConfig httpConfig;
    httpConfig.endpoint = EXAMPLE_ENDPOINT;
    httpConfig.tlsConfig.enabled = false;

    auto authProvider = std::make_shared<Mcp::BearerTokenProvider>(EXAMPLE_TOKEN);
    auto mcpClient = Mcp::McpClientFactory::CreateStreamableHttpClient(config, httpConfig, std::move(authProvider));

    // Example 1: Initialize client
    MCP_LOG(MCP_LOG_LEVEL_INFO, "=== Example Initialize ===");
    try {
        auto initFuture = mcpClient->Initialize();
        if (initFuture.wait_for(std::chrono::seconds(REQUEST_TIMEOUT)) != std::future_status::ready) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "Initialize timeout");
            return -1;
        }
        auto initResult = initFuture.get();
        MCP_LOG(MCP_LOG_LEVEL_INFO, "Initialize success");
    } catch (const std::exception &e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("Initialize failed: ") + e.what());
        return -1;
    }

    // Example 1.5: Ping
    MCP_LOG(MCP_LOG_LEVEL_INFO, "=== Example Ping ===");
    try {
        auto pingFuture = mcpClient->SendPing();
        if (pingFuture.wait_for(std::chrono::seconds(REQUEST_TIMEOUT)) != std::future_status::ready) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "Ping timeout");
            return -1;
        }
        auto pingResult = pingFuture.get();
        (void)pingResult; // EmptyResult
        MCP_LOG(MCP_LOG_LEVEL_INFO, "Ping success");
    } catch (const std::exception &e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("Ping failed: ") + e.what());
        return -1;
    }

    // Example 2: List tools
    MCP_LOG(MCP_LOG_LEVEL_INFO, "=== Example ListTools ===");
    try {
        auto listFuture = mcpClient->ListTools();
        if (listFuture.wait_for(std::chrono::seconds(REQUEST_TIMEOUT)) != std::future_status::ready) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "ListTools timeout");
            return -1;
        }
        auto listResult = listFuture.get();
        MCP_LOG(MCP_LOG_LEVEL_INFO,
                std::string("ListTools success, tool count: ") + std::to_string(listResult->tools.size()));
        for (const auto &tool : listResult->tools) {
            std::string desc = tool.description.has_value() ? tool.description.value() : std::string{};
            MCP_LOG(MCP_LOG_LEVEL_INFO, "  Tool: " + tool.name + " - " + desc);
        }
    } catch (const std::exception &e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("ListTools failed: ") + e.what());
        return -1;
    }

    // Example 3: Call tool
    MCP_LOG(MCP_LOG_LEVEL_INFO, "=== Example CallTool ===");
    try {
        nlohmann::json arguments;
        arguments["user_query"] = "Shenzhen weather";
        auto callFuture = mcpClient->CallTool(EXAMPLE_TOOL_NAME, arguments, 30);
        if (callFuture.wait_for(std::chrono::seconds(REQUEST_TIMEOUT)) != std::future_status::ready) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "CallTool timeout");
            return -1;
        }
        auto callResult = callFuture.get();
        MCP_LOG(MCP_LOG_LEVEL_INFO,
                std::string("CallTool success, isError: ") + std::to_string(static_cast<int>(callResult->isError)) +
                    ", content count: " + std::to_string(callResult->content.size()));
        for (const auto &content : callResult->content) {
            if (std::holds_alternative<Mcp::TextContent>(content)) {
                const auto &text = std::get<Mcp::TextContent>(content);
                MCP_LOG(MCP_LOG_LEVEL_INFO, "  TextContent: " + text.text);
            } else if (std::holds_alternative<Mcp::ImageContent>(content)) {
                const auto &image = std::get<Mcp::ImageContent>(content);
                MCP_LOG(MCP_LOG_LEVEL_INFO, "  ImageContent: mimeType=" + image.mimeType);
            }
        }
    } catch (const std::exception &e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("CallTool failed: ") + e.what());
        return -1;
    }

    MCP_LOG(MCP_LOG_LEVEL_INFO, "=== Example completed ===");
    return 0;
}