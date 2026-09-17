/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "protocol_version_interceptor_impl.h"

#include "common_types.h"
#include "http_common.h"

namespace A2A::Client {

ProtocolVersionInterceptorImpl::ProtocolVersionInterceptorImpl() : version_(A2A::DEFAULT_PROTOCOL_VERSION)
{
}

ProtocolVersionInterceptorImpl::~ProtocolVersionInterceptorImpl() = default;

void ProtocolVersionInterceptorImpl::Intercept(const std::string& /* methodName */, std::string& /* payload */,
    std::map<std::string, std::string>& headers, const A2A::AgentCard* /* agentCard */,
    const ClientCallContext* /* context */) const
{
    if (!version_.empty()) {
        headers[A2A::Http::K_PROTOCOL_VERSION_HEADER] = version_;
    }
}

} // namespace A2A::Client