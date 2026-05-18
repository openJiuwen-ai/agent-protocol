/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <variant>

#include <nlohmann/json.hpp>

#include "mcp_client.h"
#include "mcp_log.h"
#include "mcp_type.h"

// Constants
constexpr int REQUEST_TIMEOUT_SEC = 30;
constexpr char DEFAULT_ENDPOINT[] = "http://localhost:8000/mcp";
constexpr char AUTH_TOKEN[] = "your-token";
constexpr int DEFAULT_MAX_TOKENS = 100;
constexpr char SAMPLE_PROMPT[] = "Tell me a joke about programming";
constexpr char SAMPLE_TOOL_NAME[] = "sampling_echo";
constexpr char MODEL_NAME[] = "demo-client-model";

// Print header information
static void PrintHeader()
{
    MCP_LOG(MCP_LOG_LEVEL_INFO, "=== Sampling Client Example ===");
    MCP_LOG(MCP_LOG_LEVEL_INFO, "This example demonstrates server-to-client sampling.");
    MCP_LOG(MCP_LOG_LEVEL_INFO, "The server will send sampling requests to this client.");
    MCP_LOG(MCP_LOG_LEVEL_INFO, "");
}

// Log sampling message content
static void LogSamplingMessageContent(const Mcp::SamplingMessageContentBlock& block)
{
    if (std::holds_alternative<Mcp::TextContent>(block)) {
        const auto& text = std::get<Mcp::TextContent>(block);
        MCP_LOG(MCP_LOG_LEVEL_INFO, std::string("    Text: ") + text.text);
    } else if (std::holds_alternative<Mcp::ImageContent>(block)) {
        const auto& image = std::get<Mcp::ImageContent>(block);
        MCP_LOG(MCP_LOG_LEVEL_INFO, std::string("    Image: ") + image.mimeType);
    } else if (std::holds_alternative<Mcp::AudioContent>(block)) {
        const auto& audio = std::get<Mcp::AudioContent>(block);
        MCP_LOG(MCP_LOG_LEVEL_INFO, std::string("    Audio: ") + audio.mimeType);
    }
}

// Log received sampling message
static void LogSamplingMessage(const Mcp::SamplingMessage& msg)
{
    std::string roleStr = (msg.role == Mcp::RoleType::USER) ? "USER" : "ASSISTANT";
    MCP_LOG(MCP_LOG_LEVEL_INFO, std::string("  Message [") + roleStr + "]:");

    std::visit([](auto&& content) {
        using T = std::decay_t<decltype(content)>;
        if constexpr (std::is_same_v<T, Mcp::SamplingMessageContentBlock>) {
            std::visit([](auto&& block) {
                LogSamplingMessageContent(block);
            },
                content);
        } else if constexpr (std::is_same_v<T, std::vector<Mcp::SamplingMessageContentBlock>>) {
            MCP_LOG(MCP_LOG_LEVEL_INFO, std::string("    Content blocks count: ") + std::to_string(content.size()));
        }
    },
        msg.content);
}

// Handle sampling request from server
static Mcp::CreateMessageResult HandleSamplingRequest(
    const Mcp::CreateMessageParams& params)
{
    MCP_LOG(MCP_LOG_LEVEL_INFO, "Received sampling/createMessage request from server");
    MCP_LOG(MCP_LOG_LEVEL_INFO, std::string("  maxTokens: ") + std::to_string(params.maxTokens));

    for (const auto& msg : params.messages) {
        LogSamplingMessage(msg);
    }

    // Prepare sampling response
    Mcp::CreateMessageResult result;
    result.model = MODEL_NAME;
    result.role = Mcp::RoleType::ASSISTANT;
    result.stopReason = "stop";

    // Create response text content
    Mcp::TextContent responseText;
    responseText.text = "This is a simulated LLM response from sampling client.";
    result.content = Mcp::SamplingMessageContentBlock{responseText};

    MCP_LOG(MCP_LOG_LEVEL_INFO, "Returning sampling response to server");
    return result;
}

// Parse command line arguments
static std::string ParseEndpointFromArgs(int argc, char** argv)
{
    std::string endpoint = DEFAULT_ENDPOINT;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? std::string(argv[i]) : std::string{};
        if (arg == "--help" || arg == "-h") {
            MCP_LOG(MCP_LOG_LEVEL_INFO,
                std::string("Usage: ") + (argv[0] ? argv[0] : "SamplingExample") + " [--port=<1-65535>]");
            exit(0);
        }
        if (arg.rfind("--port=", 0) == 0) {
            const std::string value = arg.substr(std::string("--port=").size());
            int port = std::stoi(value);
            endpoint = std::string("http://localhost:") + std::to_string(port) + "/mcp";
        }
    }
    return endpoint;
}

// Create and configure MCP client
static std::shared_ptr<Mcp::McpClient> CreateMcpClient(const std::string& endpoint)
{
    Mcp::ClientConfig config;
    config.name = "SamplingExampleClient";
    config.version = "1.0.0";

    Mcp::StreamableHttpClientConfig httpConfig;
    httpConfig.endpoint = endpoint;
    httpConfig.tlsConfig.enabled = false;

    auto authProvider = std::make_shared<Mcp::BearerTokenProvider>(AUTH_TOKEN);
    return Mcp::McpClientFactory::CreateStreamableHttpClient(
        config, httpConfig, std::move(authProvider));
}

// Register sampling callback
static void RegisterSamplingCallback(std::shared_ptr<Mcp::McpClient> client)
{
    MCP_LOG(MCP_LOG_LEVEL_INFO, "=== Registering sampling callback ===");
    client->SetSamplingCreateMessageCallback(HandleSamplingRequest, Mcp::SamplingCapability{});
}

