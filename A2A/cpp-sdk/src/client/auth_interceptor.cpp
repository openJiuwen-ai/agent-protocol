/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "auth_interceptor_impl.h"
#include "client/auth_interceptor.h"

namespace a2a::client {

AuthInterceptor::AuthInterceptor(std::shared_ptr<CredentialService> service)
    : impl_(std::make_unique<AuthInterceptorImpl>(service))
{
}

AuthInterceptor::~AuthInterceptor() = default;

void AuthInterceptor::Intercept(const std::string& methodName, nlohmann::json& payload,
                                std::map<std::string, std::string>& headers, const AgentCard* agentCard,
                                const ClientCallContext* context)
{
    impl_->Intercept(methodName, payload, headers, agentCard, context);
}

} // namespace a2a::client