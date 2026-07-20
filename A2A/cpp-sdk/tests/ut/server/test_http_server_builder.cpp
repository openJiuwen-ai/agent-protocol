/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>

#include "server/http_server_builder.h"
#include "server/agent_executor.h"
#include "server/task_store.h"
#include "types.h"

using namespace A2A;
using namespace A2A::Server;

namespace {

class DummyAgentExecutor final : public AgentExecutor {
public:
    void Execute(std::shared_ptr<RequestContext>, std::shared_ptr<TaskUpdater>) override
    {
    }

    void Cancel(std::shared_ptr<RequestContext>, std::shared_ptr<TaskUpdater>) override
    {
    }
};

class DummyTaskStore final : public TaskStore {
public:
    void Save(const Task&, std::shared_ptr<ServerCallContext>) override
    {
    }

    std::shared_ptr<Task> Get(const std::string&, std::shared_ptr<ServerCallContext>) override
    {
        return nullptr;
    }

    void Delete(const std::string&, std::shared_ptr<ServerCallContext>) override
    {
    }
};

AgentCard MakeAgentCard(const std::string& url, const std::string& protocolBinding = "http")
{
    AgentCard card;
    AgentInterface iface;
    iface.url = url;
    iface.protocolBinding = protocolBinding;
    card.supportedInterfaces.push_back(iface);
    return card;
}

AgentCard MakeExtendedAgentCard()
{
    AgentCard card;
    AgentInterface iface;
    iface.url = "http://127.0.0.1:8080/jsonrpc";
    iface.protocolBinding = "http";
    card.supportedInterfaces.push_back(iface);
    return card;
}

HttpConfig MakeHttpConfig()
{
    HttpConfig cfg;
    cfg.ip = "127.0.0.1";
    cfg.port = 8080;
    cfg.ioThreadNum = 1;
    return cfg;
}

class HttpServerBuilderTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        config = MakeHttpConfig();
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