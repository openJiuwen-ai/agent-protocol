/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_CLIENT_CALL_INTERCEPTOR
#define A2A_CLIENT_CALL_INTERCEPTOR

#include <map>
#include <nlohmann/json.hpp>
#include <string>

#include "utils/types.h"

namespace a2a::client {

// Client-side call interceptor interface (sync, header/payload mutation)
class ClientCallInterceptor {
public:
    /**
     * @brief destructor
     *
     * @param[in] service credential service
     */
    virtual ~ClientCallInterceptor() = default;

    /**
     * @brief perform intercept with methodName, may mutate payload and headers
     *
     * @param[in] methodName method name to intercept
     * @param[in/out] payload data payload
     * @param[out] headers headers
     * @param[in] agentCard agent card related to this intercept. may be null
     * @param[in] context client call context. may be null
     */
    virtual void Intercept(const std::string& methodName, nlohmann::json& payload,
                           std::map<std::string, std::string>& headers, const a2a::AgentCard* agentCard,
                           const ClientCallContext* context) = 0;
};

} // namespace a2a::client

#endif
