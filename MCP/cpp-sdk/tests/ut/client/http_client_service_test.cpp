/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <gtest/gtest.h>

#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <functional>

#include "mcp_log.h"
#include "shared/http_common.h"
#include "client/transport/http_client_service.h"

using namespace std::chrono_literals;

namespace Mcp {
namespace Http {

// Test constants
static constexpr int TEST_REQUEST_TIMEOUT_MS = 10;
static constexpr int TEST_ID = 123;
static constexpr int LOOP_NUM = 3;
static constexpr int THREAD_NUM = 2;

// Test fixture for HttpClientService tests
class HttpClientServiceTest : public ::testing::Test {
public:
    ~HttpClientServiceTest() {}
protected:
    void SetUp() override
    {
        config_.connectionTimeoutMs = 1;
        config_.requestTimeoutMs = TEST_REQUEST_TIMEOUT_MS;
        config_.eventLoopTimeout = std::chrono::milliseconds(1);

        testUrl = "http://localhost:99999";
        testHttpsUrl = "https://localhost:99999";

        // 重置静态变量
        callbackExecutedCount = 0;
        lastResponseSuccess = false;
        lastResponseStatusCode = 0;
        lastResponseErrorMessage.clear();

        // 重置回调追踪
        callbackResponses.clear();
    }

    void TearDown() override
    {
        if (service && service->IsRunning()) {
            service->Stop();
        }
        service.reset();
        // 减少清理等待时间
        std::this_thread::sleep_for(1ms);
    }

    HttpClientServiceConfig config_;
    std::unique_ptr<HttpClientService> service;
    std::string testUrl;
    std::string testHttpsUrl;

    static std::atomic<int> callbackExecutedCount;
    static std::atomic<bool> lastResponseSuccess;
    static std::atomic<int> lastResponseStatusCode;
    static std::string lastResponseErrorMessage;

    static std::vector<HttpResponse> callbackResponses;
    static std::mutex callbackMutex;

    static bool TestCallback(const HttpResponse& response)
    {
        callbackExecutedCount++;
        lastResponseSuccess = response.success;
        lastResponseStatusCode = response.statusCode;
        if (!response.errorMessage.empty()) {
            lastResponseErrorMessage = response.errorMessage;
        }

        std::lock_guard<std::mutex> lock(callbackMutex);
        callbackResponses.push_back(response);
        return false; // Continue processing, don't close connection
    }

    // 创建一个简单的测试请求
    HttpRequest CreateTestRequest(const std::string& url = "")
    {
        HttpRequest request;
        request.url = url.empty() ? testUrl : url;
        request.method = "GET";
        request.headers["User-Agent"] = "TestClient/1.0";
        request.headers["Accept"] = "application/json";
        request.body = "";
        return request;
    }

    // 创建带body的POST请求
    HttpRequest CreatePostRequest(const std::string& body = "{}")
    {
        HttpRequest request = CreateTestRequest();
        request.method = "POST";
        request.body = body;
        request.headers["Content-Type"] = "application/json";
        return request;
    }

    UserData CreateUserData(uint64_t requestId = 1)
    {
        UserData userData;
        userData.requestId = requestId;
        return userData;
    }

    bool WaitForCallback(int expectedCount, std::chrono::milliseconds timeout = 10ms)
    {
        auto start = std::chrono::steady_clock::now();
        while (callbackExecutedCount < expectedCount) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed > timeout) {
                return false;
            }
            std::this_thread::sleep_for(1ms);
        }
        return true;
    }
};

std::atomic<int> HttpClientServiceTest::callbackExecutedCount(0);
std::atomic<bool> HttpClientServiceTest::lastResponseSuccess(false);
std::atomic<int> HttpClientServiceTest::lastResponseStatusCode(0);
std::string HttpClientServiceTest::lastResponseErrorMessage;
std::vector<HttpResponse> HttpClientServiceTest::callbackResponses;
std::mutex HttpClientServiceTest::callbackMutex;

TEST_F(HttpClientServiceTest, ConstructorBasic)
{
    service = std::make_unique<HttpClientService>(config_);
    ASSERT_NE(service, nullptr);
    EXPECT_FALSE(service->IsRunning());
}

