/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#include "simple_token_verifier.h"

#include "mcp_log.h"

namespace Mcp {

SimpleTokenVerifier::SimpleTokenVerifier(const std::unordered_map<std::string, std::string>& tokenScopes)
    : tokenScopes_(tokenScopes)
{
}

AuthenticationResult SimpleTokenVerifier::VerifyToken(const std::string& token) const
{
    AuthenticationResult result;
    result.authenticated = false;

    if (token.empty()) {
        result.errorDescription = "Token is empty";
        return result;
    }

    auto it = tokenScopes_.find(token);
    if (it == tokenScopes_.end()) {
        result.errorDescription = "Invalid token";
        MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Token verification failed: token not found");
        return result;
    }

    // Token is valid, create authentication result with scopes
    result.authenticated = true;
    AuthContext context;
    context.clientId = "client-" + token; // Simple client ID based on token
    context.scopes = it->second; // Use the scopes from the map
    result.context = context;

    MCP_LOG(MCP_LOG_LEVEL_DEBUG, std::string("Token verified successfully with scopes: ") + it->second);
    return result;
}

void SimpleTokenVerifier::AddToken(const std::string& token, const std::string& scopes)
{
    tokenScopes_[token] = scopes;
}

void SimpleTokenVerifier::RemoveToken(const std::string& token)
{
    tokenScopes_.erase(token);
}

} // namespace Mcp
