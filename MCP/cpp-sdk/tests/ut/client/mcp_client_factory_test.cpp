/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

#include "mcp_client.h"
#include "mcp_type.h"

namespace Mcp {

#if MCP_WITH_HTTP

namespace {
ClientConfig CreateValidClientConfig()
{
    ClientConfig config;
    config.name = "TestClient";
    config.version = "1.0.0";
    return config;
}

StreamableHttpClientConfig CreateTransportConfig(const std::string& endpoint)
{
    StreamableHttpClientConfig transport;
    transport.endpoint = endpoint;
    transport.timeout = std::chrono::milliseconds(1000);
    transport.sseTimeout = std::chrono::milliseconds(1000);
    return transport;
}
} // namespace

TEST(McpClientFactoryTest, CreateStreamableHttpClient_ValidEndpoint)
{
    auto config = CreateValidClientConfig();
    auto transport = CreateTransportConfig("http://127.0.0.1:8001/mcp");

    EXPECT_NO_THROW({
        auto client = McpClientFactory::CreateStreamableHttpClient(config, transport);
        ASSERT_NE(client, nullptr);
        client->CloseGracefully();
    });
}

TEST(McpClientFactoryTest, CreateStreamableHttpClient_InvalidEndpoint)
{
    auto config = CreateValidClientConfig();

    const std::vector<std::string> invalidEndpoints = {
        "",
        "not-a-url",
        "http://local#host:8080",
        "http://localhost:8080/path#frag",
        "ftp://localhost:8080",
        "http://localhost:99999",
        "http://localhost:8080 ",
    };

    for (const auto& endpoint : invalidEndpoints) {
        auto transport = CreateTransportConfig(endpoint);
        EXPECT_THROW({
            auto client = McpClientFactory::CreateStreamableHttpClient(config, transport);
            (void)client;
        }, std::runtime_error) << "endpoint=" << endpoint;
    }
}

#endif // MCP_WITH_HTTP

} // namespace Mcp
