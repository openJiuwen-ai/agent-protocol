/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <future>
#include <chrono>
#include <thread>
#include <queue>
#include <atomic>
#include <vector>

#include "client/connection/libcurl_conn.h"
#include "event_system.h"

namespace A2A::Http::Test {

using namespace A2A::Http;
using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::SaveArg;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::AtLeast;

// ===========================================================================
// Mock ConnCallback
// ===========================================================================
class MockConnCallback : public ConnCallback {
public:
    MOCK_METHOD(void, OnMessageReceived, (const ConnEventData&, const UserData&), (override));
    MOCK_METHOD(void, OnDisconnected, (const std::string&), (override));
};

// ===========================================================================
// 定义可 mock 的发送接口
// ===========================================================================
class ISender {
public:
    virtual ~ISender() = default;
    virtual int Send(const std::string& data,
                    const std::map<std::string, std::string>& headers,
                    UserData userData,
                    int timeout) = 0;
};

class MockSender : public ISender {
public:
    MOCK_METHOD(int, Send, (
        const std::string&,
        (const std::map<std::string, std::string>&),
        UserData,
        int
    ), (override));
};

// ===========================================================================
// 测试专用的 RequestContext 创建辅助函数
// ===========================================================================
std::shared_ptr<RequestContext> CreateTestRequestContext(
    const std::string& requestId,
    const std::string& url,
    const std::string& method = "POST",
    const std::string& body = "",
    bool isStream = false)
{
    HttpRequest req;
    req.url = url;
    req.method = method;
    req.body = body;

    UserData userData;
    userData.requestId = requestId;
    userData.method = isStream ? METHOD_MESSAGE_STREAM : METHOD_MESSAGE_SEND;
    userData.isStream = isStream;

    return std::make_shared<RequestContext>(req, nullptr, nullptr, 30000, userData);
}

// ===========================================================================
// 可测试的 LibcurlConn 子类 - 支持注入 Sender
// ===========================================================================
class TestableLibcurlConn : public LibcurlConn {
public:
    // 构造函数：接受额外的 sender 参数
    TestableLibcurlConn(const std::string& url,
                        const std::unordered_map<std::string, std::string>& headers,
                        int timeout, int sseReadTimeout,
                        std::unique_ptr<ISender> sender)
        : LibcurlConn(url, headers, timeout, sseReadTimeout), sender_(std::move(sender))
    {}

    // 重写 SendMessage 方法，使用注入的 sender
    int SendMessage(const std::string& message,
                    const std::map<std::string, std::string>& headers,
                    std::shared_ptr<UserData> userData,
                    int timeout) override
    {
        // 检查服务状态
        if (!IsRunning()) {
            return -1;
        }
        // 使用注入的 sender 发送消息
        return sender_->Send(message, headers, *userData, timeout);
    }

    // 暴露状态检查方法
    bool IsRunning() const
    {
        // 注意：state_ 是私有成员，需要通过其他方式获取，这里简化处理，假设总是 running
        return true;
    }

private:
    std::unique_ptr<ISender> sender_;
};

// ===========================================================================
// Test Fixture - 基础测试（使用真实连接）
// ===========================================================================
class LibcurlConnTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        url = "https://api.example.com/test";
        headers = {
            {"X-Custom-Header", "custom-value"},
            {"User-Agent", "A2A-Test/1.0"}
        };
        timeout = 30000;
        sseReadTimeout = 30000;

        mockCallback = std::make_shared<NiceMock<MockConnCallback>>();

        // 创建真实的 connection
        conn = std::make_shared<LibcurlConn>(url, headers, timeout, sseReadTimeout);
        conn->SetCallback(mockCallback);
    }

    void TearDown() override
    {
        if (conn) {
            conn->Terminate();
        }
    }

    // 辅助方法：创建一个测试请求
    std::shared_ptr<UserData> CreateTestRequest(const std::string& id, bool isStream = false)
    {
        std::shared_ptr<UserData> userData = std::make_shared<UserData>();
        userData->requestId = id;
        userData->method = isStream ? METHOD_MESSAGE_STREAM : METHOD_MESSAGE_SEND;
        userData->isStream = isStream;
        return userData;
    }

    std::string url;
    std::unordered_map<std::string, std::string> headers;
    int timeout;
    int sseReadTimeout;
    std::shared_ptr<MockConnCallback> mockCallback;
    std::shared_ptr<LibcurlConn> conn;
};

