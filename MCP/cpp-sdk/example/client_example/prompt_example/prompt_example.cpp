/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "mcp_client.h"
#include "mcp_log.h"
#include "mcp_type.h"

constexpr int REQUEST_TIMEOUT = 300;
constexpr char EXAMPLE_ENDPOINT[] = "http://localhost:8000/mcp";
constexpr char EXAMPLE_TOKEN[] = "your-token";

int main(int argc, char** argv)
{
    SetLogLevel(MCP_LOG_LEVEL_INFO);

    std::string endpoint = EXAMPLE_ENDPOINT;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? std::string(argv[i]) : std::string{};
        if (arg.rfind("--port=", 0) == 0) {
            const std::string value = arg.substr(std::string("--port=").size());
            int port = std::stoi(value);
            endpoint = std::string("http://localhost:") + std::to_string(port) + "/mcp";
        }
    }

    // Setup client configuration
    Mcp::ClientConfig config;
    config.name = "PromptExampleClient";
    config.version = "1.0.0";
    Mcp::StreamableHttpClientConfig httpConfig;
    httpConfig.endpoint = endpoint;
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

    // Example 1: List prompts
    MCP_LOG(MCP_LOG_LEVEL_INFO, "=== Example ListPrompts ===");
    try {
        auto listFuture = mcpClient->ListPrompts();
        if (listFuture.wait_for(std::chrono::seconds(REQUEST_TIMEOUT)) != std::future_status::ready) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "ListPrompts timeout");
            return -1;
        }
        auto promptList = listFuture.get();
        MCP_LOG(MCP_LOG_LEVEL_INFO, "ListPrompts success, prompt count: %zu", promptList->prompts.size());
        for (const auto &prompt : promptList->prompts) {
            if (prompt.arguments.has_value()) {
                MCP_LOG(MCP_LOG_LEVEL_INFO, "  Prompt: %s (arguments: %zu)", prompt.name.c_str(), prompt.arguments.value().size());
            } else {
                MCP_LOG(MCP_LOG_LEVEL_INFO, "  Prompt: %s (no arguments)", prompt.name.c_str());
            }
        }

        // Example 2: Get all prompts
        if (!promptList->prompts.empty()) {
            MCP_LOG(MCP_LOG_LEVEL_INFO, "=== Example GetPrompt (All Prompts) ===");

            for (const auto &prompt : promptList->prompts) {
                MCP_LOG(MCP_LOG_LEVEL_INFO, "Testing prompt: %s", prompt.name.c_str());

                std::optional<Mcp::JsonValue> promptArgs = std::nullopt;
                if (prompt.arguments.has_value() && !prompt.arguments.value().empty()) {
                    nlohmann::json argsJson = nlohmann::json::object();
                    argsJson["name"] = "friend";
                    argsJson["language"] = "English";
                    promptArgs = argsJson;
                }

                auto getFuture = mcpClient->GetPrompt(prompt.name, promptArgs);
                if (getFuture.wait_for(std::chrono::seconds(REQUEST_TIMEOUT)) != std::future_status::ready) {
                    MCP_LOG(MCP_LOG_LEVEL_ERROR, "GetPrompt timeout for prompt: %s", prompt.name.c_str());
                    continue;
                }
                auto promptDetail = getFuture.get();
                MCP_LOG(MCP_LOG_LEVEL_INFO, "GetPrompt success, prompt: %s, message count: %zu",
                        prompt.name.c_str(), promptDetail->messages.size());

                // Print the actual message content
                for (const auto& message : promptDetail->messages) {
                    if (std::holds_alternative<Mcp::TextContent>(message.content)) {
                        const auto& textContent = std::get<Mcp::TextContent>(message.content);
                        MCP_LOG(MCP_LOG_LEVEL_INFO, "  Message: %s", textContent.text.c_str());
                    }
                }
            }
        }
    } catch (const std::exception &e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Prompt example failed: %s", e.what());
        return -1;
    }

    MCP_LOG(MCP_LOG_LEVEL_INFO, "=== Example completed ===");
    return 0;
}
