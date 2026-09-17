/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <stdexcept>

#include "client/client_factory.h"
#include "client/client_call_interceptor.h"
#include "client/client.h"
#include "types.h"

namespace A2A::Client::Test {

using namespace A2A;
using namespace A2A::Client;
using ::testing::_;
using ::testing::Return;
using ::testing::NiceMock;

// Create a basic AgentCard with JSON-RPC and IPC interfaces.
static AgentCard MakeTestAgentCard()
{
    AgentCard card;
    card.name = "Test Agent";
    card.description = "Test Description";
    card.version = "1.0.0";

    AgentInterface jsonrpcInterface;
    jsonrpcInterface.protocolBinding = JSONRPC_TRANSPORT;
    jsonrpcInterface.url = "http://localhost:8080/jsonrpc";
    jsonrpcInterface.protocolVersion = "2.0";

    card.supportedInterfaces = {jsonrpcInterface};

    return card;
}

// Create a ClientConfig with specified transports and preference.
static ClientConfig MakeTestConfig(const std::vector<std::string>& transports, bool useClientPreference)
{
    ClientConfig config;
    config.supportedTransports = transports;
    config.useClientPreference = useClientPreference;
    return config;
}

// Create a test consumer function.
static Consumer MakeTestConsumer()
{
    auto func = [](const std::variant<Message, A2AError, std::pair<Task, std::variant<std::monostate,
        TaskStatusUpdateEvent, TaskArtifactUpdateEvent>>>&, const AgentCard&) {
        // Empty implementation for testing
    };
    return Consumer(func);
}

class MockInterceptor : public ClientCallInterceptor {
public:
    MOCK_METHOD5(Intercept,
        void(const std::string& methodName,
            std::string& payload,
            std::map<std::string, std::string>& headers,
            const AgentCard* agentCard,
            const ClientCallContext* context));
};

class ClientFactoryTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        card = MakeTestAgentCard();
        config = MakeTestConfig({JSONRPC_TRANSPORT}, true);
    }

    AgentCard card;
    ClientConfig config;
    std::vector<Consumer> emptyConsumers;
    std::vector<std::shared_ptr<ClientCallInterceptor>> emptyInterceptors;
};

// ===========================================================================
// Part I – Basic client creation
// ===========================================================================

// Valid card and config should create a client successfully.
TEST_F(ClientFactoryTest, CreateBasicClient)
{
    auto client = ClientFactory::Create(card, config, emptyConsumers, emptyInterceptors);
    EXPECT_NE(client, nullptr);
}

// Creating a client with valid HTTP/JSON-RPC configuration.
TEST_F(ClientFactoryTest, CreateHttpClient)
{
    AgentCard httpCard = card;
    httpCard.supportedInterfaces.clear();

    AgentInterface httpInterface;
    httpInterface.protocolBinding = JSONRPC_TRANSPORT;
    httpInterface.url = "https://api.example.com/a2a";
    httpInterface.protocolVersion = "2.0";
    httpCard.supportedInterfaces.push_back(httpInterface);

    ClientConfig httpConfig = MakeTestConfig({JSONRPC_TRANSPORT}, true);

    auto client = ClientFactory::Create(httpCard, httpConfig, emptyConsumers, emptyInterceptors);
    EXPECT_NE(client, nullptr);
}

// ===========================================================================
// Part II – Parameter validation
// ===========================================================================

// Factory must reject a null interceptor.
TEST_F(ClientFactoryTest, CreateWithNullInterceptor_ReturnsNull)
{
    auto mockInterceptor = std::make_shared<NiceMock<MockInterceptor>>();
    std::vector<std::shared_ptr<ClientCallInterceptor>> interceptors = {mockInterceptor, nullptr};

    auto client = ClientFactory::Create(card, config, emptyConsumers, interceptors);
    EXPECT_EQ(client, nullptr);
}

// Factory must reject an AgentCard with empty supportedInterfaces.
TEST_F(ClientFactoryTest, CreateWithEmptyInterfaces_ReturnsNull)
{
    card.supportedInterfaces.clear();

    auto client = ClientFactory::Create(card, config, emptyConsumers, emptyInterceptors);
    EXPECT_EQ(client, nullptr);
}

// Factory must reject an AgentCard with invalid interface (missing url).
TEST_F(ClientFactoryTest, CreateWithInvalidInterface_MissingUrl_ReturnsNull)
{
    AgentInterface invalidInterface;
    invalidInterface.protocolBinding = JSONRPC_TRANSPORT;
    // url missing
    invalidInterface.protocolVersion = "2.0";
    card.supportedInterfaces = {invalidInterface};

    auto client = ClientFactory::Create(card, config, emptyConsumers, emptyInterceptors);
    EXPECT_EQ(client, nullptr);
}

