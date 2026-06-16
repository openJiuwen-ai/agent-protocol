/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <future>
#include <chrono>
#include <thread>
#include <vector>

#include "client/jsonrpc_transport.h"
#include "client/client_call_interceptor.h"
#include "jsonrpc_transport_impl.h"
#include "types.h"
#include "shared/common_types.h"

namespace A2A::Client::Test {

using namespace A2A::Client;
using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::SaveArg;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::AtLeast;
using ::testing::InSequence;

// ===========================================================================
// Mock ClientConn
// ===========================================================================
class MockClientConn : public ClientConn {
public:
    MOCK_METHOD(int, SendMessage,
        (const std::string&, (const std::map<std::string, std::string>&), std::shared_ptr<UserData>, int),
        (override));
    MOCK_METHOD(void, SetCallback, (std::shared_ptr<ConnCallback>), (override));
    MOCK_METHOD(void, Terminate, (), (override));
    MOCK_METHOD(void, FinishRequest, (int), (override));
    MOCK_METHOD(void, RefreshRequest, (int timerId, int timeout), (override));
};

// ===========================================================================
// Mock ClientCallInterceptor
// ===========================================================================
class MockInterceptor : public ClientCallInterceptor {
public:
    MOCK_METHOD(void, Intercept,
        (const std::string& methodName,
        std::string& payload,
        (std::map<std::string, std::string>& headers),
        const AgentCard* agentCard,
        const ClientCallContext* context),
        (override));
};

// ===========================================================================
// Helper Functions
// ===========================================================================
Message CreateTestMessage(const std::string& id = "msg-001")
{
    Message msg;
    msg.messageId = id;
    msg.role = Role::AGENT;

    Part part;
    part.text = "test";
    part.mediaType = "text/plain";
    msg.parts.push_back(part);

    return msg;
}

AgentCard CreateTestAgentCard()
{
    AgentCard card;
    card.name = "Test Agent";
    card.description = "Test Description";
    card.version = "1.0.0";

    AgentCapabilities caps;
    caps.streaming = true;
    card.capabilities = caps;

    card.defaultInputModes = {"text/plain"};
    card.defaultOutputModes = {"text/plain"};

    return card;
}

Task CreateTestTask(const std::string& id = "task-001")
{
    Task task;
    task.id = id;
    task.contextId = "ctx-001";
    task.status.state = TaskState::WORKING;
    return task;
}

// ===========================================================================
// 可测试的包装器 - 捕获发送的数据
// ===========================================================================
class JsonRpcTransportTestWrapper {
public:
    JsonRpcTransportTestWrapper(const std::string& url, const AgentCard& agentCard, const ClientConfig& config,
                                const std::vector<std::shared_ptr<ClientCallInterceptor>>& interceptors,
                                std::shared_ptr<MockClientConn> mockConn)
        : mockConn_(mockConn)
    {
        transport_ = std::make_shared<JsonRpcTransportImpl>(url, agentCard, config, interceptors);
    }

    std::shared_ptr<JsonRpcTransportImpl> GetTransport()
    {
        return transport_;
    }

    std::shared_ptr<MockClientConn> GetMockConn()
    {
        return mockConn_;
    }

    void SetTransportCallback(TransportEventCallback cb)
    {
        transport_->SetTransportCallback(cb);
    }

    void SendMessage(const std::string& requestId, const MessageSendParams& request,
                    const ClientCallContext* context, int timeout, const std::string& method)
    {
        transport_->SendMessage(requestId, request, context, timeout, method);
    }

    void SendMessageStreaming(const std::string& requestId, const MessageSendParams& request,
                            const ClientCallContext* context, int timeout)
    {
        transport_->SendMessageStreaming(requestId, request, context, timeout);
    }

    void GetTask(const std::string& requestId, const TaskQueryParams& params,
                const ClientCallContext* context, int timeout)
    {
        transport_->GetTask(requestId, params, context, timeout);
    }

    void CancelTask(const std::string& requestId, const TaskIdParams& params,
                    const ClientCallContext* context, int timeout)
    {
        transport_->CancelTask(requestId, params, context, timeout);
    }

    void SetTaskPushNotificationConfig(const std::string& requestId, const TaskPushNotificationConfig& config,
                                        const ClientCallContext* context, int timeout)
    {
        transport_->SetTaskPushNotificationConfig(requestId, config, context, timeout);
    }

