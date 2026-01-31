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
void FileLogCallback(MCP_LOG_LEVEL logLevel, std::string message)
{
    if (!g_logFile) {
        return;
    }

    if (logLevel < GetLogLevel()) {
        return;
    }

    fprintf(g_logFile, "%s\n", message.c_str());
    fflush(g_logFile);
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
    std::string endpoint = enableAuth ? AUTH_ENDPOINT : ENDPOINT;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? std::string(argv[i]) : std::string{};
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << (argv[0] ? argv[0] : "ServerExample")
                      << " [--auth] [--port=<1-65535>] [--isJsonResponseDisable]" << std::endl;
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
        auto echoFunc = [](const std::string &name, const std::string &arguments,
                           const std::optional<std::string> &ctx) -> Mcp::CallToolResult {
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
                              [](const std::string &promptName,
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

        // add resources
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
