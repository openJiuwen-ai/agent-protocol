/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "utils/credentials.h"

namespace a2a {

std::optional<std::string> InMemoryContextCredentialStore::GetCredentials(const std::string& securitySchemeName,
                                                                          const a2a::ClientCallContext* context)
{
    if (!context) {
        return std::nullopt;
    }

    auto it = context->state.find("sessionId");
    if (it == context->state.end()) {
        return std::nullopt;
    }

    const std::string sessionId = it->second.is_string() ? it->second.get<std::string>() : std::string();
    if (sessionId.empty()) {
        return std::nullopt;
    }

    auto sit = store_.find(sessionId);
    if (sit == store_.end()) {
        return std::nullopt;
    }

    auto cit = sit->second.find(securitySchemeName);
    if (cit == sit->second.end()) {
        return std::nullopt;
    }

    return cit->second;
}

void InMemoryContextCredentialStore::SetCredentials(const std::string& sessionId, const std::string& securitySchemeName,
                                                    const std::string& credential)
{
    store_[sessionId][securitySchemeName] = credential;
}

} // namespace a2a