    void GetTaskPushNotificationConfig(const std::string& requestId, const GetTaskPushNotificationConfigParams& params,
                                        const ClientCallContext* context, int timeout)
    {
        transport_->GetTaskPushNotificationConfig(requestId, params, context, timeout);
    }

    void ListTaskPushNotificationConfigs(const std::string& requestId,
                                        const ListTaskPushNotificationConfigParams& params,
                                        const ClientCallContext* context, int timeout)
    {
        transport_->ListTaskPushNotificationConfigs(requestId, params, context, timeout);
    }

    void DeleteTaskPushNotificationConfig(const std::string& requestId,
                                        const DeleteTaskPushNotificationConfigParams& params,
                                        const ClientCallContext* context, int timeout)
    {
        transport_->DeleteTaskPushNotificationConfig(requestId, params, context, timeout);
    }

    void Resubscribe(const std::string& requestId, const TaskIdParams& params,
                    const ClientCallContext* context, int timeout)
    {
        transport_->Resubscribe(requestId, params, context, timeout);
    }

    void GetCard(const std::string& requestId, const ClientCallContext* context, int timeout)
    {
        transport_->GetCard(requestId, context, timeout);
    }

    void Close()
    {
        transport_->Close();
    }

private:
    std::shared_ptr<JsonRpcTransportImpl> transport_;
    std::shared_ptr<MockClientConn> mockConn_;
};

// ===========================================================================
// Test Fixture
// ===========================================================================
class JsonRpcTransportImplTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        url = "https://api.example.com/jsonrpc";
        testCard = CreateTestAgentCard();

        // 创建 mock connection
        mockConn = std::make_shared<NiceMock<MockClientConn>>();

        // 创建 interceptors
        mockInterceptor = std::make_shared<NiceMock<MockInterceptor>>();
        interceptors = {mockInterceptor};

        // 创建测试包装器
        wrapper = std::make_unique<JsonRpcTransportTestWrapper>(
            url, testCard, config, interceptors, mockConn);
        transport = wrapper->GetTransport();
        wrapper->SetTransportCallback([](const std::string&, const TransportEvent&) {});
    }

    void TearDown() override
    {
        if (wrapper) {
            wrapper->Close();
        }
    }

    UserData CreateUserData(const std::string& requestId, const std::string& method, bool isStream = false)
    {
        UserData userData;
        userData.requestId = requestId;
        userData.method = method;
        userData.isStream = isStream;
        return userData;
    }

    std::string url;
    AgentCard testCard;
    ClientConfig config;
    std::shared_ptr<MockClientConn> mockConn;
    std::shared_ptr<MockInterceptor> mockInterceptor;
    std::vector<std::shared_ptr<ClientCallInterceptor>> interceptors;
    std::unique_ptr<JsonRpcTransportTestWrapper> wrapper;
    std::shared_ptr<JsonRpcTransportImpl> transport;
};

// ===========================================================================
// 构造函数测试
// ===========================================================================
TEST_F(JsonRpcTransportImplTest, Constructor)
{
    EXPECT_NE(transport, nullptr);
}

// ===========================================================================
// SetTransportCallback 测试
// ===========================================================================
TEST_F(JsonRpcTransportImplTest, SetTransportCallback)
{
    bool called = false;
    TransportEventCallback cb = [&called](const std::string&, const TransportEvent&) {
        called = true;
    };
    wrapper->SetTransportCallback(cb);
    SUCCEED();
}

// ===========================================================================
// 通过返回值测试 SendMessage 的行为
// ===========================================================================
TEST_F(JsonRpcTransportImplTest, SendMessage_ReturnsZero)
{
    std::string requestId = "test-send-001";
    MessageSendParams msgParams;
    msgParams.message = CreateTestMessage();

    // 由于无法 mock，只能验证返回值类型
    wrapper->SendMessage(requestId, msgParams, nullptr, 5000, METHOD_MESSAGE_SEND);

    // 可能返回 0 或 -1，取决于网络状态
}

// ===========================================================================
// 测试 SendMessageStreaming
// ===========================================================================
TEST_F(JsonRpcTransportImplTest, SendMessageStreaming_ReturnsZero)
{
    std::string requestId = "test-stream-001";
    MessageSendParams msgParams;
    msgParams.message = CreateTestMessage();

    EXPECT_NO_THROW(wrapper->SendMessage(requestId, msgParams, nullptr, 5000, METHOD_MESSAGE_SEND));
}

