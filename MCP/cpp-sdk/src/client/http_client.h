/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef MCP_HTTP_CLIENT_INCLUDE_H_
#define MCP_HTTP_CLIENT_INCLUDE_H_

#include <chrono>
#include <optional>
#include <string>

#include "event/event_system.h"
#include "net/tcp_socket.h"
#include "shared/common_type.h"
#include "shared/http_common.h"

namespace Mcp::Http {

class HttpClient {
public:
    HttpClient();

    // Send a synchronous HTTP request and wait until a complete response is received
    // or an error/timeout occurs.
    // host: server address (IP or domain name)
    // port: server port
    // request: HttpRequest to send (method/path/headers/body)
    // timeoutMs: total timeout in milliseconds, <= 0 means no timeout (may block indefinitely)
    // Return: HttpResponse on success, std::nullopt on failure.
    std::optional<HttpResponse> SendRequest(const std::string& host, uint16_t port, const HttpRequest& request,
                                            int timeoutMs = HTTP_CLIENT_DEFAULT_TIMEOUT_MS);

private:
    // Disable copy, only allow temporary usage or custom lifetime management in upper layers.
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;
};

} // namespace Mcp::Http

#endif // MCP_HTTP_CLIENT_INCLUDE_H_