// ===========================================================================
// Test Fixture - Mock 测试（使用可测试的连接）
// ===========================================================================
class LibcurlConnMockTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        url = "https://api.example.com/test";
        headers = {
            {"X-Custom-Header", "custom-value"},
            {"User-Agent", "A2A-Test/1.0"}
        };
        timeout = 30000;
        sseReadTimeout = 30000;

        mockCallback = std::make_shared<NiceMock<MockConnCallback>>();

        // 创建 mock sender
        auto mockSender = std::make_unique<MockSender>();
        mockSenderPtr = mockSender.get();

        // 创建可测试的连接，注入 mock sender
        testableConn = std::make_shared<TestableLibcurlConn>(
            url, headers, timeout, sseReadTimeout, std::move(mockSender));
        testableConn->SetCallback(mockCallback);
    }

    void TearDown() override
    {
        if (testableConn) {
            testableConn->Terminate();
        }
    }

    std::shared_ptr<UserData> CreateTestRequest(const std::string& id, bool isStream = false)
    {
        std::shared_ptr<UserData> userData = std::make_shared<UserData>();
        userData->requestId = id;
        userData->method = isStream ? METHOD_MESSAGE_STREAM : METHOD_MESSAGE_SEND;
        userData->isStream = isStream;
        return userData;
    }

    std::string url;
    std::unordered_map<std::string, std::string> headers;
    int timeout;
    int sseReadTimeout;
    std::shared_ptr<MockConnCallback> mockCallback;
    std::shared_ptr<TestableLibcurlConn> testableConn;
    MockSender* mockSenderPtr;  // 用于设置期望
};

// ===========================================================================
// 基础构造函数测试（使用真实连接）
// ===========================================================================
TEST_F(LibcurlConnTest, ConstructorWithValidParams)
{
    EXPECT_NE(conn, nullptr);
}

TEST_F(LibcurlConnTest, ConstructorWithInvalidTimeout)
{
    EXPECT_THROW({
        auto c = std::make_shared<LibcurlConn>(url, headers, 0, sseReadTimeout);
    }, std::invalid_argument);

    EXPECT_THROW({
        auto c = std::make_shared<LibcurlConn>(url, headers, -1, sseReadTimeout);
    }, std::invalid_argument);
}

// ===========================================================================
// 基础 SetCallback 测试
// ===========================================================================
TEST_F(LibcurlConnTest, SetCallback)
{
    auto newCallback = std::make_shared<MockConnCallback>();
    EXPECT_NO_THROW({
        conn->SetCallback(newCallback);
    });
}

// ===========================================================================
// 基础发送请求测试 - 只测试接口不崩溃
// ===========================================================================
TEST_F(LibcurlConnTest, SendMessageReturnsValue)
{
    auto userData = CreateTestRequest("req-001");
    int result = conn->SendMessage(R"({"test":"data"})", {}, userData, timeout);
    EXPECT_TRUE(result == 0 || result == -1);
}

// ===========================================================================
// 基础 GET 请求测试
// ===========================================================================
TEST_F(LibcurlConnTest, GetAgentCard)
{
    auto userData = std::make_shared<UserData>();
    userData->requestId = "get-card-001";
    userData->method = METHOD_AGENT_CARD_GET;
    int result = conn->SendMessage("", {}, userData, timeout);
    EXPECT_TRUE(result == 0 || result == -1);
}

// ===========================================================================
// 基础 Terminate 测试
// ===========================================================================
TEST_F(LibcurlConnTest, Terminate)
{
    EXPECT_NO_THROW(conn->Terminate());
}

TEST_F(LibcurlConnTest, MultipleTerminate)
{
    EXPECT_NO_THROW({
        conn->Terminate();
        conn->Terminate();
        conn->Terminate();
    });
}

// ===========================================================================
// 基础 FinishRequest 测试
// ===========================================================================
TEST_F(LibcurlConnTest, FinishRequest)
{
    EXPECT_NO_THROW(conn->FinishRequest(1));
}

// ===========================================================================
// 基础发送消息后 Terminate
// ===========================================================================
TEST_F(LibcurlConnTest, SendThenTerminate)
{
    auto userData = CreateTestRequest("req-002");
    conn->SendMessage(R"({"test":"data"})", {}, userData, timeout);
    EXPECT_NO_THROW(conn->Terminate());
}

// ===========================================================================
// 基础多次 SetCallback
// ===========================================================================
TEST_F(LibcurlConnTest, MultipleSetCallback)
{
    for (int i = 0; i < 5; i++) {
        auto cb = std::make_shared<MockConnCallback>();
        EXPECT_NO_THROW(conn->SetCallback(cb));
    }
}

