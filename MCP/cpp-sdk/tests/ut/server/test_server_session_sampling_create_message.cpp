/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */
#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>

#include "shared/common_type.h"
#include "server/server_session.h"
#include "shared/jsonrpc.h"
#include "transport/transport.h"

using namespace Mcp;

namespace {
static constexpr int64_t MAX_TOKENS = 3;

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

class ServerSessionSamplingCreateMessageTest : public ::testing::Test {
protected:
    ~ServerSessionSamplingCreateMessageTest() override = default;

    void SetUp() override
    {
        transport_ = std::make_shared<CapturingServerTransport>();
        session_ = std::make_shared<ServerSession>(transport_);
        ctx_.sessionId = "ut";
        ctx_.connectionId = 1;
    }

    void InitializeSessionWithSamplingCreateMessageCapability(bool supported)
    {
        ClientCapabilities caps;
        ClientTasksCapability tasks;
        tasks.samplingCreateMessage = supported;
        caps.tasks = tasks;

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

        transport_->sendCount = 0;
        transport_->lastSerialized.clear();
    }

    void InitializeSessionWithSamplingCreateMessageCapability()
    {
        InitializeSessionWithSamplingCreateMessageCapability(true);
    }

    static CreateMessageRequestParams MakeParamsWithSingleToolResult(const std::string& toolUseId)
    {
        CreateMessageRequestParams params;
        SamplingMessage last;
        last.role = RoleType::USER;

        ToolResultContent tr;
        tr.toolUseId = toolUseId;
        last.content = SamplingMessageContentBlock{tr};
        params.messages.push_back(std::move(last));
        return params;
    }

    static CreateMessageRequestParams MakeParamsWithMixedToolResultAndText(const std::string& toolUseId)
    {
        CreateMessageRequestParams params;
        SamplingMessage last;
        last.role = RoleType::USER;

        std::vector<SamplingMessageContentBlock> blocks;
        ToolResultContent tr;
        tr.toolUseId = toolUseId;
        blocks.emplace_back(tr);
        TextContent tc;
        tc.text = "extra";
        blocks.emplace_back(tc);
        last.content = blocks;
        params.messages.push_back(std::move(last));
        return params;
    }

    static CreateMessageRequestParams MakeParamsWithPreviousTextThenToolResult(const std::string& toolUseId)
    {
        CreateMessageRequestParams params;

        SamplingMessage prev;
        prev.role = RoleType::ASSISTANT;
        TextContent prevText;
        prevText.text = "not a tool use";
        prev.content = SamplingMessageContentBlock{prevText};
        params.messages.push_back(std::move(prev));

        SamplingMessage last;
        last.role = RoleType::USER;
        ToolResultContent tr;
        tr.toolUseId = toolUseId;
        last.content = SamplingMessageContentBlock{tr};
        params.messages.push_back(std::move(last));

        return params;
    }

    static CreateMessageRequestParams MakeParamsWithToolUseThenToolResult(const std::string& toolUseId,
                                                                         const std::string& toolResultId)
    {
        CreateMessageRequestParams params;

        SamplingMessage prev;
        prev.role = RoleType::ASSISTANT;
        ToolUseContent tu;
        tu.id = toolUseId;
        tu.name = "ut-tool";
        prev.content = SamplingMessageContentBlock{tu};
        params.messages.push_back(std::move(prev));

        SamplingMessage last;
        last.role = RoleType::USER;
        ToolResultContent tr;
        tr.toolUseId = toolResultId;
        last.content = SamplingMessageContentBlock{tr};
        params.messages.push_back(std::move(last));

        return params;
    }

    int64_t GetLastRequestId() const
    {
        auto j = nlohmann::json::parse(transport_->lastSerialized);
        return j.value("id", 0);
    }

    void ReplyWithResult(int64_t reqId, std::shared_ptr<Result> resultPtr)
    {
        JSONRPCResponse resp;
        resp.jsonrpc_ = JSONRPC_VERSION;
        resp.id_ = reqId;
        resp.result_ = std::move(resultPtr);
        JSONRPCMessage respMsg{std::in_place_type<JSONRPCResponse>, std::move(resp)};
        session_->OnTransportMessage(respMsg, ctx_);
    }

    void ReplyWithError(int64_t reqId, int code, const std::string& message)
    {
        JSONRPCError err;
        err.jsonrpc_ = JSONRPC_VERSION;
        err.id_ = reqId;
        err.code_ = code;
        err.message_ = message;

        JSONRPCMessage errMsg{std::in_place_type<JSONRPCError>, std::move(err)};
        session_->OnTransportMessage(errMsg, ctx_);
    }

    CreateMessageRequestParams MakeMinimalValidParams() const
    {
        CreateMessageRequestParams params;
        SamplingMessage msg;
        msg.role = RoleType::USER;
        TextContent tc;
        tc.text = "hello";
        msg.content = SamplingMessageContentBlock{tc};
        params.messages.push_back(std::move(msg));
        params.maxTokens = MAX_TOKENS;
        return params;
    }

    std::shared_ptr<CapturingServerTransport> transport_;
    std::shared_ptr<ServerSession> session_;
    RequestContext ctx_;
};

} // namespace

TEST(ServerSessionSamplingCreateMessageStandaloneTest, SamplingCreateMessage_NoTransport_Throws)
{
    std::shared_ptr<ServerTransport> nullTransport;
    auto session = std::make_shared<ServerSession>(nullTransport);

    CreateMessageRequestParams params;
    EXPECT_THROW(session->SamplingCreateMessage(params), std::runtime_error);
}

