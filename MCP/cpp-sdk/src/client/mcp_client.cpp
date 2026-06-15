/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <regex>
#include <stdexcept>

#include "mcp_client_implement.h"
#include "transport/stdio_transport.h"
#if MCP_WITH_HTTP
#include "transport/streamable_http_client_transport.h"
#endif
#include "mcp_client.h"

namespace Mcp {
constexpr int MAX_URL_LENGTH = 2048;

static void IsValidURL(const std::string& url)
{
    if (url.length() > MAX_URL_LENGTH) {
        throw std::runtime_error("url length more than max url length");
    }
    std::regex urlRegex(R"(^(https?)://([^/\s]+)(:\d+)?(/[^\s]*)?(\?[^\s]*)?$)");
    if (!std::regex_match(url, urlRegex)) {
        throw std::runtime_error("url is not valid");
    }
}

std::shared_ptr<McpClient> McpClientFactory::CreateStreamableHttpClient([[maybe_unused]] const ClientConfig& config,
    [[maybe_unused]] const StreamableHttpClientConfig& transportConfig,
    [[maybe_unused]] std::shared_ptr<AuthProvider> authProvider)
{
#if MCP_WITH_HTTP
    IsValidURL(transportConfig.endpoint);
    std::shared_ptr<ClientTransport> transport = std::make_shared<StreamableHttpClientTransport>(
        transportConfig.endpoint, transportConfig.headers, transportConfig.timeout,
        transportConfig.sseTimeout, transportConfig.tlsConfig, authProvider);
    return std::make_shared<McpClientImplement>(config, std::move(transport));
#else
    throw std::runtime_error("HTTP client is not enabled in this build");
#endif
}

std::shared_ptr<McpClient> McpClientFactory::CreateStdioClient(const ClientConfig& config,
    const StdioClientConfig& transportConfig)
{
    std::shared_ptr<ClientTransport> transport = std::make_shared<StdioClientTransport>(transportConfig);
    return std::make_shared<McpClientImplement>(config, std::move(transport));
}

} // namespace Mcp
