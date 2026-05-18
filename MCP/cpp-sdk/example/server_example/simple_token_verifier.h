/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#ifndef SIMPLE_TOKEN_VERIFIER_H_
#define SIMPLE_TOKEN_VERIFIER_H_

#include <string>
#include <unordered_map>

#include "mcp_auth.h"

namespace Mcp {

/**
 * @brief A simple token verifier for demonstration purposes.
 *
 * This verifier maintains a map of valid tokens to their associated scopes.
 * In a production environment, this would typically verify tokens against
 * an OAuth2 authorization server or validate JWT tokens.
 */
class SimpleTokenVerifier : public TokenVerifier {
public:
    /**
     * @brief Construct a SimpleTokenVerifier with predefined tokens.
     *
     * @param tokenScopes Map of token strings to their space-separated scopes.
     *                   Example: {"valid-token" -> "read write"}
     */
    explicit SimpleTokenVerifier(const std::unordered_map<std::string, std::string>& tokenScopes);

    /**
     * @brief Destructor.
     */
    ~SimpleTokenVerifier() override = default;

    /**
     * @brief Verify a token and return authentication result with scopes.
     *
     * @param token The bearer token to verify.
     * @return AuthenticationResult with authenticated=true if token is valid,
     *         authenticated=false otherwise.
     */
    AuthenticationResult VerifyToken(const std::string& token) const override;

    /**
     * @brief Add or update a token with its scopes.
     *
     * @param token The token string.
     * @param scopes Space-separated scopes (e.g., "read write").
     */
    void AddToken(const std::string& token, const std::string& scopes);

    /**
     * @brief Remove a token.
     *
     * @param token The token to remove.
     */
    void RemoveToken(const std::string& token);

private:
    std::unordered_map<std::string, std::string> tokenScopes_;
};

} // namespace Mcp

#endif // SIMPLE_TOKEN_VERIFIER_H_
