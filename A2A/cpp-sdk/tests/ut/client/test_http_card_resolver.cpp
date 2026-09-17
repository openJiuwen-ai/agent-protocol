/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <future>
#include <chrono>
#include <thread>
#include <iostream>

#include "client/http_card_resolver.h"
#include "client/jsonrpc_transport.h"
#include "types.h"

namespace A2A::Client::Test {

using namespace A2A::Client;
using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::SaveArg;
using ::testing::DoAll;
using ::testing::NiceMock;

// ===========================================================================
// Mock JsonRpcTransport
// ===========================================================================
class MockJsonRpcTransport : public JsonRpcTransport {
public:
    MockJsonRpcTransport() : JsonRpcTransport("", AgentCard{}, ClientConfig{}, {})
    {}

    MOCK_METHOD(void, SetTransportCallback, (TransportEventCallback), (override));
    MOCK_METHOD(void, GetCard, (const std::string&, const ClientCallContext*, int), (override));
    MOCK_METHOD(void, Close, (), (override));

    MOCK_METHOD(void, SendMessage, (const std::string&, const MessageSendParams&, const ClientCallContext*, int),
        (override));
    MOCK_METHOD(void, SendMessageStreaming, (const std::string&, const MessageSendParams&,
        const ClientCallContext*, int), (override));
    MOCK_METHOD(void, GetTask, (const std::string&, const TaskQueryParams&,
        const ClientCallContext*, int), (override));
    MOCK_METHOD(void, CancelTask, (const std::string&, const TaskIdParams&, const ClientCallContext*, int),
        (override));
    MOCK_METHOD(void, SetTaskPushNotificationConfig, (const std::string&, const TaskPushNotificationConfig&,
        const ClientCallContext*, int), (override));
    MOCK_METHOD(void, GetTaskPushNotificationConfig, (const std::string&, const GetTaskPushNotificationConfigParams&,
        const ClientCallContext*, int), (override));
    MOCK_METHOD(void, ListTaskPushNotificationConfigs, (const std::string&, const ListTaskPushNotificationConfigParams&,
        const ClientCallContext*, int), (override));
    MOCK_METHOD(void, DeleteTaskPushNotificationConfig, (const std::string&,
        const DeleteTaskPushNotificationConfigParams&, const ClientCallContext*, int), (override));
    MOCK_METHOD(void, Resubscribe, (const std::string&, const TaskIdParams&, const ClientCallContext*, int),
        (override));
};

// ===========================================================================
// Testable HttpCardResolver - 测试专用子类
// ===========================================================================
class TestableHttpCardResolver : public HttpCardResolver {
public:
    TestableHttpCardResolver(std::string baseUrl,
                            const std::optional<std::string>& relativeCardPath,
                            std::map<std::string, std::string> httpKwargs)
        : HttpCardResolver(std::move(baseUrl), relativeCardPath, std::move(httpKwargs))
    {
    }

    ~TestableHttpCardResolver()
    {
        // 析构时标记为无效
        invalid_ = true;
    }

    // 允许设置 mock transport
    void SetMockTransport(std::shared_ptr<JsonRpcTransport> transport)
    {
        transport_ = transport;

        // 保存 this 指针，但会在回调中检查有效性
        TestableHttpCardResolver* rawThis = this;
        std::atomic<bool>* invalid = &invalid_;

        transport_->SetTransportCallback([rawThis, invalid](const std::string& id, const TransportEvent& ev) {
            if (!invalid->load() && rawThis) {
                rawThis->OnTransportEvent(id, ev);
            }
        });
    }

    // 暴露 protected 方法用于测试
    void TriggerOnTransportEvent(const std::string& requestId, const TransportEvent& event)
    {
        OnTransportEvent(requestId, event);
    }

    // 获取 pending promises 数量
    size_t GetPendingPromiseCount()
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        return pendingPromises_.size();
    }

