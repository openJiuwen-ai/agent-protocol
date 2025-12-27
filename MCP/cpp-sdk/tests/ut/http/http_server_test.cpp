/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <iostream>
#include <unistd.h>

#include "mcp_log.h"
#include "server/http_server.h"

int main(int argc, char* argv[]) {
    uint16_t port = 8080;
    if (argc > 1) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }

    Mcp::Http::HttpServer server;

    server.SetRoute("/hello", [](const Mcp::Http::HttpRequest& request, Mcp::Http::HttpResponse& response) {
        (void)request;
        response.headers[Mcp::Http::CONTENT_TYPE_HEADER] = "text/plain";
        response.body = "Hello, World!\n";
    });

    server.SetRoute("/", [](const Mcp::Http::HttpRequest& request, Mcp::Http::HttpResponse& response) {
        (void)request;
        response.headers[Mcp::Http::CONTENT_TYPE_HEADER] = "text/plain";
        response.body = "mcp_cpp HTTP server root\n";
    });

    server.SetRoute("/slow", [](const Mcp::Http::HttpRequest& request, Mcp::Http::HttpResponse& response) {
        (void)request;
        sleep(5);
        response.headers[Mcp::Http::CONTENT_TYPE_HEADER] = "text/plain";
        response.body = "slow done\n";
    });

    Mcp::Http::TlsConfig tlsConfig;
    tlsConfig.enabled = false;

    if (server.Start("127.0.0.1", port, tlsConfig) != Mcp::Http::HttpServerStartResult::OK) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Failed to start HTTP server on port %u", port);
        return 1;
    }

    return 0;
}
