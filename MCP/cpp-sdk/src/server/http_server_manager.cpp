/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#include "server/http_server_manager.h"

#include "mcp_log.h"

namespace Mcp {
namespace Http {

HttpServerManager::HttpServerManager(const HttpServerManagerConfig& config) : config_(config)
{
}

HttpServerManager::~HttpServerManager()
{
    Stop();
}

void HttpServerManager::Start()
{
    if (running_.exchange(true)) {
        return;
    }

    if (config_.ioThreadNum == 0) {
        running_ = false;
        return;
    }

    servers_.clear();
    servers_.reserve(config_.ioThreadNum);

    for (size_t i = 0; i < config_.ioThreadNum; ++i) {
        servers_.push_back(
            std::make_unique<Http::HttpServer>(config_.host, config_.port, config_.tlsConfig_, config_.routeMap, i));
    }

    for (auto& server : servers_) {
        if (server != nullptr) {
            server->Run();
        }
    }
}

void HttpServerManager::Stop()
{
    if (!running_.exchange(false)) {
        return;
    }

    for (auto& server : servers_) {
        if (server != nullptr) {
            server->Stop();
        }
    }

    servers_.clear();
}

} // namespace Http
} // namespace Mcp
