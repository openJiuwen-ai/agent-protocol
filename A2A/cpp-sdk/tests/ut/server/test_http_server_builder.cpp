/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>

#include "mock_agent_executor.h"
#include "mock_task_store.h"
#include "test_agent_card.h"
#include "server/http_server_builder.h"
#include "types.h"

using namespace A2A;
using namespace A2A::Server;
using namespace A2A::Test;

namespace {

AgentCard MakeExtendedAgentCard()
{
    return MakeAgentCard("http://127.0.0.1:0/jsonrpc", "JSONRPC");
}

class HttpServerBuilderTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        config = MakeHttpConfig("127.0.0.1", 0, 1);
        extendedAgentCard = MakeExtendedAgentCard();
        executor = std::make_shared<DummyAgentExecutor>();
        taskStore = std::make_shared<DummyTaskStore>();
    }

    HttpServerBuilder builder;
    HttpConfig config;
    AgentCard extendedAgentCard;
    std::shared_ptr<AgentExecutor> executor;
    std::shared_ptr<TaskStore> taskStore;
};

} // namespace

TEST_F(HttpServerBuilderTest, Build_WhenSupportedInterfacesEmpty_Throws)
{
    AgentCard agentCard;

    EXPECT_THROW(
        builder.Build(config, agentCard, extendedAgentCard, executor, taskStore),
        std::runtime_error);
}

TEST_F(HttpServerBuilderTest, Build_WhenPrimaryUrlEmpty_Throws)
{
    AgentCard agentCard = MakeAgentCard("");

    EXPECT_THROW(
        builder.Build(config, agentCard, extendedAgentCard, executor, taskStore),
        std::runtime_error);
}

TEST_F(HttpServerBuilderTest, Build_WhenUrlPortIsZero_Throws)
{
    AgentCard agentCard = MakeAgentCard("http://127.0.0.1:0/jsonrpc");

    EXPECT_THROW(
        builder.Build(config, agentCard, extendedAgentCard, executor, taskStore),
        std::runtime_error);
}

TEST_F(HttpServerBuilderTest, Build_WhenUrlPortExceedsMax_Throws)
{
    AgentCard agentCard = MakeAgentCard("http://127.0.0.1:65536/jsonrpc");

    EXPECT_THROW(
        builder.Build(config, agentCard, extendedAgentCard, executor, taskStore),
        std::runtime_error);
}

TEST_F(HttpServerBuilderTest, Build_WhenUrlHasValidPortAndPath_ReturnsServer)
{
    AgentCard agentCard = MakeAgentCard("http://127.0.0.1:9000/custom-path");

    auto server = builder.Build(config, agentCard, extendedAgentCard, executor, taskStore);

    ASSERT_NE(server, nullptr);
}

TEST_F(HttpServerBuilderTest, Build_WhenUrlHasValidPortWithoutPath_ReturnsServer)
{
    AgentCard agentCard = MakeAgentCard("http://127.0.0.1:9000");

    auto server = builder.Build(config, agentCard, extendedAgentCard, executor, taskStore);

    ASSERT_NE(server, nullptr);
}

TEST_F(HttpServerBuilderTest, Build_WhenUrlHasNoSchemeButValidHostPortPath_ReturnsServer)
{
    AgentCard agentCard = MakeAgentCard("127.0.0.1:9000/jsonrpc-alt");

    auto server = builder.Build(config, agentCard, extendedAgentCard, executor, taskStore);

    ASSERT_NE(server, nullptr);
}

TEST_F(HttpServerBuilderTest, Build_WhenUrlDoesNotMatchRegexButHasSlash_UsesFallbackAndReturnsServer)
{
    AgentCard agentCard = MakeAgentCard("not_a_standard_url/abc");

    auto server = builder.Build(config, agentCard, extendedAgentCard, executor, taskStore);

    ASSERT_NE(server, nullptr);
}

TEST_F(HttpServerBuilderTest, Build_WhenUrlDoesNotMatchRegexAndHasNoSlash_UsesDefaultEndpointAndReturnsServer)
{
    AgentCard agentCard = MakeAgentCard("not_a_standard_url");

    auto server = builder.Build(config, agentCard, extendedAgentCard, executor, taskStore);

    ASSERT_NE(server, nullptr);
}

TEST_F(HttpServerBuilderTest, Build_DoesNotMutateOriginalAgentCardProtocolBinding)
{
    AgentCard agentCard = MakeAgentCard("http://127.0.0.1:9000/jsonrpc", "something-else");
    const std::string originalProtocol = agentCard.supportedInterfaces[0].protocolBinding;

    auto server = builder.Build(config, agentCard, extendedAgentCard, executor, taskStore);

    ASSERT_NE(server, nullptr);
    EXPECT_EQ(agentCard.supportedInterfaces[0].protocolBinding, originalProtocol);
}

TEST_F(HttpServerBuilderTest, Build_AcceptsNullExecutor)
{
    AgentCard agentCard = MakeAgentCard("http://127.0.0.1:9000/jsonrpc");

    auto server = builder.Build(config, agentCard, extendedAgentCard, nullptr, taskStore);

    ASSERT_NE(server, nullptr);
}

TEST_F(HttpServerBuilderTest, Build_AcceptsNullTaskStore)
{
    AgentCard agentCard = MakeAgentCard("http://127.0.0.1:9000/jsonrpc");

    auto server = builder.Build(config, agentCard, extendedAgentCard, executor, nullptr);

    ASSERT_NE(server, nullptr);
}