/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_CLIENT_CALL_INTERCEPTOR
#define A2A_CLIENT_CALL_INTERCEPTOR

#include <string>
#include <map>

#include "types.h"

namespace A2A::Client {

/**
 * @brief Client-side request interceptor for header and payload mutation.
 * @note 同步拦截器，在请求发出前修改 payload 与 headers。
 */
class ClientCallInterceptor {
public:
    /** @brief Virtual destructor. */
    virtual ~ClientCallInterceptor() = default;

    /**
     * @brief Intercept an outbound RPC call; may mutate payload and headers.
     * @param[in]     methodName JSON-RPC method name.
     * @param[in,out] payload    Serialized request body.
     * @param[out]    headers    HTTP / transport headers to send.
     * @param[in]     agentCard  Agent card for this call; may be nullptr.
     * @param[in]     context    Per-request client context; may be nullptr.
     */
    virtual void Intercept(const std::string& methodName, std::string& payload,
        std::map<std::string, std::string>& headers, const A2A::AgentCard* agentCard,
        const ClientCallContext* context) = 0;
};

} // namespace A2A::Client

#endif
