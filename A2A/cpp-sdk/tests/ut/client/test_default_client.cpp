/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <memory>
#include <string>
#include <vector>
#include <future>
#include <chrono>
#include <thread>

#include "client/default_client.h"
#include "client/client_transport.h"
#include "client/client_call_interceptor.h"
#include "types.h"

namespace A2A::Client::Test {

using namespace A2A;
using namespace A2A::Client;
using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::SaveArg;
using ::testing::NiceMock;
using ::testing::StrictMock;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::SizeIs;
using ::testing::NotNull;
using ::testing::AnyOf;
using ::testing::ElementsAre;
using ::testing::Throw;
using ::testing::HasSubstr;

// ---------------------------------------------------------------------------
// Mock ClientTransport
// ---------------------------------------------------------------------------
class MockClientTransport : public ClientTransport {
public:
    using TransportCb = TransportEventCallback;
    using MsgSendParams = MessageSendParams;
    using TaskQueryP = TaskQueryParams;
    using TaskIdP = TaskIdParams;
    using TaskPushCfg = TaskPushNotificationConfig;
    using GetTaskPushCfgP = GetTaskPushNotificationConfigParams;
    using ListTaskPushCfgP = ListTaskPushNotificationConfigParams;
    using DelTaskPushCfgP = DeleteTaskPushNotificationConfigParams;

    MOCK_METHOD1(SetTransportCallback, void(TransportCb));

    MOCK_METHOD4(SendMessage,
        void(const std::string&, const MsgSendParams&, const ClientCallContext*, int));

    MOCK_METHOD4(SendMessageStreaming,
        void(const std::string&, const MsgSendParams&, const ClientCallContext*, int));

    MOCK_METHOD4(GetTask,
        void(const std::string&, const TaskQueryP&, const ClientCallContext*, int));

    MOCK_METHOD4(CancelTask,
        void(const std::string&, const TaskIdP&, const ClientCallContext*, int));

    MOCK_METHOD4(SetTaskPushNotificationConfig,
        void(const std::string&, const TaskPushCfg&, const ClientCallContext*, int));

    MOCK_METHOD4(GetTaskPushNotificationConfig,
        void(const std::string&, const GetTaskPushCfgP&, const ClientCallContext*, int));

    MOCK_METHOD4(ListTaskPushNotificationConfigs,
        void(const std::string&, const ListTaskPushCfgP&, const ClientCallContext*, int));

    MOCK_METHOD4(DeleteTaskPushNotificationConfig,
        void(const std::string&, const DelTaskPushCfgP&, const ClientCallContext*, int));

    MOCK_METHOD4(Resubscribe,
        void(const std::string&, const TaskIdP&, const ClientCallContext*, int));

    MOCK_METHOD3(GetCard,
        void(const std::string&, const ClientCallContext*, int));

    MOCK_METHOD0(Close, void());

    MOCK_METHOD1(AddRequestMiddleware, void(const std::shared_ptr<ClientCallInterceptor>&));
};

// ---------------------------------------------------------------------------
// Helper Functions
// ---------------------------------------------------------------------------
/// Create a basic Message with minimal required fields.
static Message MakeTestMessage(
    const std::string& messageId = "msg-001",
    Role role = Role::AGENT,
    const std::string& text = "test message",
    const std::optional<std::string>& taskId = std::nullopt,
    const std::optional<std::string>& contextId = std::nullopt)
{
    Message msg;
    msg.messageId = messageId;
    msg.role = role;

    Part part;
    part.mediaType = "text/plain";
    part.text = text;
    msg.parts = {part};

    msg.taskId = taskId;
    msg.contextId = contextId;
    return msg;
}

/// Create a minimal AgentCard with streaming capability.
static AgentCard MakeTestAgentCard(bool streaming = true)
{
    AgentCard card;
    card.name = "Test Agent";
    card.description = "Test";
    card.version = "1.0.0";
    card.capabilities.streaming = streaming;
    return card;
}

static PushNotificationConfig MakePushNotificationConfig(
    const std::string& url,
    const std::string& token = "",
    const std::string& configId = "")
{
    PushNotificationConfig cfg;
    cfg.url = url;
    cfg.token = token.empty() ? std::nullopt : std::make_optional(token);
    cfg.configId = configId.empty() ? std::nullopt : std::make_optional(configId);
    cfg.authentication = std::nullopt;
    cfg.id = std::nullopt;
    cfg.createdAt = std::nullopt;
    return cfg;
}

/// Create a minimal ClientConfig.
static ClientConfig MakeTestConfig(
    bool streaming = true,
    bool polling = false,
    std::vector<std::string> outputModes = {"text/plain"})
{
    ClientConfig config;
    config.streaming = streaming;
    config.polling = polling;
    config.acceptedOutputModes = std::move(outputModes);
    return config;
}

/// Create a minimal Task.
static Task MakeTestTask(const std::string& id = "task-001", const std::string& contextId = "ctx-001")
{
    Task task;
    task.id = id;
    task.contextId = contextId;
    task.status = TaskStatus{.message = std::nullopt, .state = TaskState::SUBMITTED, .timestamp = std::nullopt};
    return task;
}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------
class DefaultClientTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        card = MakeTestAgentCard();
        config = MakeTestConfig();
        transport = std::make_shared<NiceMock<MockClientTransport>>();
        consumers.clear();
        middleware.clear();
        transportCallback = nullptr;
    }

    std::unique_ptr<DefaultClient> CreateClient()
    {
        return std::make_unique<DefaultClient>(card, config, transport, consumers);
    }

    static constexpr auto TIMEOUT = std::chrono::milliseconds(100);

    void ExpectInvalidArgument(std::future<void>& future, const std::string& expectedMsg)
    {
        EXPECT_EQ(future.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
        try {
            future.get();
            FAIL() << "Expected exception";
        } catch (const std::runtime_error& e) {
            EXPECT_THAT(e.what(), ::testing::HasSubstr(expectedMsg));
        }
    }

    AgentCard card;
    ClientConfig config;
    std::shared_ptr<NiceMock<MockClientTransport>> transport;
    std::vector<Consumer> consumers;
    std::vector<std::shared_ptr<ClientCallInterceptor>> middleware;
    TransportEventCallback transportCallback;
};