// Factory must reject an AgentCard with invalid interface (missing protocolBinding).
TEST_F(ClientFactoryTest, CreateWithInvalidInterface_MissingProtocolBinding_ReturnsNull)
{
    AgentInterface invalidInterface;
    // protocolBinding missing
    invalidInterface.url = "http://localhost:8080/jsonrpc";
    invalidInterface.protocolVersion = "2.0";
    card.supportedInterfaces = {invalidInterface};

    auto client = ClientFactory::Create(card, config, emptyConsumers, emptyInterceptors);
    EXPECT_EQ(client, nullptr);
}

// Factory must reject an AgentCard with invalid interface (missing protocolVersion).
TEST_F(ClientFactoryTest, CreateWithInvalidInterface_MissingProtocolVersion_ReturnsNull)
{
    AgentInterface invalidInterface;
    invalidInterface.protocolBinding = JSONRPC_TRANSPORT;
    invalidInterface.url = "http://localhost:8080/jsonrpc";
    // protocolVersion missing

    card.supportedInterfaces = {invalidInterface};

    auto client = ClientFactory::Create(card, config, emptyConsumers, emptyInterceptors);
    EXPECT_EQ(client, nullptr);
}

// Factory must reject a config with empty transport string.
TEST_F(ClientFactoryTest, CreateWithInvalidConfig_EmptyTransport_ReturnsNull)
{
    ClientConfig invalidConfig = MakeTestConfig({JSONRPC_TRANSPORT, ""}, true);

    auto client = ClientFactory::Create(card, invalidConfig, emptyConsumers, emptyInterceptors);
    EXPECT_EQ(client, nullptr);
}

// ===========================================================================
// Part III – Protocol selection logic
// ===========================================================================

// With client preference, factory should choose first matching protocol.
TEST_F(ClientFactoryTest, ProtocolSelection_ClientPreference_FirstMatch)
{
    ClientConfig prefConfig = MakeTestConfig({JSONRPC_TRANSPORT}, true);

    auto client = ClientFactory::Create(card, prefConfig, emptyConsumers, emptyInterceptors);
    EXPECT_NE(client, nullptr);
}

// With server preference, factory should choose first server protocol supported by client.
TEST_F(ClientFactoryTest, ProtocolSelection_ServerPreference_FirstMatch)
{
    ClientConfig serverPrefConfig = MakeTestConfig({JSONRPC_TRANSPORT}, false);

    auto client = ClientFactory::Create(card, serverPrefConfig, emptyConsumers, emptyInterceptors);
    EXPECT_NE(client, nullptr);  // Should choose server's JSONRPC
}

// When no matching protocol exists, factory must return null.
TEST_F(ClientFactoryTest, ProtocolSelection_NoMatchingProtocol_ReturnsNull)
{
    ClientConfig noMatchConfig = MakeTestConfig({"UNKNOWN_PROTOCOL"}, true);

    auto client = ClientFactory::Create(card, noMatchConfig, emptyConsumers, emptyInterceptors);
    EXPECT_EQ(client, nullptr);
}

// When supportedTransports is empty, factory should use default JSONRPC.
TEST_F(ClientFactoryTest, ProtocolSelection_DefaultTransport_WhenConfigEmpty)
{
    ClientConfig defaultConfig = MakeTestConfig({}, true);

    auto client = ClientFactory::Create(card, defaultConfig, emptyConsumers, emptyInterceptors);
    EXPECT_NE(client, nullptr);
}

// Duplicate protocol bindings should be handled gracefully.
TEST_F(ClientFactoryTest, ProtocolSelection_DuplicateBindings_Succeeds)
{
    AgentInterface duplicateInterface;
    duplicateInterface.protocolBinding = JSONRPC_TRANSPORT;
    duplicateInterface.url = "http://localhost:8080/jsonrpc2";
    duplicateInterface.protocolVersion = "2.0";

    card.supportedInterfaces.push_back(duplicateInterface);

    auto client = ClientFactory::Create(card, config, emptyConsumers, emptyInterceptors);
    EXPECT_NE(client, nullptr);  // Should succeed, only warning logged
}

// ===========================================================================
// Part IV – Component integration
// ===========================================================================

// Factory should accept consumers.
TEST_F(ClientFactoryTest, CreateWithConsumers_Succeeds)
{
    std::vector<Consumer> consumers = {MakeTestConsumer()};

    auto client = ClientFactory::Create(card, config, consumers, emptyInterceptors);
    EXPECT_NE(client, nullptr);
}

// Factory should accept multiple consumers.
TEST_F(ClientFactoryTest, CreateWithMultipleConsumers_Succeeds)
{
    std::vector<Consumer> consumers = {
        MakeTestConsumer(),
        MakeTestConsumer(),
        MakeTestConsumer()
    };

    auto client = ClientFactory::Create(card, config, consumers, emptyInterceptors);
    EXPECT_NE(client, nullptr);
}

