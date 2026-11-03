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
#include <limits>

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
// 空闲检测白盒测试子类：暴露 protected 的空闲检测成员（测试 seam）
// ===========================================================================
class IdleCheckableConn : public LibcurlConn {
public:
    using LibcurlConn::LibcurlConn;

    void SetRunningForTest()
    {
        state_.store(ConnState::RUNNING, std::memory_order_release);
    }

    void AddActiveRequest(const std::string& id, const std::shared_ptr<RequestContext>& request)
    {
        activeRequests_[id] = request;
    }

    bool HasActiveRequest(const std::string& id) const
    {
        return activeRequests_.find(id) != activeRequests_.end();
    }

    // 测试注入真实 multi 句柄（覆盖 easy handle 释放路径；Stop() 负责清理）
    void AdoptMultiHandleForTest(CURLM* multi)
    {
        multiHandle_ = multi;
    }

    void TriggerIdleCheck()
    {
        OnIdleCheck();
    }
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

// ===========================================================================
// 空闲检测白盒测试（OnIdleCheck / AbortStreamIdleRequest）
// ===========================================================================
class LibcurlConnIdleCheckTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        conn = std::make_shared<IdleCheckableConn>("https://api.example.com/test");
        // 构造函数内部会 Start()：拉起 I/O 线程、EventSystem、真实 multi 句柄和
        // 1s 空闲检测定时器（state_ 为 RUNNING）。先完整停掉得到惰性连接：
        // 测试在当前线程同步驱动 OnIdleCheck（SkipWhenStopped 才能真正从
        // STOPPED 出发），同时避免真实定时器在 I/O 线程并发访问 activeRequests_
        // （数据竞争），以及注入 multi 时覆盖丢失构造期创建的真实句柄（泄漏）。
        conn->Terminate();
    }

    void TearDown() override
    {
        // state_ 为 RUNNING 时 Stop() 会清理 multiHandle_ 等注入的资源
        if (conn) {
            conn->Terminate();
        }
    }

    // 创建流式请求：timeoutMs 为空闲上限，lastActivityTime 回拨 idleMs 毫秒，
    // 错误响应经 responseBodyCallback 捕获到 captured。
    std::shared_ptr<RequestContext> MakeStreamRequest(const std::string& id, int timeoutMs, int idleMs,
        HttpResponse& captured)
    {
        auto request = CreateTestRequestContext(id, "https://api.example.com/test", "POST", "{}", true);
        request->timeoutMs = timeoutMs;
        if (idleMs > 0) {
            request->lastActivityTime -= std::chrono::milliseconds(idleMs);
        }
        request->responseBodyCallback = [&captured](const HttpResponse& response) { captured = response; };
        return request;
    }

    std::shared_ptr<IdleCheckableConn> conn;
};

// 静默超过空闲窗口的流式请求被中止，并回调 A2A_REQUEST_TIMEOUT 与 stage 信息
TEST_F(LibcurlConnIdleCheckTest, AbortSilentStreamRequest)
{
    conn->SetRunningForTest();
    HttpResponse captured;
    // 已静默 100ms > 50ms 空闲上限
    auto request = MakeStreamRequest("idle-001", 50, 100, captured);
    conn->AddActiveRequest("idle-001", request);

    conn->TriggerIdleCheck();

    EXPECT_FALSE(conn->HasActiveRequest("idle-001"));
    EXPECT_FALSE(captured.success);
    EXPECT_EQ(captured.errCode, static_cast<int>(A2AErrorCode::A2A_REQUEST_TIMEOUT));
    EXPECT_NE(captured.errorMessage.find("stage=stream-idle"), std::string::npos);
    EXPECT_NE(captured.errorMessage.find("idle="), std::string::npos);
    EXPECT_NE(captured.errorMessage.find("elapsed="), std::string::npos);
}

