/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */
#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "client/client_session.h"
#include "mcp_type.h"
#include "shared/jsonrpc.h"
#include "transport/transport.h"

using namespace Mcp;

namespace {

class TestClientSessionSamplingTransport final : public ClientTransport {
public:
    TestClientSessionSamplingTransport() = default;
    ~TestClientSessionSamplingTransport() override = default;

    void SetCallback(std::shared_ptr<TransportCallback> callback) override
    {
        callback_ = std::move(callback);
    }

    void Connect() override {}
    void Terminate() override {}

    void SendMessage(const JSONRPCMessage& message, std::optional<std::string> method = std::nullopt) override
    {
        lastMethod = method;
        lastSerialized = SerializeJSONRPCMessage(message, method);
        auto parsed = DeserializeJSONRPCMessage(lastSerialized, method.value_or(""));
        lastMessage = std::move(parsed);
        sendCount++;
    }

    std::shared_ptr<TransportCallback> callback_;
    JSONRPCMessage lastMessage{std::in_place_type<JSONRPCError>};
    std::optional<std::string> lastMethod;
    std::string lastSerialized;
    int sendCount{0};
};

class ClientSessionSamplingCreateMessageTest : public ::testing::Test {
protected:
    ~ClientSessionSamplingCreateMessageTest() override = default;

    void SetUp() override
    {
        transport_ = std::make_shared<TestClientSessionSamplingTransport>();
        session_ = std::make_unique<ClientSession>(transport_);
    }

    std::shared_ptr<TestClientSessionSamplingTransport> transport_;
    std::unique_ptr<ClientSession> session_;
};

static JSONRPCMessage MakeSamplingCreateMessageRequest(int64_t requestId, std::unique_ptr<CreateMessageRequestParams> p)
{
    JSONRPCRequest rpc;
    rpc.jsonrpc_ = JSONRPC_VERSION;
    rpc.id_ = requestId;
    rpc.method_ = "sampling/createMessage";
    rpc.request_ = std::make_unique<CreateMessageRequest>();
    rpc.request_->method_ = "sampling/createMessage";

    auto* req = dynamic_cast<CreateMessageRequest*>(rpc.request_.get());
    EXPECT_NE(req, nullptr);
    req->params_ = std::move(p);

    JSONRPCMessage msg{std::in_place_type<JSONRPCRequest>, std::move(rpc)};
    return msg;
}

static std::unique_ptr<CreateMessageRequestParams> MakeMinimalValidParams()
{
    auto p = std::make_unique<CreateMessageRequestParams>();
    p->maxTokens = 1;

    SamplingMessage msg;
    msg.role = RoleType::USER;
    TextContent tc;
    tc.text = "hello";
    msg.content = SamplingMessageContentBlock{tc};
    p->messages.push_back(std::move(msg));

    return p;
}

} // namespace

TEST_F(ClientSessionSamplingCreateMessageTest, NoCallbackRepliesMethodNotFound)
{
    auto p = MakeMinimalValidParams();
    auto msg = MakeSamplingCreateMessageRequest(1, std::move(p));

    RequestContext ctx;
    ctx.sessionId = "ut";
    session_->OnTransportMessage(msg, ctx);

    EXPECT_EQ(transport_->sendCount, 1);
    ASSERT_TRUE(std::holds_alternative<JSONRPCError>(transport_->lastMessage));

    const auto& err = std::get<JSONRPCError>(transport_->lastMessage);
    EXPECT_EQ(err.id_, 1);
    EXPECT_EQ(err.code_, static_cast<int>(JsonRpcErrorCode::METHOD_NOT_FOUND));
}

TEST_F(ClientSessionSamplingCreateMessageTest, ToolsProvidedButClientDidNotAdvertiseToolsCapability)
{
    SamplingCapability cap;
    cap.tools = false;
    cap.context = true;

    session_->SetSamplingCreateMessageCallback(
        [](const CreateMessageParams&) -> std::optional<CreateMessageResult> {
            return std::nullopt;
        },
        cap);

    auto p = MakeMinimalValidParams();
    p->tools = std::vector<Tool>{};

    auto msg = MakeSamplingCreateMessageRequest(1, std::move(p));
    RequestContext ctx;
    ctx.sessionId = "ut";
    session_->OnTransportMessage(msg, ctx);

    EXPECT_EQ(transport_->sendCount, 1);
    ASSERT_TRUE(std::holds_alternative<JSONRPCError>(transport_->lastMessage));

    const auto& err = std::get<JSONRPCError>(transport_->lastMessage);
    EXPECT_EQ(err.id_, 1);
    EXPECT_EQ(err.code_, static_cast<int>(JsonRpcErrorCode::INVALID_PARAMS));
}