// Factory should accept interceptors.
TEST_F(ClientFactoryTest, CreateWithInterceptors_Succeeds)
{
    auto mockInterceptor = std::make_shared<NiceMock<MockInterceptor>>();
    std::vector<std::shared_ptr<ClientCallInterceptor>> interceptors = {mockInterceptor};

    auto client = ClientFactory::Create(card, config, emptyConsumers, interceptors);
    EXPECT_NE(client, nullptr);
}

// Factory should accept multiple interceptors.
TEST_F(ClientFactoryTest, CreateWithMultipleInterceptors_Succeeds)
{
    std::vector<std::shared_ptr<ClientCallInterceptor>> interceptors;
    for (int i = 0; i < 5; i++) {
        interceptors.push_back(std::make_shared<NiceMock<MockInterceptor>>());
    }

    auto client = ClientFactory::Create(card, config, emptyConsumers, interceptors);
    EXPECT_NE(client, nullptr);
}

// Factory should accept both consumers and interceptors.
TEST_F(ClientFactoryTest, CreateWithAllComponents_Succeeds)
{
    auto mockInterceptor = std::make_shared<NiceMock<MockInterceptor>>();
    std::vector<std::shared_ptr<ClientCallInterceptor>> interceptors = {mockInterceptor};

    std::vector<Consumer> consumers = {MakeTestConsumer()};

    auto client = ClientFactory::Create(card, config, consumers, interceptors);
    EXPECT_NE(client, nullptr);
}

// ===========================================================================
// Part VII – Edge cases
// ===========================================================================

TEST_F(ClientFactoryTest, CreateWithNullTransport_ReturnsNull)
{
    auto client = ClientFactory::Create(card, config, nullptr, emptyConsumers);
    EXPECT_EQ(client, nullptr);
}

// Server preference with no matching protocol should return null.
TEST_F(ClientFactoryTest, ServerPreference_NoMatch_ReturnsNull)
{
    ClientConfig serverPrefConfig = MakeTestConfig({"UNSUPPORTED_TRANSPORT"}, false);

    // Card only supports JSONRPC
    card.supportedInterfaces.clear();
    AgentInterface jsonrpcInterface;
    jsonrpcInterface.protocolBinding = JSONRPC_TRANSPORT;
    jsonrpcInterface.url = "http://localhost:8080/jsonrpc";
    jsonrpcInterface.protocolVersion = "2.0";
    card.supportedInterfaces.push_back(jsonrpcInterface);

    auto client = ClientFactory::Create(card, serverPrefConfig, emptyConsumers, emptyInterceptors);
    EXPECT_EQ(client, nullptr);
}

// Unsupported transport type should return null.
TEST_F(ClientFactoryTest, UnsupportedTransport_ReturnsNull)
{
    AgentInterface unsupportedInterface;
    unsupportedInterface.protocolBinding = "UNSUPPORTED";
    unsupportedInterface.url = "http://localhost:8080/unsupported";
    unsupportedInterface.protocolVersion = "1.0";
    card.supportedInterfaces = {unsupportedInterface};

    ClientConfig unsupportedConfig = MakeTestConfig({"UNSUPPORTED"}, true);

    auto client = ClientFactory::Create(card, unsupportedConfig, emptyConsumers, emptyInterceptors);
    EXPECT_EQ(client, nullptr);
}

// Empty config should still work (default transport).
TEST_F(ClientFactoryTest, EmptyConfig_Succeeds)
{
    ClientConfig emptyConfig;

    auto client = ClientFactory::Create(card, emptyConfig, emptyConsumers, emptyInterceptors);
    EXPECT_NE(client, nullptr);
}

TEST_F(ClientFactoryTest, ServerPreference_WithUnsupportedFirst_FallsBack)
{
    card.supportedInterfaces.clear();

    AgentInterface unsupportedInterface;
    unsupportedInterface.protocolBinding = "UNSUPPORTED";
    unsupportedInterface.url = "unsupported://test";
    unsupportedInterface.protocolVersion = "1.0";

    AgentInterface jsonrpcInterface;
    jsonrpcInterface.protocolBinding = JSONRPC_TRANSPORT;
    jsonrpcInterface.url = "http://localhost:8080/jsonrpc";
    jsonrpcInterface.protocolVersion = "2.0";

    card.supportedInterfaces = {unsupportedInterface, jsonrpcInterface};
    ClientConfig serverPrefConfig = MakeTestConfig({JSONRPC_TRANSPORT}, false);

    auto client = ClientFactory::Create(card, serverPrefConfig, emptyConsumers, emptyInterceptors);
    EXPECT_NE(client, nullptr);
}

}