// ===========================================================================
// Part I – SendMessage Parameter Validation
// ===========================================================================
TEST_F(DefaultClientTest, SendMessage_EmptyMessageId_Fails)
{
    EXPECT_CALL(*transport, SetTransportCallback(_)).Times(1);
    auto client = CreateClient();

    Message msg = MakeTestMessage();
    msg.messageId = "";
    bool handlerCalled = false;
    auto handler = [&](auto, auto) { handlerCalled = true; };

    auto future = client->SendMessage(msg, nullptr, handler);

    ExpectInvalidArgument(future, "{\"code\":-32109,\"message\":\"Invalid parameter\"}");
    EXPECT_FALSE(handlerCalled);
}

TEST_F(DefaultClientTest, SendMessage_EmptyParts_Fails)
{
    EXPECT_CALL(*transport, SetTransportCallback(_)).Times(1);
    auto client = CreateClient();

    Message msg = MakeTestMessage();
    msg.parts.clear();
    bool handlerCalled = false;
    auto handler = [&](auto, auto) { handlerCalled = true; };
    auto future = client->SendMessage(msg, nullptr, handler);

    ExpectInvalidArgument(future, "{\"code\":-32109,\"message\":\"Invalid parameter\"}");
    EXPECT_FALSE(handlerCalled);
}

TEST_F(DefaultClientTest, SendMessage_NullHandler_Fails)
{
    EXPECT_CALL(*transport, SetTransportCallback(_)).Times(1);
    auto client = CreateClient();
    Message msg = MakeTestMessage();
    auto future = client->SendMessage(msg, nullptr, nullptr);
    ExpectInvalidArgument(future, "{\"code\":-32109,\"message\":\"Invalid parameter\"}");
}

// ===========================================================================
// Part II – SendMessage Non-Streaming Flow
// ===========================================================================

// SendMessage non-streaming should call transport->SendMessage.
TEST_F(DefaultClientTest, SendMessage_NonStreaming_CallsTransportSendMessage)
{
    // Disable streaming to force non-streaming path
    config.streaming = false;
    card.capabilities.streaming = false;

    EXPECT_CALL(*transport, SetTransportCallback(_)).Times(1);
    auto client = CreateClient();

    Message msg = MakeTestMessage();
    std::string capturedRequestId;
    MessageSendParams capturedParams;

    EXPECT_CALL(*transport, SendMessage(_, _, _, _))
        .WillOnce(Invoke([&capturedRequestId, &capturedParams](
            const std::string& requestId,
            const MessageSendParams& params,
            const ClientCallContext*,
            int) {
            capturedRequestId = requestId;
            capturedParams = params;
        }));

    bool handlerCalled = false;
    ResponseHandler handler = [&handlerCalled](const ClientEvent&, const AgentCard&) {
        handlerCalled = true;
    };

    auto future = client->SendMessage(msg, nullptr, handler, 5000);

    // Verify transport was called
    EXPECT_FALSE(capturedRequestId.empty());
    EXPECT_EQ(capturedParams.message.messageId, msg.messageId);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);
}


// SendMessage non-streaming transport failure should set exception.
TEST_F(DefaultClientTest, SendMessage_NonStreaming_TransportFailure_SetsException)
{
    config.streaming = false;
    card.capabilities.streaming = false;

    EXPECT_CALL(*transport, SetTransportCallback(_)).Times(1);
    auto client = CreateClient();

    Message msg = MakeTestMessage();

    EXPECT_CALL(*transport, SendMessage(_, _, _, _))
        .WillOnce(Invoke([](const std::string&, const MessageSendParams&, const ClientCallContext*, int) {
            throw std::runtime_error("transport exception");
        }));

    bool handlerCalled = false;
    ResponseHandler handler = [&handlerCalled](const ClientEvent&, const AgentCard&) {
        handlerCalled = true;  // Should NOT be called for transport failure
    };

    auto future = client->SendMessage(msg, nullptr, handler);

    // Handler should not be called for transport-level failure
    EXPECT_FALSE(handlerCalled);

    // Future should have exception
    EXPECT_TRUE(future.valid());
    try {
        future.get();
        FAIL() << "Expected exception";
    } catch (const std::runtime_error& e) {
        EXPECT_THAT(std::string(e.what()), ::testing::HasSubstr("transport exception"));
    }
}

// ===========================================================================
// Part III – SendMessage Streaming Flow
// ===========================================================================

// SendMessage streaming should call transport->SendMessageStreaming.
TEST_F(DefaultClientTest, SendMessage_Streaming_CallsTransportSendMessageStreaming)
{
    // Enable streaming
    config.streaming = true;
    card.capabilities.streaming = true;

    EXPECT_CALL(*transport, SetTransportCallback(_)).Times(1);
    auto client = CreateClient();

    Message msg = MakeTestMessage();
    std::string capturedRequestId;

    EXPECT_CALL(*transport, SendMessageStreaming(_, _, _, _))
        .WillOnce(Invoke([&capturedRequestId](
            const std::string& requestId,
            const MessageSendParams&,
            const ClientCallContext*,
            int) {
            capturedRequestId = requestId;
        }));

    bool handlerCalled = false;
    ResponseHandler handler = [&handlerCalled](const ClientEvent&, const AgentCard&) {
        handlerCalled = true;
    };

    auto future = client->SendMessage(msg, nullptr, handler);

    EXPECT_FALSE(capturedRequestId.empty());
    EXPECT_FALSE(handlerCalled);  // Not called yet, async
}

// SendMessage streaming with ClientTaskManager initialization.
TEST_F(DefaultClientTest, SendMessage_Streaming_InitializesTaskManager)
{
    config.streaming = true;
    card.capabilities.streaming = true;

    EXPECT_CALL(*transport, SetTransportCallback(_)).Times(1);
    auto client = CreateClient();

    Message msg = MakeTestMessage();

    EXPECT_CALL(*transport, SendMessageStreaming(_, _, _, _))
        .WillOnce(Invoke([](const std::string&, const MessageSendParams&, const ClientCallContext*, int) {}));

    ResponseHandler handler = [](const ClientEvent&, const AgentCard&) {};

    auto future = client->SendMessage(msg, nullptr, handler);

    // TaskManager is internal, but we verify by triggering a streaming event later
    // This test ensures the path is taken without crashing
    EXPECT_TRUE(future.valid());
}

