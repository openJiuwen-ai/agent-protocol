/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_PROTOCOL_VERSION_INTERCEPTOR
#define A2A_PROTOCOL_VERSION_INTERCEPTOR

#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

#include "client/client_call_interceptor.h"

namespace A2A::Client {

class ProtocolVersionInterceptorImpl;

// ProtocolVersionInterceptor: adds A2A-Version header to every request.
class ProtocolVersionInterceptor : public ClientCallInterceptor {
public:
    /**
     * @brief constructor with no parameters; uses a fixed internal version.
     */
    explicit ProtocolVersionInterceptor();

    /**
     * @brief destructor
     */
    ~ProtocolVersionInterceptor() override;

    /**
     * @brief perform intercept with methodName
     *
     * @param[in] methodName method name to intercept
     * @param[in/out] payload data payload
     * @param[out] headers headers
     * @param[in] agentCard agent card related to this intercept
     * @param[in] context client call context
     */
    void Intercept(const std::string& methodName, nlohmann::json& payload,
        std::map<std::string, std::string>& headers, const A2A::AgentCard* agentCard,
        const ClientCallContext* context) override;

private:
    std::unique_ptr<ProtocolVersionInterceptorImpl> impl_;
};

} // namespace A2A::Client

#endif
