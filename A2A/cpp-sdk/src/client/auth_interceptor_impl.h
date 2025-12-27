/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_AUTH_INTERCEPTOR_IMPL
#define A2A_AUTH_INTERCEPTOR_IMPL

#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

#include "utils/credentials.h"
#include "utils/types.h"

namespace a2a::client {

class AuthInterceptorImpl {
public:
    /**
     * @brief constructor
     *
     * @param[in] service credential service
     */
    explicit AuthInterceptorImpl(std::shared_ptr<CredentialService> service);

    /**
     * @brief perform intercept with methodName
     *
     * @param[in] methodName method name to intercept
     * @param[in] payload data payload
     * @param[in] headers headers
     * @param[in] agentCard agent card related to this intercept
     * @param[in] context client call context
     */
    void Intercept(const std::string& methodName, nlohmann::json& payload, std::map<std::string, std::string>& headers,
                   const a2a::AgentCard* agentCard, const ClientCallContext* context);

private:
    std::shared_ptr<CredentialService> credentialService_;
};

} // namespace a2a::client

#endif
