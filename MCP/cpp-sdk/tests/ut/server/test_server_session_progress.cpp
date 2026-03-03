/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */
#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>

#include "server/server_session.h"
#include "shared/jsonrpc.h"
#include "transport/transport.h"

using namespace Mcp;

namespace {

class CapturingServerTransport final : public ServerTransport {
public:
    CapturingServerTransport() = default;
    ~CapturingServerTransport() override = default;

    void SetCallback(std::shared_ptr<TransportCallback> callback) override { callback_ = std::move(callback); }
    void Listen() override {}
    void Terminate() override {}

    void SendMessage(const JSONRPCMessage& message, RequestContext& ctx) override
    {
        (void)ctx;
        // For requests/notifications serialization does not require ctx.method, but pass it anyway.
        lastSerialized = SerializeJSONRPCMessage(message, ctx.method);
        sendCount++;
    }

    void HandleRequest(const Http::HttpRequest& request, RequestContext& ctx) override
    {
        (void)request;
        (void)ctx;
    }

    std::shared_ptr<TransportCallback> callback_;
    std::string lastSerialized;
    int sendCount{0};
};

// Test constants
constexpr int64_t TEST_PROGRESS_TOKEN_INT = 12345;
constexpr double TEST_PROGRESS_VALUE = 0.75;
constexpr double TEST_PROGRESS_COMPLETE = 1.0;
constexpr double TEST_PROGRESS_HALF = 0.5;
constexpr double TEST_PROGRESS_PARTIAL = 0.3;
constexpr double TEST_PROGRESS_START = 0.1;

class ServerSessionProgressTest : public ::testing::Test {
protected:
    ServerSessionProgressTest() = default;
    ~ServerSessionProgressTest() override = default;

    void SetUp() override
    {
        transport_ = std::make_shared<CapturingServerTransport>();
        session_ = std::make_shared<ServerSession>(transport_);
        ctx_.sessionId = "ut";
        ctx_.connectionId = 1;
    }

    void InitializeSession()
    {
        ClientCapabilities caps;

        JSONRPCRequest initRpc;
        initRpc.jsonrpc_ = JSONRPC_VERSION;
        initRpc.id_ = 1;
        initRpc.method_ = "initialize";
        initRpc.request_ = std::make_unique<InitializeRequest>("ut-client", "0.0.0", caps);
        initRpc.request_->method_ = "initialize";

        JSONRPCMessage initMsg{std::in_place_type<JSONRPCRequest>, std::move(initRpc)};
        session_->OnTransportMessage(initMsg, ctx_);

        JSONRPCMessage initializedMsg{std::in_place_type<JSONRPCNotification>};
        auto& notif = std::get<JSONRPCNotification>(initializedMsg);
        notif.jsonrpc_ = JSONRPC_VERSION;
        notif.method_ = "notifications/initialized";
        notif.notification_ = std::make_unique<InitializedNotification>();
        notif.notification_->method_ = "notifications/initialized";
        session_->OnTransportMessage(initializedMsg, ctx_);

        // Clear transport capture from initialization response.
        transport_->sendCount = 0;
        transport_->lastSerialized.clear();
    }

    std::shared_ptr<CapturingServerTransport> transport_;
    std::shared_ptr<ServerSession> session_;
    RequestContext ctx_;
};

} // namespace

// Test SendProgressNotification with string token
TEST_F(ServerSessionProgressTest, SendProgressNotification_WithStringToken_SendsNotification)
{
    InitializeSession();

    session_->SendProgressNotification(
        std::string("my-task-123"),
        TEST_PROGRESS_HALF,
        TEST_PROGRESS_COMPLETE,
        std::optional<std::string>("50% complete"));

    EXPECT_EQ(transport_->sendCount, 1);
    auto j = nlohmann::json::parse(transport_->lastSerialized);
    EXPECT_EQ(j.value("method", ""), "notifications/progress");
    ASSERT_TRUE(j.contains("params"));
    EXPECT_EQ(j.at("params").value("progressToken", std::string()), "my-task-123");
    EXPECT_DOUBLE_EQ(j.at("params").value("progress", 0.0), TEST_PROGRESS_HALF);
    EXPECT_DOUBLE_EQ(j.at("params").value("total", 0.0), TEST_PROGRESS_COMPLETE);
    EXPECT_EQ(j.at("params").value("message", std::string()), "50% complete");
}

