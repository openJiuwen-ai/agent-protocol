/*
* Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "server/server.h"
#include "server_impl.h"

namespace a2a::server {

Server::Server(ServerTransportType transportType, std::shared_ptr<RequestHandler> handler,
               std::shared_ptr<AgentCard> agentCard)
    : impl_(nullptr)
{
    std::string transportStr;
    switch (transportType) {
        case SERVER_TRANSPORT_TYPE_HTTP:
            transportStr = "http";
            break;
        case SERVER_TRANSPORT_TYPE_MAX:
        default:
            transportStr = "unknown";
            break;
    }

    impl_ = std::make_unique<ServerImpl>(transportStr, handler, agentCard);
}

Server::~Server() = default;

int Server::Start(const ServerConfig& config)
{
    if (!impl_) {
        throw std::runtime_error("Server implementation not initialized");
        return -1;
    }

    // 需要从 config 中提取 ip 和 port
    std::string ip;
    int port = 0;

    // 根据 transport type 获取对应的配置
    switch (config.type) {
        case SERVER_TRANSPORT_TYPE_HTTP: {
            const auto& httpConfig = std::get<HttpConfig>(config.config);
            ip = httpConfig.ip;
            port = httpConfig.port;
            break;
        }
        case SERVER_TRANSPORT_TYPE_MAX:
        default:
            // 返回错误码
            return -1;
    }

    return impl_->Start(ip, port);
}

void Server::Stop()
{
    return impl_->Stop();
}

AgentCard Server::OnGetAuthenticatedExtendedCard(const ServerCallContext* context)
{
    if (!impl_) {
        throw std::runtime_error("Server implementation not initialized");
    }
    return impl_->OnGetAuthenticatedExtendedCard(context);
}

AgentCard Server::OnGetCard(const ServerCallContext* context)
{
    if (!impl_) {
        throw std::runtime_error("Server implementation not initialized");
    }
    return impl_->OnGetCard(context);
}

} // namespace a2a::server