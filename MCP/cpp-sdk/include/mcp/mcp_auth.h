/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef MCP_AUTH_INCLUDE_H_
#define MCP_AUTH_INCLUDE_H_

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Mcp {

// Interface for authentication providers that attach auth info into requests
class AuthProvider {
public:
    virtual ~AuthProvider() = default;

    // attach auth info into headers before sending request
    virtual void Apply(std::unordered_map<std::string, std::string>& headers) = 0;
};

// A bearer token auth provider
class BearerTokenProvider : public AuthProvider {
public:
    explicit BearerTokenProvider(const std::string& token = "");
    ~BearerTokenProvider() override;

    void SetToken(const std::string& token);
    const std::string& GetToken() const;

    // attach bearer token into headers before sending request
    void Apply(std::unordered_map<std::string, std::string>& headers) override;

private:
    std::string token_; // bearer token string
};
// NOTE: BearerTokenProvider can be further extended to implement OAuth2 authorization flows.

// ------------- Authentication ---------------

// Authentication context after successful authentication, for later use in server/tool-level authorization
struct AuthContext {
    std::optional<std::string> clientId; // client identifier
    std::optional<std::string> scopes; // authorization scopes (space-separated)
};

// Result of an authentication attempt
struct AuthenticationResult {
    bool authenticated{false};
    std::optional<AuthContext> context; // present if authenticated is true
    std::optional<std::string> errorDescription; // present if authenticated is false
};

// Interface for token verification. This may be implemented by introspection, self-contained JWT, etc.
class TokenVerifier {
public:
    virtual ~TokenVerifier() = default;
    virtual AuthenticationResult VerifyToken(const std::string& token) const = 0;
};

// Interface for authenticators
class Authenticator {
public:
    virtual ~Authenticator() = default;
    virtual AuthenticationResult Authenticate(const std::unordered_map<std::string, std::string>& headers) const = 0;
};

// An authenticator that always succeeds
class NoAuthAuthenticator : public Authenticator {
public:
    AuthenticationResult Authenticate(const std::unordered_map<std::string, std::string>& headers) const override;
};

// A bearer token authenticator that uses a token verifier to validate tokens
class BearerTokenAuthenticator : public Authenticator {
public:
    explicit BearerTokenAuthenticator(std::shared_ptr<TokenVerifier> verifier);
    AuthenticationResult Authenticate(const std::unordered_map<std::string, std::string>& headers) const override;

private:
    std::shared_ptr<TokenVerifier> verifier_; // pointer to token verifier used for validating bearer tokens
};

// -------------- Authorization ----------------
// Interface for authorizers
class Authorizer {
public:
    virtual ~Authorizer() = default;
    virtual bool Authorize(const AuthenticationResult& authResult) const = 0;
};

// A scope-based authorizer
class ScopeBasedAuthorizer : public Authorizer {
public:
    /**
     * @brief Construct a scope-based authorizer.
     *
     * @param requiredScopes Space-separated list of required scopes.
     *
     * @throw std::invalid_argument If @p requiredScopes is empty or contains only whitespace.
     */
    explicit ScopeBasedAuthorizer(const std::string& requiredScopes);

    /**
     * @brief Update required scopes.
     *
     * @param scopes Space-separated list of required scopes.
     *
     * @throw std::invalid_argument If @p scopes is empty or contains only whitespace.
     */
    void SetRequiredScopes(const std::string& scopes);

    /**
     * @brief Get the currently required authorization scopes.
     *
     * @return A space-separated string of required scopes.
     */
    std::string GetRequiredScopes() const;

    /**
     * @brief Check whether the authentication result contains all required scopes.
     *
     * @param authResult The authentication result to check.
     * @return Returns true if the authentication result contains all required scopes, otherwise false.
     */
    bool Authorize(const AuthenticationResult& authResult) const override;

private:
    std::unordered_set<std::string> requiredScopes_; // set of required scopes, space-separated
};

} // namespace Mcp

#endif // MCP_AUTH_INCLUDE_H_