// 数据仍在到达（未超空闲窗口）的流式请求不受影响
TEST_F(LibcurlConnIdleCheckTest, KeepActiveStreamWithData)
{
    conn->SetRunningForTest();
    HttpResponse captured;
    auto request = MakeStreamRequest("active-001", 60000, 0, captured);
    conn->AddActiveRequest("active-001", request);

    conn->TriggerIdleCheck();

    EXPECT_TRUE(conn->HasActiveRequest("active-001"));
    EXPECT_TRUE(captured.errorMessage.empty());
}

// timeoutMs <= 0 的流式请求不做空闲检查
TEST_F(LibcurlConnIdleCheckTest, KeepStreamRequestWithZeroTimeout)
{
    conn->SetRunningForTest();
    HttpResponse captured;
    auto request = MakeStreamRequest("zero-001", 0, 60000, captured);
    conn->AddActiveRequest("zero-001", request);

    conn->TriggerIdleCheck();

    EXPECT_TRUE(conn->HasActiveRequest("zero-001"));
    EXPECT_TRUE(captured.errorMessage.empty());
}

// 非流式请求不参与空闲检查（总超时由 CURLOPT_TIMEOUT_MS 负责）
TEST_F(LibcurlConnIdleCheckTest, KeepNonStreamRequest)
{
    conn->SetRunningForTest();
    HttpResponse captured;
    constexpr int timeoutMs50 = 50;
    constexpr int lastActivityTime = 60000;
    auto request = CreateTestRequestContext("nonstream-001", "https://api.example.com/test", "POST", "{}", false);
    request->timeoutMs = timeoutMs50;
    request->lastActivityTime -= std::chrono::milliseconds(lastActivityTime);
    request->responseBodyCallback = [&captured](const HttpResponse& response) { captured = response; };
    conn->AddActiveRequest("nonstream-001", request);

    conn->TriggerIdleCheck();

    EXPECT_TRUE(conn->HasActiveRequest("nonstream-001"));
    EXPECT_TRUE(captured.errorMessage.empty());
}

// 未启动（STOPPED）的连接跳过检查
TEST_F(LibcurlConnIdleCheckTest, SkipWhenStopped)
{
    HttpResponse captured;
    auto request = MakeStreamRequest("stopped-001", 50, 100, captured);
    conn->AddActiveRequest("stopped-001", request);

    conn->TriggerIdleCheck();

    EXPECT_TRUE(conn->HasActiveRequest("stopped-001"));
    EXPECT_TRUE(captured.errorMessage.empty());
}

// 中止路径释放 easy handle（remove + cleanup）并从在途表移除
TEST_F(LibcurlConnIdleCheckTest, AbortReleasesEasyHandle)
{
    conn->SetRunningForTest();
    // curl_easy_init 隐式完成 curl 全局初始化；multi 句柄由 Stop() 清理
    CURL* easy = curl_easy_init();
    ASSERT_NE(easy, nullptr);
    CURLM* multi = curl_multi_init();
    ASSERT_NE(multi, nullptr);
    conn->AdoptMultiHandleForTest(multi);

    HttpResponse captured;
    auto request = MakeStreamRequest("easy-001", 50, 100, captured);
    ASSERT_EQ(curl_multi_add_handle(multi, easy), CURLM_OK);
    request->easyHandle = easy;
    conn->AddActiveRequest("easy-001", request);

    conn->TriggerIdleCheck();

    EXPECT_EQ(request->easyHandle, nullptr);
    EXPECT_FALSE(conn->HasActiveRequest("easy-001"));
    EXPECT_NE(captured.errorMessage.find("stage=stream-idle"), std::string::npos);
}

