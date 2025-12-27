/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <stdexcept>

#include "client_factory_impl.h"
#include "default_client.h"
#include "jsonrpc_transport.h"

namespace a2a::client {

ClientFactoryImpl::ClientFactoryImpl(ClientConfig config, std::vector<Consumer> consumers)
    : config_(config), consumers_(std::move(consumers))
{
    RegisterDefaults();
}

void ClientFactoryImpl::RegisterTransport(const std::string& label, TransportProducer producer)
{
    registry_[label] = std::move(producer);
}

std::unique_ptr<Client> ClientFactoryImpl::Create(const a2a::AgentCard& card,
                                                  const std::vector<Consumer>& extraConsumers,
                                                  const std::vector<ClientCallInterceptor*>& interceptors) const
{
    std::string serverPreferred = card.preferredTransport.value_or("JSONRPC");
    std::map<std::string, std::string> serverSet{{serverPreferred, card.url}};
    if (card.additionalInterfaces) {
        for (const auto& itf : *card.additionalInterfaces) {
            serverSet[itf.transport] = itf.url;
        }
    }

    std::vector<std::string> clientSet =
        config_.supportedTransports.empty() ? std::vector<std::string>{"JSONRPC"} : config_.supportedTransports;
    std::string chosenProtocol;
    std::string chosenUrl;

    if (config_.useClientPreference) {
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
        throw std::runtime_error("no compatible transports found.");
    }

    auto it = registry_.find(chosenProtocol);
    if (it == registry_.end()) {
        throw std::runtime_error("no client available for transport: " + chosenProtocol);
    }

    std::vector<Consumer> allConsumers = consumers_;
    allConsumers.insert(allConsumers.end(), extraConsumers.begin(), extraConsumers.end());
    auto transport = it->second(card, chosenUrl, config_, interceptors);
    return std::make_unique<DefaultClient>(
        card, config_, transport, allConsumers,
        std::vector<ClientCallInterceptor*>{interceptors.begin(), interceptors.end()});
}

void ClientFactoryImpl::RegisterDefaults()
{
    // jsonrpc
    auto iter =
        std::find(config_.supportedTransports.begin(), config_.supportedTransports.end(), std::string("JSONRPC"));
    if (config_.supportedTransports.empty() || iter != config_.supportedTransports.end()) {
        RegisterTransport("JSONRPC", [](const a2a::AgentCard& card, const std::string& url, const ClientConfig& cfg,
                                        const std::vector<ClientCallInterceptor*>& interceptors) {
            return std::make_shared<JsonRpcTransport>(
                url, &card, std::vector<ClientCallInterceptor*>{interceptors.begin(), interceptors.end()});
        });
    }
}

} // namespace a2a::client
