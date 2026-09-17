/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#define private public
#define protected public
#include "server/jsonrpc_handler.h"
#undef private
#undef protected

#include "error.h"
#include "jsonrpc.h"
#include "server/request_handler.h"
#include "types.h"
#include "shared/common_types.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::IsNull;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::Throw;

using nlohmann::json;

using namespace A2A;
using namespace A2A::Server;

namespace {

class MockRequestHandler : public RequestHandler {
public:
    MOCK_METHOD(void, OnSendMessage,
        (const MessageSendParams&, std::shared_ptr<ServerCallContext>, StreamEmitter, const std::string&), (override));

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

class JSONRPCHandlerTest : public ::testing::Test {
protected:
    std::shared_ptr<NiceMock<MockRequestHandler>> mockHandler =
        std::make_shared<NiceMock<MockRequestHandler>>();
    std::unique_ptr<JSONRPCHandler> handler =
        std::make_unique<JSONRPCHandler>(mockHandler);

    static Message MakeMessage(const std::string& messageId = "msg-1")
    {
        Message msg;
        msg.messageId = messageId;
        msg.contextId = "ctx-1";
        msg.taskId = "task-1";
        msg.role = Role::USER;
        return msg;
    }

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

    static TaskPushNotificationConfig MakePushConfig()
    {
        json j = {
            {"taskId", "task-1"},
            {"pushNotificationConfig", {
                {"id", "cfg-1"},
                {"url", "http://callback.test"}
            }}
        };
        return j.get<TaskPushNotificationConfig>();
    }

    static AgentCard MakeAgentCard()
    {
        AgentCard card;
        card.name = "ut-agent";
        return card;
    }

