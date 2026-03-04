/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#include <csignal>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "mcp_log.h"
#include "mcp_server.h"
#include "mcp_type.h"

static FILE *g_logFile = nullptr;
volatile sig_atomic_t stopFlag = 0;

const char* const SERVER_NAME = "TestMCPServer";
const char* const SERVER_VERSION = "1.0.0";
const int IO_THREADS = 2;
const int WORKER_THREADS = 2;
const char* const ENDPOINT = "http://127.0.0.1:8000/mcp";
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
constexpr int ASYNC_PROMPT_PROCESSING_DELAY_MS = 500; // Delay for async prompt processing
constexpr int ASYNC_RESOURCE_PROCESSING_DELAY_MS = 300; // Delay for async resource processing
const char* const RESOURCE_TEMPLATE_URI = "http://example.com/resourceTemplate/{id}";
const char* const RESOURCE_TEMPLATE_NAME = "Test Resource Template";
const char* const RESOURCE_TEMPLATE_DESCRIPTION = "A test resource template for demonstration";
const char* const RESOURCE_TEMPLATE_MIME_TYPE = "text/plain";
const int HEARTBEAT_INTERVAL_SECONDS = 5;
const int LOG_INTERVAL_COUNT = 6;
const int SECONDS_PER_LOG = 30;

void signalHandler(int signal)
{
    if (signal == SIGINT || signal == SIGTERM) {
        MCP_LOG(MCP_LOG_LEVEL_INFO, "Received shutdown signal, stopping server...");
        stopFlag = 1;
    }
}

void FileLogCallback(MCP_LOG_LEVEL logLevel, const char *format, ...)
{
    if (!g_logFile) {
        return;
    }

    if (logLevel < GetLogLevel()) {
        return;
    }

    va_list args;
    va_start(args, format);
    vfprintf(g_logFile, format, args);
    va_end(args);

    fprintf(g_logFile, "\n");
    fflush(g_logFile);
}

// Helper function to extract text from CreateMessageResult
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
    errorText.text = message;
    result.content.emplace_back(errorText);
    return result;
}

