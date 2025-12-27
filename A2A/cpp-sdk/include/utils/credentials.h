/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_CREDENTIALS
#define A2A_CREDENTIALS

#include <map>
#include <memory>
#include <optional>
#include <string>

#include "utils/types.h"

namespace a2a {

// Abstract service for retrieving credentials
class CredentialService {
public:
    virtual ~CredentialService() = default;
    virtual std::optional<std::string> GetCredentials(const std::string& securitySchemeName,
                                                      const a2a::ClientCallContext* context) = 0;
};

// In-memory store keyed by sessionId (from context.state["sessionId"]).
class InMemoryContextCredentialStore : public CredentialService {
public:
    std::optional<std::string> GetCredentials(const std::string& securitySchemeName,
                                              const a2a::ClientCallContext* context) override;

    void SetCredentials(const std::string& sessionId, const std::string& securitySchemeName,
                        const std::string& credential);

private:
    // store[sessionId][scheme] = credential
    std::map<std::string, std::map<std::string, std::string>> store_;
};

} // namespace a2a

#endif
