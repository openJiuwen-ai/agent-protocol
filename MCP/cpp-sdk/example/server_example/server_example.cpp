/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#include <csignal>

#include <chrono>
#include <cstdarg>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>
#include "mcp_auth.h"
#include "mcp_log.h"
#include "mcp_server.h"
#include "mcp_type.h"
#include "simple_token_verifier.h"

static FILE *g_logFile = nullptr;
volatile sig_atomic_t stopFlag = 0;

const char* const SERVER_NAME = "TestMCPServer";
const char* const SERVER_VERSION = "1.0.0";
const int IO_THREADS = 2;
const int WORKER_THREADS = 2;
const char* const ENDPOINT = "http://127.0.0.1:8000/mcp";
const char* const AUTH_ENDPOINT = "http://127.0.0.1:8001/mcp";
const char* const LOG_FILE_NAME = "server_example.log";
const char* const ECHO_TOOL_NAME = "echo";
const char* const ECHO_TOOL_TITLE = "Echo Tool";
const char* const ECHO_TOOL_DESCRIPTION = "Echoes back the input message";
const char* const PROMPT_NAME = "example_prompt";
const char* const PROMPT_DESCRIPTION = "Generate a personalized greeting message";
const char* const RESOURCE_URI = "http://example.com/resource";
const char* const RESOURCE_NAME = "Test Resource";
const char* const RESOURCE_DESCRIPTION = "A test resource for demonstration";
const char* const RESOURCE_MIME_TYPE = "text/plain";

// Progress tool constants
constexpr int PROGRESS_TOOL_SIMULATION_DELAY_MS = 500; // Delay between progress updates in milliseconds

// Async processing delays
constexpr int ASYNC_PROMPT_PROCESSING_DELAY_MS = 300; // Delay for async prompt processing (background thread)
constexpr int ASYNC_RESOURCE_PROCESSING_DELAY_MS = 300; // Delay for async resource processing
const char* const RESOURCE_TEMPLATE_URI = "http://example.com/resourceTemplate/{id}";
const char* const RESOURCE_TEMPLATE_NAME = "Test Resource Template";
const char* const RESOURCE_TEMPLATE_DESCRIPTION = "A test resource template for demonstration";
const char* const RESOURCE_TEMPLATE_MIME_TYPE = "text/plain";
const int HEARTBEAT_INTERVAL_SECONDS = 5;
const int LOG_INTERVAL_COUNT = 6;
const int SECONDS_PER_LOG = 30;
const int EXAMPLE_BULK_COUNT = 120;

const char* const VALID_TOKEN = "valid-token-12345"; // This token has 'read write' scopes
const char* const REQUIRED_SCOPES = "read write"; // Required scopes for accessing tools

void signalHandler(int signal)
{
    if (signal == SIGINT || signal == SIGTERM) {
        MCP_LOG(MCP_LOG_LEVEL_INFO, "Received shutdown signal, stopping server...");
        stopFlag = 1;
    }
}

// Use C++ style callback with std::string instead of C-style variadic function
void FileLogCallback(MCP_LOG_LEVEL /* logLevel */, std::string message)
{
    if (!g_logFile) {
        return;
    }

    fprintf(g_logFile, "%s\n", message.c_str());
    fflush(g_logFile);
}

// ---- Sampling helper functions ----

std::string ExtractTextContentFromResult(const Mcp::CreateMessageResult& result)
{
    auto toText = [](const Mcp::SamplingMessageContentBlock& block) -> std::string {
        if (std::holds_alternative<Mcp::TextContent>(block)) {
            return std::get<Mcp::TextContent>(block).text;
        }
        return "";
    };

    if (std::holds_alternative<Mcp::SamplingMessageContentBlock>(result.content)) {
        return toText(std::get<Mcp::SamplingMessageContentBlock>(result.content));
    }

    const auto& blocks = std::get<std::vector<Mcp::SamplingMessageContentBlock>>(result.content);
    for (const auto& block : blocks) {
        std::string text = toText(block);
        if (!text.empty()) {
            return text;
        }
    }
    return "<no text content>";
}

constexpr int SAMPLING_MAX_TOKENS = 100;