    // 获取最后一个 requestId（用于测试）
    std::string GetLastRequestId()
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        if (pendingPromises_.empty()) {
            return "";
        }
        return pendingPromises_.begin()->first;
    }

    // 验证 promise 是否存在
    bool HasPromiseForRequest(const std::string& requestId)
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        return pendingPromises_.find(requestId) != pendingPromises_.end();
    }

private:
    std::atomic<bool> invalid_{false};
};

// ===========================================================================
// HttpCardResolver Mock Test Fixture
// ===========================================================================
class HttpCardResolverMockTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        baseUrl = "https://api.example.com";
        relativePath = "agent/card";
        httpKwargs = {{"timeout", "30"}};

        // 创建测试用的 AgentCard
        testCard.name = "Test Agent";
        testCard.description = "Test Description";
        testCard.version = "1.0.0";

        AgentCapabilities caps;
        caps.streaming = true;
        caps.pushNotifications = false;
        caps.extendedAgentCard = true;
        testCard.capabilities = caps;

        testCard.defaultInputModes = {"text/plain"};
        testCard.defaultOutputModes = {"text/plain"};

        // 创建 mock transport
        mockTransport = std::make_shared<NiceMock<MockJsonRpcTransport>>();

        // 创建 testable resolver
        resolver = std::make_shared<TestableHttpCardResolver>(baseUrl, relativePath, httpKwargs);

        // 设置 mock transport
        resolver->SetMockTransport(mockTransport);
    }

    std::string baseUrl;
    std::string relativePath;
    std::map<std::string, std::string> httpKwargs;
    AgentCard testCard;
    std::shared_ptr<NiceMock<MockJsonRpcTransport>> mockTransport;
    std::shared_ptr<TestableHttpCardResolver> resolver;

    TransportEventCallback capturedCallback;
};

// ===========================================================================
// 测试 SetTransportCallback 被调用
// ===========================================================================
TEST_F(HttpCardResolverMockTest, SetsTransportCallback)
{
    EXPECT_CALL(*mockTransport, SetTransportCallback(_))
        .WillOnce(SaveArg<0>(&capturedCallback));

    // 重新创建 resolver 以触发 SetTransportCallback
    resolver = std::make_shared<TestableHttpCardResolver>(baseUrl, relativePath, httpKwargs);
    resolver->SetMockTransport(mockTransport);

    ASSERT_NE(capturedCallback, nullptr);
}

// ===========================================================================
// 测试 GetAgentCard 调用 GetCard
// ===========================================================================
TEST_F(HttpCardResolverMockTest, GetAgentCardCallsGetCard)
{
    std::string capturedRequestId;

    EXPECT_CALL(*mockTransport, GetCard(_, _, _))
        .WillOnce(Invoke([&capturedRequestId](const std::string& requestId,
                                                const ClientCallContext*,
                                                int) {
            capturedRequestId = requestId;
        }));

    auto future = resolver->GetAgentCard(std::nullopt);

    // 验证 GetCard 被调用
    EXPECT_FALSE(capturedRequestId.empty());
    EXPECT_TRUE(resolver->HasPromiseForRequest(capturedRequestId));
    EXPECT_EQ(resolver->GetPendingPromiseCount(), 1);
}

// ===========================================================================
// 测试成功响应
// ===========================================================================
TEST_F(HttpCardResolverMockTest, HandlesSuccessResponse)
{
    // 捕获 requestId 和 callback
    std::string capturedRequestId;
    TransportEventCallback capturedCallback;

    EXPECT_CALL(*mockTransport, SetTransportCallback(_))
        .WillOnce(SaveArg<0>(&capturedCallback));

    EXPECT_CALL(*mockTransport, GetCard(_, _, _))
        .WillOnce(Invoke([&capturedRequestId](const std::string& requestId,
                                                const ClientCallContext*,
                                                int) {
            capturedRequestId = requestId;
        }));

    // 重新创建 resolver 以捕获 callback
    resolver = std::make_shared<TestableHttpCardResolver>(baseUrl, relativePath, httpKwargs);
    resolver->SetMockTransport(mockTransport);

    // 调用 GetAgentCard
    auto future = resolver->GetAgentCard(std::nullopt);

    // 模拟 transport 成功响应
    ASSERT_NE(capturedCallback, nullptr);
    ASSERT_FALSE(capturedRequestId.empty());

    // 创建 TransportEvent 包含 AgentCard
    TransportEvent event = testCard;

    // 触发回调
    capturedCallback(capturedRequestId, event);

    // 验证 promise 被移除
    EXPECT_EQ(resolver->GetPendingPromiseCount(), 0);

    // 验证 future 完成并返回正确的值
    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(100)), std::future_status::ready);
    AgentCard result = future.get();
    EXPECT_EQ(result.name, testCard.name);
    EXPECT_EQ(result.version, testCard.version);
}