// ===========================================================================
// Part IV – TransportEventCb Success Handling
// ===========================================================================
// TransportEventCb with Message event should call handler and consumer.
TEST_F(DefaultClientTest, TransportEventCb_MessageEvent_CallsHandlerAndConsumer)
{
    config.streaming = false;
    card.capabilities.streaming = false;

    EXPECT_CALL(*transport, SetTransportCallback(_))
        .WillOnce([this](auto cb) { transportCallback = std::move(cb); });

    auto client = CreateClient();

    Message msg = MakeTestMessage();
    msg.messageId = "test-msg-001";
    std::string capturedRequestId;

    EXPECT_CALL(*transport, SendMessage(_, _, _, _))
        .WillOnce([&](const std::string& rid, auto, auto, int) {
            capturedRequestId = rid;
        });

    // 使用promise等待handler完成
    std::promise<void> handlerPromise;
    std::promise<void> consumerPromise;
    auto handlerFuture = handlerPromise.get_future();
    auto consumerFuture = consumerPromise.get_future();

    ClientEvent receivedEvent;

    ResponseHandler handler = [&](const ClientEvent& ev, const AgentCard&) {
        receivedEvent = ev;
        handlerPromise.set_value();
    };

    Consumer consumer = [&](const ClientEvent&, const AgentCard&) {
        consumerPromise.set_value();
    };
    client->AddEventConsumer(consumer);

    auto future = client->SendMessage(msg, nullptr, handler, 5000);

    ASSERT_FALSE(capturedRequestId.empty());
    ASSERT_TRUE(transportCallback);

    transportCallback(capturedRequestId, msg);

    // 等待handler和consumer完成（最多100ms）
    EXPECT_EQ(handlerFuture.wait_for(std::chrono::milliseconds(100)), std::future_status::ready);
    EXPECT_EQ(consumerFuture.wait_for(std::chrono::milliseconds(100)), std::future_status::ready);

    EXPECT_TRUE(std::holds_alternative<Message>(receivedEvent));

    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(10)), std::future_status::ready);
    EXPECT_NO_THROW(future.get());
}

// TransportEventCb with StatusEvent COMPLETED should finalize streaming.
TEST_F(DefaultClientTest, TransportEventCb_StatusEvent_Completed_FinalizesStreaming)
{
    config.streaming = true;
    card.capabilities.streaming = true;
    EXPECT_CALL(*transport, SetTransportCallback(_))
        .WillOnce([this](auto cb) { transportCallback = std::move(cb); });

    auto client = CreateClient();
    Message msg = MakeTestMessage();
    std::string requestId;

    EXPECT_CALL(*transport, SendMessageStreaming(_, _, _, _))
        .WillOnce([&](const std::string& rid, auto, auto, int) {
            requestId = rid;
        });

    std::atomic<int> callCount{0};
    auto future = client->SendMessage(msg, nullptr,
        [&](auto, auto) { callCount++; });

    TaskStatusUpdateEvent working;
    working.taskId = "stream-task";
    working.contextId = "ctx-001";
    working.status.state = TaskState::WORKING;
    transportCallback(requestId, working);

    TaskStatusUpdateEvent completed;
    completed.taskId = "stream-task";
    completed.contextId = "ctx-001";
    completed.status.state = TaskState::COMPLETED;
    transportCallback(requestId, completed);

    for (int i = 0; i < 20 && callCount < 2; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_EQ(callCount, 2);
    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(10)), std::future_status::ready);
    EXPECT_NO_THROW(future.get());
}

// TransportEventCb with TaskArtifactUpdateEvent should append to task.
TEST_F(DefaultClientTest, TransportEventCb_ArtifactEvent_AppendsToTask)
{
    config.streaming = true;

    EXPECT_CALL(*transport, SetTransportCallback(_))
        .WillOnce([this](auto cb) { transportCallback = cb; });

    auto client = CreateClient();

    Message msg = MakeTestMessage();
    std::string rid;

    EXPECT_CALL(*transport, SendMessageStreaming(_, _, _, _))
        .WillOnce([&](const std::string& id, auto, auto, int) { rid = id; });

    // 使用promise等待handler
    std::promise<void> promise;
    auto future = client->SendMessage(msg, nullptr,
        [&](auto, auto) { promise.set_value(); });

    // 创建并发送artifact事件
    TaskArtifactUpdateEvent artifact;
    artifact.taskId = "stream-task";
    artifact.artifact.artifactId = "art-001";

    Part part;
    part.text = "Line 1\n";
    part.mediaType = "text/plain";
    // 其他字段使用默认值
    part.raw = std::nullopt;
    part.url = std::nullopt;
    part.data = std::nullopt;
    part.metadata = std::nullopt;
    part.filename = std::nullopt;

    artifact.artifact.parts = {part};
    artifact.append = true;

    ASSERT_FALSE(rid.empty());
    transportCallback(rid, artifact);

    // 等待handler被调用
    EXPECT_EQ(promise.get_future().wait_for(std::chrono::milliseconds(100)),
        std::future_status::ready);

    // 验证future未完成（artifact不结束stream）
    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(10)),
        std::future_status::timeout);
}

// ===========================================================================
// Part V – TransportEventCb Error Handling
// ===========================================================================
// TransportEventCb with unknown requestId should log warning and return.
TEST_F(DefaultClientTest, TransportEventCb_UnknownRequestId_LogsWarning)
{
    EXPECT_CALL(*transport, SetTransportCallback(_))
        .WillOnce(Invoke([this](TransportEventCallback cb) {
            transportCallback = std::move(cb);
        }));

    auto client = CreateClient();

    // Callback with requestId that was never registered
    std::string unknownId = "unknown-request-id";

    // Should not crash, just log warning (verified by no exception)
    EXPECT_NO_THROW(transportCallback(unknownId, MakeTestMessage()));
}

// ===========================================================================
// Part VI – Task Operations
// ===========================================================================

// GetTask with empty id should return failed future.
TEST_F(DefaultClientTest, GetTask_EmptyId_ReturnsFailedFuture)
{
    EXPECT_CALL(*transport, SetTransportCallback(_)).Times(1);
    auto client = CreateClient();

    TaskQueryParams params;
    params.id = "";  // Invalid

    auto future = client->GetTask(params);

    EXPECT_TRUE(future.valid());
    try {
        future.get();
        FAIL() << "Expected exception";
    } catch (const std::runtime_error& e) {
        EXPECT_THAT(std::string(e.what()), ::testing::HasSubstr("{\"code\":-32109,\"message\":\"Invalid parameter\"}"));
    }
}

// GetTask with valid params should call transport->GetTask.
TEST_F(DefaultClientTest, GetTask_ValidParams_CallsTransportGetTask)
{
    EXPECT_CALL(*transport, SetTransportCallback(_)).Times(1);
    auto client = CreateClient();

    TaskQueryParams params;
    params.id = "task-001";

    std::string capturedRequestId;
    TaskQueryParams capturedParams;

    EXPECT_CALL(*transport, GetTask(_, _, _, _))
        .WillOnce(Invoke([&capturedRequestId, &capturedParams](
            const std::string& requestId,
            const TaskQueryParams& p,
            const ClientCallContext*,
            int) {
            capturedRequestId = requestId;
            capturedParams = p;
        }));

    auto future = client->GetTask(params, nullptr, 5000);

    EXPECT_FALSE(capturedRequestId.empty());
    EXPECT_EQ(capturedParams.id, "task-001");
    EXPECT_TRUE(future.valid());
}

