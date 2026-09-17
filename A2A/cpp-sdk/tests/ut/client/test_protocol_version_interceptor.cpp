/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <map>
#include <string>

#include "client/protocol_version_interceptor.h"
#include "http_common.h"

namespace A2A::Client {
namespace {

using ::testing::Eq;

class ProtocolVersionInterceptorTest : public ::testing::Test {
protected:
    ProtocolVersionInterceptor interceptor_;
};

TEST_F(ProtocolVersionInterceptorTest, Intercept_AddsProtocolVersionHeader)
{
    std::string payload = R"({"jsonrpc":"2.0","id":1})";
    std::map<std::string, std::string> headers;

    interceptor_.Intercept("message/send", payload, headers, nullptr, nullptr);

    auto it = headers.find(A2A::Http::K_PROTOCOL_VERSION_HEADER);
    ASSERT_NE(it, headers.end());
    EXPECT_FALSE(it->second.empty());
    EXPECT_EQ(it->second, A2A::DEFAULT_PROTOCOL_VERSION);
}

TEST_F(ProtocolVersionInterceptorTest, Intercept_DoesNotModifyPayload)
{
    std::string payload = R"({"jsonrpc":"2.0","id":1})";
    const std::string originalPayload = payload;
    std::map<std::string, std::string> headers;

    interceptor_.Intercept("message/send", payload, headers, nullptr, nullptr);

    EXPECT_EQ(payload, originalPayload);
}

TEST_F(ProtocolVersionInterceptorTest, Intercept_WithDifferentMethodName_StillAddsProtocolVersionHeader)
{
    std::string payload = R"({"jsonrpc":"2.0","method":"custom"})";
    std::map<std::string, std::string> headers;

    interceptor_.Intercept("custom.method", payload, headers, nullptr, nullptr);

    auto it = headers.find(A2A::Http::K_PROTOCOL_VERSION_HEADER);
    ASSERT_NE(it, headers.end());
    EXPECT_EQ(it->second, A2A::DEFAULT_PROTOCOL_VERSION);
}

TEST_F(ProtocolVersionInterceptorTest, Intercept_OverwritesExistingProtocolVersionHeader)
{
    std::string payload = "{}";
    std::map<std::string, std::string> headers {
        {A2A::Http::K_PROTOCOL_VERSION_HEADER, "old-version"},
        {"Content-Type", "application/json"}
    };

    interceptor_.Intercept("message/send", payload, headers, nullptr, nullptr);

    EXPECT_EQ(headers.at(A2A::Http::K_PROTOCOL_VERSION_HEADER), A2A::DEFAULT_PROTOCOL_VERSION);
    EXPECT_EQ(headers.at("Content-Type"), "application/json");
}

TEST_F(ProtocolVersionInterceptorTest, Intercept_PreservesOtherHeaders)
{
    std::string payload = "{}";
    std::map<std::string, std::string> headers {
        {"Authorization", "Bearer token"},
        {"Content-Type", "application/json"}
    };

    interceptor_.Intercept("message/send", payload, headers, nullptr, nullptr);

    EXPECT_EQ(headers.at("Authorization"), "Bearer token");
    EXPECT_EQ(headers.at("Content-Type"), "application/json");
    EXPECT_EQ(headers.at(A2A::Http::K_PROTOCOL_VERSION_HEADER), A2A::DEFAULT_PROTOCOL_VERSION);
}

} // namespace
} // namespace A2A::Client