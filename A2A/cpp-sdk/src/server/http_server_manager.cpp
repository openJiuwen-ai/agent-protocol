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
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
        return;
    }

    if (config_.ioThreadNum == 0) {
        return;
    }

    servers_.clear();
    servers_.reserve(config_.ioThreadNum);

    for (size_t i = 0; i < config_.ioThreadNum; ++i) {
        servers_.push_back(
            std::make_unique<HttpServer>(config_.host, config_.port, config_.tlsConfig, config_.routeMap, i));
    }

    for (auto& server : servers_) {
        if (server != nullptr) {
            server->Run();
        }
    }

    // Mark running only after servers_ is fully populated and Run() has returned,
    // so concurrent Stop() cannot clear a half-built vector.
    running_ = true;
}

void HttpServerManager::Stop()
{
    std::vector<std::unique_ptr<HttpServer>> serversToStop;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) {
            return;
        }
        running_ = false;
        serversToStop.swap(servers_);
    }

    for (auto& server : serversToStop) {
        if (server != nullptr) {
            server->Stop();
        }
    }
    serversToStop.clear();
}
} // namespace A2A::Server
