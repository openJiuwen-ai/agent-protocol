/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "client/protocol_version_interceptor.h"
#include "protocol_version_interceptor_impl.h"

namespace A2A::Client {

ProtocolVersionInterceptor::ProtocolVersionInterceptor()
    : impl_(std::make_unique<ProtocolVersionInterceptorImpl>())
{
}

ProtocolVersionInterceptor::~ProtocolVersionInterceptor() = default;

void ProtocolVersionInterceptor::Intercept(const std::string& methodName, std::string& payload,
    std::map<std::string, std::string>& headers,
    const A2A::AgentCard* agentCard, const ClientCallContext* context)
{
    impl_->Intercept(methodName, payload, headers, agentCard, context);
}

} // namespace A2A::Client