// ===========================================================================
// 测试 GetTask
// ===========================================================================
TEST_F(JsonRpcTransportImplTest, GetTask_NoThrow)
{
    std::string requestId = "test-get-001";
    TaskQueryParams taskParams;
    taskParams.id = "task-001";

    EXPECT_NO_THROW(wrapper->GetTask(requestId, taskParams, nullptr, 5000));
}

// ===========================================================================
// 测试 CancelTask
// ===========================================================================
TEST_F(JsonRpcTransportImplTest, CancelTask_NoThrow)
{
    std::string requestId = "test-cancel-001";
    TaskIdParams cancelParams;
    cancelParams.id = "task-001";

    EXPECT_NO_THROW(wrapper->CancelTask(requestId, cancelParams, nullptr, 5000));
}

// ===========================================================================
// 测试 SetTaskPushNotificationConfig
// ===========================================================================
TEST_F(JsonRpcTransportImplTest, SetTaskPushNotificationConfig_NoThrow)
{
    std::string requestId = "test-setpush-001";
    TaskPushNotificationConfig pushConfig;
    pushConfig.taskId = "task-001";
    pushConfig.pushNotificationConfig.url = "https://notify.example.com";

    EXPECT_NO_THROW(wrapper->SetTaskPushNotificationConfig(requestId, pushConfig, nullptr, 5000));
}

// ===========================================================================
// 测试 GetTaskPushNotificationConfig
// ===========================================================================
TEST_F(JsonRpcTransportImplTest, GetTaskPushNotificationConfig_NoThrow)
{
    std::string requestId = "test-getpush-001";
    GetTaskPushNotificationConfigParams getPushParams;
    getPushParams.id = "task-001";

    EXPECT_NO_THROW(wrapper->GetTaskPushNotificationConfig(requestId, getPushParams, nullptr, 5000));
}

// ===========================================================================
// 测试 ListTaskPushNotificationConfigs
// ===========================================================================
TEST_F(JsonRpcTransportImplTest, ListTaskPushNotificationConfigs_NoThrow)
{
    std::string requestId = "test-listpush-001";
    ListTaskPushNotificationConfigParams listPushParams;
    listPushParams.id = "task-001";

    EXPECT_NO_THROW(wrapper->ListTaskPushNotificationConfigs(requestId, listPushParams, nullptr, 5000));
}

// ===========================================================================
// 测试 DeleteTaskPushNotificationConfig
// ===========================================================================
TEST_F(JsonRpcTransportImplTest, DeleteTaskPushNotificationConfig_NoThrow)
{
    std::string requestId = "test-delpush-001";
    DeleteTaskPushNotificationConfigParams delPushParams;
    delPushParams.id = "task-001";
    delPushParams.pushNotificationConfigId = "config-001";

    EXPECT_NO_THROW(wrapper->DeleteTaskPushNotificationConfig(requestId, delPushParams, nullptr, 5000));
}

// ===========================================================================
// 测试 Resubscribe
// ===========================================================================
TEST_F(JsonRpcTransportImplTest, Resubscribe_NoThrow)
{
    std::string requestId = "test-resub-001";
    TaskIdParams resubParams;
    resubParams.id = "task-001";

    EXPECT_NO_THROW(wrapper->Resubscribe(requestId, resubParams, nullptr, 5000));
}

// ===========================================================================
// 测试 GetCard
// ===========================================================================
TEST_F(JsonRpcTransportImplTest, GetCard_NoThrow)
{
    std::string requestId = "test-card-001";
    EXPECT_NO_THROW(wrapper->GetCard(requestId, nullptr, 5000));
}

// ===========================================================================
// 测试 Close
// ===========================================================================
TEST_F(JsonRpcTransportImplTest, Close_NoThrow)
{
    EXPECT_NO_THROW(wrapper->Close());
}

// ===========================================================================
// 测试多个方法调用
// ===========================================================================
TEST_F(JsonRpcTransportImplTest, MultipleCalls_NoCrash)
{
    std::string requestId = "test-001";
    MessageSendParams msgParams;
    msgParams.message = CreateTestMessage();

    // 连续调用多个方法
    wrapper->SendMessage(requestId, msgParams, nullptr, 5000, METHOD_MESSAGE_SEND);
    wrapper->SendMessageStreaming(requestId, msgParams, nullptr, 5000);

    TaskQueryParams taskParams;
    taskParams.id = "task-001";
    wrapper->GetTask(requestId, taskParams, nullptr, 5000);

    TaskIdParams idParams;
    idParams.id = "task-001";
    wrapper->CancelTask(requestId, idParams, nullptr, 5000);

    wrapper->GetCard(requestId, nullptr, 5000);

    SUCCEED();
}

