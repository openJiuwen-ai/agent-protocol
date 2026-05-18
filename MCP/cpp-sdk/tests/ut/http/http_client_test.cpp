/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <iostream>
#include <string>

#include "mcp_log.h"
#include "client/http_client.h"
#include "shared/http_common.h"

constexpr int HTTP_CLIENT_TIMEOUT_MS = 5000;

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    uint16_t port = 8080;
    std::string path = "/";

    if (argc > 1) {
        host = argv[1];
    }
    if (argc > 2) {
        port = static_cast<uint16_t>(std::stoi(argv[2]));
    }
    if (argc > 3) {
        path = argv[3];
    }

    Mcp::Http::HttpRequest request;
    request.method = "GET";
    request.url = path;
    request.version = "HTTP/1.1";
    request.headers["User-Agent"] = "mcp_cpp_http_client";
    request.headers[Mcp::Http::ACCEPT_HEADER] = "*/*";

    Mcp::Http::HttpClient client;
    auto responseOptional = client.SendRequest(host, port, request, HTTP_CLIENT_TIMEOUT_MS);

    if (!responseOptional) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "HTTP request failed");
        return 1;
    }

    const auto& response = *responseOptional;

    MCP_LOG(MCP_LOG_LEVEL_INFO, "Status: " + std::to_string(response.statusCode) + " " + response.statusText);
    MCP_LOG(MCP_LOG_LEVEL_INFO, "Headers:");
    for (const auto& header : response.headers) {
        MCP_LOG(MCP_LOG_LEVEL_INFO, "\t" + header.first + ": " + header.second);
    }
    MCP_LOG(MCP_LOG_LEVEL_INFO, "Body:\n" + response.body);

    return 0;
}