    static json MakeBaseReq(const std::string& id = "req-1")
    {
        return json{
            {"jsonrpc", "2.0"},
            {"id", id}
        };
    }
};

TEST_F(JSONRPCHandlerTest, OnMessageSend_ReturnsMessageResult)
{
    Message msg = MakeMessage("msg-send-2");
    Message returned = MakeMessage("msg-return-2");
    returned.contextId = "ctx-2";
    returned.taskId = "task-2";

    json params = json::object();
    params["message"] = msg;

    json req = MakeBaseReq();
    req["params"] = params;

    json resp = handler->OnMessageSend(req, [](const auto&) {}, METHOD_MESSAGE_SEND);

    EXPECT_EQ(resp, json());
}

TEST_F(JSONRPCHandlerTest, OnMessageSend_A2AServerError_ReturnsError)
{
    Message msg = MakeMessage("msg-send-3");

    json params = json::object();
    params["message"] = msg;

    json req = MakeBaseReq();
    req["params"] = params;

    EXPECT_CALL(*mockHandler, OnSendMessage(_, _, IsNull(), METHOD_MESSAGE_SEND))
        .WillOnce(Throw(A2AServerError("send failed",
            static_cast<int>(A2AErrorCode::JSONRPC_INVALID_REQUEST))));

    json resp = handler->OnMessageSend(req, nullptr, METHOD_MESSAGE_SEND);

    EXPECT_TRUE(resp.contains("error"));
    EXPECT_EQ(resp["error"]["code"], static_cast<int>(A2AErrorCode::JSONRPC_INVALID_REQUEST));
    EXPECT_EQ(resp["error"]["message"], "send failed");
}

TEST_F(JSONRPCHandlerTest, OnMessageSend_StdException_ReturnsInternalError)
{
    Message msg = MakeMessage("msg-send-4");

    json params = json::object();
    params["message"] = msg;

    json req = MakeBaseReq();
    req["params"] = params;

    EXPECT_CALL(*mockHandler, OnSendMessage(_, _, _, METHOD_MESSAGE_SEND))
        .WillOnce(Throw(std::runtime_error("boom")));

    json resp = handler->OnMessageSend(req, [](const auto&) {}, METHOD_MESSAGE_SEND);

    EXPECT_TRUE(resp.contains("error"));
    EXPECT_EQ(resp["error"]["code"], static_cast<int>(A2AErrorCode::JSONRPC_INTERNAL_ERROR));
    EXPECT_EQ(resp["error"]["message"], "boom");
}

TEST_F(JSONRPCHandlerTest, OnGetTask_Success_WithHistoryLength)
{
    Task task = MakeTask("task-9");

    json params = json::object();
    params["id"] = "task-9";
    params["historyLength"] = 3;

    json req = MakeBaseReq();
    req["params"] = params;

    EXPECT_CALL(*mockHandler, OnGetTask(_, IsNull()))
        .WillOnce(Return(task));

    json resp = handler->OnGetTask(req);

    EXPECT_EQ(resp["jsonrpc"], "2.0");
    EXPECT_EQ(resp["id"], "req-1");
    EXPECT_EQ(resp["result"]["id"], "task-9");
}

TEST_F(JSONRPCHandlerTest, OnGetTask_A2AServerError_ReturnsError)
{
    json params = json::object();
    params["id"] = "task-404";

    json req = MakeBaseReq();
    req["params"] = params;

    EXPECT_CALL(*mockHandler, OnGetTask(_, IsNull()))
        .WillOnce(Throw(A2AServerError("not found",
            static_cast<int>(A2AErrorCode::TASK_NOT_FOUND))));

    json resp = handler->OnGetTask(req);

    EXPECT_TRUE(resp.contains("error"));
    EXPECT_EQ(resp["error"]["code"], static_cast<int>(A2AErrorCode::TASK_NOT_FOUND));
    EXPECT_EQ(resp["error"]["message"], "not found");
}

TEST_F(JSONRPCHandlerTest, OnCancelTask_Success)
{
    Task task = MakeTask("task-cancel");

    json params = json::object();
    params["id"] = "task-cancel";

    json req = MakeBaseReq();
    req["params"] = params;

    EXPECT_CALL(*mockHandler, OnCancelTask(_, IsNull()))
        .WillOnce(Return(task));

    json resp = handler->OnCancelTask(req);

    EXPECT_EQ(resp["jsonrpc"], "2.0");
    EXPECT_EQ(resp["id"], "req-1");
    EXPECT_EQ(resp["result"]["id"], "task-cancel");
}

TEST_F(JSONRPCHandlerTest, OnCancelTask_StdException_ReturnsInternalError)
{
    json params = json::object();
    params["id"] = "task-cancel";

    json req = MakeBaseReq();
    req["params"] = params;

    EXPECT_CALL(*mockHandler, OnCancelTask(_, IsNull()))
        .WillOnce(Throw(std::runtime_error("cancel boom")));

    json resp = handler->OnCancelTask(req);

    EXPECT_TRUE(resp.contains("error"));
    EXPECT_EQ(resp["error"]["code"], static_cast<int>(A2AErrorCode::JSONRPC_INTERNAL_ERROR));
    EXPECT_EQ(resp["error"]["message"], "cancel boom");
}

TEST_F(JSONRPCHandlerTest, OnSetPushNotificationConfig_Success)
{
    TaskPushNotificationConfig cfg = MakePushConfig();
    TaskPushNotificationConfig captured{};

    json req = MakeBaseReq();
    req["params"] = cfg;

    EXPECT_CALL(*mockHandler, OnSetTaskPushNotificationConfig(_, IsNull()))
        .WillOnce(DoAll(
            SaveArg<0>(&captured)
        ));

    handler->OnSetPushNotificationConfig(req);

    EXPECT_EQ(captured.taskId, "task-1");
    EXPECT_EQ(captured.pushNotificationConfig.url, "http://callback.test");
}

TEST_F(JSONRPCHandlerTest, OnGetPushNotificationConfig_Success)
{
    TaskPushNotificationConfig cfg = MakePushConfig();
    GetTaskPushNotificationConfigParams captured{};

    GetTaskPushNotificationConfigParams p;
    p.id = "task-1";

    json req = MakeBaseReq();
    req["params"] = p;

    EXPECT_CALL(*mockHandler, OnGetTaskPushNotificationConfig(_, IsNull()))
        .WillOnce(DoAll(
            SaveArg<0>(&captured),
            Return(cfg)
        ));

    json resp = handler->OnGetPushNotificationConfig(req);

    EXPECT_EQ(captured.id, "task-1");
    EXPECT_EQ(resp["result"]["taskId"], "task-1");
}

TEST_F(JSONRPCHandlerTest, OnListPushNotificationConfig_Success)
{
    TaskPushNotificationConfig cfg1 = MakePushConfig();
    TaskPushNotificationConfig cfg2 = MakePushConfig();
    cfg2.pushNotificationConfig.url = "http://callback2.test";

    ListTaskPushNotificationConfigParams captured{};
    ListTaskPushNotificationConfigParams p;
    p.id = "task-1";

    json req = MakeBaseReq();
    req["params"] = p;

    EXPECT_CALL(*mockHandler, OnListTaskPushNotificationConfigs(_, IsNull()))
        .WillOnce(DoAll(
            SaveArg<0>(&captured),
            Return(std::vector<TaskPushNotificationConfig>{cfg1, cfg2})
        ));

    json resp = handler->OnListPushNotificationConfig(req);

    EXPECT_EQ(captured.id, "task-1");
    ASSERT_EQ(resp["result"].size(), 2u);
    EXPECT_EQ(resp["result"][0]["pushNotificationConfig"]["url"], "http://callback.test");
    EXPECT_EQ(resp["result"][1]["pushNotificationConfig"]["url"], "http://callback2.test");
}

TEST_F(JSONRPCHandlerTest, OnDeletePushNotificationConfig_Success)
{
    DeleteTaskPushNotificationConfigParams captured{};
    DeleteTaskPushNotificationConfigParams p;
    p.id = "task-1";
    p.pushNotificationConfigId = "cfg-1";

    json req = MakeBaseReq();
    req["params"] = p;

    EXPECT_CALL(*mockHandler, OnDeleteTaskPushNotificationConfig(_, IsNull()))
        .WillOnce(DoAll(
            SaveArg<0>(&captured)
        ));

    json resp = handler->OnDeletePushNotificationConfig(req);

    EXPECT_EQ(captured.id, "task-1");
    EXPECT_EQ(captured.pushNotificationConfigId, "cfg-1");
    EXPECT_TRUE(resp["result"].is_null());
}

TEST_F(JSONRPCHandlerTest, OnDeletePushNotificationConfig_A2AServerError_ReturnsError)
{
    DeleteTaskPushNotificationConfigParams p;
    p.id = "task-1";
    p.pushNotificationConfigId = "cfg-1";

    json req = MakeBaseReq();
    req["params"] = p;

    EXPECT_CALL(*mockHandler, OnDeleteTaskPushNotificationConfig(_, IsNull()))
        .WillOnce(Throw(A2AServerError("delete failed",
            static_cast<int>(A2AErrorCode::TASK_NOT_FOUND))));

    json resp = handler->OnDeletePushNotificationConfig(req);

    EXPECT_TRUE(resp.contains("error"));
    EXPECT_EQ(resp["error"]["code"], static_cast<int>(A2AErrorCode::TASK_NOT_FOUND));
    EXPECT_EQ(resp["error"]["message"], "delete failed");
}

TEST_F(JSONRPCHandlerTest, OnGetAgentCard_Success)
{
    AgentCard card = MakeAgentCard();

    json req = MakeBaseReq();

    EXPECT_CALL(*mockHandler, OnGetCard(IsNull()))
        .WillOnce(Return(card));

    json resp = handler->OnGetAgentCard(req);

    EXPECT_EQ(resp["jsonrpc"], "2.0");
    EXPECT_EQ(resp["id"], "req-1");
    EXPECT_EQ(resp["result"]["name"], "ut-agent");
}

TEST_F(JSONRPCHandlerTest, OnGetAgentCard_StdException_ReturnsInternalError)
{
    json req = MakeBaseReq();

    EXPECT_CALL(*mockHandler, OnGetCard(IsNull()))
        .WillOnce(Throw(std::runtime_error("card boom")));

    json resp = handler->OnGetAgentCard(req);

    EXPECT_TRUE(resp.contains("error"));
    EXPECT_EQ(resp["error"]["code"], static_cast<int>(A2AErrorCode::JSONRPC_INTERNAL_ERROR));
    EXPECT_EQ(resp["error"]["message"], "card boom");
}

TEST_F(JSONRPCHandlerTest, OnMessageSendStreaming_PassesParamsAndEmitter)
{
    Message msg = MakeMessage("msg-stream-in");
    MessageSendParams captured{};
    std::vector<StreamEvent> emitted;

    json params = json::object();
    params["message"] = msg;

    json req = MakeBaseReq();
    req["params"] = params;

    StreamEmitter emit = [&](const StreamEvent& ev) {
        emitted.push_back(ev);
    };

    EXPECT_CALL(*mockHandler, OnSendMessageStreaming(_, _, IsNull()))
        .WillOnce([&](const MessageSendParams& p,
                        const StreamEmitter& cb,
                        std::shared_ptr<ServerCallContext>) {
            captured = p;
            Task task = MakeTask("task-stream");
            cb(task);
        });

    handler->OnMessageSendStreaming(req, emit);

    EXPECT_EQ(captured.message.messageId, "msg-stream-in");
    ASSERT_TRUE(captured.message.taskId.has_value());
    EXPECT_EQ(*captured.message.taskId, "task-1");

    ASSERT_EQ(emitted.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<Task>(emitted[0]));
    EXPECT_EQ(std::get<Task>(emitted[0]).id, "task-stream");
}

TEST_F(JSONRPCHandlerTest, OnResubscribeToTask_PassesParamsAndEmitter)
{
    TaskIdParams captured{};
    std::vector<StreamEvent> emitted;

    json params = json::object();
    params["id"] = "task-sub";

    json req = MakeBaseReq();
    req["params"] = params;

    StreamEmitter emit = [&](const StreamEvent& ev) {
        emitted.push_back(ev);
    };

    EXPECT_CALL(*mockHandler, OnResubscribeToTask(_, _, IsNull()))
        .WillOnce([&](const TaskIdParams& p,
                        const StreamEmitter& cb,
                        std::shared_ptr<ServerCallContext>) {
            captured = p;
            Message out = MakeMessage("msg-sub-1");
            out.taskId = "task-sub";
            cb(out);
        });

    handler->OnResubscribeToTask(req, emit);

    EXPECT_EQ(captured.id, "task-sub");

    ASSERT_EQ(emitted.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<Message>(emitted[0]));
    const auto& out = std::get<Message>(emitted[0]);
    EXPECT_EQ(out.messageId, "msg-sub-1");
    ASSERT_TRUE(out.taskId.has_value());
    EXPECT_EQ(*out.taskId, "task-sub");
}

TEST_F(JSONRPCHandlerTest, OnMessageSendStreaming_WhenParamsMissing_Throws)
{
    json req = MakeBaseReq();
    req["params"] = json::object();

    StreamEmitter emit = [&](const StreamEvent&) {};

    EXPECT_THROW(handler->OnMessageSendStreaming(req, emit), std::exception);
}

TEST_F(JSONRPCHandlerTest, OnResubscribeToTask_WhenParamsMissingId_Throws)
{
    json req = MakeBaseReq();
    req["params"] = json::object();

    StreamEmitter emit = [&](const StreamEvent&) {};

    EXPECT_THROW(handler->OnResubscribeToTask(req, emit), std::exception);
}

} // namespace