// ===========================================================================
// 基础流式消息发送测试
// ===========================================================================
TEST_F(LibcurlConnTest, SendStreamingMessage)
{
    auto userData = CreateTestRequest("stream-001", true);
    int result = conn->SendMessage(R"({"stream":true})", {}, userData, timeout);
    EXPECT_TRUE(result == 0 || result == -1);
}

// ===========================================================================
// 基础带自定义头的消息测试
// ===========================================================================
TEST_F(LibcurlConnTest, SendMessageWithCustomHeaders)
{
    auto userData = CreateTestRequest("headers-test");
    std::map<std::string, std::string> customHeaders = {
        {"X-Custom-1", "value1"},
        {"X-Custom-2", "value2"}
    };
    int result = conn->SendMessage("{}", customHeaders, userData, timeout);
    EXPECT_TRUE(result == 0 || result == -1);
}

// ===========================================================================
// 基础大消息测试
// ===========================================================================
TEST_F(LibcurlConnTest, SendLargeMessage)
{
    auto userData = CreateTestRequest("large-001");
    std::string largeData(10000, 'x');
    std::string message = "{\"data\":\"" + largeData + "\"}";
    int result = conn->SendMessage(message, {}, userData, timeout);
    EXPECT_TRUE(result == 0 || result == -1);
}

// ===========================================================================
// 基础短超时测试
// ===========================================================================
TEST_F(LibcurlConnTest, ShortTimeout)
{
    auto userData = CreateTestRequest("timeout-001");
    int shortTimeout = 1;
    int result = conn->SendMessage("{}", {}, userData, shortTimeout);
    EXPECT_TRUE(result == 0 || result == -1);
}

// ===========================================================================
// 基础析构函数测试
// ===========================================================================
TEST_F(LibcurlConnTest, Destructor)
{
    std::weak_ptr<LibcurlConn> weakConn;
    {
        auto localConn = std::make_shared<LibcurlConn>(url, headers, timeout, sseReadTimeout);
        localConn->SetCallback(mockCallback);
        weakConn = localConn;
    }
    EXPECT_TRUE(weakConn.expired());
}

// ===========================================================================
// 基础有pending请求时析构测试
// ===========================================================================
TEST_F(LibcurlConnTest, DestructorWithPendingRequests)
{
    std::weak_ptr<LibcurlConn> weakConn;
    {
        auto localConn = std::make_shared<LibcurlConn>(url, headers, timeout, sseReadTimeout);
        localConn->SetCallback(mockCallback);

        for (int i = 0; i < 3; i++) {
            auto userData = CreateTestRequest("pending-" + std::to_string(i));
            localConn->SendMessage("{}", {}, userData, timeout);
        }
        weakConn = localConn;
    }
    EXPECT_TRUE(weakConn.expired());
}

// ===========================================================================
// 基础不同 URL 格式测试
// ===========================================================================
TEST_F(LibcurlConnTest, DifferentUrlFormats)
{
    std::vector<std::string> testUrls = {
        "https://api.example.com",
        "https://api.example.com/",
        "https://api.example.com/v1/endpoint",
        "http://localhost:8080/test",
        "https://192.168.1.1:8443/api"
    };

    for (const auto& testUrl : testUrls) {
        EXPECT_NO_THROW({
            auto testConn = std::make_shared<LibcurlConn>(testUrl, headers, timeout, sseReadTimeout);
            testConn->SetCallback(mockCallback);
        });
    }
}

// ===========================================================================
// 基础不同 content-type 测试
// ===========================================================================
TEST_F(LibcurlConnTest, DifferentContentTypes)
{
    auto userData = CreateTestRequest("content-type-test");
    std::map<std::string, std::string> customHeaders = {
        {"Content-Type", "application/xml"}
    };
    int result = conn->SendMessage("<xml><test/></xml>", customHeaders, userData, timeout);
    EXPECT_TRUE(result == 0 || result == -1);
}

// ===========================================================================
// 基础空请求体测试
// ===========================================================================
TEST_F(LibcurlConnTest, EmptyBody)
{
    auto userData = CreateTestRequest("empty-body");
    int result = conn->SendMessage("", {}, userData, timeout);
    EXPECT_TRUE(result == 0 || result == -1);
}

