/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include "a2a_log.h"
#include "default_client.h"
#include "jsonrpc_transport.h"
#include "client/client_factory.h"

namespace A2A::Client {

ClientFactory::~ClientFactory() = default;

std::shared_ptr<Client> ClientFactory::Create(const AgentCard& card, const ClientConfig& config,
    const std::vector<Consumer>& consumers,
    const std::vector<std::shared_ptr<ClientCallInterceptor>>& interceptors)
{
    try {
        for (const auto& itc : interceptors) {
            if (itc == nullptr) {
                return nullptr;
            }
        }
        if (!config.supportedTransports.empty()) {
            for (const auto& t : config.supportedTransports) {
                if (t.empty()) {
                    return nullptr;
                }
            }
        }
        if (card.supportedInterfaces.empty()) {
            return nullptr;
        }
        for (const auto& itf : card.supportedInterfaces) {
            if (itf.protocolBinding.empty() || itf.url.empty() || itf.protocolVersion.empty()) {
                return nullptr;
            }
        }

        std::map<std::string, std::string> serverSet;
        for (const auto& itf : card.supportedInterfaces) {
            auto it = serverSet.find(itf.protocolBinding);
            if (it == serverSet.end()) {
                serverSet[itf.protocolBinding] = itf.url;
            } else {
                A2A_LOG(A2A_LOG_LEVEL_WARN, "Duplicate protocolBinding:" + itf.protocolBinding);
            }
        }

        std::vector<std::string> clientSet = config.supportedTransports.empty() ?
            std::vector<std::string>{JSONRPC_TRANSPORT} : config.supportedTransports;
        std::string chosenProtocol;
        std::string chosenUrl;

        if (config.useClientPreference) {
            for (const auto& x : clientSet) {
                auto it = serverSet.find(x);
                if (it != serverSet.end()) {
                    chosenProtocol = x;
                    chosenUrl = it->second;
                    break;
                }
            }
        } else {
            for (const auto& kv : std::as_const(serverSet)) {
                if (std::find(clientSet.begin(), clientSet.end(), kv.first) != clientSet.end()) {
                    chosenProtocol = kv.first;
                    chosenUrl = kv.second;
                    break;
                }
            }
        }
        if (chosenProtocol.empty() || chosenUrl.empty()) {
            return nullptr;
        }

        std::shared_ptr<ClientTransport> transport = nullptr;
        if (chosenProtocol == JSONRPC_TRANSPORT) {
            transport = std::make_shared<JsonRpcTransport>(chosenUrl, card, interceptors);
        }

        if (transport == nullptr) {
            return nullptr;
        }

        return std::make_shared<DefaultClient>(card, config, transport, consumers, interceptors);
    } catch (const std::exception& e) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, std::string("exception occured: ") + e.what());
        return nullptr;
    }
}

std::shared_ptr<Client> ClientFactory::Create(const AgentCard& card, const ClientConfig& config,
    std::shared_ptr<ClientTransport> transport, const std::vector<Consumer>& consumers)
{
    try {
        if (transport == nullptr) {
            return nullptr;
        }
        return std::make_shared<DefaultClient>(card, config, transport, consumers);
    } catch (const std::exception& e) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, std::string("exception occured: ") + e.what());
        return nullptr;
    }
}

} // namespace A2A::Client
