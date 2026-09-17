/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <cctype>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#include "mcp_auth.h"
#include "shared/http_common.h"

namespace Mcp {

AuthenticationResult NoAuthAuthenticator::Authenticate(const std::unordered_map<std::string, std::string>& headers)
    const
{
    AuthenticationResult result;
    result.authenticated = true;
    result.context = AuthContext{};
    return result;
}

BearerTokenAuthenticator::BearerTokenAuthenticator(std::shared_ptr<TokenVerifier> verifier)
    : verifier_(std::move(verifier))
{
}

AuthenticationResult BearerTokenAuthenticator::Authenticate(
    const std::unordered_map<std::string, std::string>& headers) const
{
    AuthenticationResult result;
    result.authenticated = false;

    if (!verifier_) {
        result.errorDescription = "Token verifier not configured";
        return result;
    }

    auto authHeader = headers.find(Http::AUTHORIZATION_HEADER);
    if (authHeader == headers.end()) {
        result.errorDescription = "Missing Authorization header";
        return result;
    }

    const std::string kBearerPrefix = "bearer ";
    const std::string& value = authHeader->second;

    if (value.size() < kBearerPrefix.size()) {
        result.errorDescription = "Authorization header must use Bearer scheme";
        return result;
    }

    for (std::size_t i = 0; i < kBearerPrefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(value[i])) != kBearerPrefix[i]) {
            result.errorDescription = "Authorization header must use Bearer scheme";
            return result;
        }
    }

    std::string tokenPart = value.substr(kBearerPrefix.size());

    // Skip extra leading spaces after the scheme
    const auto firstNonSpace = tokenPart.find_first_not_of(" \t");
    if (firstNonSpace == std::string::npos) {
        result.errorDescription = "Bearer token is empty";
        return result;
    }

    std::string token = tokenPart.substr(firstNonSpace);
    return verifier_->VerifyToken(token);
}

ScopeBasedAuthorizer::ScopeBasedAuthorizer(const std::string& requiredScopes)
{
    SetRequiredScopes(requiredScopes);
}

void ScopeBasedAuthorizer::SetRequiredScopes(const std::string& scopes)
{
    requiredScopes_.clear();
    std::istringstream iss(scopes);
    std::string scope;
    while (iss >> scope) {
        requiredScopes_.insert(scope);
    }

    if (requiredScopes_.empty()) {
        throw std::invalid_argument("requiredScopes must not be empty");
    }
}

std::string ScopeBasedAuthorizer::GetRequiredScopes() const
{
    std::ostringstream joined;

    bool first = true;
    for (const auto& scope : requiredScopes_) {
        if (!first) {
            joined << ' ';
        }
        joined << scope;
        first = false;
    }

    return joined.str();
}

bool ScopeBasedAuthorizer::Authorize(const AuthenticationResult& authResult) const
{
    if (!authResult.authenticated) {
        return false;
    }

    if (!authResult.context.has_value()) {
        return false;
    }

    const auto& ctx = authResult.context.value();
    if (!ctx.scopes.has_value()) {
        return false;
    }

    std::unordered_set<std::string> providedScopes;
    std::istringstream iss(*ctx.scopes);
    std::string scope;
    while (iss >> scope) {
        providedScopes.insert(scope);
    }

    if (providedScopes.empty()) {
        return false;
    }

    for (const auto& requiredScope : requiredScopes_) {
        if (providedScopes.find(requiredScope) == providedScopes.end()) {
            return false;
        }
    }

    return true;
}

} // namespace Mcp
