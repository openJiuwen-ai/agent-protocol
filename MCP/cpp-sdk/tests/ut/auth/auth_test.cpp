/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <gtest/gtest.h>

#include "mcp_auth.h"
#include "recording_verifier.h"
#include "shared/http_common.h"

#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace Mcp {

TEST(BearerTokenProviderTest, SkipsEmptyToken) {
    Mcp::Http::HttpRequest request;
    BearerTokenProvider provider;  // default empty token
    provider.Apply(request.headers);

    EXPECT_TRUE(request.headers.empty());
}

TEST(BearerTokenProviderTest, SetsAuthorizationHeader) {
    Mcp::Http::HttpRequest request;
    BearerTokenProvider provider("abc123");
    provider.Apply(request.headers);

    ASSERT_EQ(request.headers.count(Http::AUTHORIZATION_HEADER), 1u);
    EXPECT_EQ(request.headers.at(Http::AUTHORIZATION_HEADER), "Bearer abc123");

    provider.SetToken("new-token");
    EXPECT_EQ(provider.GetToken(), "new-token");

    provider.Apply(request.headers);
    EXPECT_EQ(request.headers.at(Http::AUTHORIZATION_HEADER), "Bearer new-token");
}

TEST(NoAuthAuthenticatorTest, AlwaysAuthenticatedWithContext) {
    NoAuthAuthenticator authenticator;
    Mcp::Http::HttpRequest request;

    AuthenticationResult result = authenticator.Authenticate(request.headers);
    EXPECT_TRUE(result.authenticated);
    ASSERT_TRUE(result.context.has_value());
}

TEST(BearerTokenAuthenticatorTest, MissingVerifierFails) {
    BearerTokenAuthenticator authenticator(nullptr);
    Mcp::Http::HttpRequest request;

    AuthenticationResult result = authenticator.Authenticate(request.headers);
    EXPECT_FALSE(result.authenticated);
    EXPECT_EQ(result.errorDescription, "Token verifier not configured");
}

TEST(BearerTokenAuthenticatorTest, MissingHeaderFails) {
    auto verifier = std::make_shared<RecordingVerifier>(AuthenticationResult{});
    BearerTokenAuthenticator authenticator(verifier);
    Mcp::Http::HttpRequest request;

    AuthenticationResult result = authenticator.Authenticate(request.headers);
    EXPECT_FALSE(result.authenticated);
    EXPECT_EQ(result.errorDescription, "Missing Authorization header");
    EXPECT_EQ(verifier->callCount, 0);
}

TEST(BearerTokenAuthenticatorTest, NonBearerSchemeFails) {
    auto verifier = std::make_shared<RecordingVerifier>(AuthenticationResult{});
    BearerTokenAuthenticator authenticator(verifier);
    Mcp::Http::HttpRequest request;
    request.headers[Http::AUTHORIZATION_HEADER] = "Basic abc";

    AuthenticationResult result = authenticator.Authenticate(request.headers);
    EXPECT_FALSE(result.authenticated);
    EXPECT_EQ(result.errorDescription, "Authorization header must use Bearer scheme");
    EXPECT_EQ(verifier->callCount, 0);
}

TEST(BearerTokenAuthenticatorTest, EmptyTokenFails) {
    auto verifier = std::make_shared<RecordingVerifier>(AuthenticationResult{});
    BearerTokenAuthenticator authenticator(verifier);
    Mcp::Http::HttpRequest request;
    request.headers[Http::AUTHORIZATION_HEADER] = "Bearer ";

    AuthenticationResult result = authenticator.Authenticate(request.headers);
    EXPECT_FALSE(result.authenticated);
    EXPECT_EQ(result.errorDescription, "Bearer token is empty");
    EXPECT_EQ(verifier->callCount, 0);
}

TEST(BearerTokenAuthenticatorTest, ForwardsTokenToVerifier) {
    AuthenticationResult verifierResult;
    verifierResult.authenticated = true;
    verifierResult.context = AuthContext{ "client-1", "scope:a scope:b" };

    auto verifier = std::make_shared<RecordingVerifier>(verifierResult);
    BearerTokenAuthenticator authenticator(verifier);
    Mcp::Http::HttpRequest request;
    request.headers[Http::AUTHORIZATION_HEADER] = "Bearer live-token";

    AuthenticationResult result = authenticator.Authenticate(request.headers);

    EXPECT_TRUE(result.authenticated);
    EXPECT_EQ(verifier->callCount, 1);
    EXPECT_EQ(verifier->lastToken, "live-token");
    ASSERT_TRUE(result.context.has_value());
    EXPECT_EQ(result.context->clientId, "client-1");
    EXPECT_EQ(result.context->scopes, "scope:a scope:b");
}

TEST(BearerTokenAuthenticatorTest, AcceptsBearerSchemeCaseInsensitively) {
    AuthenticationResult verifierResult;
    verifierResult.authenticated = true;
    verifierResult.context = AuthContext{ "client-1", std::nullopt };

    auto verifier = std::make_shared<RecordingVerifier>(verifierResult);
    BearerTokenAuthenticator authenticator(verifier);
    Mcp::Http::HttpRequest request;
    request.headers[Http::AUTHORIZATION_HEADER] = "bEaReR MiXeD";

    AuthenticationResult result = authenticator.Authenticate(request.headers);

    EXPECT_TRUE(result.authenticated);
    EXPECT_EQ(verifier->callCount, 1);
    EXPECT_EQ(verifier->lastToken, "MiXeD");
}

TEST(ScopeBasedAuthorizerTest, EmptyRequiredScopesIsDisallowed) {
    EXPECT_THROW(ScopeBasedAuthorizer(""), std::invalid_argument);
}

TEST(ScopeBasedAuthorizerTest, FailsWhenNotAuthenticatedOrMissingContext) {
    ScopeBasedAuthorizer authorizer("alpha");

    AuthenticationResult unauthenticated;
    EXPECT_FALSE(authorizer.Authorize(unauthenticated));

    AuthenticationResult missingCtx;
    missingCtx.authenticated = true;
    EXPECT_FALSE(authorizer.Authorize(missingCtx));
}

TEST(ScopeBasedAuthorizerTest, RequiresScopesPresent) {
    ScopeBasedAuthorizer authorizer("read write");

    AuthenticationResult authResult;
    authResult.authenticated = true;
    authResult.context = AuthContext{};
    authResult.context->scopes = "read write delete";

    EXPECT_TRUE(authorizer.Authorize(authResult));

    authResult.context->scopes = "read";
    EXPECT_FALSE(authorizer.Authorize(authResult));
}

TEST(ScopeBasedAuthorizerTest, UpdatesRequiredScopes) {
    ScopeBasedAuthorizer authorizer("read");
    authorizer.SetRequiredScopes("admin manage");

    std::unordered_set<std::string> required;
    std::istringstream iss(authorizer.GetRequiredScopes());
    std::string scope;
    while (iss >> scope) {
        required.insert(scope);
    }

    EXPECT_EQ(required.size(), 2u);
    EXPECT_NE(required.find("admin"), required.end());
    EXPECT_NE(required.find("manage"), required.end());
}

} // namespace Mcp
