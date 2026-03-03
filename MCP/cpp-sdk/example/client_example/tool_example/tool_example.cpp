/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <variant>

#include <nlohmann/json.hpp>

#include "mcp_client.h"
#include "mcp_log.h"
#include "mcp_type.h"

constexpr int REQUEST_TIMEOUT = 300;
constexpr char EXAMPLE_ENDPOINT[] = "http://localhost:8000/mcp";
constexpr char EXAMPLE_TOKEN[] = "your-token";
constexpr char EXAMPLE_TOOL_NAME[] = "echo";

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
    config.name = "ToolExampleClient";
    config.version = "1.0.0";
    Mcp::StreamableHttpClientConfig httpConfig;
    httpConfig.endpoint = endpoint;
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
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Initialize failed: %s", e.what());
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
        MCP_LOG(MCP_LOG_LEVEL_INFO, "ListTools success, tool count: %zu", listResult->tools.size());
        for (const auto &tool : listResult->tools) {
            MCP_LOG(MCP_LOG_LEVEL_INFO, "  Tool: %s - %s", tool.name.c_str(), tool.description.c_str());
        }
    } catch (const std::exception &e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "ListTools failed: %s", e.what());
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
        MCP_LOG(MCP_LOG_LEVEL_INFO, "CallTool success, isError: %d, content count: %zu", callResult->isError,
                callResult->content.size());
        for (const auto &content : callResult->content) {
            if (std::holds_alternative<Mcp::TextContent>(content)) {
                const auto &text = std::get<Mcp::TextContent>(content);
                MCP_LOG(MCP_LOG_LEVEL_INFO, "  TextContent: %s", text.text.c_str());
            } else if (std::holds_alternative<Mcp::ImageContent>(content)) {
                const auto &image = std::get<Mcp::ImageContent>(content);
                MCP_LOG(MCP_LOG_LEVEL_INFO, "  ImageContent: mimeType=%s", image.mimeType.c_str());
            }
        }
    } catch (const std::exception &e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "CallTool failed: %s", e.what());
        return -1;
    }

    // Example 3.5: Call tool with progress callback
    MCP_LOG(MCP_LOG_LEVEL_INFO, "=== Example CallTool with Progress Callback ===");
    try {
        nlohmann::json progressArguments;
        progressArguments["task_name"] = "Data Processing";
        progressArguments["steps"] = 5; // 5 steps
        
        // Define progress callback function
        std::function<void(double, std::optional<double>, const std::optional<std::string>&)> progressCallback =
            [](double progress, std::optional<double> total, const std::optional<std::string>& message) {
            // Always log when progress callback is called
            MCP_LOG(MCP_LOG_LEVEL_INFO, "=== PROGRESS CALLBACK CALLED ===");
            MCP_LOG(MCP_LOG_LEVEL_INFO, "Progress: %.2f (%.1f%%)", progress, progress * 100);
            
            if (message) {
                MCP_LOG(MCP_LOG_LEVEL_INFO, "Message: %s", message->c_str());
            }
        };
        
        auto progressCallFuture = mcpClient->CallTool("progress_tool", progressArguments, 60, progressCallback);
        if (progressCallFuture.wait_for(std::chrono::seconds(REQUEST_TIMEOUT)) != std::future_status::ready) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "CallTool with progress callback timeout");
            return -1;
        }
        auto progressCallResult = progressCallFuture.get();
        MCP_LOG(MCP_LOG_LEVEL_INFO, "CallTool with progress callback success, isError: %d, content count: %zu",
                progressCallResult->isError, progressCallResult->content.size());
        for (const auto &content : progressCallResult->content) {
            if (std::holds_alternative<Mcp::TextContent>(content)) {
                const auto &text = std::get<Mcp::TextContent>(content);
                MCP_LOG(MCP_LOG_LEVEL_INFO, "  TextContent: %s", text.text.c_str());
            }
        }
    } catch (const std::exception &e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "CallTool with progress callback failed: %s", e.what());
        return -1;
    }

    // Example 3.1: Call async echo tool
    MCP_LOG(MCP_LOG_LEVEL_INFO, "=== Example CallTool (Async Echo) ===");
    try {
        nlohmann::json asyncArguments;
        asyncArguments["user_query"] = "Hello from async echo";
        auto asyncCallFuture = mcpClient->CallTool("async_echo", asyncArguments, 30);
        if (asyncCallFuture.wait_for(std::chrono::seconds(REQUEST_TIMEOUT)) != std::future_status::ready) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "Async CallTool timeout");
            return -1;
        }
        auto asyncCallResult = asyncCallFuture.get();
        MCP_LOG(MCP_LOG_LEVEL_INFO, "Async CallTool success, isError: %d, content count: %zu",
                asyncCallResult->isError, asyncCallResult->content.size());
        for (const auto &content : asyncCallResult->content) {
            if (std::holds_alternative<Mcp::TextContent>(content)) {
                const auto &text = std::get<Mcp::TextContent>(content);
                MCP_LOG(MCP_LOG_LEVEL_INFO, "  Async TextContent: %s", text.text.c_str());
            }
        }
    } catch (const std::exception &e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Async CallTool failed: %s", e.what());
        return -1;
    }

    // Example 4: Complete - Get completions for a prompt argument
    MCP_LOG(MCP_LOG_LEVEL_INFO, "=== Example Complete (Prompt Completions) ===");
    try {
        // Create a PromptReference variant
        Mcp::PromptReference promptRef;
        promptRef.name = "code_generator";
        Mcp::CompleteReference ref = promptRef;

        Mcp::CompletionArgument arg;
        arg.name = "language";
        arg.value = "py";  // User has typed "py"

        Mcp::CompletionContext ctx;
        std::unordered_map<std::string, std::string> argsMap;
        argsMap["framework"] = "django";
        ctx.arguments = argsMap;

        auto completeFuture = mcpClient->Complete(ref, arg, ctx);
        if (completeFuture.wait_for(std::chrono::seconds(REQUEST_TIMEOUT)) != std::future_status::ready) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "Complete timeout");
            return -1;
        }
        auto completeResult = completeFuture.get();
        MCP_LOG(MCP_LOG_LEVEL_INFO, "Complete success, completion count: %zu",
                completeResult->completion.values.size());
        for (const auto &completion : completeResult->completion.values) {
            MCP_LOG(MCP_LOG_LEVEL_INFO, "  Suggestion: %s", completion.c_str());
        }
    } catch (const std::exception &e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Complete failed: %s", e.what());
        return -1;
    }

    // Example 5: Complete - Get completions for a resource template argument
    MCP_LOG(MCP_LOG_LEVEL_INFO, "=== Example Complete (Resource Template Completions) ===");
    try {
        // Create a ResourceTemplateReference variant
        Mcp::ResourceTemplateReference resourceRef;
        resourceRef.uri = "file:///path/to/template";
        Mcp::CompleteReference ref = resourceRef;

        Mcp::CompletionArgument arg;
        arg.name = "format";
        arg.value = "j";  // User has typed "j"

        Mcp::CompletionContext ctx;
        std::unordered_map<std::string, std::string> argsMap;
        argsMap["extension"] = "json";
        ctx.arguments = argsMap;

        auto completeFuture = mcpClient->Complete(ref, arg, ctx);
        if (completeFuture.wait_for(std::chrono::seconds(REQUEST_TIMEOUT)) != std::future_status::ready) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "Complete timeout");
            return -1;
        }
        auto completeResult = completeFuture.get();
        MCP_LOG(MCP_LOG_LEVEL_INFO, "Complete success, completion count: %zu",
                completeResult->completion.values.size());
        for (const auto &completion : completeResult->completion.values) {
            MCP_LOG(MCP_LOG_LEVEL_INFO, "  Suggestion: %s", completion.c_str());
        }
    } catch (const std::exception &e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Complete failed: %s", e.what());
        return -1;
    }

    MCP_LOG(MCP_LOG_LEVEL_INFO, "=== Example completed ===");
    return 0;
}