// ===========================================================================
// 测试错误响应
// ===========================================================================
TEST_F(HttpCardResolverMockTest, HandlesErrorResponse)
{
    std::string capturedRequestId;
    TransportEventCallback capturedCallback;

    EXPECT_CALL(*mockTransport, SetTransportCallback(_))
        .WillOnce(SaveArg<0>(&capturedCallback));

    EXPECT_CALL(*mockTransport, GetCard(_, _, _))
        .WillOnce(Invoke([&capturedRequestId](const std::string& requestId,
                                                const ClientCallContext*,
                                                int) {
            capturedRequestId = requestId;
        }));

    resolver = std::make_shared<TestableHttpCardResolver>(baseUrl, relativePath, httpKwargs);
    resolver->SetMockTransport(mockTransport);

    auto future = resolver->GetAgentCard(std::nullopt);

    // 模拟 transport 错误响应
    TransportError error;
    error.errorCode = 404;
    error.errInfo = "Not found";

    capturedCallback(capturedRequestId, error);

    // 验证 promise 被移除
    EXPECT_EQ(resolver->GetPendingPromiseCount(), 0);

    // 验证 future 抛出异常
    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(100)), std::future_status::ready);
    EXPECT_THROW(future.get(), std::runtime_error);
}

// ===========================================================================
// 测试错误响应类型
// ===========================================================================
TEST_F(HttpCardResolverMockTest, HandlesUnexpectedResponseType)
{
    std::string capturedRequestId;
    TransportEventCallback capturedCallback;

    EXPECT_CALL(*mockTransport, SetTransportCallback(_))
        .WillOnce(SaveArg<0>(&capturedCallback));

    EXPECT_CALL(*mockTransport, GetCard(_, _, _))
        .WillOnce(Invoke([&capturedRequestId](const std::string& requestId,
                                                const ClientCallContext*,
                                                int) {
            capturedRequestId = requestId;
        }));

    resolver = std::make_shared<TestableHttpCardResolver>(baseUrl, relativePath, httpKwargs);
    resolver->SetMockTransport(mockTransport);

    auto future = resolver->GetAgentCard(std::nullopt);

    // 模拟错误响应类型（比如返回了 Message）
    A2A::Message message;
    message.messageId = "test-msg";

    capturedCallback(capturedRequestId, message);

    // 验证 promise 被移除
    EXPECT_EQ(resolver->GetPendingPromiseCount(), 0);

    // 验证 future 抛出异常
    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(100)), std::future_status::ready);
    EXPECT_THROW(future.get(), std::runtime_error);
}

// ===========================================================================
// 测试超时/无响应
// ===========================================================================
TEST_F(HttpCardResolverMockTest, HandlesNoResponse)
{
    std::string capturedRequestId;

    EXPECT_CALL(*mockTransport, GetCard(_, _, _))
        .WillOnce(Invoke([&capturedRequestId](const std::string& requestId,
                                                const ClientCallContext*,
                                                int) {
            capturedRequestId = requestId;
        }));

    resolver = std::make_shared<TestableHttpCardResolver>(baseUrl, relativePath, httpKwargs);
    resolver->SetMockTransport(mockTransport);

    auto future = resolver->GetAgentCard(std::nullopt);

    // 不触发回调，验证 promise 仍然存在
    EXPECT_EQ(resolver->GetPendingPromiseCount(), 1);
    EXPECT_TRUE(resolver->HasPromiseForRequest(capturedRequestId));
    EXPECT_TRUE(future.valid());
}

