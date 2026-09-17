/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef HTTP_CARD_RESOLVER_BUILDER
#define HTTP_CARD_RESOLVER_BUILDER

#include <string>
#include <future>
#include <memory>

#include "client/a2a_card_resolver.h"
#include "types.h"

namespace A2A::Client {

/**
 * @brief Factory for HTTP-based A2ACardResolver instances.
 */
class HttpCardResolverBuilder final {
public:
    /** @brief Destructor. */
    ~HttpCardResolverBuilder();

    /**
     * @brief Build an HTTP card resolver.
     * @param[in] baseUrl        Server base URL (e.g. "http://host:port").
     * @param[in] agentCardPath  Path to the agent card (default: /.well-known/agent-card.json).
     * @param[in] httpKwargs     Extra HTTP headers or options.
     * @return Shared pointer to the created resolver.
     */
    static std::shared_ptr<A2ACardResolver> Build(std::string baseUrl, std::string agentCardPath,
        const std::map<std::string, std::string>& httpKwargs = {});
};

} // namespace A2A::Client

#endif
