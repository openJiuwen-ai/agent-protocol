/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#define private public
#define protected public
#include "shared/base_session.h"
#include "transport/transport.h"
#include "shared/jsonrpc.h"
#undef private
#undef protected

using namespace Mcp;

namespace {

// ---------- Fake ClientTransport ----------
class FakeClientTransport : public ClientTransport {
public:
    void SetCallback(std::shared_ptr<TransportCallback> cb) override { cb_ = std::move(cb); }

    void Connect() override {}
    void Terminate() override {}

    void SendMessage(const JSONRPCMessage& message) override
    {
        (void)message;
        sentCount_++;
    }

    int SentCount() const { return sentCount_; }

    void EmitIncoming(JSONRPCMessage message, RequestContext& ctx)
    {
        if (cb_) cb_->OnMessageReceived(message, ctx);
    }

private:
    std::shared_ptr<TransportCallback> cb_;
    int sentCount_{0};
};

// ---------- Fake ServerTransport ----------
class FakeServerTransport : public ServerTransport {
public:
    void SetCallback(std::shared_ptr<TransportCallback> cb) override { cb_ = std::move(cb); }

    void Listen() override {}
    void Terminate() override {}

    void SendMessage(const JSONRPCMessage& message, const RequestContext& ctx) override
    {
        (void)message;
        (void)ctx;
        sentCount_++;
    }

    void HandleRequest(const Http::HttpRequest& request, RequestContext& ctx) override
    {
        (void)request;
        (void)ctx;
    }

    int SentCount() const { return sentCount_; }

    void EmitIncoming(JSONRPCMessage message, RequestContext& ctx)
    {
        if (cb_) cb_->OnMessageReceived(message, ctx);
    }

private:
    std::shared_ptr<TransportCallback> cb_;
    int sentCount_{0};
};

// ---------- Concrete test session ----------
class TestClientSession : public BaseSession {
public:
    explicit TestClientSession(std::shared_ptr<ClientTransport> t)
        : BaseSession(std::move(t), std::nullopt, "ut")
    {}

    void SendNotification(const Notification&, std::optional<int64_t>) override {}

    int reqCount{0};
    int notifCount{0};

protected:
    void ReceivedRequest(int64_t, const Request&, RequestContext&) override { reqCount++; }
    void ReceivedNotification(const Notification&) override { notifCount++; }
};

class TestServerSession : public BaseSession {
public:
    explicit TestServerSession(std::shared_ptr<ServerTransport> t)
        : BaseSession(std::move(t), "ut")
    {}

    void SendNotification(const Notification&, std::optional<int64_t>) override {}
};

class DefaultHookSession : public BaseSession {
public:
    explicit DefaultHookSession(std::shared_ptr<ClientTransport> t)
        : BaseSession(std::move(t), std::nullopt, "hdr_ut_client") {}

    explicit DefaultHookSession(std::shared_ptr<ServerTransport> t)
        : BaseSession(std::move(t), "hdr_ut_server") {}

    void SendNotification(const Notification&, std::optional<int64_t>) override {}
};

static JSONRPCMessage MakeRequestMsg(int64_t id, const std::string& method)
{
    JSONRPCRequest r;
    r.jsonrpc_ = JSONRPC_VERSION;
    r.id_ = id;
    r.method_ = method;

    auto req = std::make_unique<Request>();
    req->method_ = method;
    r.request_ = std::move(req);

    return JSONRPCMessage{std::in_place_type<JSONRPCRequest>, std::move(r)};
}

static JSONRPCMessage MakeNotificationMsg(const std::string& method)
{
    JSONRPCNotification n;
    n.jsonrpc_ = JSONRPC_VERSION;

    auto notif = std::make_unique<Notification>();
    notif->method_ = method;
    n.notification_ = std::move(notif);

    return JSONRPCMessage{std::in_place_type<JSONRPCNotification>, std::move(n)};
}

static JSONRPCMessage MakeNullNotificationMsg()
{
    JSONRPCNotification n;
    n.jsonrpc_ = JSONRPC_VERSION;
    n.notification_.reset();
    return JSONRPCMessage{std::in_place_type<JSONRPCNotification>, std::move(n)};
}

static JSONRPCMessage MakeResponseMsg(int64_t id)
{
    JSONRPCResponse r;
    r.jsonrpc_ = JSONRPC_VERSION;
    r.id_ = id;
    r.result_.reset();

    return JSONRPCMessage{std::in_place_type<JSONRPCResponse>, std::move(r)};
}

static JSONRPCMessage MakeErrorMsg(int64_t id)
{
    JSONRPCError e;
    e.jsonrpc_ = JSONRPC_VERSION;
    e.id_ = id;
    e.code_ = -32603;
    e.message_ = "err";
    return JSONRPCMessage{std::in_place_type<JSONRPCError>, std::move(e)};
}

} // namespace

TEST(BaseSessionTest, SendRequest_Null_Throws)
{
    auto t = std::make_shared<FakeClientTransport>();
    TestClientSession s(t);

    EXPECT_THROW(
        s.SendRequest(nullptr,
                      [](std::shared_ptr<Result>) {},
                      std::nullopt,
                      std::nullopt),
        std::invalid_argument);
}