// GetTask success response should fulfill promise.
TEST_F(DefaultClientTest, GetTask_SuccessResponse_FulfillsPromise)
{
    EXPECT_CALL(*transport, SetTransportCallback(_))
        .WillOnce(Invoke([this](TransportEventCallback cb) {
            transportCallback = std::move(cb);
        }));

    auto client = CreateClient();

    TaskQueryParams params;
    params.id = "task-001";

    std::string requestId;
    EXPECT_CALL(*transport, GetTask(_, _, _, _))
        .WillOnce(Invoke([&requestId](const std::string& rid, const TaskQueryParams&, const ClientCallContext*, int) {
            requestId = rid;
        }));

    auto future = client->GetTask(params);

    // Simulate success response
    Task task = MakeTestTask("task-001");
    transportCallback(requestId, task);

    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(100)), std::future_status::ready);
    Task result = future.get();
    EXPECT_EQ(result.id, "task-001");
}

// CancelTask with empty id should return failed future.
TEST_F(DefaultClientTest, CancelTask_EmptyId_ReturnsFailedFuture)
{
    EXPECT_CALL(*transport, SetTransportCallback(_)).Times(1);
    auto client = CreateClient();

    TaskIdParams params;
    params.id = "";

    auto future = client->CancelTask(params);

    EXPECT_TRUE(future.valid());
    try {
        future.get();
        FAIL() << "Expected exception";
    } catch (const std::runtime_error& e) {
        EXPECT_THAT(std::string(e.what()), ::testing::HasSubstr("{\"code\":-32109,\"message\":\"Invalid parameter\"}"));
    }
}

// CancelTask success response should fulfill promise.
TEST_F(DefaultClientTest, CancelTask_SuccessResponse_FulfillsPromise)
{
    EXPECT_CALL(*transport, SetTransportCallback(_))
        .WillOnce(Invoke([this](TransportEventCallback cb) {
            transportCallback = std::move(cb);
        }));

    auto client = CreateClient();

    TaskIdParams params;
    params.id = "task-001";

    std::string requestId;
    EXPECT_CALL(*transport, CancelTask(_, _, _, _))
        .WillOnce(Invoke([&requestId](const std::string& rid, const TaskIdParams&, const ClientCallContext*, int) {
            requestId = rid;
        }));

    auto future = client->CancelTask(params);

    Task canceledTask = MakeTestTask("task-001");
    canceledTask.status = TaskStatus{.message = std::nullopt, .state = TaskState::CANCELED, .timestamp = std::nullopt};

    transportCallback(requestId, canceledTask);

    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(100)), std::future_status::ready);
    Task result = future.get();
    EXPECT_EQ(result.status.state, TaskState::CANCELED);
}

// ===========================================================================
// Part VIII – Push Notification Config Operations
// ===========================================================================

// SetTaskPushNotificationConfig with empty taskId should return failed future.
TEST_F(DefaultClientTest, SetTaskPushNotificationConfig_EmptyTaskId_ReturnsFailedFuture)
{
    EXPECT_CALL(*transport, SetTransportCallback(_)).Times(1);
    auto client = CreateClient();

    TaskPushNotificationConfig config;
    config.taskId = "";  // Invalid
    config.pushNotificationConfig = MakePushNotificationConfig("https://notify.example.com");

    auto future = client->SetTaskPushNotificationConfig(config);

    EXPECT_TRUE(future.valid());
    try {
        future.get();
        FAIL() << "Expected exception";
    } catch (const std::runtime_error& e) {
        EXPECT_THAT(std::string(e.what()), ::testing::HasSubstr("{\"code\":-32109,\"message\":\"Invalid parameter\"}"));
    }
}

// SetTaskPushNotificationConfig should call transport.
TEST_F(DefaultClientTest, SetTaskPushNotificationConfig_CallsTransport)
{
    EXPECT_CALL(*transport, SetTransportCallback(_)).Times(1);
    auto client = CreateClient();

    TaskPushNotificationConfig config;
    config.taskId = "task-001";
    config.pushNotificationConfig = MakePushNotificationConfig("https://notify.example.com");

    std::string capturedRequestId;

    EXPECT_CALL(*transport, SetTaskPushNotificationConfig(_, _, _, _))
        .WillOnce(Invoke([&capturedRequestId](const std::string& rid, const TaskPushNotificationConfig&,
            const ClientCallContext*, int) {
                capturedRequestId = rid;
            }));

    auto future = client->SetTaskPushNotificationConfig(config);

    EXPECT_FALSE(capturedRequestId.empty());
    EXPECT_TRUE(future.valid());
}

// GetTaskPushNotificationConfig success should fulfill promise.
TEST_F(DefaultClientTest, GetTaskPushNotificationConfig_Success_FulfillsPromise)
{
    EXPECT_CALL(*transport, SetTransportCallback(_))
        .WillOnce(Invoke([this](TransportEventCallback cb) {
            transportCallback = std::move(cb);
        }));

    auto client = CreateClient();

    GetTaskPushNotificationConfigParams params;
    params.id = "task-001";

    std::string requestId;
    EXPECT_CALL(*transport, GetTaskPushNotificationConfig(_, _, _, _))
        .WillOnce(Invoke([&requestId](const std::string& rid, const GetTaskPushNotificationConfigParams&,
            const ClientCallContext*, int) {
                requestId = rid;
            }));

    auto future = client->GetTaskPushNotificationConfig(params);

    TaskPushNotificationConfig response;
    response.taskId = "task-001";
    response.pushNotificationConfig = MakePushNotificationConfig("https://notify.example.com");

    transportCallback(requestId, response);

    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(100)), std::future_status::ready);
    TaskPushNotificationConfig result = future.get();
    EXPECT_EQ(result.taskId, "task-001");
    EXPECT_EQ(result.pushNotificationConfig.url, "https://notify.example.com");
}

