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

class ServerSessionListRootsTest : public ::testing::Test {
protected:
    ServerSessionListRootsTest() = default;
    ~ServerSessionListRootsTest() override = default;

    void SetUp() override
    {
        transport_ = std::make_shared<CapturingServerTransport>();
        session_ = std::make_shared<ServerSession>(transport_);
        ctx_.sessionId = "ut";
        ctx_.connectionId = 1;
    }

    void InitializeSessionWithRootsCapability()
    {
        ClientCapabilities caps;
        caps.roots = RootsCapability{.listChanged = false};

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

    void InitializeSessionWithoutRootsCapability()
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

TEST_F(ServerSessionListRootsTest, ListRootsSendsRequestAndCompletesFuture)
{
    InitializeSessionWithRootsCapability();

    auto fut = session_->ListRoots();

    EXPECT_EQ(transport_->sendCount, 1);
    auto j = nlohmann::json::parse(transport_->lastSerialized);
    EXPECT_EQ(j.value("method", ""), "roots/list");
    const int64_t reqId = j.value("id", 0);
    EXPECT_GT(reqId, 0);

    // Simulate the client's response.
    ListRootsResult result;
    Root r;
    r.uri = "file:///tmp";
    r.name = std::optional<std::string>("tmp");
    result.roots.push_back(std::move(r));

    JSONRPCResponse resp;
    resp.jsonrpc_ = JSONRPC_VERSION;
    resp.id_ = reqId;
    resp.result_ = std::make_shared<ListRootsResult>(std::move(result));
    JSONRPCMessage respMsg{std::in_place_type<JSONRPCResponse>, std::move(resp)};

    session_->OnTransportMessage(respMsg, ctx_);

    auto rootsRes = fut.get();
    ASSERT_TRUE(rootsRes != nullptr);
    ASSERT_EQ(rootsRes->roots.size(), 1u);
    EXPECT_EQ(rootsRes->roots[0].uri, "file:///tmp");
}

TEST_F(ServerSessionListRootsTest, ListRootsThrowsIfClientDidNotAdvertiseRootsCapability)
{
    InitializeSessionWithoutRootsCapability();

    EXPECT_EQ(transport_->sendCount, 0);
    EXPECT_THROW(session_->ListRoots(), std::runtime_error);
    EXPECT_EQ(transport_->sendCount, 0);
}