Mcp::CreateMessageParams BuildSamplingParams(const std::string& prompt, int maxTokens)
{
    Mcp::CreateMessageParams params;
    params.maxTokens = maxTokens;

    Mcp::SamplingMessage userMsg;
    userMsg.role = Mcp::RoleType::USER;
    Mcp::TextContent userText;
    userText.text = prompt;
    userMsg.content = Mcp::SamplingMessageContentBlock{userText};
    params.messages.push_back(std::move(userMsg));
    return params;
}

Mcp::CallToolResult MakeSamplingErrorResult(const std::string& message)
{
    Mcp::CallToolResult result;
    result.isError = true;
    Mcp::TextContent errorText;
    errorText.type = "text";
    errorText.text = message;
    result.content.push_back(errorText);
    return result;
}

std::string ParseSamplingPrompt(const std::string& arguments)
{
    std::string prompt = "Hello";
    try {
        auto j = nlohmann::json::parse(arguments);
        if (j.contains("prompt") && j["prompt"].is_string()) {
            prompt = j["prompt"].get<std::string>();
        }
    } catch (const std::exception& e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("Error parsing sampling echo arguments: ") + e.what());
    }
    return prompt;
}

Mcp::CallToolResult RunSamplingRequest(const Mcp::ServerContext& ctx, const std::string& prompt)
{
    if (!ctx.session) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "No session available for sampling");
        return MakeSamplingErrorResult("Error: No session available for sampling");
    }

    try {
        const auto params = BuildSamplingParams(prompt, SAMPLING_MAX_TOKENS);
        MCP_LOG(MCP_LOG_LEVEL_INFO, "Sending sampling/createMessage request to client...");

        auto samplingFuture = ctx.session->SamplingCreateMessage(params);
        auto samplingResult = samplingFuture.get();
        if (!samplingResult) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "Sampling returned null result");
            return MakeSamplingErrorResult("Error: Sampling returned null result");
        }

        const std::string sampledText = ExtractTextContentFromResult(*samplingResult);
        MCP_LOG(MCP_LOG_LEVEL_INFO, std::string("Sampling response received: ") + sampledText);

        Mcp::CallToolResult result;
        result.isError = false;
        Mcp::TextContent responseText;
        responseText.type = "text";
        responseText.text = "Sampling response: " + sampledText;
        result.content.push_back(responseText);
        return result;
    } catch (const std::exception& e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("Sampling failed: ") + e.what());
        return MakeSamplingErrorResult(std::string("Sampling error: ") + e.what());
    }
}

// ParseSamplingPrompt and RunSamplingRequest are defined above (string-based versions)

void SendSamplingResponse(const Mcp::ServerContext& ctx, const Mcp::CallToolResult& result)
{
    if (ctx.responseCallback) {
        MCP_LOG(MCP_LOG_LEVEL_INFO, "Sending sampling echo response");
        ctx.responseCallback(result);
        return;
    }
    MCP_LOG(MCP_LOG_LEVEL_ERROR, "No response callback available for sampling echo");
}

