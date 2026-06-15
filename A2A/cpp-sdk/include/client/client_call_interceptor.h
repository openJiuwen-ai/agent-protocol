/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_CLIENT_CALL_INTERCEPTOR
#define A2A_CLIENT_CALL_INTERCEPTOR

#include <string>
#include <map>

#include "types.h"

namespace A2A::Client {

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
    virtual void Intercept(const std::string& methodName, std::string& payload,
        std::map<std::string, std::string>& headers, const A2A::AgentCard* agentCard,
        const ClientCallContext* context) = 0;
};

} // namespace A2A::Client

#endif