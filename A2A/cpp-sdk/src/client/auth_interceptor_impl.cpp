/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "auth_interceptor_impl.h"

namespace a2a::client {

AuthInterceptorImpl::AuthInterceptorImpl(std::shared_ptr<CredentialService> service)
    : credentialService_(std::move(service))
{
}

void AuthInterceptorImpl::Intercept(const std::string& methodName, nlohmann::json& payload,
                                    std::map<std::string, std::string>& headers, const a2a::AgentCard* agentCard,
                                    const ClientCallContext* context)
{
    if (!agentCard || !agentCard->security || !agentCard->securitySchemes) {
        return; // nothing to do
    }

    // Iterate over requirements: each requirement is a map<string, json>
    for (const auto& requirementJson : *agentCard->security) {
        if (!requirementJson.is_object()) {
            continue;
        }

        for (auto it = requirementJson.begin(); it != requirementJson.end(); ++it) {
            const std::string schemeName = it.key();
            auto cred = credentialService_->GetCredentials(schemeName, context);
            if (!cred) {
                continue;
            }

            auto schemesIt = agentCard->securitySchemes->find(schemeName);
            if (schemesIt == agentCard->securitySchemes->end()) {
                continue;
            }

            const SecurityScheme& schemeUnion = schemesIt->second;
            // Inspect variant type
            if (std::holds_alternative<HTTPAuthSecurityScheme>(schemeUnion.v)) {
                const auto& s = std::get<HTTPAuthSecurityScheme>(schemeUnion.v);
                std::string schemeLower = s.scheme;
                std::transform(schemeLower.begin(), schemeLower.end(), schemeLower.begin(), ::tolower);
                if (schemeLower == "bearer") {
                    headers["Authorization"] = std::string("Bearer ") + *cred;
                    return; // applied
                }
            } else if (std::holds_alternative<OAuth2SecurityScheme>(schemeUnion.v) ||
                       std::holds_alternative<OpenIdConnectSecurityScheme>(schemeUnion.v)) {
                headers["Authorization"] = std::string("Bearer ") + *cred;
                return; // applied
            } else if (std::holds_alternative<APIKeySecurityScheme>(schemeUnion.v)) {
                const auto& s = std::get<APIKeySecurityScheme>(schemeUnion.v);
                if (s.in_ == "header") {
                    headers[s.name] = *cred;
                    return; // applied
                }
                // Note: query/cookie not handled
            }
        }
    }
}

} // namespace a2a::client
