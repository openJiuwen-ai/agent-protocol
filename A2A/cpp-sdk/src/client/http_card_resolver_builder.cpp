/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "a2a_log.h"
#include "http_card_resolver.h"
#include "client/http_card_resolver_builder.h"

namespace A2A::Client {

HttpCardResolverBuilder::~HttpCardResolverBuilder() = default;

std::shared_ptr<A2ACardResolver> HttpCardResolverBuilder::Build(std::string baseUrl, std::string agentCardPath,
    const std::map<std::string, std::string>& httpKwargs)
{
    try {
        if (baseUrl.empty() || agentCardPath.empty()) {
            A2A_LOG(A2A_LOG_LEVEL_ERROR, "HttpCardResolverBuilder::Build invalid parameter.");
            return nullptr;
        }

        return std::make_shared<HttpCardResolver>(std::move(baseUrl), std::move(agentCardPath), httpKwargs);
    } catch (const std::exception& e) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, std::string("exception occured: ") + e.what());
        return nullptr;
    }
}

} // namespace A2A::Client