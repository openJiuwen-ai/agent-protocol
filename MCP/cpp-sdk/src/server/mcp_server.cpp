/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "mcp_server.h"

#include "mcp_server_implement.h"

namespace Mcp {

std::unique_ptr<McpServer> McpServerFactory::CreateStreamableHttpServer(const ServerConfig& config,
    const StreamableHttpServerConfig& transportConfig)
{
    return std::make_unique<McpServerImplement>(config, transportConfig);
}

std::unique_ptr<McpServer> McpServerFactory::CreateStdioServer(const ServerConfig& config)
{
    return std::make_unique<McpServerImplement>(config);
}

} // namespace Mcp