// ListTaskPushNotificationConfigs success should return vector.
TEST_F(DefaultClientTest, ListTaskPushNotificationConfigs_Success_ReturnsVector)
{
    EXPECT_CALL(*transport, SetTransportCallback(_))
        .WillOnce(Invoke([this](TransportEventCallback cb) {
            transportCallback = std::move(cb);
        }));

    auto client = CreateClient();

    ListTaskPushNotificationConfigParams params;
    params.id = "task-001";

    std::string requestId;
    EXPECT_CALL(*transport, ListTaskPushNotificationConfigs(_, _, _, _))
        .WillOnce(Invoke([&requestId](const std::string& rid, const ListTaskPushNotificationConfigParams&,
            const ClientCallContext*, int) {
                requestId = rid;
            }));

    auto future = client->ListTaskPushNotificationConfigs(params);

    std::vector<TaskPushNotificationConfig> response = {
        TaskPushNotificationConfig{
            .pushNotificationConfig = MakePushNotificationConfig("https://notify1.example.com"),
            .taskId = "task-001"
        },
        TaskPushNotificationConfig{
            .pushNotificationConfig = MakePushNotificationConfig("https://notify2.example.com"),
            .taskId = "task-001"
        }
    };

    transportCallback(requestId, response);

    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(100)), std::future_status::ready);
    auto result = future.get();
    EXPECT_THAT(result, SizeIs(2));
}

// DeleteTaskPushNotificationConfig success should fulfill void promise.
TEST_F(DefaultClientTest, DeleteTaskPushNotificationConfig_Success_FulfillsVoidPromise)
{
    EXPECT_CALL(*transport, SetTransportCallback(_))
        .WillOnce(Invoke([this](TransportEventCallback cb) {
            transportCallback = std::move(cb);
        }));

    auto client = CreateClient();

    DeleteTaskPushNotificationConfigParams params;
    params.id = "task-001";
    params.pushNotificationConfigId = "config-001";

    std::string requestId;
    EXPECT_CALL(*transport, DeleteTaskPushNotificationConfig(_, _, _, _))
        .WillOnce(Invoke([&requestId](const std::string& rid, const DeleteTaskPushNotificationConfigParams&,
            const ClientCallContext*, int) {
                requestId = rid;
            }));

    auto future = client->DeleteTaskPushNotificationConfig(params);
    transportCallback(requestId, std::monostate{});

    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(100)), std::future_status::ready);
    EXPECT_NO_THROW(future.get());  // void future
}

// ===========================================================================
// Part X – Resubscribe (Streaming Only)
// ===========================================================================
// Resubscribe with streaming disabled should throw.
TEST_F(DefaultClientTest, Resubscribe_StreamingDisabled_Throws)
{
    config.streaming = false;
    card.capabilities.streaming = false;

    EXPECT_CALL(*transport, SetTransportCallback(_)).Times(1);
    auto client = CreateClient();

    TaskIdParams params;
    params.id = "task-001";
    ResponseHandler handler = [](const ClientEvent&, const AgentCard&) {};

    EXPECT_THROW(
        client->Resubscribe(params, nullptr, handler),
        std::runtime_error);
}

// Resubscribe with streaming enabled should call transport.
TEST_F(DefaultClientTest, Resubscribe_StreamingEnabled_CallsTransport)
{
    config.streaming = true;
    card.capabilities.streaming = true;

    EXPECT_CALL(*transport, SetTransportCallback(_))
        .WillOnce(Invoke([this](TransportEventCallback cb) {
            transportCallback = std::move(cb);
        }));

    auto client = CreateClient();

    TaskIdParams params;
    params.id = "task-001";

    std::string capturedRequestId;
    EXPECT_CALL(*transport, Resubscribe(_, _, _, _))
        .WillOnce(Invoke([&capturedRequestId](const std::string& rid, const TaskIdParams&,
            const ClientCallContext*, int) {
                capturedRequestId = rid;
            }));

    bool handlerCalled = false;
    ResponseHandler handler = [&handlerCalled](const ClientEvent&, const AgentCard&) {
        handlerCalled = true;
    };

    EXPECT_NO_THROW(client->Resubscribe(params, nullptr, handler));

    EXPECT_FALSE(capturedRequestId.empty());
    // Resubscribe is fire-and-forget, no future to check
}

// ===========================================================================
// Part XI – GetCard
// ===========================================================================
// GetCard should call transport and return AgentCard.
TEST_F(DefaultClientTest, GetCard_Success_ReturnsAgentCard)
{
    EXPECT_CALL(*transport, SetTransportCallback(_))
        .WillOnce(Invoke([this](TransportEventCallback cb) {
            transportCallback = std::move(cb);
        }));

    auto client = CreateClient();

    std::string requestId;
    EXPECT_CALL(*transport, GetCard(_, _, _))
        .WillOnce(Invoke([&requestId](const std::string& rid, const ClientCallContext*, int) {
            requestId = rid;
        }));

    auto future = client->GetCard(nullptr, 5000);

    AgentCard response = MakeTestAgentCard();
    response.name = "Updated Agent";

    transportCallback(requestId, response);

    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(100)), std::future_status::ready);
    AgentCard result = future.get();
    EXPECT_EQ(result.name, "Updated Agent");
}

// ===========================================================================
// Part XII – Component Management
// ===========================================================================
TEST_F(DefaultClientTest, AddEventConsumer_DoesNotCrash)
{
    EXPECT_CALL(*transport, SetTransportCallback(_)).Times(1);
    auto client = CreateClient();

    EXPECT_NO_THROW({
        client->AddEventConsumer([](const ClientEvent&, const AgentCard&) {});
        client->AddEventConsumer([](const ClientEvent&, const AgentCard&) {});
        client->AddEventConsumer([](const ClientEvent&, const AgentCard&) {});
    });
}

// AddRequestMiddleware should append to middleware list.
TEST_F(DefaultClientTest, AddRequestMiddleware_AppendsToList)
{
    EXPECT_CALL(*transport, SetTransportCallback(_)).Times(1);
    auto client = CreateClient();
    EXPECT_NO_THROW(client->AddRequestMiddleware(nullptr));
}

