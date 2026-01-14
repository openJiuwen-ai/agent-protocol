/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <optional>

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
    config.name = "PromptExampleClient";
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
        MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("Initialize failed: ") + e.what());
        return -1;
    }

    // Example 1: List prompts
    MCP_LOG(MCP_LOG_LEVEL_INFO, "=== Example ListPrompts ===");
    try {
        auto listFuture = mcpClient->ListPrompts();
        if (listFuture.wait_for(std::chrono::seconds(REQUEST_TIMEOUT)) != std::future_status::ready) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "ListPrompts timeout");
            return -1;
        }
        auto promptList = listFuture.get();
        MCP_LOG(MCP_LOG_LEVEL_INFO, "ListPrompts success, prompt count: " + std::to_string(promptList->prompts.size()));
        for (const auto &prompt : promptList->prompts) {
            if (prompt.arguments.has_value()) {
                MCP_LOG(MCP_LOG_LEVEL_INFO, "  Prompt: " + prompt.name + " (arguments: " +
                    std::to_string(prompt.arguments.value().size()) + ")");
            } else {
                MCP_LOG(MCP_LOG_LEVEL_INFO, "  Prompt: " + prompt.name + " (no arguments)");
            }
        }

        // Example 2: Get prompt (if available)
        if (!promptList->prompts.empty()) {
            MCP_LOG(MCP_LOG_LEVEL_INFO, "=== Example GetPrompt ===");
            const auto &firstPrompt = promptList->prompts.front();
            std::optional<Mcp::JsonValue> promptArgs = std::nullopt;
            if (firstPrompt.arguments.has_value() && !firstPrompt.arguments.value().empty()) {
                nlohmann::json argsJson = nlohmann::json::object();
                argsJson["tone"] = "neutral";
                promptArgs = argsJson;
            }

            auto getFuture = mcpClient->GetPrompt(firstPrompt.name, promptArgs);
            if (getFuture.wait_for(std::chrono::seconds(REQUEST_TIMEOUT)) != std::future_status::ready) {
                MCP_LOG(MCP_LOG_LEVEL_ERROR, "GetPrompt timeout");
                return -1;
            }
            auto promptDetail = getFuture.get();
            MCP_LOG(MCP_LOG_LEVEL_INFO, "GetPrompt success, prompt: " + firstPrompt.name + ", message count: " +
                std::to_string(promptDetail->messages.size()));
        }
    } catch (const std::exception &e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Prompt example failed: " + std::string(e.what()));
        return -1;
    }

    MCP_LOG(MCP_LOG_LEVEL_INFO, "=== Example completed ===");
    return 0;
}