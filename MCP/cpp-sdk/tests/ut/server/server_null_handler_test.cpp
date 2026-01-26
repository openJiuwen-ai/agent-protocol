/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
*/

#include <gtest/gtest.h>

#include "mcp_log.h"
#include "mcp_server.h"
#include "mcp_type.h"

const char* const SERVER_NAME = "TestMCPServer";
const char* const SERVER_VERSION = "1.0.0";
const char* const ENDPOINT = "http://127.0.0.1:9000/mcp";
const char* const ECHO_TOOL_NAME = "echo";
const char* const PROMPT_NAME = "example_prompt";
const char* const RESOURCE_URI = "http://example.com/resource";
const char* const RESOURCE_NAME = "Test Resource";


class ServerNullHandlerTest : public ::testing::Test {
public:
    ~ServerNullHandlerTest() {}
protected:
    void SetUp() override
    {
        SetLogLevel(MCP_LOG_LEVEL_ERROR);

        config.name = SERVER_NAME;
        config.version = SERVER_VERSION;
        config.workerThreads = 1;

        streamableHttpConfig.ioThreads = 1;
        streamableHttpConfig.tlsConfig.enabled = false;
        streamableHttpConfig.endpoint = ENDPOINT;
        streamableHttpConfig.isJsonResponseEnabled = true;

        server = Mcp::McpServerFactory::CreateStreamableHttpServer(config, streamableHttpConfig);
        ASSERT_NE(server, nullptr) << "Failed to create MCP server instance";
    }

    void TearDown() override
    {
        if (server && server->IsRunning()) {
            server->Stop();
        }
        server.reset();
    }

    Mcp::ServerConfig config;
    Mcp::StreamableHttpServerConfig streamableHttpConfig;
    std::unique_ptr<Mcp::McpServer> server;
};

TEST_F(ServerNullHandlerTest, AddToolWithNullHandler)
{
    try {
        server->AddTool(ECHO_TOOL_NAME, nullptr, Mcp::AddToolOptionalParams{});
        FAIL() << "AddTool with nullptr handler should have thrown an exception";
    } catch (...) {
        SUCCEED();
    }
}

TEST_F(ServerNullHandlerTest, AddPromptWithNullHandler)
{
    try {
        Mcp::AddPromptOptionalParams params;
        server->AddPrompt(PROMPT_NAME, nullptr, params);
        FAIL() << "AddPrompt with nullptr handler should have thrown an exception";
    } catch (...) {
        SUCCEED();
    }
}

TEST_F(ServerNullHandlerTest, AddResourceWithNullHandler)
{
    try {
        Mcp::AddResourceOptionalParams params;
        server->AddResource(RESOURCE_URI, RESOURCE_NAME, nullptr, params);
        FAIL() << "AddResource with nullptr handler should have thrown an exception";
    } catch (...) {
        SUCCEED();
    }
}