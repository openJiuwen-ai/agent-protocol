/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#include <csignal>

#include <chrono>
#include <cstdarg>
#include <iostream>
#include <string>
#include <thread>

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

int main(int argc, char** argv)
{
    std::cout << "=== MCP Server Test Example ===" << std::endl;

    bool isJsonResponseEnabled = true;
    std::string endpoint = ENDPOINT;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? std::string(argv[i]) : std::string{};
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << (argv[0] ? argv[0] : "ServerExample")
                      << " [--port=<1-65535>] [--isJsonResponseDisable]" << std::endl;
            return 0;
        }
        if (arg == "--isJsonResponseDisable") {
            isJsonResponseEnabled = false;
        } else if (arg.rfind("--port=", 0) == 0) {
            const std::string value = arg.substr(std::string("--port=").size());
            int port = std::stoi(value);
            endpoint = std::string("http://127.0.0.1:") + std::to_string(port) + "/mcp";
        }
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
        echoTool.func = [](const std::string &name, const Mcp::JsonValue &arguments,
                           const std::optional<Mcp::JsonValue> &ctx) -> Mcp::CallToolResult {
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
        };
        try {
            server->AddTool(echoTool);
            MCP_LOG(MCP_LOG_LEVEL_INFO, "add tool success: %s", echoTool.name.c_str());
        } catch (const std::exception &e) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "add tool failed: %s", e.what());
        } catch (...) {
            MCP_LOG(MCP_LOG_LEVEL_INFO, "add tool failed as expected");
        }

        // add Prompt
        Mcp::PromptInfo greetingPrompt;
        greetingPrompt.name = PROMPT_NAME;
        greetingPrompt.description = PROMPT_DESCRIPTION;
        greetingPrompt.arguments = {
            Mcp::PromptArgument{"name", "The name of the person to greet", true},
            Mcp::PromptArgument{"language", "Language for the greeting (default: English)", false}};

        try {
            server->AddPrompt(greetingPrompt,
                              [](const std::string &promptName,
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
                              });
            MCP_LOG(MCP_LOG_LEVEL_INFO, "add prompt success: %s", greetingPrompt.name.c_str());
        } catch (const std::exception &e) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "add prompt failed: %s", e.what());
        } catch (...) {
            MCP_LOG(MCP_LOG_LEVEL_INFO, "add prompt failed as expected");
        }

        // add resources
        Mcp::ResourceInfo resource;
        resource.uri = RESOURCE_URI;
        resource.name = RESOURCE_NAME;
        resource.description = RESOURCE_DESCRIPTION;
        resource.mimeType = RESOURCE_MIME_TYPE;

        Mcp::ReadResourceFunc readResourceFunc = [](const std::string &uri) -> Mcp::ReadResourceResult {
            Mcp::ReadResourceResult result;
            Mcp::TextResourceContents textContents;
            textContents.uri = uri;
            textContents.text = "hello, " + uri;
            textContents.mimeType = RESOURCE_MIME_TYPE;
            result.contents.push_back(textContents);
            return result;
        };
        try {
            server->AddResource(resource, readResourceFunc);
            MCP_LOG(MCP_LOG_LEVEL_INFO, "add resource success: %s", resource.uri.c_str());
        } catch (const std::exception &e) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "add resource failed: %s", e.what());
        } catch (...) {
            MCP_LOG(MCP_LOG_LEVEL_INFO, "add resource failed as expected");
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
    }

    std::cout << "=== Test completed successfully ===" << std::endl;

    return 0;
}
