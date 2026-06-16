/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <memory>
#include <variant>
#include <regex>

#include "server/server_impl.h"
#include "server/task_store.h"
#include "server/agent_executor.h"
#include "a2a_log.h"
#include "types.h"
#include "server/http_server_builder.h"

namespace A2A::Server {

constexpr size_t PORT_GROUP = 3;
constexpr size_t PATH_GROUP = 4;
constexpr int MAX_PORT = 65535;
constexpr int MIN_PORT = 1;

HttpServerBuilder::~HttpServerBuilder() = default;

static std::shared_ptr<AgentCard> setProtocolBinding(const AgentCard &agentCard, const std::string& transportType)
{
    auto modifiedAgentCard = std::make_shared<AgentCard>(agentCard);
    if (modifiedAgentCard->supportedInterfaces[0].protocolBinding != transportType) {
        A2A_LOG(A2A_LOG_LEVEL::WARN, "Agent card's primaryInterface.protocolBinding is not " +
            transportType + ", changing to default value");
        modifiedAgentCard->supportedInterfaces[0].protocolBinding = transportType;
    }
    return modifiedAgentCard;
}

std::shared_ptr<Server> HttpServerBuilder::Build(const HttpConfig& config,
    const AgentCard& agentCard, const AgentCard& extendedAgentCard,
    std::shared_ptr<AgentExecutor> agentExecutor, std::shared_ptr<TaskStore> taskStore)
{
    std::string endpoint = "/jsonrpc";
    
    if (agentCard.supportedInterfaces.empty()) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "agentCard.supportedInterfaces is empty");
        throw std::runtime_error("agentCard.supportedInterfaces is empty");
    }

    // Validate and parse agentCard.url
    if (agentCard.supportedInterfaces[0].url.empty()) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "agentCard.supportedInterfaces[0].url is empty");
        throw std::runtime_error("agentCard.supportedInterfaces[0].url is empty");
    }
    
    // Parse URL format: http://host:port or http://host:port/path
    std::regex url_regex(R"(^(https?:\/\/)?([^:\/\s]+):(\d+)(\/.*)?$)");
    std::smatch matches;
    
    if (std::regex_match(agentCard.supportedInterfaces[0].url, matches, url_regex)) {
        // Validate port range
        int port = std::stoi(matches[PORT_GROUP].str());
        if (port < MIN_PORT || port > MAX_PORT) {
            A2A_LOG(A2A_LOG_LEVEL::ERROR, "Invalid port in agentCard.url");
            throw std::runtime_error("Invalid port in agentCard.url");
        }
        
        // Extract endpoint if path present
        if (matches[PATH_GROUP].matched) {
            endpoint = matches[PATH_GROUP].str();
        }
    } else {
        // Fallback: extract last segment as endpoint
        size_t last_slash = agentCard.supportedInterfaces[0].url.rfind('/');
        if (last_slash != std::string::npos) {
            endpoint = agentCard.supportedInterfaces[0].url.substr(last_slash);
        }
        A2A_LOG(A2A_LOG_LEVEL::WARN, "Using fallback endpoint extraction: " + endpoint);
    }
    
    // Create config with endpoint
    HttpConfig configWithEndpoint;
    configWithEndpoint.ip = config.ip;
    configWithEndpoint.port = config.port;
    configWithEndpoint.ioThreadNum = config.ioThreadNum;
    configWithEndpoint.endpoint = endpoint;
    
    ServerConfig serverConfig = configWithEndpoint;
    
    std::shared_ptr<AgentCard> modifiedAgentCard = setProtocolBinding(agentCard, JSONRPC_TRANSPORT);
    auto server = std::make_shared<ServerImpl>(
        modifiedAgentCard,
        std::make_shared<AgentCard>(extendedAgentCard),
        agentExecutor,
        serverConfig,
        taskStore
    );

    return server;
}

} // namespace A2A::Server