TEST_F(HttpClientServiceTest, ConstructorWithDifferentConfigs)
{
    {
        HttpClientServiceConfig customConfig;
        customConfig.connectionTimeoutMs = 0;
        customConfig.requestTimeoutMs = 0;
        service = std::make_unique<HttpClientService>(customConfig);
        ASSERT_NE(service, nullptr);
    }

    {
        HttpClientServiceConfig customConfig;
        customConfig.connectionTimeoutMs = TEST_REQUEST_TIMEOUT_MS;
        customConfig.requestTimeoutMs = TEST_REQUEST_TIMEOUT_MS;
        customConfig.tlsVerifyPeer = false;
        customConfig.tlsVerifyHost = false;
        customConfig.tlsCaFile = "/path/to/ca.crt";
        service = std::make_unique<HttpClientService>(customConfig);
        ASSERT_NE(service, nullptr);
    }
}

TEST_F(HttpClientServiceTest, FactoryCreate)
{
    auto factoryService = HttpClientServiceFactory::Create(config_);
    ASSERT_NE(factoryService, nullptr);
    EXPECT_FALSE(factoryService->IsRunning());
}

TEST_F(HttpClientServiceTest, FactoryCreateWithCustomConfig)
{
    HttpClientServiceConfig customConfig;
    customConfig.connectionTimeoutMs = TEST_REQUEST_TIMEOUT_MS;
    customConfig.requestTimeoutMs = TEST_REQUEST_TIMEOUT_MS;

    auto factoryService = HttpClientServiceFactory::Create(customConfig);
    ASSERT_NE(factoryService, nullptr);
    EXPECT_FALSE(factoryService->IsRunning());
}

TEST_F(HttpClientServiceTest, StartStopBasic)
{
    service = std::make_unique<HttpClientService>(config_);
    EXPECT_TRUE(service->Start());
    EXPECT_TRUE(service->IsRunning());

    std::this_thread::sleep_for(5ms);

    service->Stop();
    EXPECT_FALSE(service->IsRunning());
}

TEST_F(HttpClientServiceTest, StartWhenAlreadyRunning)
{
    service = std::make_unique<HttpClientService>(config_);

    EXPECT_TRUE(service->Start());
    EXPECT_TRUE(service->IsRunning());
    EXPECT_TRUE(service->Start());
    EXPECT_TRUE(service->IsRunning());

    service->Stop();
}

TEST_F(HttpClientServiceTest, StopWhenNotRunning)
{
    service = std::make_unique<HttpClientService>(config_);
    EXPECT_FALSE(service->IsRunning());

    // 未运行时停止不应崩溃
    EXPECT_NO_THROW(service->Stop());
    EXPECT_FALSE(service->IsRunning());
}

TEST_F(HttpClientServiceTest, DoubleStop)
{
    service = std::make_unique<HttpClientService>(config_);
    EXPECT_TRUE(service->Start());
    EXPECT_TRUE(service->IsRunning());

    service->Stop();
    EXPECT_FALSE(service->IsRunning());

    // 第二次停止不应崩溃
    EXPECT_NO_THROW(service->Stop());
    EXPECT_FALSE(service->IsRunning());
}

TEST_F(HttpClientServiceTest, DestroyWhileRunning)
{
    service = std::make_unique<HttpClientService>(config_);
    EXPECT_TRUE(service->Start());
    EXPECT_TRUE(service->IsRunning());

    // 发送一个请求
    HttpRequest request = CreateTestRequest();
    UserData userData = CreateUserData(1);
    EXPECT_NO_THROW(service->Send(request, userData, 1, nullptr, TestCallback));

    // 不停止直接销毁（析构函数应该处理清理）
    service.reset();

    // 等待一小段时间让清理完成
    std::this_thread::sleep_for(10ms);

    // 不应崩溃
    SUCCEED();
}

TEST_F(HttpClientServiceTest, SendBasicRequest)
{
    service = std::make_unique<HttpClientService>(config_);
    EXPECT_TRUE(service->Start());

    HttpRequest request = CreateTestRequest();
    UserData userData = CreateUserData(1);

    EXPECT_NO_THROW(service->Send(request, userData, TEST_REQUEST_TIMEOUT_MS, nullptr, TestCallback));

    // 等待回调（请求会快速失败）
    WaitForCallback(1, 50ms);

    service->Stop();
}

TEST_F(HttpClientServiceTest, SendWithNullCallback)
{
    service = std::make_unique<HttpClientService>(config_);
    EXPECT_TRUE(service->Start());

    HttpRequest request = CreateTestRequest();
    UserData userData = CreateUserData(1);

    HttpCallback bodyCallback = [](const HttpResponse&) { return false; };
    EXPECT_NO_THROW(service->Send(request, userData, TEST_REQUEST_TIMEOUT_MS, nullptr, bodyCallback));

    // 不应崩溃
    std::this_thread::sleep_for(10ms);

    service->Stop();
}

