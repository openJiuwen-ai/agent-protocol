/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_PROTOCOL_VERSION_INTERCEPTOR_IMPL
#define A2A_PROTOCOL_VERSION_INTERCEPTOR_IMPL

#include <map>
#include <nlohmann/json.hpp>
#include <string>

#include "types.h"

namespace A2A::Client {

class ProtocolVersionInterceptorImpl {
public:
    /**
    * @brief constructor with no parameters; uses a fixed internal version.
    */
    explicit ProtocolVersionInterceptorImpl();

    /**
    * @brief destructor
    */
    ~ProtocolVersionInterceptorImpl();

    /**
    * @brief perform intercept with methodName
    *
    * @param[in] methodName method name to intercept
    * @param[in] payload data payload
    * @param[in] headers headers
    * @param[in] agentCard agent card related to this intercept
    * @param[in] context client call context
    */
    void Intercept(const std::string& methodName, std::string& payload, std::map<std::string, std::string>& headers,
                    const A2A::AgentCard* agentCard, const ClientCallContext* context) const;

private:
    std::string version_;
};

} // namespace A2A::Client

#endif