TEST_F(ClientSessionSamplingCreateMessageTest, IncludeContextBeyondNoneButClientDidNotAdvertiseContextCapability)
{
    SamplingCapability cap;
    cap.tools = true;
    cap.context = false;

    session_->SetSamplingCreateMessageCallback(
        [](const CreateMessageParams&) -> std::optional<CreateMessageResult> {
            return std::nullopt;
        },
        cap);

    auto p = MakeMinimalValidParams();
    p->includeContext = std::string{"thisServer"};

    auto msg = MakeSamplingCreateMessageRequest(1, std::move(p));
    RequestContext ctx;
    ctx.sessionId = "ut";
    session_->OnTransportMessage(msg, ctx);

    EXPECT_EQ(transport_->sendCount, 1);
    ASSERT_TRUE(std::holds_alternative<JSONRPCError>(transport_->lastMessage));

    const auto& err = std::get<JSONRPCError>(transport_->lastMessage);
    EXPECT_EQ(err.id_, 1);
    EXPECT_EQ(err.code_, static_cast<int>(JsonRpcErrorCode::INVALID_PARAMS));
}

TEST_F(ClientSessionSamplingCreateMessageTest, InvalidToolUseToolResultSequenceRepliesInvalidParams)
{
    SamplingCapability cap;
    cap.tools = true;
    cap.context = true;

    session_->SetSamplingCreateMessageCallback(
        [](const CreateMessageParams&) -> std::optional<CreateMessageResult> {
            return std::nullopt;
        },
        cap);

    auto p = std::make_unique<CreateMessageRequestParams>();
    p->maxTokens = 1;

    SamplingMessage toolResult;
    toolResult.role = RoleType::USER;
    ToolResultContent tr;
    tr.toolUseId = "missing-tool-use";
    toolResult.content = SamplingMessageContentBlock{tr};
    p->messages.push_back(std::move(toolResult));

    auto msg = MakeSamplingCreateMessageRequest(1, std::move(p));
    RequestContext ctx;
    ctx.sessionId = "ut";
    session_->OnTransportMessage(msg, ctx);

    EXPECT_EQ(transport_->sendCount, 1);
    ASSERT_TRUE(std::holds_alternative<JSONRPCError>(transport_->lastMessage));

    const auto& err = std::get<JSONRPCError>(transport_->lastMessage);
    EXPECT_EQ(err.id_, 1);
    EXPECT_EQ(err.code_, static_cast<int>(JsonRpcErrorCode::INVALID_PARAMS));
}

TEST_F(ClientSessionSamplingCreateMessageTest, UserRejectionRepliesMinusOne)
{
    SamplingCapability cap;
    cap.tools = true;
    cap.context = true;

    session_->SetSamplingCreateMessageCallback(
        [](const CreateMessageParams&) -> std::optional<CreateMessageResult> {
            return std::nullopt;
        },
        cap);

    auto p = MakeMinimalValidParams();
    auto msg = MakeSamplingCreateMessageRequest(1, std::move(p));

    RequestContext ctx;
    ctx.sessionId = "ut";
    session_->OnTransportMessage(msg, ctx);

    EXPECT_EQ(transport_->sendCount, 1);
    ASSERT_TRUE(std::holds_alternative<JSONRPCError>(transport_->lastMessage));

    const auto& err = std::get<JSONRPCError>(transport_->lastMessage);
    EXPECT_EQ(err.id_, 1);
    EXPECT_EQ(err.code_, -1);
}

TEST_F(ClientSessionSamplingCreateMessageTest, SuccessRepliesWithCreateMessageResult)
{
    SamplingCapability cap;
    cap.tools = true;
    cap.context = true;

    session_->SetSamplingCreateMessageCallback(
        [](const CreateMessageParams&) -> std::optional<CreateMessageResult> {
            CreateMessageResult res;
            res.model = "ut-model";
            res.role = RoleType::ASSISTANT;
            TextContent tc;
            tc.text = "ok";
            res.content = SamplingMessageContentBlock{tc};
            return res;
        },
        cap);

    auto p = MakeMinimalValidParams();
    auto msg = MakeSamplingCreateMessageRequest(1, std::move(p));

    RequestContext ctx;
    ctx.sessionId = "ut";
    session_->OnTransportMessage(msg, ctx);

    EXPECT_EQ(transport_->sendCount, 1);
    ASSERT_TRUE(std::holds_alternative<JSONRPCResponse>(transport_->lastMessage));
    EXPECT_EQ(transport_->lastMethod.value_or(""), "sampling/createMessage");

    const auto& resp = std::get<JSONRPCResponse>(transport_->lastMessage);
    EXPECT_EQ(resp.id_, 1);

    auto resultPtr = std::dynamic_pointer_cast<CreateMessageResult>(resp.result_);
    ASSERT_TRUE(resultPtr != nullptr);
    EXPECT_EQ(resultPtr->model, "ut-model");
    EXPECT_EQ(resultPtr->role, RoleType::ASSISTANT);
}