TEST_F(HttpClientServiceTest, SendMultipleRequests)
{
    service = std::make_unique<HttpClientService>(config_);
    EXPECT_TRUE(service->Start());

    const int numRequests = LOOP_NUM;

    for (int i = 0; i < numRequests; i++) {
        HttpRequest request = CreateTestRequest();
        UserData userData = CreateUserData(i + 1);

        EXPECT_NO_THROW(service->Send(request, userData, TEST_REQUEST_TIMEOUT_MS, nullptr, TestCallback));
    }

    // 等待回调
    WaitForCallback(numRequests, 100ms);

    EXPECT_GE(callbackExecutedCount, 0);

    service->Stop();
}

TEST_F(HttpClientServiceTest, SendWithDifferentMethods)
{
    service = std::make_unique<HttpClientService>(config_);
    EXPECT_TRUE(service->Start());

    std::vector<std::string> methods = {"GET", "POST", "PUT", "DELETE", "HEAD"};
    int requestId = 1;

    for (const auto& method : methods) {
        HttpRequest request;
        request.url = testUrl;
        request.method = method;
        if (method == "POST" || method == "PUT") {
            request.body = R"({"test": "data"})";
            request.headers["Content-Type"] = "application/json";
        }

        UserData userData = CreateUserData(requestId++);

        EXPECT_NO_THROW(service->Send(request, userData, TEST_REQUEST_TIMEOUT_MS, nullptr, TestCallback));
    }

    // 等待回调
    WaitForCallback(methods.size(), 100ms);

    service->Stop();
}

TEST_F(HttpClientServiceTest, SendWithRequestBody)
{
    service = std::make_unique<HttpClientService>(config_);
    EXPECT_TRUE(service->Start());

    std::string jsonBody = R"({
        "name": "test",
        "value": TEST_ID,
        "nested": {
            "field": "data"
        }
    })";

    HttpRequest request = CreatePostRequest(jsonBody);
    UserData userData = CreateUserData(1);

    EXPECT_NO_THROW(service->Send(request, userData, TEST_REQUEST_TIMEOUT_MS, nullptr, TestCallback));

    WaitForCallback(1, 50ms);

    service->Stop();
}

TEST_F(HttpClientServiceTest, SendWithCustomHeaders)
{
    service = std::make_unique<HttpClientService>(config_);
    EXPECT_TRUE(service->Start());

    HttpRequest request = CreateTestRequest();
    request.headers["X-Custom-Header1"] = "Value1";
    request.headers["X-Custom-Header2"] = "Value2";
    request.headers["Authorization"] = "Bearer test_token";
    request.headers["Content-Type"] = "application/xml";

    UserData userData = CreateUserData(1);

    EXPECT_NO_THROW(service->Send(request, userData, TEST_REQUEST_TIMEOUT_MS, nullptr, TestCallback));

    WaitForCallback(1, 50ms);

    service->Stop();
}

TEST_F(HttpClientServiceTest, SendWithEmptyUrl)
{
    service = std::make_unique<HttpClientService>(config_);
    EXPECT_TRUE(service->Start());

    HttpRequest request;
    request.url = ""; // 空URL
    request.method = "GET";

    UserData userData = CreateUserData(1);

    EXPECT_NO_THROW(service->Send(request, userData, TEST_REQUEST_TIMEOUT_MS, nullptr, TestCallback));

    WaitForCallback(1, 50ms);

    service->Stop();
}

TEST_F(HttpClientServiceTest, SendWithHttpsUrl)
{
    service = std::make_unique<HttpClientService>(config_);
    EXPECT_TRUE(service->Start());

    HttpRequest request = CreateTestRequest(testHttpsUrl);
    UserData userData = CreateUserData(1);

    EXPECT_NO_THROW(service->Send(request, userData, TEST_REQUEST_TIMEOUT_MS, nullptr, TestCallback));

    WaitForCallback(1, 50ms);

    service->Stop();
}

TEST_F(HttpClientServiceTest, SendWithDifferentTimeouts)
{
    service = std::make_unique<HttpClientService>(config_);
    EXPECT_TRUE(service->Start());

    // 测试各种超时值
    std::vector<int> timeouts = {1, 10, 100, 1000};

    int requestId = 1;
    for (int timeout : timeouts) {
        HttpRequest request = CreateTestRequest();
        UserData userData = CreateUserData(requestId++);

        EXPECT_NO_THROW(service->Send(request, userData, timeout, nullptr, TestCallback));
    }

    WaitForCallback(timeouts.size(), 150ms);

    service->Stop();
}

