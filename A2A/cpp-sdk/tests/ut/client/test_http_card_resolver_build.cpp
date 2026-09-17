/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <string>

#include "client/http_card_resolver_builder.h"

namespace A2A::Client {
namespace {

class HttpCardResolverBuilderTest : public ::testing::Test {
public:
    HttpCardResolverBuilder builder_;
};

TEST_F(HttpCardResolverBuilderTest, Build_BaseUrlEmpty_ReturnsNullptr)
{
    std::map<std::string, std::string> httpKwargs {
        {"timeout", "10"},
        {"header", "application/json"}
    };

    auto resolver = builder_.Build("", "/.well-known/agent.json", httpKwargs);

    EXPECT_EQ(resolver, nullptr);
}

TEST_F(HttpCardResolverBuilderTest, Build_AgentCardPathEmpty_ReturnsNullptr)
{
    std::map<std::string, std::string> httpKwargs {
        {"timeout", "10"}
    };

    auto resolver = builder_.Build("https://example.com", "", httpKwargs);

    EXPECT_EQ(resolver, nullptr);
}

TEST_F(HttpCardResolverBuilderTest, Build_BaseUrlAndAgentCardPathEmpty_ReturnsNullptr)
{
    std::map<std::string, std::string> httpKwargs;

    auto resolver = builder_.Build("", "", httpKwargs);

    EXPECT_EQ(resolver, nullptr);
}

TEST_F(HttpCardResolverBuilderTest, Build_BaseUrlAndAgentCardPathValid_ReturnsResolver)
{
    std::map<std::string, std::string> httpKwargs {
        {"timeout", "10"},
        {"verify", "true"}
    };

    auto resolver = builder_.Build("https://example.com", "/.well-known/agent.json", httpKwargs);

    ASSERT_NE(resolver, nullptr);
}

} // namespace
} // namespace A2A::Client