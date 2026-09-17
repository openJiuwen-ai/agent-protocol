/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <cstring>

#include "mcp_auth.h"
#include "shared/http_common.h"

namespace Mcp {

BearerTokenProvider::BearerTokenProvider(const std::string& token) : token_(token)
{
}

BearerTokenProvider::~BearerTokenProvider()
{
    if (!token_.empty()) {
        std::memset(token_.data(), 0, token_.size());
        token_.clear();
    }
}

void BearerTokenProvider::SetToken(const std::string& token)
{
    token_ = token;
}

const std::string& BearerTokenProvider::GetToken() const
{
    return token_;
}

void BearerTokenProvider::Apply(std::unordered_map<std::string, std::string>& headers)
{
    if (token_.empty()) {
        return;
    }

    // Defend against HTTP header injection (e.g., CRLF in the token)
    if (token_.find_first_of("\r\n") != std::string::npos) {
        return;
    }

    headers[Http::AUTHORIZATION_HEADER] = "Bearer " + token_;
}

} // namespace Mcp