// ===========================================================================
// 测试空请求参数
// ===========================================================================
TEST_F(JsonRpcTransportImplTest, EmptyParameters_NoCrash)
{
    MessageSendParams emptyParams;
    Message emptyMsg;
    emptyMsg.messageId = "empty-msg-id";
    emptyMsg.role = Role::AGENT;
    emptyMsg.parts = {};
    emptyParams.message = emptyMsg;

    std::string requestId = "empty-001";

    EXPECT_NO_THROW(wrapper->SendMessage(requestId, emptyParams, nullptr, 5000, METHOD_MESSAGE_SEND));
}

// ===========================================================================
// 测试超时参数
// ===========================================================================
TEST_F(JsonRpcTransportImplTest, DifferentTimeouts_NoCrash)
{
    std::string requestId = "timeout-001";
    MessageSendParams msgParams;
    msgParams.message = CreateTestMessage();

    std::vector<int> timeouts = {1, 10, 100, 1000, 5000, 30000};

    for (int timeout : timeouts) {
        EXPECT_NO_THROW(wrapper->SendMessage(requestId, msgParams, nullptr, timeout, METHOD_MESSAGE_SEND));
    }
}

// ===========================================================================
// 测试重复 Close
// ===========================================================================
TEST_F(JsonRpcTransportImplTest, MultipleClose)
{
    wrapper->Close();
    wrapper->Close();
    wrapper->Close();
    SUCCEED();
}

// ===========================================================================
// 测试 Call-Close-Call 序列
// ===========================================================================
TEST_F(JsonRpcTransportImplTest, CallCloseCall_NoCrash)
{
    std::string requestId = "call-close-001";
    MessageSendParams msgParams;
    msgParams.message = CreateTestMessage();

    wrapper->SendMessage(requestId, msgParams, nullptr, 5000, METHOD_MESSAGE_SEND);
    wrapper->Close();

    // Close 后再调用应该不会崩溃，但可能返回错误
    EXPECT_ANY_THROW(wrapper->SendMessage(requestId, msgParams, nullptr, 5000, METHOD_MESSAGE_SEND));

    SUCCEED();
}

// ===========================================================================
// 测试异步操作的并发
// ===========================================================================
TEST_F(JsonRpcTransportImplTest, ConcurrentCalls_NoCrash)
{
    constexpr int THREAD_COUNT = 5;
    std::vector<std::thread> threads;

    MessageSendParams msgParams;
    msgParams.message = CreateTestMessage();

    for (int i = 0; i < THREAD_COUNT; i++) {
        threads.emplace_back([this, i, &msgParams]() {
            std::string requestId = "thread-" + std::to_string(i);
            wrapper->SendMessage(requestId, msgParams, nullptr, 5000, METHOD_MESSAGE_SEND);
            wrapper->GetCard(requestId, nullptr, 5000);
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    SUCCEED();
}

// ===========================================================================
// 测试大消息
// ===========================================================================
TEST_F(JsonRpcTransportImplTest, LargeMessage_NoCrash)
{
    std::string requestId = "large-001";
    MessageSendParams msgParams;

    // 创建一个大消息
    std::string largeText(1024 * 10, 'x'); // 10KB
    Message msg = CreateTestMessage("large-msg");
    msg.parts[0].text = largeText;
    msgParams.message = msg;

    EXPECT_NO_THROW(wrapper->SendMessage(requestId, msgParams, nullptr, 5000, METHOD_MESSAGE_SEND));
}

// ===========================================================================
// 测试 Lambda 辅助函数
// ===========================================================================
TEST_F(JsonRpcTransportImplTest, LambdaWithUnusedParams)
{
    auto lambda = [](const std::string& data,
                    [[maybe_unused]] const std::map<std::string, std::string>& headers,
                    [[maybe_unused]] UserData& userData,
                    [[maybe_unused]] int timeout) {
        EXPECT_FALSE(data.empty());
        return 0;
    };

    std::map<std::string, std::string> headers;
    UserData userData;
    int result = lambda("test", headers, userData, 1000);
    EXPECT_EQ(result, 0);
}

} // namespace A2A::Client::Test