// ===========================================================================
// 测试多个并发请求
// ===========================================================================
TEST_F(HttpCardResolverMockTest, HandlesConcurrentRequests)
{
    std::vector<std::string> capturedRequestIds;
    TransportEventCallback capturedCallback;

    EXPECT_CALL(*mockTransport, SetTransportCallback(_))
        .WillOnce(SaveArg<0>(&capturedCallback));

    EXPECT_CALL(*mockTransport, GetCard(_, _, _))
        .Times(3)
        .WillRepeatedly(Invoke([&capturedRequestIds](const std::string& requestId,
                                                        const ClientCallContext*,
                                                        int) {
            capturedRequestIds.push_back(requestId);
        }));

    resolver = std::make_shared<TestableHttpCardResolver>(baseUrl, relativePath, httpKwargs);
    resolver->SetMockTransport(mockTransport);

    // 发送多个请求
    std::vector<std::future<AgentCard>> futures;
    for (int i = 0; i < 3; i++) {
        futures.push_back(resolver->GetAgentCard(std::nullopt));
    }

    // 验证所有请求都被记录
    EXPECT_EQ(capturedRequestIds.size(), 3);
    EXPECT_EQ(resolver->GetPendingPromiseCount(), 3);

    // 依次响应每个请求
    for (size_t i = 0; i < capturedRequestIds.size(); i++) {
        capturedCallback(capturedRequestIds[i], testCard);
        EXPECT_EQ(resolver->GetPendingPromiseCount(), 2 - i);
    }

    // 验证所有 future 都完成
    for (auto& future : futures) {
        EXPECT_EQ(future.wait_for(std::chrono::milliseconds(100)), std::future_status::ready);
        EXPECT_NO_THROW(future.get());
    }
}

// ===========================================================================
// 测试 GetAllAgentCards
// ===========================================================================
TEST_F(HttpCardResolverMockTest, GetAllAgentCardsWorks)
{
    std::string capturedRequestId;
    TransportEventCallback capturedCallback;

    EXPECT_CALL(*mockTransport, SetTransportCallback(_))
        .WillOnce(SaveArg<0>(&capturedCallback));

    EXPECT_CALL(*mockTransport, GetCard(_, _, _))
        .WillOnce(Invoke([&capturedRequestId](const std::string& requestId,
                                                const ClientCallContext*,
                                                int) {
            capturedRequestId = requestId;
        }));

    resolver = std::make_shared<TestableHttpCardResolver>(baseUrl, relativePath, httpKwargs);
    resolver->SetMockTransport(mockTransport);

    auto future = resolver->GetAllAgentCards();

    // 验证 GetCard 被调用
    EXPECT_FALSE(capturedRequestId.empty());

    // 模拟响应
    capturedCallback(capturedRequestId, testCard);

    // 验证结果
    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(100)), std::future_status::ready);
    std::vector<AgentCard> cards = future.get();
    ASSERT_EQ(cards.size(), 1);
    EXPECT_EQ(cards[0].name, testCard.name);
}

// ===========================================================================
// 测试异常处理
// ===========================================================================
TEST_F(HttpCardResolverMockTest, HandlesTransportException)
{
    EXPECT_CALL(*mockTransport, GetCard(_, _, _))
        .WillOnce(Invoke([](const std::string&, const ClientCallContext*, int) {
            throw std::runtime_error("Transport error");
        }));

    resolver = std::make_shared<TestableHttpCardResolver>(baseUrl, relativePath, httpKwargs);
    resolver->SetMockTransport(mockTransport);

    auto future = resolver->GetAgentCard(std::nullopt);

    // 验证 promise 被立即移除
    EXPECT_EQ(resolver->GetPendingPromiseCount(), 0);

    // 验证 future 抛出异常
    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(100)), std::future_status::ready);
    EXPECT_THROW(future.get(), std::runtime_error);
}

} // namespace A2A::Client::Test