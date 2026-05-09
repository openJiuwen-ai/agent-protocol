/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 */

#include "http_server_manager.h"

#include "a2a_log.h"
#include "http_server.h"

namespace A2A::Server {

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
            std::make_unique<HttpServer>(config_.host, config_.port, config_.tlsConfig, config_.routeMap));
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
} // namespace A2A::Server