TEST_F(ServerSessionSamplingCreateMessageTest, SamplingCreateMessage_NotInitialized_Throws)
{
    auto params = MakeMinimalValidParams();
    EXPECT_THROW(session_->SamplingCreateMessage(params), std::runtime_error);
}

TEST_F(ServerSessionSamplingCreateMessageTest, SamplingCreateMessage_UnsupportedByClient_Throws)
{
    InitializeSessionWithSamplingCreateMessageCapability(false);
    auto params = MakeMinimalValidParams();
    EXPECT_THROW(session_->SamplingCreateMessage(params), std::runtime_error);
}

TEST_F(ServerSessionSamplingCreateMessageTest, SamplingCreateMessage_ValidationToolResultWithoutToolUse_Throws)
{
    InitializeSessionWithSamplingCreateMessageCapability();
    auto params = MakeParamsWithSingleToolResult("id-1");
    EXPECT_THROW(session_->SamplingCreateMessage(params), std::runtime_error);
}

TEST_F(ServerSessionSamplingCreateMessageTest, SamplingCreateMessage_ValidationToolResultMixedContent_Throws)
{
    InitializeSessionWithSamplingCreateMessageCapability();
    auto params = MakeParamsWithMixedToolResultAndText("id-1");
    EXPECT_THROW(session_->SamplingCreateMessage(params), std::runtime_error);
}

TEST_F(ServerSessionSamplingCreateMessageTest,
    SamplingCreateMessage_ValidationToolResultWithPreviousNonToolUse_Throws)
{
    InitializeSessionWithSamplingCreateMessageCapability();
    auto params = MakeParamsWithPreviousTextThenToolResult("id-1");
    EXPECT_THROW(session_->SamplingCreateMessage(params), std::runtime_error);
}

TEST_F(ServerSessionSamplingCreateMessageTest, SamplingCreateMessage_ValidationToolUseResultIdMismatch_Throws)
{
    InitializeSessionWithSamplingCreateMessageCapability();
    auto params = MakeParamsWithToolUseThenToolResult("use-1", "use-2");
    EXPECT_THROW(session_->SamplingCreateMessage(params), std::runtime_error);
}

TEST_F(ServerSessionSamplingCreateMessageTest, SamplingCreateMessage_SendsRequestAndCompletesFuture)
{
    InitializeSessionWithSamplingCreateMessageCapability();

    auto params = MakeMinimalValidParams();
    auto fut = session_->SamplingCreateMessage(params);

    EXPECT_EQ(transport_->sendCount, 1);
    auto j = nlohmann::json::parse(transport_->lastSerialized);
    EXPECT_EQ(j.value("method", ""), "sampling/createMessage");
    ASSERT_TRUE(j.contains("params"));
    EXPECT_EQ(j.at("params").value("maxTokens", 0), MAX_TOKENS);
    ASSERT_TRUE(j.at("params").contains("messages"));
    ASSERT_TRUE(j.at("params").at("messages").is_array());

    const int64_t reqId = j.value("id", 0);
    EXPECT_GT(reqId, 0);

    CreateMessageResult result;
    result.model = "ut-model";
    result.role = RoleType::ASSISTANT;
    TextContent tc;
    tc.text = "ok";
    result.content = SamplingMessageContentBlock{tc};

    JSONRPCResponse resp;
    resp.jsonrpc_ = JSONRPC_VERSION;
    resp.id_ = reqId;
    resp.result_ = std::make_shared<CreateMessageResult>(std::move(result));
    JSONRPCMessage respMsg{std::in_place_type<JSONRPCResponse>, std::move(resp)};

    session_->OnTransportMessage(respMsg, ctx_);

    auto res = fut.get();
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->model, "ut-model");
    EXPECT_EQ(res->role, RoleType::ASSISTANT);
}

TEST_F(ServerSessionSamplingCreateMessageTest, SamplingCreateMessage_ErrorResponse_CompletesFutureWithException)
{
    InitializeSessionWithSamplingCreateMessageCapability();

    auto params = MakeMinimalValidParams();
    auto fut = session_->SamplingCreateMessage(params);

    const int64_t reqId = GetLastRequestId();
    ASSERT_GT(reqId, 0);

    ReplyWithError(reqId, static_cast<int>(JsonRpcErrorCode::INTERNAL_ERROR), "err");

    try {
        (void)fut.get();
        FAIL() << "Expected exception";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("SamplingCreateMessage"), std::string::npos);
    }
}

TEST_F(ServerSessionSamplingCreateMessageTest,
    SamplingCreateMessage_NullResultResponse_CompletesFutureWithException)
{
    InitializeSessionWithSamplingCreateMessageCapability();

    auto params = MakeMinimalValidParams();
    auto fut = session_->SamplingCreateMessage(params);

    const int64_t reqId = GetLastRequestId();
    ASSERT_GT(reqId, 0);

    ReplyWithResult(reqId, nullptr);

    EXPECT_THROW((void)fut.get(), std::runtime_error);
}

TEST_F(ServerSessionSamplingCreateMessageTest,
    SamplingCreateMessage_ResultTypeMismatch_CompletesFutureWithException)
{
    InitializeSessionWithSamplingCreateMessageCapability();

    auto params = MakeMinimalValidParams();
    auto fut = session_->SamplingCreateMessage(params);

    const int64_t reqId = GetLastRequestId();
    ASSERT_GT(reqId, 0);

    // Reply with the wrong result type: should surface as a type mismatch.
    ListRootsResult wrong;
    ReplyWithResult(reqId, std::make_shared<ListRootsResult>(std::move(wrong)));

    EXPECT_THROW((void)fut.get(), std::runtime_error);
}