std::string ParseSamplingPrompt(const Mcp::JsonValue& arguments)
{
    std::string prompt = "Hello";
    try {
        if (arguments.contains("prompt") && arguments.at("prompt").is_string()) {
            prompt = arguments.at("prompt").get<std::string>();
        }
    } catch (const std::exception& e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Error parsing sampling echo arguments: %s", e.what());
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
        MCP_LOG(MCP_LOG_LEVEL_INFO, "Sampling response received: %s", sampledText.c_str());

        Mcp::CallToolResult result;
        result.isError = false;
        Mcp::TextContent responseText;
        responseText.text = "Sampling response: " + sampledText;
        result.content.emplace_back(responseText);
        return result;
    } catch (const std::exception& e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Sampling failed: %s", e.what());
        return MakeSamplingErrorResult(std::string("Sampling error: ") + e.what());
    }
}

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
    std::cout << "=== MCP Server Test Example ===" << std::endl;

    bool isJsonResponseEnabled = true;
    bool stateless = false;
    std::string endpoint = ENDPOINT;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? std::string(argv[i]) : std::string{};
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << (argv[0] ? argv[0] : "ServerExample")
                      << " [--port=<1-65535>] [--stateless] [--isJsonResponseDisable]" << std::endl;
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

    // Synchronous prompt handler
    auto syncPromptHandler = [](const Mcp::ServerContext& ctx [[maybe_unused]], const std::string &promptName,
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
        tc.text = "Hello, " + who + "! (language=" + lang + ") [SYNC]";
        Mcp::PromptMessage msg;
        msg.role = Mcp::RoleType::ASSISTANT;
        msg.content = tc;
        result.messages.push_back(msg);
        return result;
    };

    // Asynchronous prompt handler
    auto asyncPromptHandler = [](const Mcp::ServerContext& ctx, const std::string &promptName,
        const std::optional<Mcp::JsonValue> &arguments) -> void {
        // Simulate async processing in a separate thread
        std::thread([ctx, promptName, arguments]() {
            // Simulate some processing delay
            std::this_thread::sleep_for(std::chrono::milliseconds(ASYNC_PROMPT_PROCESSING_DELAY_MS));

            Mcp::GetPromptResult result;
            result.description = "async_" + promptName;

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
            tc.text = "Hello, " + who + "! (language=" + lang + ") [ASYNC]";
            Mcp::PromptMessage msg;
            msg.role = Mcp::RoleType::ASSISTANT;
            msg.content = tc;
            result.messages.push_back(msg);

            // Send result via unified callback
            if (ctx.responseCallback) {
                ctx.responseCallback(result);
            }
        }).detach();
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

        auto server = Mcp::McpServerFactory::CreateStreamableHttpServer(config, streamableHttpConfig);
        if (server == nullptr) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "Failed to create MCP server instance");
            return -1;
        }
        MCP_LOG(MCP_LOG_LEVEL_INFO, "MCP server instance created successfully");

        // add tool
        Mcp::ToolInfo echoTool;
        echoTool.name = ECHO_TOOL_NAME;
        echoTool.title = ECHO_TOOL_TITLE;
        echoTool.description = ECHO_TOOL_DESCRIPTION;
        echoTool.inputSchema = {
            {"type", "object"},
            {"properties", {{"user_query", {{"type", "string"}, {"description", "The user query."}}}}},
            {"required", {"user_query"}}};
        echoTool.outputSchema = {
            {"type", "object"},
            {"properties", {{"result", {{"type", "string"}, {"description", "The echoed message"}}}}}};
        echoTool.func = Mcp::ToolFunc(Mcp::SyncToolFunc([](const Mcp::ServerContext& ctx [[maybe_unused]],
                            const std::string &name, const Mcp::JsonValue &arguments) -> Mcp::CallToolResult {
            Mcp::CallToolResult result;
            result.isError = false;
            try {
                std::string userQuery = "";
                if (arguments.contains("user_query") && arguments.at("user_query").is_string()) {
                    userQuery = arguments.at("user_query").get<std::string>();
                }
                Mcp::TextContent textContent;
                textContent.text = "Echo: " + userQuery;
                result.content.push_back(textContent);
            } catch (const std::exception &e) {
                result.isError = true;
                Mcp::TextContent errorContent;
                errorContent.text = std::string("Error: ") + e.what();
                result.content.push_back(errorContent);
            }
            return result;
        }));
        try {
            server->AddTool(echoTool);
            MCP_LOG(MCP_LOG_LEVEL_INFO, "add tool success: %s", echoTool.name.c_str());
        } catch (const std::exception &e) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "add tool failed: %s", e.what());
        } catch (...) {
            MCP_LOG(MCP_LOG_LEVEL_INFO, "add tool failed as expected");
        }

        /*
         * ========== ASYNC TOOL EXAMPLES ==========
         *
         * Async tools use AsyncToolFunc type and return results via ResponseCallback.
         *
         * Key concepts:
         * 1. AsyncToolFunc - Async tool function type, no direct return value
         * 2. ResponseCallback - Send responses through callback
         * 3. Thread safety - Responses handled via queue system
         *
         * Usage:
         * 1. Create background thread for task execution
         * 2. Send result via ctx.responseCallback(result)
         * 3. Handle errors and logging properly
         */

        // Add async echo tool example (similar to sync echo tool)
        Mcp::ToolInfo asyncEchoTool;
        asyncEchoTool.name = "async_echo";
        asyncEchoTool.title = "Async Echo Tool";
        asyncEchoTool.description = "Async version of echo tool - echoes back the input message after delay";
        asyncEchoTool.inputSchema = {
            {"type", "object"},
            {"properties", {{"user_query", {{"type", "string"}, {"description", "The user query."}}}}},
            {"required", {"user_query"}}};
        asyncEchoTool.outputSchema = {
            {"type", "object"},
            {"properties", {{"result", {{"type", "string"}, {"description", "The echoed message"}}}}}};

        // Async echo tool function using ResponseCallback
        asyncEchoTool.func = Mcp::AsyncToolFunc([](const Mcp::ServerContext& ctx, const std::string& name,
                                                  const Mcp::JsonValue& arguments) {
            MCP_LOG(MCP_LOG_LEVEL_INFO, "Async echo tool '%s' started", name.c_str());

            // Extract user query (same as sync echo tool)
            std::string userQuery = "";
            try {
                if (arguments.contains("user_query") && arguments.at("user_query").is_string()) {
                    userQuery = arguments.at("user_query").get<std::string>();
                }
            } catch (const std::exception& e) {
                MCP_LOG(MCP_LOG_LEVEL_ERROR, "Error parsing async echo arguments: %s", e.what());
            }

            // Create async thread to simulate async processing
            std::thread([ctx, userQuery, name]() {
                MCP_LOG(MCP_LOG_LEVEL_INFO, "Async echo thread started for query: %s", userQuery.c_str());

                // Simulate some async processing time (1 second)
                std::this_thread::sleep_for(std::chrono::seconds(1));

                // Prepare result (same format as sync echo tool)
                Mcp::CallToolResult result;
                result.isError = false;

                Mcp::TextContent textContent;
                textContent.text = "Async Echo: " + userQuery;  // Same format as sync version
                result.content.push_back(textContent);

                // Send response via callback from user thread
                if (ctx.responseCallback) {
                    MCP_LOG(MCP_LOG_LEVEL_INFO, "Sending async echo response");
                    ctx.responseCallback(result);
                } else {
                    MCP_LOG(MCP_LOG_LEVEL_ERROR, "No response callback available for async echo");
                }

                MCP_LOG(MCP_LOG_LEVEL_INFO, "Async echo thread completed");
            }).detach();

            MCP_LOG(MCP_LOG_LEVEL_INFO, "Async echo tool '%s' dispatched to background thread", name.c_str());
        });

        try {
            server->AddTool(asyncEchoTool);
            MCP_LOG(MCP_LOG_LEVEL_INFO, "Added async echo tool success: %s", asyncEchoTool.name.c_str());
        } catch (const std::exception& e) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "Add async echo tool failed: %s", e.what());
        }

        /*
         * ========== SAMPLING TOOL EXAMPLE ==========
         *
         * The sampling_echo tool demonstrates server-to-client sampling.
         * When called, the tool sends a sampling/createMessage request to the client,
         * receives the response, and returns it as the tool result.
         *
         * Key concepts:
         * 1. Server->Client sampling via ctx.session->SamplingCreateMessage()
         * 2. CreateMessageParams - parameters for the sampling request
         * 3. CreateMessageResult - the sampling response from the client
         */
        Mcp::ToolInfo samplingEchoTool;
        samplingEchoTool.name = "sampling_echo";
        samplingEchoTool.title = "Sampling Echo Tool";
        samplingEchoTool.description =
            "Demonstrates server-to-client sampling: requests sampling from client and returns "
            "the response";
        samplingEchoTool.inputSchema = nlohmann::json::object({
            {"type", "object"},
            {"properties", nlohmann::json::object({
                {"prompt", nlohmann::json::object({
                    {"type", "string"},
                    {"description", "The prompt to send for sampling"}
                })}
            })},
            {"required", nlohmann::json::array({"prompt"})}
        });
        samplingEchoTool.outputSchema = nlohmann::json::object({
            {"type", "object"},
            {"properties", nlohmann::json::object({
                {"result", nlohmann::json::object({
                    {"type", "string"},
                    {"description", "The sampling response from client"}
                })}
            })}
        });

        samplingEchoTool.func = Mcp::AsyncToolFunc([](const Mcp::ServerContext& ctx,
                                                      const std::string& name,
                                                      const Mcp::JsonValue& arguments) {
            MCP_LOG(MCP_LOG_LEVEL_INFO, "Sampling echo tool '%s' started", name.c_str());
            const std::string prompt = ParseSamplingPrompt(arguments);

            // Create async thread to handle sampling
            std::thread([ctx, prompt, name]() {
                MCP_LOG(MCP_LOG_LEVEL_INFO, "Sampling thread started for prompt: %s", prompt.c_str());
                const Mcp::CallToolResult result = RunSamplingRequest(ctx, prompt);
                SendSamplingResponse(ctx, result);

                MCP_LOG(MCP_LOG_LEVEL_INFO, "Sampling thread completed");
            }).detach();

            MCP_LOG(MCP_LOG_LEVEL_INFO, "Sampling echo tool '%s' dispatched to background thread", name.c_str());
        });

        try {
            server->AddTool(samplingEchoTool);
            MCP_LOG(MCP_LOG_LEVEL_INFO, "Added sampling echo tool success: %s", samplingEchoTool.name.c_str());
        } catch (const std::exception& e) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "Add sampling echo tool failed: %s", e.what());
        }
        
        /*
         * ========== PROGRESS NOTIFICATION TOOL EXAMPLE ==========
         *
         * This tool demonstrates how to send progress notifications during long-running operations.
         * Key concepts:
         * 1. Progress notifications can be sent from server to client
         * 2. Progress token identifies the operation (string or int64_t)
         * 3. Progress value ranges from 0.0 to 1.0 (or actual value)
         * 4. Optional total value and message can be included
         */

        // Add progress notification tool example
        Mcp::ToolInfo progressTool;
        progressTool.name = "progress_tool";
        progressTool.title = "Progress Notification Tool";
        progressTool.description = "Demonstrates progress notifications during long-running operations";
        progressTool.inputSchema = {
            {"type", "object"},
            {"properties", {
                {"task_name", {{"type", "string"}, {"description", "Name of the task to process"}}},
                {"steps", {{"type", "integer"}, {"description", "Number of steps to simulate"}, {"minimum", 1}}}
            }},
            {"required", {"task_name", "steps"}}
        };
        progressTool.outputSchema = {
            {"type", "object"},
            {"properties", {
                {"result", {{"type", "string"}, {"description", "Final result message"}}},
                {"total_time_ms", {{"type", "integer"}, {"description", "Total processing time in milliseconds"}}}
            }}
        };

        // Progress tool function with progress notifications
        progressTool.func = Mcp::AsyncToolFunc([](const Mcp::ServerContext& ctx, const std::string& name,
                                                  const Mcp::JsonValue& arguments) {
            MCP_LOG(MCP_LOG_LEVEL_INFO, "Progress tool '%s' started", name.c_str());

            // Extract arguments
            std::string taskName = "Unknown Task";
            int steps = 5;
            try {
                if (arguments.contains("task_name") && arguments.at("task_name").is_string()) {
                    taskName = arguments.at("task_name").get<std::string>();
                }
                if (arguments.contains("steps") && arguments.at("steps").is_number_integer()) {
                    steps = arguments.at("steps").get<int>();
                    if (steps < 1) steps = 1;
                    if (steps > 100) steps = 100; // Limit to reasonable number: 100
                }
            } catch (const std::exception& e) {
                MCP_LOG(MCP_LOG_LEVEL_ERROR, "Error parsing progress tool arguments: %s", e.what());
            }

            // Create async thread to simulate long-running operation with progress updates
            std::thread([ctx, taskName, steps, name]() {
                MCP_LOG(MCP_LOG_LEVEL_INFO, "Progress tool thread started for task: %s, steps: %d",
                        taskName.c_str(), steps);

                auto startTime = std::chrono::steady_clock::now();

                // Use progressToken from client request if available, otherwise generate one
                Mcp::ProgressToken progressToken;
                if (ctx.meta && ctx.meta->progressToken) {
                    // Use the progressToken from client request
                    progressToken = ctx.meta->progressToken.value();
                    // Log progress token value
                    if (std::holds_alternative<std::string>(progressToken)) {
                        MCP_LOG(MCP_LOG_LEVEL_INFO, "Using client-provided progress token: %s",
                                std::get<std::string>(progressToken).c_str());
                    } else if (std::holds_alternative<int64_t>(progressToken)) {
                        MCP_LOG(MCP_LOG_LEVEL_INFO, "Using client-provided progress token: %ld",
                                std::get<int64_t>(progressToken));
                    }
                } else {
                    // Fallback: generate a progress token
                    progressToken = "progress-" + taskName + "-" + std::to_string(std::time(nullptr));
                    MCP_LOG(MCP_LOG_LEVEL_INFO, "Generated progress token: %s",
                            std::get<std::string>(progressToken).c_str());
                }

                // Simulate processing with progress updates
                for (int i = 0; i <= steps; i++) {
                    // Calculate progress
                    double progress = static_cast<double>(i) / steps;

                    // Send progress notification
                    if (ctx.session) {
                        std::string message = "Processing " + taskName + ": step " +
                                              std::to_string(i) + " of " + std::to_string(steps);

                        ctx.session->SendProgressNotification(
                            progressToken, // Progress token (string)
                            progress,      // Current progress (0.0 to 1.0)
                            steps,         // Total steps
                            message);      // Human-readable message

                        MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Sent progress notification: %.1f%% - %s",
                                progress * 100, message.c_str()); // 100: percent
                    }

                    // Simulate work
                    if (i < steps) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(PROGRESS_TOOL_SIMULATION_DELAY_MS));
                    }
                }

                auto endTime = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

                // Prepare final result
                Mcp::CallToolResult result;
                result.isError = false;

                Mcp::TextContent textContent;
                textContent.text = "Task '" + taskName + "' completed successfully in " +
                                   std::to_string(duration.count()) + "ms";
                result.content.push_back(textContent);

                // Send final response
                if (ctx.responseCallback) {
                    MCP_LOG(MCP_LOG_LEVEL_INFO, "Sending progress tool final response");
                    ctx.responseCallback(result);
                } else {
                    MCP_LOG(MCP_LOG_LEVEL_ERROR, "No response callback available for progress tool");
                }
            }).detach();
        });

        try {
            server->AddTool(progressTool);
            MCP_LOG(MCP_LOG_LEVEL_INFO, "add progress tool success: %s", progressTool.name.c_str());
        } catch (const std::exception &e) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "add progress tool failed: %s", e.what());
        } catch (...) {
            MCP_LOG(MCP_LOG_LEVEL_INFO, "add progress tool failed as expected");
        }

        // Add synchronous prompt
        Mcp::PromptInfo greetingPrompt;
        greetingPrompt.name = PROMPT_NAME;
        greetingPrompt.description = PROMPT_DESCRIPTION;
        greetingPrompt.arguments = {
            Mcp::PromptArgument{"name", "The name of the person to greet", true},
            Mcp::PromptArgument{"language", "Language for the greeting (default: English)", false}};

        try {
            // Explicitly cast to SyncRenderPromptFunc
            Mcp::SyncRenderPromptFunc syncFunc = syncPromptHandler;
            server->AddPrompt(greetingPrompt, Mcp::RenderPromptFunc(syncFunc));
            MCP_LOG(MCP_LOG_LEVEL_INFO, "add sync prompt success: %s", greetingPrompt.name.c_str());
        } catch (const std::exception &e) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "add sync prompt failed: %s", e.what());
        }

        // Add asynchronous prompt
        Mcp::PromptInfo asyncGreetingPrompt;
        asyncGreetingPrompt.name = "async_" + std::string(PROMPT_NAME);
        asyncGreetingPrompt.description = "Async version of " + std::string(PROMPT_DESCRIPTION);
        asyncGreetingPrompt.arguments = {
            Mcp::PromptArgument{"name", "The name of the person to greet", true},
            Mcp::PromptArgument{"language", "Language for the greeting (default: English)", false}};

        try {
            // Explicitly cast to AsyncRenderPromptFunc
            Mcp::AsyncRenderPromptFunc asyncFunc = asyncPromptHandler;
            server->AddPrompt(asyncGreetingPrompt, Mcp::RenderPromptFunc(asyncFunc));
            MCP_LOG(MCP_LOG_LEVEL_INFO, "add async prompt success: %s", asyncGreetingPrompt.name.c_str());
        } catch (const std::exception &e) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "add async prompt failed: %s", e.what());
        }

        // Synchronous resource handler
        auto syncResourceHandler = [](const Mcp::ServerContext& ctx [[maybe_unused]],
            const std::string &uri) -> Mcp::ReadResourceResult {
            Mcp::ReadResourceResult result;
            Mcp::TextResourceContents textContents;
            textContents.uri = uri;
            textContents.text = "Hello from sync resource: " + uri + " [SYNC]";
            textContents.mimeType = RESOURCE_MIME_TYPE;
            result.contents.push_back(textContents);
            return result;
        };

        // Asynchronous resource handler
        auto asyncResourceHandler = [](const Mcp::ServerContext& ctx, const std::string &uri) -> void {
            // Simulate async processing in a separate thread
            std::thread([ctx, uri]() {
                // Simulate some processing delay
                std::this_thread::sleep_for(std::chrono::milliseconds(ASYNC_RESOURCE_PROCESSING_DELAY_MS));

                Mcp::ReadResourceResult result;
                Mcp::TextResourceContents textContents;
                textContents.uri = uri;
                textContents.text = "Hello from async resource: " + uri + " [ASYNC]";
                textContents.mimeType = RESOURCE_MIME_TYPE;
                result.contents.push_back(textContents);

                // Send result via unified callback
                if (ctx.responseCallback) {
                    ctx.responseCallback(result);
                }
            }).detach();
        };

        // Add synchronous resource
        Mcp::ResourceInfo resource;
        resource.uri = RESOURCE_URI;
        resource.name = RESOURCE_NAME;
        resource.description = RESOURCE_DESCRIPTION;
        resource.mimeType = RESOURCE_MIME_TYPE;

        try {
            // Explicitly cast to SyncReadResourceFunc
            Mcp::SyncReadResourceFunc syncFunc = syncResourceHandler;
            server->AddResource(resource, Mcp::ReadResourceFunc(syncFunc));
            MCP_LOG(MCP_LOG_LEVEL_INFO, "add sync resource success: %s", resource.uri.c_str());
        } catch (const std::exception &e) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "add sync resource failed: %s", e.what());
        }

        // Add asynchronous resource
        Mcp::ResourceInfo asyncResource;
        asyncResource.uri = "async_" + std::string(RESOURCE_URI);
        asyncResource.name = "Async " + std::string(RESOURCE_NAME);
        asyncResource.description = "Async version of " + std::string(RESOURCE_DESCRIPTION);
        asyncResource.mimeType = RESOURCE_MIME_TYPE;

        try {
            // Explicitly cast to AsyncReadResourceFunc
            Mcp::AsyncReadResourceFunc asyncFunc = asyncResourceHandler;
            server->AddResource(asyncResource, Mcp::ReadResourceFunc(asyncFunc));
            MCP_LOG(MCP_LOG_LEVEL_INFO, "add async resource success: %s", asyncResource.uri.c_str());
        } catch (const std::exception &e) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "add async resource failed: %s", e.what());
        }

        // add ResourceTemplate
        Mcp::ResourceTemplate resourceTemplate;
        resourceTemplate.uriTemplate = RESOURCE_TEMPLATE_URI;
        resourceTemplate.name = RESOURCE_TEMPLATE_NAME;
        resourceTemplate.description = RESOURCE_TEMPLATE_DESCRIPTION;
        resourceTemplate.mimeType = RESOURCE_TEMPLATE_MIME_TYPE;
        try {
            server->AddResourceTemplate(resourceTemplate);
            MCP_LOG(MCP_LOG_LEVEL_INFO, "add resource template success: %s", resourceTemplate.uriTemplate.c_str());
        } catch (const std::exception &e) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "add resource template failed: %s", e.what());
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
        MCP_LOG(MCP_LOG_LEVEL_INFO, "MCP server started successfully on %s",
                streamableHttpConfig.endpoint.c_str());

        std::cout << "Server is now running. Press Ctrl+C to stop gracefully..." << std::endl;
        std::cout << "  - Name: " << config.name.c_str() << std::endl;
        std::cout << "  - Version: " << config.version.c_str() << std::endl;
        std::cout << "  - IO Threads: " << streamableHttpConfig.ioThreads << std::endl;
        std::cout << "  - Worker Threads: " << config.workerThreads
                  << std::endl;
        std::cout << "  - Endpoint: " << streamableHttpConfig.endpoint.c_str() << std::endl;
        std::cout << "Test endpoints:" << std::endl;
        std::cout << "  - MCP endpoint: " << streamableHttpConfig.endpoint << std::endl;

        int counter = 0;
        while (!stopFlag && server->IsRunning()) {
            std::this_thread::sleep_for(std::chrono::seconds(HEARTBEAT_INTERVAL_SECONDS));
            counter++;
            MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Server heartbeat %d - still running...", counter);
            if (counter % LOG_INTERVAL_COUNT == 0) {  // Every 30 seconds
                MCP_LOG(MCP_LOG_LEVEL_INFO, "Server active for %d seconds", counter * HEARTBEAT_INTERVAL_SECONDS);
            }
        }

        server->Stop();
        MCP_LOG(MCP_LOG_LEVEL_INFO, "MCP server shutdown completed successfully");
    } catch (const std::invalid_argument &e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Configuration error: %s", e.what());
        return -1;
    } catch (const std::runtime_error &e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Runtime error: %s", e.what());
        return -1;
    } catch (const std::exception &e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Unexpected error: %s", e.what());
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
