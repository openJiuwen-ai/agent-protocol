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

class HttpCardResolverBuilder final {
public:
    /**
    * @brief destructor
    */
    ~HttpCardResolverBuilder();

    /**
    * @brief build card resolver
    *
    * @param[in] baseUrl base url
    * @param[in] agentCardPath agent card path
    * @param[in] httpKwargs http args
    * @return A shared pointer to the created card resolver instance
    */
    static std::shared_ptr<A2ACardResolver> Build(std::string baseUrl, std::string agentCardPath,
        const std::map<std::string, std::string>& httpKwargs = {});
};

} // namespace A2A::Client

#endif