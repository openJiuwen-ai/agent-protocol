/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef MCP_TEST_AUTH_RECORDING_VERIFIER_H
#define MCP_TEST_AUTH_RECORDING_VERIFIER_H

#include "mcp_auth.h"

namespace Mcp {

class RecordingVerifier : public TokenVerifier {
public:
    explicit RecordingVerifier(AuthenticationResult toReturn)
        : returnValue_(std::move(toReturn)) {}

    AuthenticationResult VerifyToken(const std::string& token) const override {
        lastToken = token;
        ++callCount;
        return returnValue_;
    }

    mutable std::string lastToken;
    mutable int callCount{0};

private:
    AuthenticationResult returnValue_;
};

} // namespace Mcp

#endif // MCP_TEST_AUTH_RECORDING_VERIFIER_H