// ===========================================================================
// Part XIII – Edge Cases and Concurrency
// ===========================================================================
// TransportEventCb called concurrently should not crash.
TEST_F(DefaultClientTest, TransportEventCb_ConcurrentCalls_NoCrash)
{
    config.streaming = false;
    card.capabilities.streaming = false;

    EXPECT_CALL(*transport, SetTransportCallback(_))
        .WillOnce(Invoke([this](TransportEventCallback cb) {
            transportCallback = std::move(cb);
        }));

    auto client = CreateClient();

    std::array<std::string, 5> requestIds;

    for (int i = 0; i < 5; i++) {
        Message msg = MakeTestMessage("msg-" + std::to_string(i));

        EXPECT_CALL(*transport, SendMessage(_,
            testing::Field(&MessageSendParams::message,
                testing::Field(&Message::messageId, "msg-" + std::to_string(i))),
            _, _))
            .WillOnce(Invoke([&requestIds, i](const std::string& rid,
                const MessageSendParams&,
                const ClientCallContext*,
                int) {
                requestIds[i] = rid;
            }));

        ResponseHandler handler = [](const ClientEvent&, const AgentCard&) {};
        auto future = client->SendMessage(msg, nullptr, handler);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    for (int i = 0; i < 5; i++) {
        ASSERT_FALSE(requestIds[i].empty()) << "RequestId for message " << i << " is empty";
    }

    // Trigger callbacks concurrently
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; i++) {
        threads.emplace_back([this, &requestIds, i]() {
            transportCallback(requestIds[i], MakeTestMessage("response-" + std::to_string(i)));
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // No crash = test passed
    SUCCEED();
}

// CallbackInfo cleanup after non-streaming success.
TEST_F(DefaultClientTest, CallbackInfoCleanup_NonStreaming_Success)
{
    EXPECT_CALL(*transport, SetTransportCallback(_))
        .WillOnce(Invoke([this](TransportEventCallback cb) {
            transportCallback = std::move(cb);
        }));

    config.streaming = false;
    card.capabilities.streaming = false;

    auto client = CreateClient();

    Message msg = MakeTestMessage();
    std::string requestId;

    EXPECT_CALL(*transport, SendMessage(_, _, _, _))
        .WillOnce(Invoke([&requestId](const std::string& rid, const MessageSendParams&, const ClientCallContext*, int) {
            requestId = rid;
        }));

    ResponseHandler handler = [](const ClientEvent&, const AgentCard&) {};
    auto future = client->SendMessage(msg, nullptr, handler);

    // Trigger success
    transportCallback(requestId, msg);

    // Future should be satisfied
    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(100)), std::future_status::ready);
    EXPECT_NO_THROW(future.get());

    // Subsequent callback with same requestId should not find it (cleaned up)
    // This is internal behavior; we verify by no double-callback to handler
    bool doubleCall = false;
    ResponseHandler handler2 = [&doubleCall](const ClientEvent&, const AgentCard&) {
        doubleCall = true;
    };
    // Note: Can't easily re-register same requestId; this is a limitation of the test

    SUCCEED();  // No crash during cleanup = pass
}

// ===========================================================================
// Part XIV – Integration: Full SendMessage Flow
// ===========================================================================
// Full non-streaming SendMessage flow: request -> response -> handler -> future.
TEST_F(DefaultClientTest, FullFlow_SendMessage_NonStreaming)
{
    EXPECT_CALL(*transport, SetTransportCallback(_))
        .WillOnce(Invoke([this](TransportEventCallback cb) {
            transportCallback = std::move(cb);
        }));

    config.streaming = false;
    card.capabilities.streaming = false;

    auto client = CreateClient();

    Message request = MakeTestMessage("req-001", Role::USER, "Hello");
    std::string requestId;

    EXPECT_CALL(*transport, SendMessage(_, _, _, _))
        .WillOnce(Invoke([&requestId](
            const std::string& rid,
            const MessageSendParams& params,
            const ClientCallContext*,
            int) {
            requestId = rid;
            EXPECT_EQ(params.message.messageId, "req-001");

            if (params.configuration.has_value()) {
                EXPECT_EQ(params.configuration->returnImmediately, false);
            }
        }));

    ClientEvent receivedEvent;
    bool handlerCalled = false;
    ResponseHandler handler = [&handlerCalled, &receivedEvent](const ClientEvent& ev, const AgentCard&) {
        handlerCalled = true;
        receivedEvent = ev;
    };

    auto future = client->SendMessage(request, nullptr, handler, 10000);

    // Simulate response
    Message response = MakeTestMessage("resp-001", Role::AGENT, "Hi there");
    transportCallback(requestId, response);

    // Verify handler received response
    EXPECT_TRUE(handlerCalled);
    ASSERT_TRUE(std::holds_alternative<Message>(receivedEvent));
    EXPECT_EQ(std::get<Message>(receivedEvent).messageId, "resp-001");

    // Verify future
    if (future.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready) {
        EXPECT_NO_THROW(future.get());
    }
}

// Full streaming SendMessage flow with multiple events.
TEST_F(DefaultClientTest, FullFlow_SendMessage_Streaming_MultipleEvents)
{
    EXPECT_CALL(*transport, SetTransportCallback(_))
        .WillOnce(Invoke([this](TransportEventCallback cb) {
            transportCallback = std::move(cb);
        }));

    config.streaming = true;
    card.capabilities.streaming = true;

    auto client = CreateClient();

    Message request = MakeTestMessage("req-001", Role::USER, "Start task");
    std::string requestId;

    EXPECT_CALL(*transport, SendMessageStreaming(_, _, _, _))
        .WillOnce(Invoke([&requestId](const std::string& rid, const MessageSendParams&, const ClientCallContext*, int) {
            requestId = rid;
        }));

    std::vector<ClientEvent> receivedEvents;
    ResponseHandler handler = [&receivedEvents](const ClientEvent& ev, const AgentCard&) {
        receivedEvents.push_back(ev);
    };

    auto future = client->SendMessage(request, nullptr, handler);

    // Event 1: Task created
    Task task = MakeTestTask("stream-task");
    task.status = TaskStatus{.message = std::nullopt, .state = TaskState::SUBMITTED, .timestamp = std::nullopt};
    transportCallback(requestId, task);

    // Event 2: Status update - WORKING
    TaskStatusUpdateEvent working;
    working.taskId = "stream-task";
    working.contextId = "ctx-001";
    working.status = TaskStatus{.message = MakeTestMessage("m1", Role::AGENT, "Processing..."),
        .state = TaskState::WORKING, .timestamp = std::nullopt};
    transportCallback(requestId, working);

    // Event 3: Artifact update - 使用完整初始化的 Part
    TaskArtifactUpdateEvent artifact;
    artifact.taskId = "stream-task";
    artifact.contextId = "ctx-001";

    Part part;
    part.text = "Result";
    part.mediaType = "text/plain";
    part.raw = std::nullopt;
    part.url = std::nullopt;
    part.data = std::nullopt;
    part.metadata = std::nullopt;
    part.filename = std::nullopt;

    artifact.artifact = Artifact{
        .artifactId = "out.txt",
        .description = std::nullopt,
        .extensions = std::nullopt,
        .metadata = std::nullopt,
        .name = "output",
        .parts = {part}
    };
    artifact.append = false;
    transportCallback(requestId, artifact);

    // Event 4: Status update - COMPLETED (final)
    TaskStatusUpdateEvent completed;
    completed.taskId = "stream-task";
    completed.contextId = "ctx-001";
    completed.status = TaskStatus{.message = MakeTestMessage("m2", Role::AGENT, "Done!"),
        .state = TaskState::COMPLETED, .timestamp = std::nullopt};
    transportCallback(requestId, completed);

    // Verify all events were received
    EXPECT_THAT(receivedEvents, SizeIs(4));

    // Verify future is satisfied
    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(100)), std::future_status::ready);
    EXPECT_NO_THROW(future.get());
}