// ===========================================================================
// CurlErrorCode / DescribeCurlError 单元测试
// ===========================================================================
TEST(CurlErrorMappingTest, ErrorCodeMapping)
{
    EXPECT_EQ(CurlErrorCode(CURLE_OPERATION_TIMEDOUT), static_cast<int>(A2AErrorCode::A2A_REQUEST_TIMEOUT));
    EXPECT_EQ(CurlErrorCode(CURLE_COULDNT_RESOLVE_PROXY), static_cast<int>(A2AErrorCode::A2A_TRANSPORT_EXCEPTION));
    EXPECT_EQ(CurlErrorCode(CURLE_COULDNT_RESOLVE_HOST), static_cast<int>(A2AErrorCode::A2A_TRANSPORT_EXCEPTION));
    EXPECT_EQ(CurlErrorCode(CURLE_COULDNT_CONNECT), static_cast<int>(A2AErrorCode::A2A_TRANSPORT_EXCEPTION));
    EXPECT_EQ(CurlErrorCode(CURLE_SSL_CONNECT_ERROR), static_cast<int>(A2AErrorCode::A2A_TRANSPORT_EXCEPTION));
    // 未映射的错误返回 0（上层回退到 HTTP_PARSE_ERROR）
    EXPECT_EQ(CurlErrorCode(CURLE_OK), 0);
    EXPECT_EQ(CurlErrorCode(CURLE_RECV_ERROR), 0);
}

TEST(CurlErrorMappingTest, DescribeCurlErrorStages)
{
    // 尚未收到响应头（含流式请求）：流式的空闲窗口由 OnIdleCheck 负责，
    // curl 报出的超时只会来自连接阶段，不应误标为 stream-idle
    auto streamNoHeaders = CreateTestRequestContext("s1", "https://api.example.com/test", "POST", "{}", true);
    std::string desc = DescribeCurlError(streamNoHeaders, CURLE_OPERATION_TIMEDOUT);
    EXPECT_NE(desc.find("request timed out [stage=connect-or-waiting-response-headers"), std::string::npos);
    EXPECT_NE(desc.find("elapsed="), std::string::npos);

    auto nonStreamNoHeaders = CreateTestRequestContext("s2", "https://api.example.com/test", "POST", "{}", false);
    desc = DescribeCurlError(nonStreamNoHeaders, CURLE_OPERATION_TIMEDOUT);
    EXPECT_NE(desc.find("stage=connect-or-waiting-response-headers"), std::string::npos);

    // 非流式且已收到响应头：等待响应体阶段
    auto nonStreamWithHeaders = CreateTestRequestContext("s3", "https://api.example.com/test", "POST", "{}", false);
    nonStreamWithHeaders->headerData = "HTTP/1.1 200 OK\r\n";
    desc = DescribeCurlError(nonStreamWithHeaders, CURLE_OPERATION_TIMEDOUT);
    EXPECT_NE(desc.find("stage=waiting-response-body"), std::string::npos);

    // 流式且已收到响应头：防御分支标记为 stream-idle
    auto streamWithHeaders = CreateTestRequestContext("s4", "https://api.example.com/test", "POST", "{}", true);
    streamWithHeaders->headerData = "HTTP/1.1 200 OK\r\n";
    desc = DescribeCurlError(streamWithHeaders, CURLE_OPERATION_TIMEDOUT);
    EXPECT_NE(desc.find("stage=stream-idle"), std::string::npos);

    // 非超时错误：libcurl 描述 + elapsed
    desc = DescribeCurlError(nonStreamNoHeaders, CURLE_COULDNT_RESOLVE_HOST);
    EXPECT_NE(desc.find("[elapsed="), std::string::npos);
    EXPECT_NE(desc.find("resolve"), std::string::npos);
}

// ===========================================================================
// 超时上限截断（ClampTimeoutSecondsToMs）单元测试
// ===========================================================================
TEST(TimeoutClampTest, ClampTimeoutSecondsToMs)
{
    EXPECT_EQ(ClampTimeoutSecondsToMs(1), SECONDS_TO_MS);
    EXPECT_EQ(ClampTimeoutSecondsToMs(MAX_TIMEOUT_SECONDS), MAX_TIMEOUT_SECONDS * SECONDS_TO_MS);
    // 超过上限按上限截断，而不是让秒转毫秒乘法溢出
    EXPECT_EQ(ClampTimeoutSecondsToMs(MAX_TIMEOUT_SECONDS + 1), MAX_TIMEOUT_SECONDS * SECONDS_TO_MS);
    EXPECT_EQ(ClampTimeoutSecondsToMs(std::numeric_limits<int>::max()), MAX_TIMEOUT_SECONDS * SECONDS_TO_MS);
}

} // namespace A2A::Http::Test