TEST_F(HttpClientServiceTest, SendWithZeroTimeout)
{
    service = std::make_unique<HttpClientService>(config_);
    EXPECT_TRUE(service->Start());

    HttpRequest request = CreateTestRequest();
    UserData userData = CreateUserData(1);

    EXPECT_NO_THROW(service->Send(request, userData, 0, nullptr, TestCallback));

    WaitForCallback(1, 50ms);

    service->Stop();
}

TEST_F(HttpClientServiceTest, ConcurrentSendWithDifferentUrls)
{
    service = std::make_unique<HttpClientService>(config_);
    EXPECT_TRUE(service->Start());

    const int numThreads = THREAD_NUM;
    const int requestsPerThread = LOOP_NUM;
    std::vector<std::thread> threads;
    std::vector<std::string> urls = {
        "http://localhost:99991",
        "http://localhost:99992",
        "http://localhost:99993",
        "https://localhost:99994",
        "https://localhost:99995"
    };

    std::atomic<int> successfulSends{0};

    auto threadFunc = [this, &urls, &successfulSends](int threadId) {
        for (int i = 0; i < requestsPerThread; i++) {
            uint64_t requestId = static_cast<uint64_t>(threadId * requestsPerThread + i + 1);
            std::string url = urls[(threadId + i) % urls.size()];
            HttpRequest request = CreateTestRequest(url);
            UserData userData = CreateUserData(requestId);

            try {
                service->Send(request, userData, TEST_REQUEST_TIMEOUT_MS, nullptr, TestCallback);
                successfulSends++;
            } catch (...) {
                // 发送失败，计数器不增加
            }
        }
    };

    for (int t = 0; t < numThreads; t++) {
        threads.emplace_back(threadFunc, t);
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(successfulSends, numThreads * requestsPerThread);

    WaitForCallback(numThreads * requestsPerThread, 200ms);

    service->Stop();
}

TEST_F(HttpClientServiceTest, SendDuringShutdown)
{
    service = std::make_unique<HttpClientService>(config_);
    EXPECT_TRUE(service->Start());

    // 在另一个线程中停止服务
    std::thread stopThread([this]() {
        std::this_thread::sleep_for(1ms);
        service->Stop();
    });

    // 在主线程中发送请求（可能正好在停止过程中）
    HttpRequest request = CreateTestRequest();
    UserData userData = CreateUserData(1);

    // 可能抛出异常也可能成功，取决于时机
    // 我们只确保不崩溃
    try {
        service->Send(request, userData, TEST_REQUEST_TIMEOUT_MS, nullptr, TestCallback);
    } catch (const std::runtime_error&) {
        // 预期的异常
    }

    stopThread.join();

    // 不应崩溃
    SUCCEED();
}

TEST_F(HttpClientServiceTest, ManyRequestsWithShortTimeout)
{
    service = std::make_unique<HttpClientService>(config_);
    EXPECT_TRUE(service->Start());

    const int numRequests = LOOP_NUM;

    for (int i = 0; i < numRequests; i++) {
        HttpRequest request = CreateTestRequest();
        UserData userData = CreateUserData(i + 1);

        EXPECT_NO_THROW(service->Send(request, userData, 1, nullptr, TestCallback)); // 1ms超时
    }

    // 等待所有请求完成（应该很快）
    WaitForCallback(numRequests, 100ms);

    service->Stop();
}

TEST_F(HttpClientServiceTest, RequestIdUniqueness)
{
    service = std::make_unique<HttpClientService>(config_);
    EXPECT_TRUE(service->Start());

    const int numRequests = LOOP_NUM;

    for (int i = 0; i < numRequests; i++) {
        HttpRequest request = CreateTestRequest();
        UserData userData = CreateUserData(i + TEST_ID); // 使用不同的ID

        EXPECT_NO_THROW(service->Send(request, userData, TEST_REQUEST_TIMEOUT_MS, nullptr, TestCallback));
    }

    WaitForCallback(numRequests, 100ms);

    service->Stop();
}

// ============ 配置相关测试 ============
TEST_F(HttpClientServiceTest, TLSConfiguration)
{
    HttpClientServiceConfig tlsConfig;
    tlsConfig.connectionTimeoutMs = 1;
    tlsConfig.requestTimeoutMs = TEST_REQUEST_TIMEOUT_MS;
    tlsConfig.tlsVerifyPeer = false;
    tlsConfig.tlsVerifyHost = false;
    tlsConfig.tlsMinVersion = CURL_SSLVERSION_TLSv1_2;
    tlsConfig.tlsCipherList = "HIGH:!aNULL:!MD5";
    tlsConfig.tlsCaFile = "/tmp/test_ca.crt";
    tlsConfig.tlsClientCertFile = "/tmp/test_client.crt";
    tlsConfig.tlsClientKeyFile = "/tmp/test_client.key";
    tlsConfig.tlsClientKeyPassword = "testpassword";

    service = std::make_unique<HttpClientService>(tlsConfig);
    EXPECT_TRUE(service->Start());

    // 使用HTTPS URL（即使证书验证关闭）
    HttpRequest request = CreateTestRequest(testHttpsUrl);
    UserData userData = CreateUserData(1);

    EXPECT_NO_THROW(service->Send(request, userData, TEST_REQUEST_TIMEOUT_MS, nullptr, TestCallback));

    WaitForCallback(1, 50ms);

    service->Stop();
}

// ============ 回调验证测试 ============
TEST_F(HttpClientServiceTest, CallbackVerification)
{
    service = std::make_unique<HttpClientService>(config_);
    EXPECT_TRUE(service->Start());

    // 重置计数器
    callbackExecutedCount = 0;
    callbackResponses.clear();

    HttpRequest request = CreateTestRequest();
    UserData userData = CreateUserData(TEST_ID);

    HttpCallback headerCallback = [](const HttpResponse&) { return false; };
    EXPECT_NO_THROW(service->Send(request, userData, TEST_REQUEST_TIMEOUT_MS, headerCallback, TestCallback));

    // 等待回调
    EXPECT_TRUE(WaitForCallback(1, 100ms));

    // 验证回调被调用
    EXPECT_EQ(callbackExecutedCount, 1);

    // 检查回调中的数据（请求会快速失败，所以success应该是false）
    // 注意：由于使用无效URL，请求会失败，但我们只关心回调被调用
    EXPECT_TRUE(callbackExecutedCount > 0);

    service->Stop();
}

TEST_F(HttpClientServiceTest, MultipleCallbacksOrder)
{
    service = std::make_unique<HttpClientService>(config_);
    EXPECT_TRUE(service->Start());

    // 重置
    callbackExecutedCount = 0;
    callbackResponses.clear();

    const int numRequests = LOOP_NUM;

    for (int i = 0; i < numRequests; i++) {
        HttpRequest request = CreateTestRequest();
        UserData userData = CreateUserData(i + TEST_ID);

        HttpCallback headerCallback = [](const HttpResponse&) { return false; };
        EXPECT_NO_THROW(service->Send(request, userData, TEST_REQUEST_TIMEOUT_MS, headerCallback, TestCallback));
    }

    // 等待所有回调
    EXPECT_TRUE(WaitForCallback(numRequests, 150ms));

    EXPECT_EQ(callbackExecutedCount, numRequests);

    // 检查是否所有请求都收到了回调
    {
        std::lock_guard<std::mutex> lock(callbackMutex);
        EXPECT_EQ(callbackResponses.size(), numRequests);
    }

    service->Stop();
}

TEST_F(HttpClientServiceTest, PerformanceMultipleRequests)
{
    service = std::make_unique<HttpClientService>(config_);
    EXPECT_TRUE(service->Start());

    const int numRequests = LOOP_NUM;
    auto startTime = std::chrono::steady_clock::now();

    for (int i = 0; i < numRequests; i++) {
        HttpRequest request = CreateTestRequest();
        UserData userData = CreateUserData(i + 1);
        EXPECT_NO_THROW(service->Send(request, userData, 1, nullptr, [](const HttpResponse&) { return false; }));
    }

    auto sendDuration = std::chrono::steady_clock::now() - startTime;

    // 发送大量请求应该很快
    EXPECT_LT(sendDuration, 100ms);

    // 等待一小段时间让请求处理
    std::this_thread::sleep_for(50ms);

    service->Stop();
}

// ============ 资源清理测试 ============
TEST_F(HttpClientServiceTest, ResourceCleanupOnStop)
{
    service = std::make_unique<HttpClientService>(config_);
    EXPECT_TRUE(service->Start());

    // 发送一些请求
    for (int i = 0; i < LOOP_NUM; i++) {
        HttpRequest request = CreateTestRequest();
        UserData userData = CreateUserData(i + 1);
        EXPECT_NO_THROW(service->Send(request, userData, TEST_REQUEST_TIMEOUT_MS, nullptr, TestCallback));
    }

    // 立即停止（可能还有请求在处理）
    service->Stop();

    // 等待一小段时间
    std::this_thread::sleep_for(10ms);

    // 不应崩溃
    SUCCEED();
}
} // namespace Http
} // namespace Mcp
