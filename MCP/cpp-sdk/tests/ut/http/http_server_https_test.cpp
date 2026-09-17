/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <iostream>
#include <cstdlib>
#include <unistd.h>

#include "mcp_log.h"
#include "server/http_server.h"
#include "shared/http_tls_config.h"

int main(int argc, char *argv[])
{
    uint16_t port = 8443;
    if (argc > 1) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }

    Mcp::Http::HttpServer server;
    server.SetRoute("/hello", [](const Mcp::Http::HttpRequest &request, Mcp::Http::HttpResponse &response) {
        (void)request;
        response.headers[Mcp::Http::CONTENT_TYPE_HEADER] = "text/plain";
        response.body = "Hello, HTTPS!\n";
    });

    server.SetRoute("/", [](const Mcp::Http::HttpRequest &request, Mcp::Http::HttpResponse &response) {
        (void)request;
        response.headers[Mcp::Http::CONTENT_TYPE_HEADER] = "text/plain";
        response.body = "mcp_cpp HTTPS server root\n";
    });

    server.SetRoute("/slow", [](const Mcp::Http::HttpRequest &request, Mcp::Http::HttpResponse &response) {
        (void)request;
        sleep(5);
        response.headers[Mcp::Http::CONTENT_TYPE_HEADER] = "text/plain";
        response.body = "slow done\n";
    });

    const char *certPath = std::getenv("MCP_TEST_TLS_CERT");
    const char *keyPath = std::getenv("MCP_TEST_TLS_KEY");
    const char *caPath = std::getenv("MCP_TEST_TLS_CA");

    if (certPath == nullptr || keyPath == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "MCP_TEST_TLS_CERT or MCP_TEST_TLS_KEY is not set");
        return 1;
    }

    Mcp::Http::TlsConfig tlsConfig;
    tlsConfig.enabled = true;
    tlsConfig.certFile = certPath;
    tlsConfig.keyFile = keyPath;
    tlsConfig.verifyPeer = false;
    if (caPath != nullptr) {
        tlsConfig.caFile = caPath;
    }

    if (server.Start("127.0.0.1", port, tlsConfig) != Mcp::Http::HttpServerStartResult::OK) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR,
                std::string("Failed to start HTTPS server on port ") + std::to_string(port));
        return 1;
    }

    return 0;
}