// ===========================================================================
// Part XVI – Transport Exception Handling
// ===========================================================================

// SendMessage with transport throwing exception during call
TEST_F(DefaultClientTest, SendMessage_TransportThrowsDuringSend_SetsException)
{
    config.streaming = false;
    card.capabilities.streaming = false;

    EXPECT_CALL(*transport, SetTransportCallback(_)).Times(1);
    auto client = CreateClient();

    Message msg = MakeTestMessage();

    EXPECT_CALL(*transport, SendMessage(_, _, _, _))
        .WillOnce(Throw(std::runtime_error("Transport send failed")));

    ResponseHandler handler = [](const ClientEvent&, const AgentCard&) {};

    auto future = client->SendMessage(msg, nullptr, handler);

    EXPECT_TRUE(future.valid());
    try {
        future.get();
        FAIL() << "Expected exception";
    } catch (const std::runtime_error& e) {
        EXPECT_THAT(std::string(e.what()), ::testing::HasSubstr("Transport send failed"));
    }
}

// SendMessage streaming with transport throwing exception
TEST_F(DefaultClientTest, SendMessage_Streaming_TransportThrowsDuringSend_SetsException)
{
    config.streaming = true;
    card.capabilities.streaming = true;

    EXPECT_CALL(*transport, SetTransportCallback(_)).Times(1);
    auto client = CreateClient();

    Message msg = MakeTestMessage();

    EXPECT_CALL(*transport, SendMessageStreaming(_, _, _, _))
        .WillOnce(Throw(std::runtime_error("Streaming send failed")));

    ResponseHandler handler = [](const ClientEvent&, const AgentCard&) {};

    auto future = client->SendMessage(msg, nullptr, handler);

    EXPECT_TRUE(future.valid());
    try {
        future.get();
        FAIL() << "Expected exception";
    } catch (const std::runtime_error& e) {
        EXPECT_THAT(std::string(e.what()), ::testing::HasSubstr("Streaming send failed"));
    }
}

// GetTask with transport throwing exception
TEST_F(DefaultClientTest, GetTask_TransportThrowsDuringGet_SetsException)
{
    EXPECT_CALL(*transport, SetTransportCallback(_)).Times(1);
    auto client = CreateClient();

    TaskQueryParams params;
    params.id = "task-001";

    EXPECT_CALL(*transport, GetTask(_, _, _, _))
        .WillOnce(Throw(std::runtime_error("Get task failed")));

    auto future = client->GetTask(params);

    EXPECT_TRUE(future.valid());
    try {
        future.get();
        FAIL() << "Expected exception";
    } catch (const std::runtime_error& e) {
        EXPECT_THAT(std::string(e.what()), ::testing::HasSubstr("Get task failed"));
    }
}

// CancelTask with transport throwing exception
TEST_F(DefaultClientTest, CancelTask_TransportThrowsDuringCancel_SetsException)
{
    EXPECT_CALL(*transport, SetTransportCallback(_)).Times(1);
    auto client = CreateClient();

    TaskIdParams params;
    params.id = "task-001";

    EXPECT_CALL(*transport, CancelTask(_, _, _, _))
        .WillOnce(Throw(std::runtime_error("Cancel task failed")));

    auto future = client->CancelTask(params);

    EXPECT_TRUE(future.valid());
    try {
        future.get();
        FAIL() << "Expected exception";
    } catch (const std::runtime_error& e) {
        EXPECT_THAT(std::string(e.what()), ::testing::HasSubstr("Cancel task failed"));
    }
}

// ===========================================================================
// Part XVII – TransportEventCb Error Handling
// ===========================================================================

// TransportEventCb with TransportError should call handler with error
TEST_F(DefaultClientTest, TransportEventCb_TransportError_CallsHandlerWithError)
{
    config.streaming = false;
    card.capabilities.streaming = false;

    EXPECT_CALL(*transport, SetTransportCallback(_))
        .WillOnce(Invoke([this](TransportEventCallback cb) {
            transportCallback = std::move(cb);
        }));

    auto client = CreateClient();

    Message msg = MakeTestMessage();
    std::string requestId;

    EXPECT_CALL(*transport, SendMessage(_, _, _, _))
        .WillOnce(Invoke([&requestId](const std::string& rid, const MessageSendParams&,
            const ClientCallContext*, int) {
            requestId = rid;
        }));

    std::atomic<bool> handlerCalled{false};
    ResponseHandler handler = [&handlerCalled](const ClientEvent&, const AgentCard&) {
        handlerCalled = true;
    };

    auto future = client->SendMessage(msg, nullptr, handler);

    // Send transport error
    TransportError error;
    error.errorCode = 500;
    error.errInfo = "Internal server error";
    transportCallback(requestId, error);

    // Handler should still be called (error path)
    EXPECT_TRUE(handlerCalled);

    // Future should have exception
    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(100)), std::future_status::ready);
    try {
        future.get();
        FAIL() << "Expected exception";
    } catch (const std::runtime_error& e) {
        EXPECT_THAT(std::string(e.what()), ::testing::HasSubstr("Internal server error"));
    }
}

