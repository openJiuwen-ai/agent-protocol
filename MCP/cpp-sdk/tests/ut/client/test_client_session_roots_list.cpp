/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */
#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>

#include "client/client_session.h"
#include "shared/jsonrpc.h"
#include "transport/transport.h"

using namespace Mcp;

namespace {

class TestClientSessionRootsListTransport final : public ClientTransport {
public:
    TestClientSessionRootsListTransport() = default;
    ~TestClientSessionRootsListTransport() override = default;

    void SetCallback(std::shared_ptr<TransportCallback> callback) override
    {
        callback_ = std::move(callback);
    }

    void Connect() override {}
    void Terminate() override {}

    void SendMessage(const JSONRPCMessage& message, std::optional<std::string> method = std::nullopt) override
    {
        // JSONRPCMessage is not copyable (it contains unique_ptr payloads), so capture
        // the serialized form and re-parse into an owned variant.
        lastMethod = method;
        lastSerialized = SerializeJSONRPCMessage(message, method);

        std::string deserializeMethod = method.value_or("");
        if (deserializeMethod.empty()) {
            try {
                const auto j = nlohmann::json::parse(lastSerialized);
                deserializeMethod = j.value("method", "");
            } catch (...) {
                deserializeMethod.clear();
            }
        }

        auto parsed = DeserializeJSONRPCMessage(lastSerialized, deserializeMethod);
        lastMessage = std::move(parsed);
        sendCount++;
    }

    std::shared_ptr<TransportCallback> callback_;
    JSONRPCMessage lastMessage{std::in_place_type<JSONRPCError>};
    std::optional<std::string> lastMethod;
    std::string lastSerialized;
    int sendCount{0};
};

class ClientSessionRootsListTest : public ::testing::Test {
protected:
    ClientSessionRootsListTest() = default;
    ~ClientSessionRootsListTest() override = default;

    void SetUp() override
    {
        transport_ = std::make_shared<TestClientSessionRootsListTransport>();
        session_ = std::make_unique<ClientSession>(transport_);
    }

    std::shared_ptr<TestClientSessionRootsListTransport> transport_;
    std::unique_ptr<ClientSession> session_;
};

} // namespace

TEST_F(ClientSessionRootsListTest, IncomingRootsListInvokesCallbackAndReplies)
{
    std::atomic<int> cbCount{0};
    session_->SetListRootsCallback([&cbCount]() {
        cbCount++;
        ListRootsResult res;
        Root r;
        r.uri = "file:///tmp";
        r.name = std::optional<std::string>("tmp");
        res.roots.push_back(std::move(r));
        return res;
    });

    // Simulate an incoming JSON-RPC request from the server.
    JSONRPCRequest rpc;
    rpc.jsonrpc_ = JSONRPC_VERSION;
    rpc.id_ = 1;
    rpc.method_ = "roots/list";
    rpc.request_ = std::make_unique<ListRootsRequest>();
    rpc.request_->method_ = "roots/list";
    RequestContext ctx;
    ctx.sessionId = "ut";

    JSONRPCMessage msg{std::in_place_type<JSONRPCRequest>, std::move(rpc)};
    session_->OnTransportMessage(msg, ctx);

    EXPECT_EQ(cbCount.load(), 1);
    EXPECT_EQ(transport_->sendCount, 1);
    ASSERT_TRUE(std::holds_alternative<JSONRPCResponse>(transport_->lastMessage));
    EXPECT_EQ(transport_->lastMethod.value_or(""), "roots/list");

    const auto& resp = std::get<JSONRPCResponse>(transport_->lastMessage);
    EXPECT_EQ(resp.id_, 1);
    auto rootsResult = std::dynamic_pointer_cast<ListRootsResult>(resp.result_);
    ASSERT_TRUE(rootsResult != nullptr);
    ASSERT_EQ(rootsResult->roots.size(), 1u);
    EXPECT_EQ(rootsResult->roots[0].uri, "file:///tmp");
}

TEST_F(ClientSessionRootsListTest, SendRootsListChangedSendsNotification)
{
    session_->SendRootsListChanged();

    EXPECT_EQ(transport_->sendCount, 1);

    const auto j = nlohmann::json::parse(transport_->lastSerialized);
    EXPECT_EQ(j.value("jsonrpc", ""), JSONRPC_VERSION);
    EXPECT_EQ(j.value("method", ""), "notifications/roots/list_changed");
    ASSERT_TRUE(j.contains("params"));
    EXPECT_TRUE(j.at("params").is_object());

    ASSERT_TRUE(std::holds_alternative<JSONRPCNotification>(transport_->lastMessage));
    const auto& notif = std::get<JSONRPCNotification>(transport_->lastMessage);
    EXPECT_EQ(notif.method_, "notifications/roots/list_changed");
    EXPECT_NE(dynamic_cast<RootsListChangedNotification*>(notif.notification_.get()), nullptr);
}