TEST(BaseSessionTest, SendRequest_WithProgressCallback_Registers)
{
    auto t = std::make_shared<FakeClientTransport>();
    TestClientSession s(t);

    auto req = std::make_unique<Request>();
    req->method_ = "x";

    ProgressCallback pcb = [](double, std::optional<double>, const std::optional<std::string>&) {};
    s.SendRequest(std::move(req),
                  [](std::shared_ptr<Result>) {},
                  std::nullopt,
                  pcb);

    EXPECT_EQ(t->SentCount(), 1);
    EXPECT_EQ(s.progressCallbacks_.size(), 1u);
    EXPECT_EQ(s.completionCallbacks_.size(), 1u);
}

TEST(BaseSessionTest, SendResponse_ResultAndError_SendViaTransport)
{
    auto st = std::make_shared<FakeServerTransport>();
    TestServerSession s(st);

    RequestContext ctx;
    ctx.sessionId = "ut";

    // result overload
    std::unique_ptr<Result> result;
    EXPECT_NO_THROW(s.SendResponse(1, std::move(result), ctx));

    // error overload
    JSONRPCError err;
    err.jsonrpc_ = JSONRPC_VERSION;
    err.id_ = 2;
    err.code_ = -1;
    err.message_ = "m";
    EXPECT_NO_THROW(s.SendResponse(2, err, ctx));

    EXPECT_EQ(st->SentCount(), 2);
}

TEST(BaseSessionTest, OnTransportMessage_RoutesAllKinds)
{
    auto t = std::make_shared<FakeClientTransport>();
    TestClientSession s(t);

    RequestContext ctx;
    ctx.sessionId = "ut";

    s.OnTransportMessage(MakeRequestMsg(1, "tools/list"), ctx);
    EXPECT_EQ(s.reqCount, 1);
    EXPECT_EQ(ctx.method, "tools/list");

    s.OnTransportMessage(MakeNotificationMsg("n1"), ctx);
    EXPECT_EQ(s.notifCount, 1);

    {
        auto req = std::make_unique<Request>();
        req->method_ = "x";
        std::atomic<int> called{0};
        s.SendRequest(std::move(req),
                      [&](std::shared_ptr<Result>) { called++; },
                      std::nullopt,
                      std::nullopt);

        s.OnTransportMessage(MakeResponseMsg(1), ctx);
        EXPECT_EQ(called.load(), 1);
    }

    {
        auto req = std::make_unique<Request>();
        req->method_ = "y";
        std::atomic<int> called{0};
        s.SendRequest(std::move(req),
                      [&](std::shared_ptr<Result> r) {
                          EXPECT_EQ(r, nullptr);
                          called++;
                      },
                      std::nullopt,
                      std::nullopt);

        s.OnTransportMessage(MakeErrorMsg(2), ctx);
        EXPECT_EQ(called.load(), 1);
    }
}

TEST(BaseSessionTest, ProcessIncomingNotification_Null_NoCrash)
{
    auto t = std::make_shared<FakeClientTransport>();
    TestClientSession s(t);

    RequestContext ctx;
    ctx.sessionId = "ut";

    EXPECT_NO_THROW(s.OnTransportMessage(MakeNullNotificationMsg(), ctx));
}

TEST(BaseSessionTest, SendProgressNotification_NoCrash)
{
    auto t = std::make_shared<FakeClientTransport>();
    TestClientSession s(t);

    EXPECT_NO_THROW(s.SendProgressNotification(1, 0.1, 1.0, std::optional<std::string>("hi")));
}

// ================= header coverage =================
TEST(BaseSessionHeaderTest, SessionTransportCallback_OnDisconnected)
{
    auto t = std::make_shared<FakeClientTransport>();
    DefaultHookSession s(t);

    SessionTransportCallback cb(&s);
    EXPECT_NO_THROW(cb.OnDisconnected("test"));
}

TEST(BaseSessionHeaderTest, SessionTransportCallback_OnMessageReceived)
{
    auto t = std::make_shared<FakeClientTransport>();
    DefaultHookSession s(t);

    SessionTransportCallback cb(&s);

    RequestContext ctx;
    ctx.sessionId = "hdr";

    JSONRPCRequest r;
    r.jsonrpc_ = JSONRPC_VERSION;
    r.id_ = 1;
    r.method_ = "tools/list";
    r.request_ = std::make_unique<Request>();
    r.request_->method_ = "tools/list";

    JSONRPCMessage msg{std::in_place_type<JSONRPCRequest>, std::move(r)};
    EXPECT_NO_THROW(cb.OnMessageReceived(msg, ctx));
}

TEST(BaseSessionHeaderTest, BaseSession_DefaultReceivedRequest)
{
    auto t = std::make_shared<FakeClientTransport>();
    DefaultHookSession s(t);

    RequestContext ctx;
    ctx.sessionId = "hdr";

    JSONRPCRequest r;
    r.jsonrpc_ = JSONRPC_VERSION;
    r.id_ = 10;
    r.method_ = "x";
    r.request_ = std::make_unique<Request>();
    r.request_->method_ = "x";

    JSONRPCMessage msg{std::in_place_type<JSONRPCRequest>, std::move(r)};
    EXPECT_NO_THROW(s.OnTransportMessage(msg, ctx));

    EXPECT_EQ(s.GetSessionId(), "hdr_ut_client");
}

TEST(BaseSessionHeaderTest, BaseSession_NullTransport_Ctor)
{
    DefaultHookSession s(std::shared_ptr<ClientTransport>(nullptr));
    EXPECT_EQ(s.GetSessionId(), "hdr_ut_client");
}
