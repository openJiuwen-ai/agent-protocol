/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef HTTP_CARD_RESOLVER
#define HTTP_CARD_RESOLVER

#include <string>
#include <map>
#include <future>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <optional>

#include "client/a2a_card_resolver.h"
#include "client/jsonrpc_transport.h"

namespace A2A::Client {

class HttpCardResolver : public A2ACardResolver {
public:
    HttpCardResolver(std::string baseUrl, const std::optional<std::string>& relativeCardPath,
                    std::map<std::string, std::string> httpKwargs = {});

    ~HttpCardResolver() override = default;

    std::future<AgentCard> GetAgentCard(
        const std::optional<std::string>& relativeCardPath = std::nullopt) const override;

    std::future<std::vector<AgentCard>> GetAllAgentCards() const override;

protected:
    std::string baseUrl_;
    std::optional<std::string> relativeCardPath_;
    std::map<std::string, std::string> httpKwargs_;
    std::shared_ptr<JsonRpcTransport> transport_;

    void OnTransportEvent(const std::string& requestId, const TransportEvent& event);
    std::exception_ptr CreateExceptionPtr(int code, const std::string& msg) const;
    mutable std::mutex callbackMutex_;
    mutable std::unordered_map<std::string, std::shared_ptr<std::promise<AgentCard>>> pendingPromises_;
};

} // namespace A2A::Client

#endif