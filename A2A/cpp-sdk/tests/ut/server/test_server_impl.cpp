/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#define private public
#define protected public
#include "server/server_impl.h"
#undef private
#undef protected

#include "error.h"
#include "jsonrpc.h"
#include "server/request_handler.h"
#include "transport/server_transport.h"
#include "transport/transport_emitter.h"
#include "types.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::IsNull;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::Throw;

using nlohmann::json;

using namespace A2A;
using namespace A2A::Server;

namespace {

class MockTransportEmitter : public Transport::TransportEmitter {
public:
    MOCK_METHOD(void, WriteStreamingData, (const std::string& data), (override));
    MOCK_METHOD(void, WriteNonStreamingData, (const std::string& data), (override));
    MOCK_METHOD(void, WriteDone, (), (override));
};

std::shared_ptr<Transport::TransportEmitter> WrapEmitter(MockTransportEmitter& emitter)
{
    return std::shared_ptr<Transport::TransportEmitter>(&emitter, [](auto*) {});
}

class MockServerTransport : public Transport::ServerTransport {
public:
    MOCK_METHOD(int, Start, (), (override));
    MOCK_METHOD(void, Stop, (), (override));
    MOCK_METHOD(int, SendData, (const std::string& url, const std::string& data), (const, override));
    MOCK_METHOD(void, SetRpcHandler, (Transport::ServerTransportRpcHandler handler), (override));
    MOCK_METHOD(void, SetCardHandler, (Transport::ServerTransportCardHandler handler), (override));

    Transport::ServerTransportRpcHandler rpcHandler;
    Transport::ServerTransportCardHandler cardHandler;
};

class MockRequestHandler : public RequestHandler {
public:
    MOCK_METHOD(void, OnSendMessage,
        (const MessageSendParams&, const std::shared_ptr<ServerCallContext>, StreamEmitter, const std::string&),
        (override));

    MOCK_METHOD(Task, OnGetTask,
        (const TaskQueryParams&, std::shared_ptr<ServerCallContext>), (override));

    MOCK_METHOD(Task, OnCancelTask,
        (const TaskIdParams&, std::shared_ptr<ServerCallContext>), (override));

    MOCK_METHOD(void, OnSetTaskPushNotificationConfig,
        (const TaskPushNotificationConfig&, std::shared_ptr<ServerCallContext>), (override));

    MOCK_METHOD(TaskPushNotificationConfig, OnGetTaskPushNotificationConfig,
        (const GetTaskPushNotificationConfigParams&, std::shared_ptr<ServerCallContext>), (override));

    MOCK_METHOD((std::vector<TaskPushNotificationConfig>), OnListTaskPushNotificationConfigs,
        (const ListTaskPushNotificationConfigParams&, std::shared_ptr<ServerCallContext>), (override));

    MOCK_METHOD(void, OnDeleteTaskPushNotificationConfig,
        (const DeleteTaskPushNotificationConfigParams&, std::shared_ptr<ServerCallContext>), (override));

    MOCK_METHOD(AgentCard, OnGetCard,
        (std::shared_ptr<ServerCallContext>), (override));

    MOCK_METHOD(void, OnSendMessageStreaming,
        (const MessageSendParams&, StreamEmitter, std::shared_ptr<ServerCallContext>), (override));

    MOCK_METHOD(void, OnResubscribeToTask,
        (const TaskIdParams&, StreamEmitter, std::shared_ptr<ServerCallContext>), (override));
};

class ServerImplTest : public ::testing::Test {
protected:
    std::shared_ptr<NiceMock<MockServerTransport>> mockTransport =
        std::make_shared<NiceMock<MockServerTransport>>();
    std::shared_ptr<NiceMock<MockRequestHandler>> mockHandler =
        std::make_shared<NiceMock<MockRequestHandler>>();

    std::shared_ptr<AgentCard> agentCard = std::make_shared<AgentCard>();
    std::shared_ptr<AgentCard> extendedAgentCard = std::make_shared<AgentCard>();

    HttpConfig config {"127.0.0.1", 8080, 1, "/jsonrpc"};

    std::unique_ptr<ServerImpl> server;

    static Task MakeTask(const std::string& id = "task-1")
    {
        Task task;
        task.id = id;
        task.contextId = "ctx-1";

        TaskStatus status;
        status.state = TaskState::WORKING;
        status.timestamp = "2026-03-17T00:00:00Z";
        task.status = status;

        return task;
    }

    static Message MakeMessage(const std::string& messageId = "msg-1")
    {
        Message msg;
        msg.messageId = messageId;
        msg.role = Role::USER;
        return msg;
    }

    static AgentCard MakeAgentCard()
    {
        AgentCard card;
        card.name = "ut-agent";
        card.capabilities.streaming = true;
        return card;
    }