int main(int argc, char** argv)
{
    bool enableAuth = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--auth") {
            enableAuth = true;
        }
    }

    std::cout << "=== MCP Server Example ===" << std::endl;
    if (enableAuth) {
        std::cout << "Mode: Authentication and Authorization (Bearer token + Scopes)\n";
    }

    bool isJsonResponseEnabled = true;
    bool stateless = false;
    std::string endpoint = enableAuth ? AUTH_ENDPOINT : ENDPOINT;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? std::string(argv[i]) : std::string{};
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << (argv[0] ? argv[0] : "ServerExample")
                      << " [--auth] [--port=<1-65535>] [--stateless] [--isJsonResponseDisable]" << std::endl;
            return 0;
        }
        if (arg == "--stateless") {
            stateless = true;
        } else if (arg == "--isJsonResponseDisable") {
            isJsonResponseEnabled = false;
        } else if (arg.rfind("--port=", 0) == 0) {
            const std::string value = arg.substr(std::string("--port=").size());
            int port = std::stoi(value);
            endpoint = std::string("http://127.0.0.1:") + std::to_string(port) + "/mcp";
        }
    }

    if (stateless && !isJsonResponseEnabled) {
        std::cerr << "Error: --stateless requires JSON responses; do not use --isJsonResponseDisable" << std::endl;
        return -1;
    }

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    std::string logFileName = LOG_FILE_NAME;
    g_logFile = fopen(logFileName.c_str(), "a");
    if (!g_logFile) {
        printf("Failed to open log file: %s\n", logFileName.c_str());
        return -1;
    }
    SetLogLevel(MCP_LOG_LEVEL_DEBUG);
    SetLogCallback(FileLogCallback);

    MCP_LOG(MCP_LOG_LEVEL_INFO, "Starting MCP Server test...");

    auto promptHandler = [](const Mcp::ServerContext& ctx [[maybe_unused]], const std::string &promptName,
        const std::optional<Mcp::JsonValue> &arguments) -> Mcp::GetPromptResult {
        Mcp::GetPromptResult result;
        result.description = PROMPT_NAME;

        std::string who = "friend";
        std::string lang = "English";

        if (arguments.has_value()) {
            const auto &j = arguments.value();
            if (j.contains("name") && j["name"].is_string()) {
                who = j["name"].get<std::string>();
            }
            if (j.contains("language") && j["language"].is_string()) {
                lang = j["language"].get<std::string>();
            }
        }
        Mcp::TextContent tc;
        tc.type = "text";
        tc.text = "Hello, " + who + "! (language=" + lang + ")";
        Mcp::PromptMessage msg;
        msg.role = Mcp::RoleType::ASSISTANT;
        msg.content = tc;
        result.messages.push_back(msg);
        return result;
    };

    try {
        Mcp::ServerConfig config;
        config.name = SERVER_NAME;
        config.version = SERVER_VERSION;
        config.workerThreads = WORKER_THREADS;
        Mcp::StreamableHttpServerConfig streamableHttpConfig;
        streamableHttpConfig.ioThreads = IO_THREADS;
        streamableHttpConfig.tlsConfig.enabled = false;
        streamableHttpConfig.endpoint = endpoint;
        streamableHttpConfig.isJsonResponseEnabled = isJsonResponseEnabled;
        streamableHttpConfig.stateless = stateless;

        if (enableAuth) {
            // Setup authentication and authorization
            std::unordered_map<std::string, std::string> tokenScopes;
            tokenScopes[VALID_TOKEN] = "read write";      // Full access

            auto tokenVerifier = std::make_shared<Mcp::SimpleTokenVerifier>(tokenScopes);
            auto authenticator = std::make_shared<Mcp::BearerTokenAuthenticator>(tokenVerifier);
            auto authorizer = std::make_shared<Mcp::ScopeBasedAuthorizer>(REQUIRED_SCOPES);

            streamableHttpConfig.authenticator = authenticator;
            streamableHttpConfig.authorizer = authorizer;

            MCP_LOG(MCP_LOG_LEVEL_INFO, "Authentication and authorization configured:");
            MCP_LOG(MCP_LOG_LEVEL_INFO, std::string("  - Required scopes: ") + REQUIRED_SCOPES);
            MCP_LOG(MCP_LOG_LEVEL_INFO, std::string("  - Valid token: ") + VALID_TOKEN + " (scopes: read write)");
        }

        auto server = Mcp::McpServerFactory::CreateStreamableHttpServer(config, streamableHttpConfig);
        if (server == nullptr) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "Failed to create MCP server instance");
            return -1;
        }
        MCP_LOG(MCP_LOG_LEVEL_INFO, "MCP server instance created successfully");

        // add tool
        auto echoFunc = [](const Mcp::ServerContext & /* ctx */, const std::string &name,
                           const std::string &arguments) -> Mcp::CallToolResult {
            Mcp::CallToolResult result;
            result.isError = false;
            try {
                std::string userQuery = "";
                // Parse string arguments to JSON for internal processing
                nlohmann::json argumentsJson = nlohmann::json::parse(arguments);
                if (argumentsJson.contains("user_query") && argumentsJson.at("user_query").is_string()) {
                    userQuery = argumentsJson.at("user_query").get<std::string>();
                }
                
                // Set content
                Mcp::TextContent textContent;
                textContent.text = "Echo: " + userQuery;
                result.content.push_back(textContent);

                // Provide structuredContent when outputSchema is present
                nlohmann::json structuredJson;
                structuredJson["result"] = userQuery;
                result.structuredContent = structuredJson.dump();
            } catch (const std::exception &e) {
                result.isError = true;
                Mcp::TextContent errorContent;
                errorContent.text = std::string("Error: ") + e.what();
                result.content.push_back(errorContent);
            }
            return result;
        };
        std::string echoInputSchema = R"({"type": "object", "properties": {"user_query": {"type": "string",
            "description": "The user query."}}, "required": ["user_query"]})";
        std::string echoOutputSchema = R"({"type": "object", "properties": {"result": {"type": "string", "description":
            "The echoed message"}}})";
        std::string echoTitle = ECHO_TOOL_TITLE;
        std::string echoDescription = ECHO_TOOL_DESCRIPTION;
        try {
            Mcp::AddToolOptionalParams toolParams;
            toolParams.title = std::cref(echoTitle);
            toolParams.description = std::cref(echoDescription);
            toolParams.inputSchema = std::cref(echoInputSchema);
            toolParams.outputSchema = std::cref(echoOutputSchema);
            server->AddTool(ECHO_TOOL_NAME, echoFunc, toolParams);
            MCP_LOG(MCP_LOG_LEVEL_INFO, std::string("add tool success: ") + ECHO_TOOL_NAME);
        } catch (const std::exception &e) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("add tool failed: ") + e.what());
        } catch (...) {
            MCP_LOG(MCP_LOG_LEVEL_INFO, "add tool failed as expected");
        }

        // minimal placeholders: register more tools to create multiple pages
        try {
            for (int i = 0; i < EXAMPLE_BULK_COUNT; ++i) {
                std::string toolName = std::string("echo_") + std::to_string(i);
                Mcp::AddToolOptionalParams tp;
                std::string minimalInputSchema = R"({"type": "object", "properties": {},
                    "additionalProperties": true})";
                tp.inputSchema = std::cref(minimalInputSchema);
                server->AddTool(toolName, echoFunc, tp);
            }
            MCP_LOG(MCP_LOG_LEVEL_INFO,
                std::string("bulk add tools completed: ") + std::to_string(EXAMPLE_BULK_COUNT));
        } catch (const std::exception &e) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("bulk add tools failed: ") + e.what());
        }

        // Async echo tool example
        try {
            auto asyncEchoFunc = [](const Mcp::ServerContext &ctx, const std::string &name,
                                     const std::string &arguments) -> void {
                (void)name;
                std::string userQuery;
                try {
                    auto j = nlohmann::json::parse(arguments);
                    if (j.contains("user_query") && j["user_query"].is_string()) {
                        userQuery = j["user_query"].get<std::string>();
                    }
                } catch (...) {
                    // Malformed or non-JSON tool arguments: ignore and use empty user_query for async echo.
                }

                // Simulate async work in a background thread
                std::thread([ctx, userQuery]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));

                    Mcp::CallToolResult result;
                    result.isError = false;
                    Mcp::TextContent textContent;
                    textContent.type = "text";
                    textContent.text = "Async Echo: " + userQuery;
                    result.content.push_back(textContent);

                    if (ctx.responseCallback) {
                        ctx.responseCallback(result);
                    }
                }).detach();
            };
            std::string asyncInputSchema = R"({"type": "object", "properties": {"user_query": {"type": "string",
                "description": "The user query."}}, "required": ["user_query"]})";
            std::string asyncOutputSchema = R"({"type": "object", "properties": {"result": {"type": "string",
                "description": "The echoed message"}}})";
            std::string asyncTitle = "Async Echo Tool";
            std::string asyncDescription = "Async version of echo tool - echoes back the input after a short delay";
            Mcp::AddToolOptionalParams asyncParams;
            asyncParams.title = std::cref(asyncTitle);
            asyncParams.description = std::cref(asyncDescription);
            asyncParams.inputSchema = std::cref(asyncInputSchema);
            asyncParams.outputSchema = std::cref(asyncOutputSchema);
            server->AddTool("async_echo", asyncEchoFunc, asyncParams);
            MCP_LOG(MCP_LOG_LEVEL_INFO, std::string("add async tool success: async_echo"));
        } catch (const std::exception &e) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("add async tool failed: ") + e.what());
        }

        // Sampling echo tool example - demonstrates server-to-client sampling
        try {
            auto samplingEchoFunc = [](const Mcp::ServerContext &ctx, const std::string &name,
                                        const std::string &arguments) -> void {
                (void)name;
                const std::string prompt = ParseSamplingPrompt(arguments);
                std::thread([ctx, prompt]() {
                    const Mcp::CallToolResult result = RunSamplingRequest(ctx, prompt);
                    if (ctx.responseCallback) {
                        ctx.responseCallback(result);
                    }
                }).detach();
            };
            std::string samplingInputSchema = R"({"type": "object", "properties": {"prompt": {"type": "string",
                "description": "The prompt to send for sampling"}}, "required": ["prompt"]})";
            std::string samplingOutputSchema = R"({"type": "object", "properties": {"result": {"type": "string",
                "description": "The sampling response from client"}}})";
            std::string samplingTitle = "Sampling Echo Tool";
            std::string samplingDesc =
                "Demonstrates server-to-client sampling: requests sampling from client and returns the response";
            Mcp::AddToolOptionalParams samplingParams;
            samplingParams.title = std::cref(samplingTitle);
            samplingParams.description = std::cref(samplingDesc);
            samplingParams.inputSchema = std::cref(samplingInputSchema);
            samplingParams.outputSchema = std::cref(samplingOutputSchema);
            server->AddTool("sampling_echo", samplingEchoFunc, samplingParams);
            MCP_LOG(MCP_LOG_LEVEL_INFO, std::string("add sampling echo tool success: sampling_echo"));
        } catch (const std::exception &e) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("add sampling echo tool failed: ") + e.what());
        }

        // Progress notification tool example - demonstrates server-side progress
        try {
            auto progressFunc = [](const Mcp::ServerContext &ctx, const std::string &name,
                                    const std::string &arguments) -> void {
                (void)name;
                std::string taskName = "Unknown Task";
                int steps = 5;
                try {
                    auto j = nlohmann::json::parse(arguments);
                    if (j.contains("task_name") && j["task_name"].is_string()) {
                        taskName = j["task_name"].get<std::string>();
                    }
                    if (j.contains("steps") && j["steps"].is_number_integer()) {
                        steps = j["steps"].get<int>();
                        if (steps < 1) {
                            steps = 1;
                        }
                        if (steps > 100) {
                            steps = 100;
                        }
                    }
                } catch (const std::exception &e) {
                    MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("Error parsing progress tool arguments: ") + e.what());
                }

                std::thread([ctx, taskName, steps]() {
                    auto startTime = std::chrono::steady_clock::now();

                    Mcp::ProgressToken progressToken;
                    if (ctx.meta && ctx.meta->progressToken) {
                        progressToken = ctx.meta->progressToken.value();
                    } else {
                        progressToken = std::string("progress-") + taskName + "-" + std::to_string(std::time(nullptr));
                    }

                    for (int i = 0; i <= steps; i++) {
                        double progress = static_cast<double>(i) / steps;
                        if (ctx.session) {
                            std::string msg = "Processing " + taskName + ": step " +
                                              std::to_string(i) + " of " + std::to_string(steps);
                            ctx.session->SendProgressNotification(progressToken, progress, steps, msg);
                        }
                        if (i < steps) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(PROGRESS_TOOL_SIMULATION_DELAY_MS));
                        }
                    }

                    auto endTime = std::chrono::steady_clock::now();
                    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

                    Mcp::CallToolResult result;
                    result.isError = false;
                    Mcp::TextContent textContent;
                    textContent.type = "text";
                    textContent.text = "Task '" + taskName + "' completed successfully in " +
                                       std::to_string(duration.count()) + "ms";
                    result.content.push_back(textContent);

                    if (ctx.responseCallback) {
                        ctx.responseCallback(result);
                    }
                }).detach();
            };
            std::string progressInputSchema = R"({"type": "object", "properties": {"task_name": {"type": "string",
                "description": "Name of the task to process"}, "steps": {"type": "integer",
                "description": "Number of steps to simulate", "minimum": 1}}, "required": ["task_name", "steps"]})";
            std::string progressOutputSchema = R"({"type": "object", "properties": {"result": {"type": "string",
                "description": "Final result message"}, "total_time_ms": {"type": "integer",
                "description": "Total processing time in milliseconds"}}})";
            std::string progressTitle = "Progress Notification Tool";
            std::string progressDesc = "Demonstrates progress notifications during long-running operations";
            Mcp::AddToolOptionalParams progressParams;
            progressParams.title = std::cref(progressTitle);
            progressParams.description = std::cref(progressDesc);
            progressParams.inputSchema = std::cref(progressInputSchema);
            progressParams.outputSchema = std::cref(progressOutputSchema);
            server->AddTool("progress_tool", progressFunc, progressParams);
            MCP_LOG(MCP_LOG_LEVEL_INFO, std::string("add progress tool success: progress_tool"));
        } catch (const std::exception &e) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("add progress tool failed: ") + e.what());
        }

        // add Prompt
        try {
            std::string promptDescription = PROMPT_DESCRIPTION;
            Mcp::AddPromptOptionalParams promptParams;
            promptParams.description = std::cref(promptDescription);
            const std::vector<Mcp::PromptArgument> promptArgs = {
                Mcp::PromptArgument{"name", "The name of the person to greet", true},
                Mcp::PromptArgument{"language", "Language for the greeting (default: English)", false}};
            promptParams.arguments = std::cref(promptArgs);

            server->AddPrompt(PROMPT_NAME,
                              [](const Mcp::ServerContext & /* ctx */, const std::string &promptName,
                                 const std::optional<std::string> &arguments) -> Mcp::GetPromptResult {
                                  Mcp::GetPromptResult result;

                                             (void)promptName;

                                  std::string who = "friend";
                                  std::string lang = "English";

                                  if (arguments.has_value()) {
                                      // Parse string arguments to JSON for internal processing
                                      nlohmann::json j = nlohmann::json::parse(arguments.value());
                                      if (j.contains("name") && j["name"].is_string()) {
                                          who = j["name"].get<std::string>();
                                      }
                                      if (j.contains("language") && j["language"].is_string()) {
                                          lang = j["language"].get<std::string>();
                                      }
                                  }
                                  Mcp::TextContent tc;
                                  tc.type = "text";
                                  tc.text = "Hello, " + who + "! (language=" + lang + ")";
                                  Mcp::PromptMessage msg;
                                  msg.role = Mcp::RoleType::ASSISTANT;
                                  msg.content = tc;
                                  result.messages.push_back(msg);
                                  return result;
                              }, promptParams);
            MCP_LOG(MCP_LOG_LEVEL_INFO, std::string("add prompt success: ") + PROMPT_NAME);
        } catch (const std::exception &e) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("add prompt failed: ") + e.what());
        } catch (...) {
            MCP_LOG(MCP_LOG_LEVEL_INFO, "add prompt failed as expected");
        }

        // Async prompt example
        try {
            std::string asyncPromptDesc = "Async version of greeting prompt";
            Mcp::AddPromptOptionalParams asyncPromptParams;
            asyncPromptParams.description = std::cref(asyncPromptDesc);
            const std::vector<Mcp::PromptArgument> asyncPromptArgs = {
                Mcp::PromptArgument{"name", "The name of the person to greet", true},
                Mcp::PromptArgument{"language", "Language for the greeting", false}};
            asyncPromptParams.arguments = std::cref(asyncPromptArgs);

            server->AddPrompt("async_" + std::string(PROMPT_NAME),
                [](const Mcp::ServerContext &ctx, const std::string &promptName,
                   const std::optional<std::string> &arguments) -> void {
                    (void)promptName;
                    std::string who = "friend";
                    if (arguments.has_value()) {
                        try {
                            auto j = nlohmann::json::parse(arguments.value());
                            if (j.contains("name") && j["name"].is_string()) {
                                who = j["name"].get<std::string>();
                            }
                        } catch (...) {
                            // Malformed or non-JSON prompt arguments: ignore and keep default who ("friend").
                        }
                    }
                    std::thread([ctx, who]() {
                        std::this_thread::sleep_for(std::chrono::milliseconds(ASYNC_PROMPT_PROCESSING_DELAY_MS));
                        Mcp::GetPromptResult result;
                        Mcp::TextContent tc;
                        tc.type = "text";
                        tc.text = "Async Hello, " + who + "!";
                        Mcp::PromptMessage msg;
                        msg.role = Mcp::RoleType::ASSISTANT;
                        msg.content = tc;
                        result.messages.push_back(msg);
                        if (ctx.responseCallback) {
                            ctx.responseCallback(result);
                        }
                    }).detach();
                }, asyncPromptParams);
            MCP_LOG(MCP_LOG_LEVEL_INFO, std::string("add async prompt success: async_") + PROMPT_NAME);
        } catch (const std::exception &e) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("add async prompt failed: ") + e.what());
        }

        // add resources
        Mcp::ReadResourceFunc readResourceFunc = [](const Mcp::ServerContext & /* ctx */,
                                                    const std::string &uri) -> Mcp::ReadResourceResult {
            Mcp::ReadResourceResult result;
            Mcp::TextResourceContents textContents;
            textContents.uri = uri;
            textContents.text = "hello, " + uri;
            textContents.mimeType = RESOURCE_MIME_TYPE;
            result.contents.push_back(textContents);
            return result;
        };
        try {
            std::string resourceDescription = RESOURCE_DESCRIPTION;
            std::string resourceMimeType = RESOURCE_MIME_TYPE;
            Mcp::AddResourceOptionalParams resourceParams;
            resourceParams.description = std::cref(resourceDescription);
            resourceParams.mimeType = std::cref(resourceMimeType);
            const std::vector<Mcp::Icon> resourceIcons{Mcp::Icon{.src = "http://example.com/icon.png",
                .mimeType = "image/png", .sizes = std::vector<std::string>{"32x32", "64x64"}, .theme = "light"}};
            resourceParams.icons = std::cref(resourceIcons);

            server->AddResource(RESOURCE_URI, RESOURCE_NAME, readResourceFunc, resourceParams);
            MCP_LOG(MCP_LOG_LEVEL_INFO, std::string("add resource success: ") + RESOURCE_URI);
        } catch (const std::exception &e) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("add resource failed: ") + e.what());
        } catch (...) {
            MCP_LOG(MCP_LOG_LEVEL_INFO, "add resource failed as expected");
        }

        // Async resource example
        try {
            auto asyncReadFunc = [](const Mcp::ServerContext &ctx, const std::string &uri) -> void {
                std::thread([ctx, uri]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ASYNC_RESOURCE_PROCESSING_DELAY_MS));
                    Mcp::ReadResourceResult result;
                    Mcp::TextResourceContents textContents;
                    textContents.uri = uri;
                    textContents.text = "Async hello, " + uri;
                    textContents.mimeType = RESOURCE_MIME_TYPE;
                    result.contents.push_back(textContents);
                    if (ctx.responseCallback) {
                        ctx.responseCallback(result);
                    }
                }).detach();
            };
            std::string asyncResDesc = "Async version of sample resource";
            Mcp::AddResourceOptionalParams asyncResParams;
            asyncResParams.description = std::cref(asyncResDesc);
            std::string asyncResMimeType = RESOURCE_MIME_TYPE;
            asyncResParams.mimeType = std::cref(asyncResMimeType);
            server->AddResource("async_" + std::string(RESOURCE_URI), "async_" + std::string(RESOURCE_NAME),
                                asyncReadFunc, asyncResParams);
            MCP_LOG(MCP_LOG_LEVEL_INFO, std::string("add async resource success: async_") + RESOURCE_URI);
        } catch (const std::exception &e) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("add async resource failed: ") + e.what());
        }

        // minimal placeholders: register more resources to create multiple pages
        try {
            for (int i = 0; i < EXAMPLE_BULK_COUNT; ++i) {
                std::string uri = std::string("http://example.com/resource/") + std::to_string(i);
                std::string name = std::string("res_") + std::to_string(i);
                Mcp::AddResourceOptionalParams rp; // minimal
                server->AddResource(uri, name, readResourceFunc, rp);
            }
            MCP_LOG(MCP_LOG_LEVEL_INFO,
                std::string("bulk add resources completed: ") + std::to_string(EXAMPLE_BULK_COUNT));
        } catch (const std::exception &e) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("bulk add resources failed: ") + e.what());
        }

        // add ResourceTemplate
        try {
            std::string resourceTemplateDescription = RESOURCE_TEMPLATE_DESCRIPTION;
            std::string resourceTemplateMimeType = RESOURCE_TEMPLATE_MIME_TYPE;
            Mcp::AddResourceTemplateOptionalParams resourceTemplateParams;
            resourceTemplateParams.description = std::cref(resourceTemplateDescription);
            resourceTemplateParams.mimeType = std::cref(resourceTemplateMimeType);
            server->AddResourceTemplate(RESOURCE_TEMPLATE_URI, RESOURCE_TEMPLATE_NAME, resourceTemplateParams);
            MCP_LOG(MCP_LOG_LEVEL_INFO, std::string("add resource template success: ") + RESOURCE_TEMPLATE_URI);
        } catch (const std::exception &e) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("add resource template failed: ") + e.what());
        } catch (...) {
            MCP_LOG(MCP_LOG_LEVEL_INFO, "add resource template failed as expected");
        }

        // Register completion handler to respond to completion/complete requests
        server->AddCompletion([](const Mcp::CompleteReference &ref, const Mcp::CompletionArgument &arg,
                                 const std::optional<Mcp::CompletionContext> &ctx) -> Mcp::CompleteResult {
            (void)arg; // not used in this simple demo
            Mcp::CompleteResult result;

            // Suggest values based on reference type
            std::visit([
            &result](auto &&r) {
                using T = std::decay_t<decltype(r)>;
                if constexpr (std::is_same_v<T, Mcp::PromptReference>) {
                    result.completion.values = {"python", "pytorch", "pydantic"};
                } else {
                    result.completion.values = {"json", "yaml", "txt"};
                }
                }, ref);

            // Optionally echo back a framework hint if provided
            if (ctx && ctx->arguments) {
                auto it = ctx->arguments->find("framework");
                if (it != ctx->arguments->end()) {
                    result.completion.values.push_back(it->second);
                }
            }

            result.completion.total = static_cast<int64_t>(result.completion.values.size());
            result.completion.hasMore = false;
            return result;
        });

        if (!server->Run()) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "Failed to start MCP server");
            return -1;
        }

        if (server->IsRunning()) {
            MCP_LOG(MCP_LOG_LEVEL_INFO, "Server status check: RUNNING");
        } else {
            MCP_LOG(MCP_LOG_LEVEL_WARN, "Server status check: NOT RUNNING");
            return -1;
        }
        MCP_LOG(MCP_LOG_LEVEL_INFO, "MCP server started successfully on " + streamableHttpConfig.endpoint);

        std::cout << "Server is now running. Press Ctrl+C to stop gracefully..." << std::endl;
        std::cout << "  - Name: " << config.name << std::endl;
        std::cout << "  - Version: " << config.version << std::endl;
        std::cout << "  - IO Threads: " << streamableHttpConfig.ioThreads << std::endl;
        std::cout << "  - Worker Threads: " << config.workerThreads
                  << std::endl;
        std::cout << "  - Endpoint: " << streamableHttpConfig.endpoint << std::endl;
        if (enableAuth) {
            std::cout << "  - Required Scopes: " << REQUIRED_SCOPES << std::endl;
        }
        std::cout << "Test endpoints:" << std::endl;
        std::cout << "  - MCP endpoint: " << streamableHttpConfig.endpoint << std::endl;
        if (enableAuth) {
            std::cout << "Valid tokens for testing:" << std::endl;
            std::cout << "  - " << VALID_TOKEN << " (scopes: read write) - will succeed" << std::endl;
        }

        int counter = 0;
        while (!stopFlag && server->IsRunning()) {
            std::this_thread::sleep_for(std::chrono::seconds(HEARTBEAT_INTERVAL_SECONDS));
            counter++;
            MCP_LOG(MCP_LOG_LEVEL_DEBUG, std::string("Server heartbeat ") + std::to_string(counter) +
                " - still running...");
            if (counter % LOG_INTERVAL_COUNT == 0) {  // Every 30 seconds
                MCP_LOG(MCP_LOG_LEVEL_INFO, std::string("Server active for ") +
                    std::to_string(counter * HEARTBEAT_INTERVAL_SECONDS) + " seconds");
            }
        }

        server->Stop();
        MCP_LOG(MCP_LOG_LEVEL_INFO, "MCP server shutdown completed successfully");
    } catch (const std::invalid_argument &e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("Configuration error: ") + e.what());
        return -1;
    } catch (const std::runtime_error &e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("Runtime error: ") + e.what());
        return -1;
    } catch (const std::exception &e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("Unexpected error: ") + e.what());
        return -1;
    }

    MCP_LOG(MCP_LOG_LEVEL_INFO, "MCP Server test completed");
    if (g_logFile) {
        fclose(g_logFile);
        g_logFile = nullptr;
    }

    std::cout << "=== Test completed successfully ===" << std::endl;

    return 0;
}
