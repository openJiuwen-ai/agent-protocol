/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_AUTH_INTERCEPTOR
#define A2A_AUTH_INTERCEPTOR

#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

#include "client/client_call_interceptor.h"
#include "utils/credentials.h"

namespace a2a::client {

class AuthInterceptorImpl;

// AuthInterceptor: adds authentication details to requests based on AgentCard security schemes.
class AuthInterceptor : public ClientCallInterceptor {
public:
    /**
     * @brief constructor
     *
     * @param[in] service credential service
     */
    explicit AuthInterceptor(std::shared_ptr<CredentialService> service);

    /**
     * @brief destructor
     */
    ~AuthInterceptor();

    /**
     * @brief perform intercept with methodName
     *
     * @param[in] methodName method name to intercept
     * @param[in/out] payload data payload
     * @param[out] headers headers
     * @param[in] agentCard agent card related to this intercept
     * @param[in] context client call context
     */
    void Intercept(const std::string& methodName, nlohmann::json& payload, std::map<std::string, std::string>& headers,
                   const a2a::AgentCard* agentCard, const ClientCallContext* context) override;

private:
    std::unique_ptr<AuthInterceptorImpl> impl_;
};

} // namespace a2a::client

#endif