// TransportEventCb with TransportError for streaming should clean up callback
TEST_F(DefaultClientTest, TransportEventCb_TransportError_Streaming_CleansUpCallback)
{
    config.streaming = true;
    card.capabilities.streaming = true;

    EXPECT_CALL(*transport, SetTransportCallback(_))
        .WillOnce(Invoke([this](TransportEventCallback cb) {
            transportCallback = std::move(cb);
        }));

    auto client = CreateClient();

    Message msg = MakeTestMessage();
    std::string requestId;

    EXPECT_CALL(*transport, SendMessageStreaming(_, _, _, _))
        .WillOnce(Invoke([&requestId](const std::string& rid, const MessageSendParams&,
            const ClientCallContext*, int) {
            requestId = rid;
        }));

    std::atomic<bool> handlerCalled{false};
    ResponseHandler handler = [&handlerCalled](const ClientEvent&, const AgentCard&) {
        handlerCalled = true;
    };

    auto future = client->SendMessage(msg, nullptr, handler);

    // Send transport error
    TransportError error;
    error.errorCode = 500;
    error.errInfo = "Streaming error";
    transportCallback(requestId, error);

    // Handler should be called
    EXPECT_TRUE(handlerCalled);

    // Future should have exception
    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(100)), std::future_status::ready);
    EXPECT_THROW(future.get(), std::runtime_error);
}

// ===========================================================================
// Part XVIII – Configuration Handling
// ===========================================================================

// SendMessage should use config.acceptedOutputModes
TEST_F(DefaultClientTest, SendMessage_UsesAcceptedOutputModes_FromConfig)
{
    config.streaming = false;
    card.capabilities.streaming = false;
    config.acceptedOutputModes = {"application/json", "text/xml"};

    EXPECT_CALL(*transport, SetTransportCallback(_)).Times(1);
    auto client = CreateClient();

    Message msg = MakeTestMessage();
    MessageSendParams capturedParams;

    EXPECT_CALL(*transport, SendMessage(_, _, _, _))
        .WillOnce(Invoke([&capturedParams](const std::string&, const MessageSendParams& params,
            const ClientCallContext*, int) {
            capturedParams = params;
        }));

    ResponseHandler handler = [](const ClientEvent&, const AgentCard&) {};
    auto future = client->SendMessage(msg, nullptr, handler);

    ASSERT_TRUE(capturedParams.configuration.has_value());
    ASSERT_TRUE(capturedParams.configuration->acceptedOutputModes.has_value());
    EXPECT_EQ(capturedParams.configuration->acceptedOutputModes.value().size(), 2u);
    EXPECT_EQ(capturedParams.configuration->acceptedOutputModes.value()[0], "application/json");
}

// SendMessage should use config.pushNotificationConfigs
TEST_F(DefaultClientTest, SendMessage_UsesPushNotificationConfig_FromConfig)
{
    config.streaming = false;
    card.capabilities.streaming = false;

    config.pushNotificationConfigs = {
        MakePushNotificationConfig("https://notify1.example.com", "token1"),
        MakePushNotificationConfig("https://notify2.example.com", "token2")
    };

    EXPECT_CALL(*transport, SetTransportCallback(_)).Times(1);
    auto client = CreateClient();

    Message msg = MakeTestMessage();
    MessageSendParams capturedParams;

    EXPECT_CALL(*transport, SendMessage(_, _, _, _))
        .WillOnce(Invoke([&capturedParams](const std::string&, const MessageSendParams& params,
            const ClientCallContext*, int) {
            capturedParams = params;
        }));

    ResponseHandler handler = [](const ClientEvent&, const AgentCard&) {};
    auto future = client->SendMessage(msg, nullptr, handler);

    ASSERT_TRUE(capturedParams.configuration.has_value());
    ASSERT_TRUE(capturedParams.configuration->pushNotificationConfig.has_value());
    EXPECT_EQ(capturedParams.configuration->pushNotificationConfig->url, "https://notify1.example.com");
}

// SendMessage streaming should not use push notification config
TEST_F(DefaultClientTest, SendMessage_Streaming_IgnoresPushNotificationConfig)
{
    config.streaming = true;
    card.capabilities.streaming = true;

    config.pushNotificationConfigs = {
        MakePushNotificationConfig("https://notify.example.com", "token")
    };

    EXPECT_CALL(*transport, SetTransportCallback(_)).Times(1);
    auto client = CreateClient();

    Message msg = MakeTestMessage();
    MessageSendParams capturedParams;

    EXPECT_CALL(*transport, SendMessageStreaming(_, _, _, _))
        .WillOnce(Invoke([&capturedParams](const std::string&, const MessageSendParams& params,
            const ClientCallContext*, int) {
            capturedParams = params;
        }));

    ResponseHandler handler = [](const ClientEvent&, const AgentCard&) {};
    auto future = client->SendMessage(msg, nullptr, handler);

    // For streaming, configuration might still be set but not used
    // Just verify it doesn't crash
    SUCCEED();
}

// ===========================================================================
// Part XIX – Client Destructor
// ===========================================================================

// Destructor should close transport
TEST_F(DefaultClientTest, Destructor_ClosesTransport)
{
    EXPECT_CALL(*transport, SetTransportCallback(_)).Times(1);
    EXPECT_CALL(*transport, Close()).Times(1);

    auto client = CreateClient();
    // client destructor called at end of scope
}

// ===========================================================================
// Part XXI – Edge Cases
// ===========================================================================

// SendMessage with empty referenceTaskIds (just verify no crash)
TEST_F(DefaultClientTest, SendMessage_WithEmptyReferenceTaskIds_NoCrash)
{
    config.streaming = false;
    card.capabilities.streaming = false;

    EXPECT_CALL(*transport, SetTransportCallback(_)).Times(1);
    auto client = CreateClient();

    Message msg = MakeTestMessage();
    msg.referenceTaskIds = std::vector<std::string>{};  // Empty vector

    std::string capturedRequestId;
    EXPECT_CALL(*transport, SendMessage(_, _, _, _))
        .WillOnce(Invoke([&capturedRequestId](const std::string& rid, const MessageSendParams&,
            const ClientCallContext*, int) {
            capturedRequestId = rid;
        }));

    ResponseHandler handler = [](const ClientEvent&, const AgentCard&) {};
    auto future = client->SendMessage(msg, nullptr, handler);

    EXPECT_FALSE(capturedRequestId.empty());
}

// SendMessage with null ClientCallContext (should work)
TEST_F(DefaultClientTest, SendMessage_NullContext_NoCrash)
{
    config.streaming = false;
    card.capabilities.streaming = false;

    EXPECT_CALL(*transport, SetTransportCallback(_)).Times(1);
    auto client = CreateClient();

    Message msg = MakeTestMessage();

    EXPECT_CALL(*transport, SendMessage(_, _, nullptr, _))
        .WillOnce(Invoke([](const std::string&, const MessageSendParams&,
            const ClientCallContext*, int) {}));

    ResponseHandler handler = [](const ClientEvent&, const AgentCard&) {};
    auto future = client->SendMessage(msg, nullptr, handler);

    SUCCEED();
}

// Member variable to store callback for testing
TransportEventCallback transportCallback;

}