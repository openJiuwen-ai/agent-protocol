/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "mcp_client.h"

#include "mcp_client_implement.h"
#include "transport/stdio_transport.h"
#include "transport/streamable_http_client_transport.h"
 
namespace Mcp {
 
std::shared_ptr<McpClient> McpClientFactory::CreateStreamableHttpClient(const ClientConfig& config,
    const StreamableHttpClientConfig& transportConfig, std::shared_ptr<AuthProvider> authProvider)
{
    std::shared_ptr<ClientTransport> transport = std::make_shared<StreamableHttpClientTransport>(
        transportConfig.endpoint, transportConfig.headers, transportConfig.timeout,
        transportConfig.sseTimeout, transportConfig.tlsConfig);
    return std::make_shared<McpClientImplement>(config, std::move(transport), std::move(authProvider));
}

std::shared_ptr<McpClient> McpClientFactory::CreateStdioClient(const ClientConfig& config,
    const StdioClientConfig& transportConfig)
{
    std::shared_ptr<ClientTransport> transport = std::make_shared<StdioClientTransport>(transportConfig);
    return std::make_shared<McpClientImplement>(config, std::move(transport));
}

} // namespace Mcp