// Test SendProgressNotification with int64_t token
TEST_F(ServerSessionProgressTest, SendProgressNotification_WithInt64Token_SendsNotification)
{
    InitializeSession();

    session_->SendProgressNotification(ProgressToken(static_cast<int64_t>(TEST_PROGRESS_TOKEN_INT)),
                                       TEST_PROGRESS_VALUE, std::nullopt, std::nullopt);

    EXPECT_EQ(transport_->sendCount, 1);
    auto j = nlohmann::json::parse(transport_->lastSerialized);
    EXPECT_EQ(j.value("method", ""), "notifications/progress");
    ASSERT_TRUE(j.contains("params"));
    EXPECT_TRUE(j.at("params").at("progressToken").is_number_integer());
    EXPECT_EQ(j.at("params").at("progressToken").get<int64_t>(), TEST_PROGRESS_TOKEN_INT);
    EXPECT_DOUBLE_EQ(j.at("params").value("progress", 0.0), TEST_PROGRESS_VALUE);
    EXPECT_FALSE(j.at("params").contains("total"));
    EXPECT_FALSE(j.at("params").contains("message"));
}

// Test SendProgressNotification with only progress (no total or message)
TEST_F(ServerSessionProgressTest, SendProgressNotification_ProgressOnly_SendsNotification)
{
    InitializeSession();

    session_->SendProgressNotification(std::string("task-456"), TEST_PROGRESS_PARTIAL, std::nullopt, std::nullopt);

    EXPECT_EQ(transport_->sendCount, 1);
    auto j = nlohmann::json::parse(transport_->lastSerialized);
    EXPECT_EQ(j.value("method", ""), "notifications/progress");
    ASSERT_TRUE(j.contains("params"));
    EXPECT_EQ(j.at("params").value("progressToken", std::string()), "task-456");
    EXPECT_DOUBLE_EQ(j.at("params").value("progress", 0.0), TEST_PROGRESS_PARTIAL);
    EXPECT_FALSE(j.at("params").contains("total"));
    EXPECT_FALSE(j.at("params").contains("message"));
}

// Test SendProgressNotification with null transport (should not crash)
TEST_F(ServerSessionProgressTest, SendProgressNotification_NullTransport_NoCrash)
{
    // Create session with null transport
    auto nullSession = std::make_shared<ServerSession>(nullptr);
    
    // This should not crash
    EXPECT_NO_THROW(nullSession->SendProgressNotification(
        std::string("test"),
        TEST_PROGRESS_HALF,
        TEST_PROGRESS_COMPLETE,
        std::optional<std::string>("test")));
}

// Test SendProgressNotification before initialization (should still work)
TEST_F(ServerSessionProgressTest, SendProgressNotification_BeforeInitialization_SendsNotification)
{
    // Don't call InitializeSession()
    
    session_->SendProgressNotification(
        std::string("early-task"),
        TEST_PROGRESS_START,
        std::nullopt,
        std::optional<std::string>("starting"));

    EXPECT_EQ(transport_->sendCount, 1);
    auto j = nlohmann::json::parse(transport_->lastSerialized);
    EXPECT_EQ(j.value("method", ""), "notifications/progress");
    ASSERT_TRUE(j.contains("params"));
    EXPECT_EQ(j.at("params").value("progressToken", std::string()), "early-task");
    EXPECT_DOUBLE_EQ(j.at("params").value("progress", 0.0), TEST_PROGRESS_START);
    EXPECT_FALSE(j.at("params").contains("total"));
    EXPECT_EQ(j.at("params").value("message", std::string()), "starting");
}