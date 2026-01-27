/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include "default_client.h"
#include "jsonrpc_transport.h"
#include "client/client_factory.h"

namespace A2A::Client {

ClientFactory::~ClientFactory() = default;

std::shared_ptr<Client> ClientFactory::Create(const AgentCard& card, const ClientConfig& config,
    const std::vector<Consumer>& consumers,
    const std::vector<std::shared_ptr<ClientCallInterceptor>>& interceptors)
{
    std::string serverPreferred = card.preferredTransport.value_or("JSONRPC");
    std::map<std::string, std::string> serverSet{{serverPreferred, card.url}};
    if (card.additionalInterfaces) {
        for (const auto& itf : *card.additionalInterfaces) {
            serverSet[itf.transport] = itf.url;
        }
    }

    std::vector<std::string> clientSet = config.supportedTransports.empty() ?
        std::vector<std::string>{"JSONRPC"} : config.supportedTransports;
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
        for (const auto& kv : serverSet) {
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
    if (chosenProtocol == "JSONRPC") {
        transport = std::make_shared<JsonRpcTransport>(chosenUrl, &card, interceptors);
    }

    if (transport == nullptr) {
        return nullptr;
    }

    return std::make_shared<DefaultClient>(card, config, transport, consumers, interceptors);
}

} // namespace A2A::Client