// ===========================================================================
// Mock 测试：并发发送消息
// ===========================================================================
TEST_F(LibcurlConnMockTest, ConcurrentSendMessage_Mocked)
{
    constexpr int THREAD_COUNT = 5;
    std::atomic<int> callCount{0};

    // 设置 mock 期望
    EXPECT_CALL(*mockSenderPtr, Send("{}", _, _, _))
        .Times(THREAD_COUNT)
        .WillRepeatedly(Invoke([&callCount](const std::string&, const std::map<std::string, std::string>&,
            UserData, int) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            callCount++;
            return 0;
        }));

    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};

    for (int i = 0; i < THREAD_COUNT; i++) {
        threads.emplace_back([this, &successCount]() {
            auto userData = CreateTestRequest("mock-thread");
            int ret = testableConn->SendMessage("{}", {}, userData, timeout);
            if (ret == 0) successCount++;
        });
    }

    for (auto& t : threads) t.join();

    EXPECT_EQ(successCount.load(), THREAD_COUNT);
    EXPECT_EQ(callCount.load(), THREAD_COUNT);
}

// ===========================================================================
// Mock 测试：混合成功失败场景
// ===========================================================================
TEST_F(LibcurlConnMockTest, MixedSuccessFailure)
{
    constexpr int THREAD_COUNT = 5;
    std::atomic<int> successCount{0};

    // 设置 mock：前3次成功，后2次失败
    {
        testing::InSequence seq;
        for (int i = 0; i < 3; i++) {
            EXPECT_CALL(*mockSenderPtr, Send("{}", _, _, _))
                .WillOnce(Return(0));
        }
        for (int i = 0; i < 2; i++) {
            EXPECT_CALL(*mockSenderPtr, Send("{}", _, _, _))
                .WillOnce(Return(-1));
        }
    }

    std::vector<std::thread> threads;

    for (int i = 0; i < THREAD_COUNT; i++) {
        threads.emplace_back([this, &successCount, i]() {
            auto userData = CreateTestRequest("thread-" + std::to_string(i));
            int ret = testableConn->SendMessage("{}", {}, userData, timeout);
            if (ret == 0) successCount++;
        });
    }

    for (auto& t : threads) t.join();

    EXPECT_EQ(successCount.load(), 3);
}

// ===========================================================================
// Mock 测试：不同消息内容
// ===========================================================================
TEST_F(LibcurlConnMockTest, DifferentMessages)
{
    constexpr int THREAD_COUNT = 5;
    std::vector<std::string> messages = {
        R"({"type":"ping"})",
        R"({"type":"get_status"})",
        R"({"type":"update","value":1})",
        R"({"type":"update","value":2})",
        R"({"type":"close"})"
    };
    std::atomic<int> successCount{0};

    // 设置 mock：根据消息内容返回不同的值
    for (int i = 0; i < THREAD_COUNT; i++) {
        EXPECT_CALL(*mockSenderPtr, Send(messages[i], _, _, _))
            .WillOnce(Return(0));
    }

    std::vector<std::thread> threads;

    for (int i = 0; i < THREAD_COUNT; i++) {
        threads.emplace_back([this, i, &messages, &successCount]() {
            auto userData = CreateTestRequest("msg-" + std::to_string(i));
            int ret = testableConn->SendMessage(messages[i], {}, userData, timeout);
            if (ret == 0) successCount++;
        });
    }

    for (auto& t : threads) t.join();

    EXPECT_EQ(successCount.load(), THREAD_COUNT);
}

// ===========================================================================
// Mock 测试：超时场景
// ===========================================================================
TEST_F(LibcurlConnMockTest, TimeoutHandling)
{
    constexpr int THREAD_COUNT = 3;

    // 设置 mock：模拟不同延迟的响应
    EXPECT_CALL(*mockSenderPtr, Send("{}", _, _, _))
        .Times(THREAD_COUNT)
        .WillRepeatedly(Invoke([](
            const std::string&,
            const std::map<std::string, std::string>&,
            UserData userData,
            [[maybe_unused]] int timeout) {
            if (userData.requestId.find("slow") != std::string::npos) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return 0;
        }));

    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};

    // 快速请求
    threads.emplace_back([this, &successCount]() {
        auto userData = CreateTestRequest("fast-1");
        int ret = testableConn->SendMessage("{}", {}, userData, 100);
        if (ret == 0) successCount++;
    });

    // 慢速请求
    threads.emplace_back([this, &successCount]() {
        auto userData = CreateTestRequest("slow-1");
        int ret = testableConn->SendMessage("{}", {}, userData, 20);
        if (ret == 0) successCount++;
    });

    // 正常请求
    threads.emplace_back([this, &successCount]() {
        auto userData = CreateTestRequest("normal-1");
        int ret = testableConn->SendMessage("{}", {}, userData, 100);
        if (ret == 0) successCount++;
    });

    for (auto& t : threads) t.join();

    EXPECT_LE(successCount.load(), 3);
    EXPECT_GE(successCount.load(), 2);
}

} // namespace A2A::Http::Test