    static json MakeBaseReq(const std::string& method, const json& id = "req-1")
    {
        return json{
            {"jsonrpc", "2.0"},
            {"id", id},
            {"method", method}
        };
    }

    void SetUp() override
    {
        agentCard->capabilities.streaming = true;
        server = std::make_unique<ServerImpl>(
            agentCard,
            extendedAgentCard,
            nullptr,
            config,
            mockTransport,
            nullptr);

        server->handler_ = mockHandler;
        server->jsonRpcHandler_ = std::make_unique<JSONRPCHandler>(mockHandler);
    }
};

TEST_F(ServerImplTest, Start_WhenTransportIsNull_ReturnsOne)
{
    server->transport_.reset();

    EXPECT_EQ(server->Start(), 1);
}

TEST_F(ServerImplTest, Start_Success_RegistersHandlersAndStartsTransport)
{
    EXPECT_CALL(*mockTransport, SetRpcHandler(_))
        .WillOnce(Invoke([&](Transport::ServerTransportRpcHandler handler) {
            mockTransport->rpcHandler = std::move(handler);
        }));

    EXPECT_CALL(*mockTransport, SetCardHandler(_))
        .WillOnce(Invoke([&](Transport::ServerTransportCardHandler handler) {
            mockTransport->cardHandler = std::move(handler);
        }));

    EXPECT_CALL(*mockTransport, Start())
        .WillOnce(Return(0));

    EXPECT_EQ(server->Start(), 0);
    EXPECT_TRUE(server->started_.load());
    EXPECT_TRUE(static_cast<bool>(mockTransport->rpcHandler));
    EXPECT_TRUE(static_cast<bool>(mockTransport->cardHandler));
}

TEST_F(ServerImplTest, Start_WhenAlreadyStarted_DoesNotCallTransportStartAgain)
{
    EXPECT_CALL(*mockTransport, SetRpcHandler(_))
        .WillOnce(Invoke([&](Transport::ServerTransportRpcHandler handler) {
            mockTransport->rpcHandler = std::move(handler);
        }));

    EXPECT_CALL(*mockTransport, SetCardHandler(_))
        .WillOnce(Invoke([&](Transport::ServerTransportCardHandler handler) {
            mockTransport->cardHandler = std::move(handler);
        }));

    EXPECT_CALL(*mockTransport, Start())
        .WillOnce(Return(0));

    EXPECT_EQ(server->Start(), 0);
    EXPECT_EQ(server->Start(), 0);
}

TEST_F(ServerImplTest, Stop_StopsTransportAndResetsIt)
{
    server->Stop();
    EXPECT_FALSE(server->started_.load());
}

TEST_F(ServerImplTest, OnGetCard_ReturnsAgentCard)
{
    agentCard->name = "ut-agent";

    AgentCard card = server->OnGetCard(nullptr);

    EXPECT_EQ(card.name, "ut-agent");
}

TEST_F(ServerImplTest, OnGetCard_WhenAgentCardNull_Throws)
{
    server->agentCard_.reset();

    EXPECT_THROW(server->OnGetCard(nullptr), std::runtime_error);
}

TEST_F(ServerImplTest, OnGetAuthenticatedExtendedCard_ReturnsAgentCard)
{
    agentCard->name = "ut-agent";

    AgentCard card = server->OnGetAuthenticatedExtendedCard(nullptr);

    EXPECT_EQ(card.name, "ut-agent");
}

TEST_F(ServerImplTest, HandleNonStreamingRequest_ForCardPath_ReturnsAgentCard)
{
    AgentCard card = MakeAgentCard();
    EXPECT_CALL(*mockHandler, OnGetCard(IsNull()))
        .WillOnce(Return(card));

    std::string resp;
    json req = json::object();

    NiceMock<MockTransportEmitter> emitter;
    server->HandleNonStreamingRequest(req, "{}", resp, "", WrapEmitter(emitter));

    json out = json::parse(resp);
    EXPECT_EQ(out["result"]["name"], "ut-agent");
}

TEST_F(ServerImplTest, ProcessStandardJsonRpc_OnGetTask_RoutesCorrectly)
{
    Task task = MakeTask("task-9");
    json req = MakeBaseReq(METHOD_TASK_GET);
    req["params"] = {
        {"id", "task-9"},
        {"historyLength", 3}
    };

    EXPECT_CALL(*mockHandler, OnGetTask(_, IsNull()))
        .WillOnce(Return(task));

    std::string resp;
    NiceMock<MockTransportEmitter> emitter;
    server->ProcessStandardJsonRpc(req, resp, METHOD_TASK_GET, WrapEmitter(emitter));

    json out = json::parse(resp);
    EXPECT_EQ(out["jsonrpc"], "2.0");
    EXPECT_EQ(out["id"], "req-1");
    EXPECT_EQ(out["result"]["id"], "task-9");
}

TEST_F(ServerImplTest, ProcessStandardJsonRpc_OnCancelTask_RoutesCorrectly)
{
    Task task = MakeTask("task-cancel");
    json req = MakeBaseReq(METHOD_TASK_CANCEL);
    req["params"] = {
        {"id", "task-cancel"}
    };

    EXPECT_CALL(*mockHandler, OnCancelTask(_, IsNull()))
        .WillOnce(Return(task));

    std::string resp;
    NiceMock<MockTransportEmitter> emitter;
    server->ProcessStandardJsonRpc(req, resp, METHOD_TASK_CANCEL, WrapEmitter(emitter));

    json out = json::parse(resp);
    EXPECT_EQ(out["result"]["id"], "task-cancel");
}

TEST_F(ServerImplTest, ProcessStandardJsonRpc_UnknownMethod_ReturnsMethodNotFound)
{
    json req = MakeBaseReq("unknown.method");

    std::string resp;
    NiceMock<MockTransportEmitter> emitter;
    server->ProcessStandardJsonRpc(req, resp, "unknown.method", WrapEmitter(emitter));

    json out = json::parse(resp);
    EXPECT_TRUE(out.contains("error"));
    EXPECT_EQ(out["error"]["code"], static_cast<int>(A2AErrorCode::JSONRPC_METHOD_NOT_FOUND));
}

TEST_F(ServerImplTest, CreateStreamEmitter_WritesSuccessResponse)
{
    NiceMock<MockTransportEmitter> emitter;
    std::function<void(const StreamEvent&)> streamEmit;

    json req = MakeBaseReq(METHOD_MESSAGE_STREAM, "stream-1");

    EXPECT_CALL(emitter, WriteStreamingData(_))
        .WillOnce(Invoke([&](const std::string& s) {
            json out = json::parse(s);
            EXPECT_EQ(out["id"], "stream-1");
            EXPECT_TRUE(out.contains("result"));
        }));

    server->CreateStreamEmitter(req, streamEmit, WrapEmitter(emitter), true);

    Task task = MakeTask("task-stream");
    streamEmit(task);
}

TEST_F(ServerImplTest, HandleStreamingRequest_MessageStream_WhenMessageInvalid_WritesInternalError)
{
    NiceMock<MockTransportEmitter> emitter;

    json req = MakeBaseReq(METHOD_MESSAGE_STREAM, "stream-2");
    req["params"] = {
        {"message", {
            {"messageId", "msg-stream"},
            {"role", "USER"}
        }}
    };

    EXPECT_CALL(*mockHandler, OnSendMessageStreaming(_, IsNull(), _)).Times(0);

    EXPECT_CALL(emitter, WriteStreamingData(_))
        .WillOnce(Invoke([&](const std::string& s) {
            json out = json::parse(s);
            EXPECT_TRUE(out.contains("error"));
            EXPECT_EQ(out["error"]["code"], static_cast<int>(A2AErrorCode::JSONRPC_INTERNAL_ERROR));
        }));

    EXPECT_CALL(emitter, WriteDone()).Times(1);

    server->HandleStreamingRequest(req, METHOD_MESSAGE_STREAM, WrapEmitter(emitter));
}

TEST_F(ServerImplTest, HandleStreamingRequest_TaskResubscribe_RoutesCorrectly)
{
    NiceMock<MockTransportEmitter> emitter;

    json req = MakeBaseReq(METHOD_TASK_RESUBSCRIBE, "stream-3");
    req["params"] = {
        {"id", "task-sub"}
    };

    EXPECT_CALL(*mockHandler, OnResubscribeToTask(_, _, IsNull()))
        .WillOnce(Invoke([&](const TaskIdParams&,
                            StreamEmitter&& cb,
                            std::shared_ptr<ServerCallContext>) {
            cb(MakeMessage("msg-sub"));
        }));

    EXPECT_CALL(emitter, WriteStreamingData(_)).Times(1);

    server->HandleStreamingRequest(req, METHOD_TASK_RESUBSCRIBE, WrapEmitter(emitter));
}

TEST_F(ServerImplTest, HandleStreamingRequest_UnsupportedMethod_WritesMethodNotFound)
{
    NiceMock<MockTransportEmitter> emitter;

    json req = MakeBaseReq("stream.unknown", "stream-4");

    EXPECT_CALL(emitter, WriteStreamingData(_))
        .WillOnce(Invoke([&](const std::string& s) {
            json out = json::parse(s);
            EXPECT_TRUE(out.contains("error"));
            EXPECT_EQ(out["error"]["code"], static_cast<int>(A2AErrorCode::JSONRPC_METHOD_NOT_FOUND));
        }));

    EXPECT_CALL(emitter, WriteDone()).Times(1);

    server->HandleStreamingRequest(req, "stream.unknown", WrapEmitter(emitter));
}

TEST_F(ServerImplTest, HandleStreamingRequest_WhenHandlerThrowsServerError_WritesError)
{
    NiceMock<MockTransportEmitter> emitter;

    json req = MakeBaseReq(METHOD_TASK_RESUBSCRIBE, "stream-5");
    req["params"] = {
        {"id", "task-sub"}
    };

    EXPECT_CALL(*mockHandler, OnResubscribeToTask(_, _, IsNull()))
        .WillOnce(Throw(A2AServerError("stream failed",
            static_cast<int>(A2AErrorCode::JSONRPC_METHOD_NOT_FOUND))));

    EXPECT_CALL(emitter, WriteStreamingData(_))
        .WillOnce(Invoke([&](const std::string& s) {
            json out = json::parse(s);
            EXPECT_TRUE(out.contains("error"));
            EXPECT_EQ(out["error"]["code"], static_cast<int>(A2AErrorCode::JSONRPC_METHOD_NOT_FOUND));
            EXPECT_EQ(out["error"]["message"], "stream failed");
        }));

    EXPECT_CALL(emitter, WriteDone()).Times(1);

    server->HandleStreamingRequest(req, METHOD_TASK_RESUBSCRIBE, WrapEmitter(emitter));
}

TEST_F(ServerImplTest, HandleStreamingRequest_WhenHandlerThrowsStdException_WritesInternalError)
{
    NiceMock<MockTransportEmitter> emitter;

    json req = MakeBaseReq(METHOD_TASK_RESUBSCRIBE, "stream-6");
    req["params"] = {
        {"id", "task-sub"}
    };

    EXPECT_CALL(*mockHandler, OnResubscribeToTask(_, _, IsNull()))
        .WillOnce(Throw(std::runtime_error("boom")));

    EXPECT_CALL(emitter, WriteStreamingData(_))
        .WillOnce(Invoke([&](const std::string& s) {
            json out = json::parse(s);
            EXPECT_TRUE(out.contains("error"));
            EXPECT_EQ(out["error"]["code"], static_cast<int>(A2AErrorCode::JSONRPC_INTERNAL_ERROR));
        }));

    EXPECT_CALL(emitter, WriteDone()).Times(1);

    server->HandleStreamingRequest(req, METHOD_TASK_RESUBSCRIBE, WrapEmitter(emitter));
}

TEST_F(ServerImplTest, Start_RegisteredRpcHandler_HandlesInvalidJson)
{
    EXPECT_CALL(*mockTransport, SetRpcHandler(_))
        .WillOnce(Invoke([&](Transport::ServerTransportRpcHandler handler) {
            mockTransport->rpcHandler = std::move(handler);
        }));

    EXPECT_CALL(*mockTransport, SetCardHandler(_))
        .WillOnce(Invoke([&](Transport::ServerTransportCardHandler handler) {
            mockTransport->cardHandler = std::move(handler);
        }));

    EXPECT_CALL(*mockTransport, Start())
        .WillOnce(Return(0));

    ASSERT_EQ(server->Start(), 0);

    std::shared_ptr<Transport::TransportEmitter> emitter;
    std::string resp;

    mockTransport->rpcHandler("{invalid json", resp, emitter);

    json out = json::parse(resp);
    EXPECT_TRUE(out.contains("error"));
    EXPECT_EQ(out["error"]["code"], static_cast<int>(A2AErrorCode::JSONRPC_INTERNAL_ERROR));
}

TEST_F(ServerImplTest, Start_RegisteredCardHandler_ReturnsCardResponse)
{
    agentCard->name = "ut-agent";

    EXPECT_CALL(*mockTransport, SetRpcHandler(_))
        .WillOnce(Invoke([&](Transport::ServerTransportRpcHandler handler) {
            mockTransport->rpcHandler = std::move(handler);
        }));

    EXPECT_CALL(*mockTransport, SetCardHandler(_))
        .WillOnce(Invoke([&](Transport::ServerTransportCardHandler handler) {
            mockTransport->cardHandler = std::move(handler);
        }));

    EXPECT_CALL(*mockTransport, Start())
        .WillOnce(Return(0));

    ASSERT_EQ(server->Start(), 0);
    ASSERT_TRUE(static_cast<bool>(mockTransport->cardHandler));

    std::string resp;
    mockTransport->cardHandler("{}", resp);

    json out = json::parse(resp);
    EXPECT_EQ(out["name"], "ut-agent");
}

} // namespace