// Initialize MCP client
static bool InitializeClient(std::shared_ptr<Mcp::McpClient> client)
{
    MCP_LOG(MCP_LOG_LEVEL_INFO, "=== Initialize ===");

    try {
        auto initFuture = client->Initialize();
        if (initFuture.wait_for(std::chrono::seconds(REQUEST_TIMEOUT_SEC))
            != std::future_status::ready) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "Initialize timeout");
            return false;
        }

        auto initResult = initFuture.get();
        MCP_LOG(MCP_LOG_LEVEL_INFO, "Initialize success");
        MCP_LOG(MCP_LOG_LEVEL_INFO, std::string("  Server name: ") + initResult->serverInfo.name);
        MCP_LOG(MCP_LOG_LEVEL_INFO, std::string("  Server version: ") + initResult->serverInfo.version);
        return true;
    } catch (const std::exception& e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("Initialize failed: ") + e.what());
        return false;
    }
}

// List available tools
static bool ListTools(std::shared_ptr<Mcp::McpClient> client)
{
    MCP_LOG(MCP_LOG_LEVEL_INFO, "=== ListTools ===");

    try {
        auto listFuture = client->ListTools();
        if (listFuture.wait_for(std::chrono::seconds(REQUEST_TIMEOUT_SEC))
            != std::future_status::ready) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "ListTools timeout");
            return false;
        }

        auto listResult = listFuture.get();
        MCP_LOG(MCP_LOG_LEVEL_INFO,
            std::string("ListTools success, tool count: ") + std::to_string(listResult->tools.size()));

        for (const auto& tool : listResult->tools) {
            MCP_LOG(MCP_LOG_LEVEL_INFO, std::string("  Tool: ") + tool.name);
            const std::string desc =
                tool.description.has_value() ? tool.description.value() : std::string("");
            MCP_LOG(MCP_LOG_LEVEL_INFO, std::string("    Description: ") + desc);
        }
        return true;
    } catch (const std::exception& e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("ListTools failed: ") + e.what());
        return false;
    }
}

// Log tool call result content
static void LogToolResultContent(const Mcp::ContentType& content)
{
    if (std::holds_alternative<Mcp::TextContent>(content)) {
        const auto& text = std::get<Mcp::TextContent>(content);
            MCP_LOG(MCP_LOG_LEVEL_INFO, std::string("  Response: ") + text.text);
    } else if (std::holds_alternative<Mcp::ImageContent>(content)) {
        const auto& image = std::get<Mcp::ImageContent>(content);
            MCP_LOG(MCP_LOG_LEVEL_INFO, std::string("  Image: mimeType=") + image.mimeType);
    }
}

// Call sampling echo tool
static bool CallSamplingEchoTool(std::shared_ptr<Mcp::McpClient> client)
{
    MCP_LOG(MCP_LOG_LEVEL_INFO, "=== CallTool (sampling_echo) ===");
    MCP_LOG(MCP_LOG_LEVEL_INFO, "This will trigger server-to-client sampling.");
    MCP_LOG(MCP_LOG_LEVEL_INFO, "Watch for sampling callback invocations above.");
    MCP_LOG(MCP_LOG_LEVEL_INFO, "");

    try {
        nlohmann::json arguments;
        arguments["prompt"] = SAMPLE_PROMPT;
        MCP_LOG(MCP_LOG_LEVEL_INFO,
            std::string("Calling sampling_echo with prompt: '") + arguments["prompt"].get<std::string>() + "'");

        auto callFuture = client->CallTool(SAMPLE_TOOL_NAME, arguments.dump(), DEFAULT_MAX_TOKENS);
        if (callFuture.wait_for(std::chrono::seconds(REQUEST_TIMEOUT_SEC))
            != std::future_status::ready) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "CallTool timeout");
            return false;
        }

        auto callResult = callFuture.get();
        MCP_LOG(MCP_LOG_LEVEL_INFO, "");
        MCP_LOG(MCP_LOG_LEVEL_INFO,
            std::string("CallTool complete, isError: ") + std::to_string(callResult->isError) + ", content count: " +
                std::to_string(callResult->content.size()));

        for (const auto& content : callResult->content) {
            LogToolResultContent(content);
        }
        return true;
    } catch (const std::exception& e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("CallTool failed: ") + e.what());
        return false;
    }
}

// Print success message
static void PrintSuccessMessage()
{
    MCP_LOG(MCP_LOG_LEVEL_INFO, "");
    MCP_LOG(MCP_LOG_LEVEL_INFO, "=== Example completed successfully ===");
    MCP_LOG(MCP_LOG_LEVEL_INFO, "The sampling callback was invoked by server,");
    MCP_LOG(MCP_LOG_LEVEL_INFO, "and this client provided a simulated LLM response.");
}

int main(int argc, char** argv)
{
    SetLogLevel(MCP_LOG_LEVEL_INFO);
    PrintHeader();

    std::string endpoint = ParseEndpointFromArgs(argc, argv);
    auto mcpClient = CreateMcpClient(endpoint);

    // IMPORTANT: Register sampling callback BEFORE Initialize()
    RegisterSamplingCallback(mcpClient);

    if (!InitializeClient(mcpClient)) {
        return -1;
    }

    if (!ListTools(mcpClient)) {
        return -1;
    }

    if (!CallSamplingEchoTool(mcpClient)) {
        return -1;
    }

    PrintSuccessMessage();
    return 0;
}
