/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "mcp_log.h"
#include "shared/http_common.h"
#include "client/http_client.h"

using namespace std::chrono_literals;

static constexpr int INVALID_PORT = 65534;
static constexpr int LOOP_NUM = 3;

namespace Mcp::Http {

// Test fixture for HttpClient tests
class HttpClientTest : public ::testing::Test {
public:
    ~HttpClientTest() {}
protected:
    void SetUp() override
    {
        // 测试使用无效的主机和端口确保快速失败
        invalidHost = "localhost";
        invalidPort = INVALID_PORT; // 不太可能被占用的高端口
    }

    void TearDown() override
    {
        // 不需要特殊清理
    }

    std::string invalidHost;
    uint16_t invalidPort;

    // 创建测试HTTP请求
    HttpRequest CreateTestRequest()
    {
        HttpRequest request;
        request.method = "GET";
        request.url = "/test";
        request.headers["User-Agent"] = "TestClient";
        request.body = "";
        return request;
    }

    // 创建POST请求
    HttpRequest CreatePostRequest()
    {
        HttpRequest request;
        request.method = "POST";
        request.url = "/api";
        request.headers["Content-Type"] = "application/json";
        request.body = R"({"test": "data"})";
        return request;
    }
};

// 构造函数测试
TEST_F(HttpClientTest, Constructor)
{
    HttpClient client;
    // 构造函数不应崩溃
    SUCCEED();
}

// SendRequest测试 - 使用无效地址
TEST_F(HttpClientTest, SendRequestToInvalidAddress)
{
    HttpClient client;
    HttpRequest request = CreateTestRequest();

    // 使用无效端口，请求应该快速失败
    auto response = client.SendRequest(invalidHost, invalidPort, request, 1);

    // 由于网络不可达，应该返回nullopt
    EXPECT_FALSE(response.has_value());
}

TEST_F(HttpClientTest, SendRequestToInvalidAddressWithCustomTimeout)
{
    HttpClient client;
    HttpRequest request = CreateTestRequest();

    // 使用自定义超时时间
    auto response = client.SendRequest(invalidHost, invalidPort, request, 10);
    EXPECT_FALSE(response.has_value());
}

TEST_F(HttpClientTest, SendRequestToInvalidAddressWithZeroTimeout)
{
    HttpClient client;
    HttpRequest request = CreateTestRequest();

    // 使用0超时（无超时）
    auto response = client.SendRequest(invalidHost, invalidPort, request, 0);
    EXPECT_FALSE(response.has_value());
}

TEST_F(HttpClientTest, SendRequestToInvalidAddressWithNegativeTimeout)
{
    HttpClient client;
    HttpRequest request = CreateTestRequest();

    // 使用负超时
    auto response = client.SendRequest(invalidHost, invalidPort, request, -1);
    EXPECT_FALSE(response.has_value());
}

// 测试POST请求
TEST_F(HttpClientTest, SendPostRequestToInvalidAddress)
{
    HttpClient client;
    HttpRequest request = CreatePostRequest();

    auto response = client.SendRequest(invalidHost, invalidPort, request, 1);
    EXPECT_FALSE(response.has_value());
}

// 测试空主机名
TEST_F(HttpClientTest, SendRequestToEmptyHost)
{
    HttpClient client;
    HttpRequest request = CreateTestRequest();

    auto response = client.SendRequest("", 80, request, 1);
    EXPECT_FALSE(response.has_value());
}

// 测试空请求
TEST_F(HttpClientTest, SendEmptyRequest)
{
    HttpClient client;
    HttpRequest request;

    auto response = client.SendRequest(invalidHost, invalidPort, request, 1);
    EXPECT_FALSE(response.has_value());
}

// 测试带有自定义头部的请求
TEST_F(HttpClientTest, SendRequestWithCustomHeaders)
{
    HttpClient client;
    HttpRequest request = CreateTestRequest();
    request.headers["X-Custom-Header"] = "CustomValue";
    request.headers["Accept"] = "application/json";

    auto response = client.SendRequest(invalidHost, invalidPort, request, 1);
    EXPECT_FALSE(response.has_value());
}

// 测试重复调用
TEST_F(HttpClientTest, MultipleSendRequests)
{
    HttpClient client;
    HttpRequest request = CreateTestRequest();

    // 多次调用SendRequest
    for (int i = 0; i < LOOP_NUM; i++) {
        auto response = client.SendRequest(invalidHost, invalidPort, request, 1);
        EXPECT_FALSE(response.has_value());
    }
}

// 测试不同的HTTP方法
TEST_F(HttpClientTest, SendRequestWithDifferentMethods)
{
    HttpClient client;

    std::vector<std::string> methods = {"GET", "POST", "PUT", "DELETE", "HEAD"};

    for (const auto& method : methods) {
        HttpRequest request;
        request.method = method;
        request.url = "/test";
        request.body = (method == "POST" || method == "PUT") ? "test data" : "";

        auto response = client.SendRequest(invalidHost, invalidPort, request, 1);
        EXPECT_FALSE(response.has_value());
    }
}

// 测试大超时值
TEST_F(HttpClientTest, SendRequestWithLargeTimeout)
{
    HttpClient client;
    HttpRequest request = CreateTestRequest();

    // 使用较大的超时，但立即失败
    auto response = client.SendRequest(invalidHost, invalidPort, request, 10000);
    EXPECT_FALSE(response.has_value());
}

// 测试客户端生命周期
TEST_F(HttpClientTest, ClientLifecycle)
{
    // 测试客户端可以重复使用
    HttpClient client;

    for (int i = 0; i < LOOP_NUM; i++) {
        HttpRequest request = CreateTestRequest();
        auto response = client.SendRequest(invalidHost, invalidPort, request, 1);
        EXPECT_FALSE(response.has_value());
    }
}

// 测试错误处理
TEST_F(HttpClientTest, ErrorHandling)
{
    HttpClient client;

    // 使用各种无效参数组合
    struct TestCase {
        std::string host;
        uint16_t port;
        HttpRequest request;
        int timeout;
    };

    std::vector<TestCase> testCases = {
        {"", 80, CreateTestRequest(), 1},
        {invalidHost, 0, CreateTestRequest(), 1},
        {invalidHost, invalidPort, HttpRequest(), 1},
        {invalidHost, invalidPort, CreateTestRequest(), -100},
    };

    for (const auto& tc : testCases) {
        auto response = client.SendRequest(tc.host, tc.port, tc.request, tc.timeout);
        EXPECT_FALSE(response.has_value());
    }
}

// 测试快速失败
TEST_F(HttpClientTest, QuickFailure)
{
    HttpClient client;
    HttpRequest request = CreateTestRequest();

    // 设置极短超时，确保测试快速完成
    auto start = std::chrono::steady_clock::now();
    auto response = client.SendRequest(invalidHost, invalidPort, request, 1);
    auto end = std::chrono::steady_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // 测试应该在短时间内完成
    EXPECT_FALSE(response.has_value());
    // 注意：由于系统调用和重试，实际时间可能超过1ms，但应该远小于200ms
    EXPECT_LT(duration.count(), 200);
}

} // namespace Mcp::Http
