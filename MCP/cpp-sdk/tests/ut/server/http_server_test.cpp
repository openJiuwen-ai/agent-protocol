/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <future>
#include <vector>
#include <memory>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <sstream>

#include "server/http_server.h"
#include "mcp_type.h"
#include "shared/http_common.h"
#include "mcp_log.h"

using namespace Mcp;
using namespace Mcp::Http;

static constexpr int WAIT_TIME_MS = 100;
static constexpr int TIME_OUT_MS = 5000;
static constexpr int MS_PER_SECOND = 1000;
static constexpr int BODY_SIZE = 4;
static constexpr int POS = 15;
static constexpr int HTTP_SIZE = 10;
static constexpr int LETTER_SIZE = 26;
static constexpr int DEFAULT_PORT = 19000;
static constexpr int BUFFER_SIZE = 8192;


inline sockaddr* ToSockaddr(sockaddr_in* addr)
{
    return static_cast<sockaddr*>(static_cast<void*>(addr));
}

class TestHttpClient {
public:
    TestHttpClient() : sockfd_(-1), connected_(false) {}

    ~TestHttpClient()
    {
        DisConnect();
    }

    bool Connect(const std::string& host, uint16_t port, int timeoutMs = TIME_OUT_MS)
    {
        sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd_ < 0) {
            return false;
        }

        struct sockaddr_in serverAddr;
        memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);

        if (inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr) <= 0) {
            close(sockfd_);
            sockfd_ = -1;
            return false;
        }

        // 设置非阻塞以便设置连接超时
        int flags = fcntl(sockfd_, F_GETFL, 0);
        fcntl(sockfd_, F_SETFL, flags | O_NONBLOCK);

        int result = ::connect(sockfd_, ToSockaddr(&serverAddr), sizeof(serverAddr));
        if (result < 0) {
            if (errno == EINPROGRESS) {
                // 使用select等待连接完成
                fd_set writefds;
                FD_ZERO(&writefds);
                FD_SET(sockfd_, &writefds);

                struct timeval tv;
                tv.tv_sec = timeoutMs / MS_PER_SECOND;
                tv.tv_usec = (timeoutMs % MS_PER_SECOND) * MS_PER_SECOND;

                result = select(sockfd_ + 1, nullptr, &writefds, nullptr, &tv);
                if (result <= 0) {
                    close(sockfd_);
                    sockfd_ = -1;
                    return false;
                }
            } else {
                close(sockfd_);
                sockfd_ = -1;
                return false;
            }
        }

        fcntl(sockfd_, F_SETFL, flags & ~O_NONBLOCK);
        connected_ = true;
        return true;
    }

    bool DisConnect()
    {
        if (sockfd_ >= 0) {
            close(sockfd_);
            sockfd_ = -1;
        }
        connected_ = false;
        return true;
    }

    std::string SendRequest(const std::string& request, int timeoutMs = TIME_OUT_MS)
    {
        if (!IsConnectionValid()) {
            return "";
        }

        if (!SendAllData(request)) {
            return "";
        }

        return ReceiveResponse(timeoutMs);
    }

    bool IsConnectionValid() const
    {
        return sockfd_ >= 0 && connected_;
    }

    bool SendAllData(const std::string& request)
    {
        ssize_t sent = ::send(sockfd_, request.c_str(), request.length(), 0);
        return sent == static_cast<ssize_t>(request.length());
    }

    std::string ReceiveResponse(int timeoutMs)
    {
        std::string response;

        const size_t maxResponseSize = HTTP_SIZE * 1024 * 1024;
        const constexpr int maxRetries = 1000;
        int retryCount = 0;

        auto startTime = std::chrono::steady_clock::now();

        while (retryCount < maxRetries) {
            if (response.size() >= maxResponseSize) {
                break;
            }

            if (IsTimeoutExceeded(startTime, timeoutMs)) {
                break;
            }

            int remainingMs = CalculateRemainingTime(startTime, timeoutMs);
            if (remainingMs <= 0) {
                break;
            }

            if (!WaitForData(remainingMs)) {
                break;
            }

            std::string buffer;
            ssize_t received = ReceiveData(buffer, BUFFER_SIZE);
            if (received < 0) {
                if (ShouldRetryReceive()) {
                    ++retryCount;
                    continue;
                }
                break;
            } else if (received == 0) {
                break;
            }

            response.append(buffer);

            if (IsResponseComplete(response)) {
                break;
            }
            ++retryCount;
        }

        if (retryCount >= maxRetries) {
        }

        return response;
    }

    bool IsTimeoutExceeded(const std::chrono::steady_clock::time_point& startTime, int timeoutMs)
    {
        auto currentTime = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            currentTime - startTime).count();
        return elapsedMs >= timeoutMs;
    }

    int CalculateRemainingTime(const std::chrono::steady_clock::time_point& startTime, int timeoutMs)
    {
        auto currentTime = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            currentTime - startTime).count();
        return timeoutMs - static_cast<int>(elapsedMs);
    }

    bool WaitForData(int remainingMs)
    {
        fd_set readfds;
        struct timeval tv;

        tv.tv_sec = remainingMs / MS_PER_SECOND;
        tv.tv_usec = (remainingMs % MS_PER_SECOND) * MS_PER_SECOND;

        FD_ZERO(&readfds);
        FD_SET(sockfd_, &readfds);
        int result = select(sockfd_ + 1, &readfds, nullptr, nullptr, &tv);
        if (result == 0) {
            return false; // 超时
        } else if (result < 0) {
            if (errno == EINTR) {
                return true; // 允许重试
            }
            return false; // 错误
        }

        return true; // 有数据可读
    }

    ssize_t ReceiveData(std::string& buffer, size_t bufferSize)
    {
        buffer.resize(bufferSize);
        ssize_t received = recv(sockfd_, &buffer[0], bufferSize, 0);
        if (received > 0) {
            buffer.resize(received);
        } else if (received == 0) {
            buffer.clear();
        } else {
            buffer.clear();
        }

        return received;
    }

    bool ShouldRetryReceive()
    {
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
    }

    bool IsConnected() const
    {
        return connected_ && sockfd_ >= 0;
    }

    int Sockfd() const { return sockfd_; }

private:
    bool IsResponseComplete(const std::string& response)
    {
        size_t headerEnd = FindHeaderEnd(response);
        if (headerEnd == std::string::npos) {
            return false;
        }

        if (HasCompleteBodyByContentLength(response, headerEnd)) {
            return true;
        }

        if (HasCompleteBodyByChunkedEncoding(response)) {
            return true;
        }

        return true;
    }

    size_t FindHeaderEnd(const std::string& response)
    {
        return response.find("\r\n\r\n");
    }

    bool HasCompleteBodyByContentLength(const std::string& response, size_t headerEnd)
    {
        const std::string contentLengthHeader = "Content-Length:";
        size_t clPos = response.find(contentLengthHeader);
        if (clPos == std::string::npos || clPos >= headerEnd) {
            return false;
        }
        size_t clEnd = response.find("\r\n", clPos);
        if (clEnd == std::string::npos) {
            return false;
        }
        std::string clValueStr = ExtractAndTrimContent(response, clPos, clEnd, contentLengthHeader.length());
        if (clValueStr.empty()) {
            return false;
        }

        return CheckBodyLength(response, headerEnd, clValueStr);
    }

    std::string ExtractAndTrimContent(const std::string& response,
                                        size_t clPos,
                                        size_t clEnd,
                                        size_t headerLength)
    {
        std::string clStr = response.substr(clPos + headerLength, clEnd - clPos - headerLength);

        size_t start = clStr.find_first_not_of(" \t");
        if (start == std::string::npos) {
            return "";
        }

        size_t end = clStr.find_last_not_of(" \t");
        return clStr.substr(start, end - start + 1);
    }

    bool CheckBodyLength(const std::string& response, size_t headerEnd, const std::string& clValueStr)
    {
        try {
            int contentLength = std::stoi(clValueStr);
            const size_t bodyStartOffset = 4; // "\r\n\r\n"的长度
            size_t bodyStart = headerEnd + bodyStartOffset;

            return (response.length() - bodyStart >= static_cast<size_t>(contentLength));
        }
        catch (...) {
            return false;
        }
    }

    bool HasCompleteBodyByChunkedEncoding(const std::string& response)
    {
        const std::string chunkedHeader = "Transfer-Encoding: chunked";
        if (response.find(chunkedHeader) == std::string::npos) {
            return false;
        }

        const std::string chunkedEndMarker = "\r\n0\r\n\r\n";
        return response.find(chunkedEndMarker) != std::string::npos;
    }

    int sockfd_;
    bool connected_;
};

class TestRouteHandlers {
public:
    static void HandleRoot(const Http::HttpRequest& request, Mcp::RequestContext& ctx)
    {
        Http::HttpResponse response;
        response.statusCode = HTTP_STATUS_OK;
        response.statusText = "OK";
        response.body = "Hello from root endpoint";
        response.headers["ConTENt-Type"] = "text/plain";
        response.headers["X-Test-Header"] = "test-value";
        ctx.httpSendFunc(response, ctx);
    }

    static void HandleEcho(const Http::HttpRequest& request, Mcp::RequestContext& ctx)
    {
        Http::HttpResponse response;
        response.statusCode = HTTP_STATUS_OK;
        response.statusText = "OK";
        response.body = "Echo: " + request.body;
        response.headers["ConTENt-Type"] = "text/plain";
        ctx.httpSendFunc(response, ctx);
    }

    static void HandleJson(const Http::HttpRequest& request, Mcp::RequestContext& ctx)
    {
        Http::HttpResponse response;
        response.statusCode = HTTP_STATUS_OK;
        response.statusText = "OK";
        response.body = R"({"status":"success","message":"JSON response"})";
        response.headers["ConTENt-Type"] = "application/json";
        ctx.httpSendFunc(response, ctx);
    }

    static void HandleError(const Http::HttpRequest& request, Mcp::RequestContext& ctx)
    {
        // 模拟处理器抛出异常
        throw std::runtime_error("Simulated handler exception for testing");
    }

    static void HandleSSE(const Http::HttpRequest& request, Mcp::RequestContext& ctx)
    {
        Http::HttpResponse response;
        response.statusCode = HTTP_STATUS_OK;
        response.statusText = "OK";
        response.headers["ConTENt-Type"] = "text/event-stream";
        response.headers["Cache-Control"] = "no-cache";
        response.body = "data: event 1\n\ndata: event 2\n\ndata: event 3\n\n";
        ctx.httpSendFunc(response, ctx);
    }

    static void HandleLargeResponse(const Http::HttpRequest& request, Mcp::RequestContext& ctx)
    {
        Http::HttpResponse response;
        response.statusCode = HTTP_STATUS_OK;
        response.statusText = "OK";
        response.headers["ConTENt-Type"] = "text/plain";

        // 生成10KB的响应
        std::string largeBody;
        for (int i = 0; i < MS_PER_SECOND; i++) {
            largeBody += "Line " + std::to_string(i) + ": ";
            largeBody += std::string(HTTP_SIZE, 'A' + (i % LETTER_SIZE));
            largeBody += "\n";
        }
        response.body = largeBody;

        ctx.httpSendFunc(response, ctx);
    }

    static void HandleCustomHeaders(const Http::HttpRequest& request, Mcp::RequestContext& ctx)
    {
        Http::HttpResponse response;
        response.statusCode = HTTP_STATUS_OK;
        response.statusText = "OK";
        response.body = "Response with custom headers";
        response.headers["ConTENt-Type"] = "text/plain";
        response.headers["X-Custom-1"] = "Value1";
        response.headers["X-Custom-2"] = "Value2";
        response.headers["Server"] = "TestServer";
        ctx.httpSendFunc(response, ctx);
    }

    static void HandlePOStOnly(const Http::HttpRequest& request, Mcp::RequestContext& ctx)
    {
        Http::HttpResponse response;
        if (request.method == "POST") {
            response.statusCode = HTTP_STATUS_OK;
            response.statusText = "OK";
            response.body = "POST request accepted";
        } else {
            response.statusCode = HTTP_STATUS_METHOD_NOT_ALLOWED;
            response.statusText = "Method Not Allowed";
            response.body = "Only POST method is allowed";
            response.headers["Allow"] = "POST";
        }
        response.headers["ConTENt-Type"] = "text/plain";
        ctx.httpSendFunc(response, ctx);
    }
};

class TestPortManager {
public:
    static uint16_t GetAvailablePort()
    {
        static uint16_t startPort = DEFAULT_PORT;
        static std::mutex portMutex;

        std::lock_guard<std::mutex> lock(portMutex);

        for (int i = 0; i < WAIT_TIME_MS; i++) {
            uint16_t port = startPort + i;

            // 尝试绑定到端口来检查是否可用
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) {
                continue;
            }

            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
            addr.sin_port = htons(port);

            int opt = 1;
            setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

            if (bind(sock, ToSockaddr(&addr), sizeof(addr)) == 0) {
                close(sock);
                return port;
            }

            close(sock);
        }

        // 如果找不到可用端口，使用一个固定范围内的随机端口
        return DEFAULT_PORT + (std::rand() % MS_PER_SECOND);
    }
};

bool ValidateHttpResponse(const std::string& response, int expectedStatus = HTTP_STATUS_OK)
{
    if (response.empty()) {
        return false;
    }

    // 检查响应行
    size_t lineEnd = response.find("\r\n");
    if (lineEnd == std::string::npos) {
        return false;
    }

    std::string statusLine = response.substr(0, lineEnd);
    if (statusLine.find("HTTP/1.1") == std::string::npos) {
        return false;
    }

    std::string statusStr = std::to_string(expectedStatus);
    if (statusLine.find(statusStr) == std::string::npos) {
        return false;
    }

    return true;
}

int GetResponseStatusCode(const std::string& response)
{
    if (response.empty()) {
        return 0;
    }

    size_t space1 = response.find(' ');
    if (space1 == std::string::npos) {
        return 0;
    }

    size_t space2 = response.find(' ', space1 + 1);
    if (space2 == std::string::npos) {
        return 0;
    }

    std::string codeStr = response.substr(space1 + 1, space2 - space1 - 1);
    try {
        return std::stoi(codeStr);
    } catch (...) {
        return 0;
    }
}

std::string GetResponseBody(const std::string& response)
{
    size_t headerEnd = response.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        return "";
    }

    return response.substr(headerEnd + BODY_SIZE);
}

bool ResponseContainsHeader(const std::string& response, const std::string& headerName)
{
    std::string headerLower = headerName;
    std::transform(headerLower.begin(), headerLower.end(), headerLower.begin(), ::tolower);

    std::istringstream stream(response);
    std::string line;

    // 跳过状态行
    std::getline(stream, line);

    while (std::getline(stream, line)) {
        if (line.empty() || line == "\r") {
            break; // 头部结束
        }

        // 去除回车
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        size_t colonPOS = line.find(':');
        if (colonPOS != std::string::npos) {
            std::string currentHeader = line.substr(0, colonPOS);
            std::transform(currentHeader.begin(), currentHeader.end(), currentHeader.begin(), ::tolower);

            if (currentHeader == headerLower) {
                return true;
            }
        }
    }

    return false;
}

class HttpServerTest : public ::testing::Test {
public:
    ~HttpServerTest() {}
protected:
    void SetUp() override
    {
        // 获取一个可用的测试端口
        testPort_ = TestPortManager::GetAvailablePort();

        // 设置测试路由
        routes_["/"] = TestRouteHandlers::HandleRoot;
        routes_["/echo"] = TestRouteHandlers::HandleEcho;
        routes_["/json"] = TestRouteHandlers::HandleJson;
        routes_["/error"] = TestRouteHandlers::HandleError;
        routes_["/sse"] = TestRouteHandlers::HandleSSE;
        routes_["/large"] = TestRouteHandlers::HandleLargeResponse;
        routes_["/headers"] = TestRouteHandlers::HandleCustomHeaders;
        routes_["/POSt-only"] = TestRouteHandlers::HandlePOStOnly;

        // 创建服务器（不带TLS）
        tlsConfig_.enabled = false;
        server_ = std::make_unique<HttpServer>("127.0.0.1", testPort_, tlsConfig_, routes_);
    }

    void TearDown() override
    {
        StopServer();
        // 给系统一些时间来清理
        std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));
    }

    void StartServer()
    {
        if (serverRunning_) {
            return;
        }

        serverThread_ = std::thread([this]() {
            try {
                server_->Run();
            } catch (const std::exception& e) {
                // 记录错误但不终止测试
                std::cerr << "Server error in thread: " << e.what() << std::endl;
            }
        });

        // 等待服务器启动
        std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));

        // 验证服务器是否在运行
        TestHttpClient testClient;
        bool reachable = false;
        for (int i = 0; i < HTTP_SIZE; i++) {
            if (testClient.Connect("127.0.0.1", testPort_, WAIT_TIME_MS)) {
                reachable = true;
                testClient.DisConnect();
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));
        }

        if (!reachable) {
            StopServer();
            FAIL() << "Server failed to start on port " << testPort_;
        }

        serverRunning_ = true;
    }

    void StopServer()
    {
        if (server_) {
            server_->Stop();
        }

        if (serverThread_.joinable()) {
            serverThread_.join();
        }

        serverRunning_ = false;
    }

    std::string createHttpRequest(const std::string& method,
                                  const std::string& path,
                                  const std::string& body = "",
                                  const std::unordered_map<std::string, std::string>& headers = {})
    {
        std::string request = method + " " + path + " HTTP/1.1\r\n";
        request += "Host: localhost:" + std::to_string(testPort_) + "\r\n";
        request += "Connection: close\r\n";

        // 添加自定义头部
        for (const auto& header : headers) {
            request += header.first + ": " + header.second + "\r\n";
        }

        // 如果有请求体，添加ConTENt-Length
        if (!body.empty()) {
            request += "ConTENt-Length: " + std::to_string(body.size()) + "\r\n";
        }

        request += "\r\n";

        if (!body.empty()) {
            request += body;
        }

        return request;
    }

    uint16_t testPort_;
    Http::RouteMap routes_;
    Mcp::TlsConfig tlsConfig_;
    std::unique_ptr<HttpServer> server_;
    std::thread serverThread_;
    bool serverRunning_ = false;
};

TEST_F(HttpServerTest, ServerStartsAndStops)
{
    // 测试服务器正常启动和停止
    ASSERT_NO_THROW(StartServer());
    ASSERT_TRUE(serverRunning_);

    // 验证服务器响应请求
    TestHttpClient client;
    ASSERT_TRUE(client.Connect("127.0.0.1", testPort_));

    std::string request = createHttpRequest("GET", "/");
    std::string response = client.SendRequest(request, WAIT_TIME_MS);

    EXPECT_TRUE(ValidateHttpResponse(response, HTTP_STATUS_OK));

    // 正常停止服务器
    ASSERT_NO_THROW(StopServer());
    EXPECT_FALSE(serverRunning_);
}

TEST_F(HttpServerTest, ConstructorWithDifferentConfigs)
{
    // 测试不同配置的构造函数
    Http::RouteMap emptyRoutes;
    Mcp::TlsConfig noTls;
    noTls.enabled = false;

    // 应该能正常构造
    HttpServer server1("127.0.0.1", 0, noTls, emptyRoutes);
    HttpServer server2("0.0.0.0", 0, noTls, routes_);

    SUCCEED(); // 构造成功
}

TEST_F(HttpServerTest, HandleGetRequestRoot)
{
    StartServer();

    TestHttpClient client;
    ASSERT_TRUE(client.Connect("127.0.0.1", testPort_));

    std::string request = createHttpRequest("GET", "/");
    std::string response = client.SendRequest(request, MS_PER_SECOND);

    EXPECT_TRUE(ValidateHttpResponse(response, HTTP_STATUS_OK));
    EXPECT_NE(GetResponseBody(response).find("Hello from root endpoint"), std::string::npos);
    EXPECT_TRUE(ResponseContainsHeader(response, "X-Test-Header"));

    client.DisConnect();
}

TEST_F(HttpServerTest, HandlePOStRequestWithBody)
{
    StartServer();

    TestHttpClient client;
    ASSERT_TRUE(client.Connect("127.0.0.1", testPort_));

    std::string requestBody = "This is a test message for echo";
    std::string request = createHttpRequest("POST", "/echo", requestBody);
    std::string response = client.SendRequest(request, MS_PER_SECOND);

    EXPECT_TRUE(ValidateHttpResponse(response, HTTP_STATUS_OK));
    std::string responseBody = GetResponseBody(response);
    EXPECT_NE(responseBody.find(requestBody), std::string::npos);

    client.DisConnect();
}

TEST_F(HttpServerTest, HandleJsonResponse)
{
    StartServer();

    TestHttpClient client;
    ASSERT_TRUE(client.Connect("127.0.0.1", testPort_));

    std::string request = createHttpRequest("GET", "/json");
    std::string response = client.SendRequest(request, MS_PER_SECOND);

    EXPECT_TRUE(ValidateHttpResponse(response, HTTP_STATUS_OK));
    EXPECT_TRUE(ResponseContainsHeader(response, "ConTENt-Type"));
    std::string responseBody = GetResponseBody(response);
    EXPECT_NE(responseBody.find("success"), std::string::npos);
    EXPECT_NE(responseBody.find("JSON response"), std::string::npos);

    client.DisConnect();
}

TEST_F(HttpServerTest, HandleNotFound)
{
    StartServer();

    TestHttpClient client;
    ASSERT_TRUE(client.Connect("127.0.0.1", testPort_));

    std::string request = createHttpRequest("GET", "/nonexisTENt");
    std::string response = client.SendRequest(request, MS_PER_SECOND);

    int statusCode = GetResponseStatusCode(response);
    EXPECT_EQ(statusCode, HTTP_STATUS_NOT_FOUND);
    EXPECT_FALSE(ValidateHttpResponse(response, HTTP_STATUS_OK));

    client.DisConnect();
}

TEST_F(HttpServerTest, HandleHandlerException)
{
    StartServer();

    TestHttpClient client;
    ASSERT_TRUE(client.Connect("127.0.0.1", testPort_));

    std::string request = createHttpRequest("GET", "/error");
    std::string response = client.SendRequest(request, MS_PER_SECOND);

    int statusCode = GetResponseStatusCode(response);
    EXPECT_EQ(statusCode, HTTP_STATUS_INTERNAL_SERVER_ERROR);

    client.DisConnect();
}

TEST_F(HttpServerTest, HandleLargeResponse)
{
    StartServer();

    TestHttpClient client;
    ASSERT_TRUE(client.Connect("127.0.0.1", testPort_));

    std::string request = createHttpRequest("GET", "/large");
    std::string response = client.SendRequest(request, MS_PER_SECOND); // 更长超时

    EXPECT_TRUE(ValidateHttpResponse(response, HTTP_STATUS_OK));
    EXPECT_GT(GetResponseBody(response).size(), TIME_OUT_MS); // 应该远大于5KB

    client.DisConnect();
}

TEST_F(HttpServerTest, HandleServerSentEvents)
{
    StartServer();

    TestHttpClient client;
    ASSERT_TRUE(client.Connect("127.0.0.1", testPort_));

    std::string request = createHttpRequest("GET", "/sse");
    std::string response = client.SendRequest(request, MS_PER_SECOND);

    EXPECT_TRUE(ValidateHttpResponse(response, HTTP_STATUS_OK));
    EXPECT_TRUE(ResponseContainsHeader(response, "ConTENt-Type"));

    std::string body = GetResponseBody(response);
    EXPECT_NE(body.find("event 1"), std::string::npos);
    EXPECT_NE(body.find("event 2"), std::string::npos);
    EXPECT_NE(body.find("event 3"), std::string::npos);

    client.DisConnect();
}

TEST_F(HttpServerTest, HandleCustomHeaders)
{
    StartServer();

    TestHttpClient client;
    ASSERT_TRUE(client.Connect("127.0.0.1", testPort_));

    std::string request = createHttpRequest("GET", "/headers");
    std::string response = client.SendRequest(request, MS_PER_SECOND);

    EXPECT_TRUE(ValidateHttpResponse(response, HTTP_STATUS_OK));
    EXPECT_TRUE(ResponseContainsHeader(response, "X-Custom-1"));
    EXPECT_TRUE(ResponseContainsHeader(response, "X-Custom-2"));
    EXPECT_TRUE(ResponseContainsHeader(response, "Server"));

    client.DisConnect();
}

TEST_F(HttpServerTest, HandleMethodNotAllowed)
{
    StartServer();

    TestHttpClient client;
    ASSERT_TRUE(client.Connect("127.0.0.1", testPort_));

    // 尝试GET到只允许POST的端点
    std::string request = createHttpRequest("GET", "/POSt-only");
    std::string response = client.SendRequest(request, MS_PER_SECOND);

    int statusCode = GetResponseStatusCode(response);
    EXPECT_EQ(statusCode, HTTP_STATUS_METHOD_NOT_ALLOWED);
    EXPECT_TRUE(ResponseContainsHeader(response, "Allow"));

    client.DisConnect();
}

TEST_F(HttpServerTest, HandlePOStMethodAllowed)
{
    StartServer();

    TestHttpClient client;
    ASSERT_TRUE(client.Connect("127.0.0.1", testPort_));

    // 发送POST到只允许POST的端点
    std::string request = createHttpRequest("POST", "/POSt-only", "test data");
    std::string response = client.SendRequest(request, MS_PER_SECOND);

    EXPECT_TRUE(ValidateHttpResponse(response, HTTP_STATUS_OK));
    EXPECT_NE(GetResponseBody(response).find("POST request accepted"), std::string::npos);

    client.DisConnect();
}

TEST_F(HttpServerTest, ConcurrentClients)
{
    StartServer();

    const int numClients = 5;
    std::vector<std::future<std::pair<int, bool>>> futures;

    auto makeRequest = [this](int clientId) -> std::pair<int, bool> {
        TestHttpClient client;
        if (!client.Connect("127.0.0.1", testPort_, TIME_OUT_MS)) {
            return {clientId, false};
        }

        std::string request = createHttpRequest("GET", "/", "Client " + std::to_string(clientId));
        std::string response = client.SendRequest(request, MS_PER_SECOND);

        bool success = ValidateHttpResponse(response, HTTP_STATUS_OK) &&
                      !GetResponseBody(response).empty();

        client.DisConnect();
        return {clientId, success};
    };

    // 启动多个客户端线程
    for (int i = 0; i < numClients; i++) {
        futures.push_back(std::async(std::launch::async, makeRequest, i));
    }

    // 收集结果
    int successCount = 0;
    for (auto& future : futures) {
        auto result = future.get();
        if (result.second) {
            successCount++;
        }
    }

    // 大多数请求应该成功
    EXPECT_GE(successCount, numClients - 1);
}

TEST_F(HttpServerTest, HandleMalformedRequest)
{
    StartServer();

    TestHttpClient client;
    ASSERT_TRUE(client.Connect("127.0.0.1", testPort_));

    // 发送畸形的HTTP请求
    std::string malformedRequest = "INVALID REQUEST LINE\r\n\r\n";
    std::string response = client.SendRequest(malformedRequest, MS_PER_SECOND);

    int statusCode = GetResponseStatusCode(response);
    // 应该是400错误请求或类似错误
    EXPECT_TRUE(statusCode == HTTP_STATUS_BAD_REQUEST || statusCode == HTTP_STATUS_METHOD_NOT_ALLOWED ||
                statusCode >= HTTP_STATUS_INTERNAL_SERVER_ERROR);

    client.DisConnect();
}

TEST_F(HttpServerTest, HandleLargeRequest)
{
    StartServer();

    TestHttpClient client;
    ASSERT_TRUE(client.Connect("127.0.0.1", testPort_));

    // 发送大请求体
    std::string largeBody(TIME_OUT_MS, 'X');
    std::string request = createHttpRequest("POST", "/echo", largeBody);
    std::string response = client.SendRequest(request, MS_PER_SECOND);

    EXPECT_TRUE(ValidateHttpResponse(response, HTTP_STATUS_OK));
    // 响应应该包含回显的数据
    EXPECT_GT(GetResponseBody(response).size(), WAIT_TIME_MS);

    client.DisConnect();
}

TEST_F(HttpServerTest, HandleRequestWithVariousHeaders)
{
    StartServer();

    TestHttpClient client;
    ASSERT_TRUE(client.Connect("127.0.0.1", testPort_));

    std::unordered_map<std::string, std::string> headers = {
        {"User-Agent", "TestClient/1.0"},
        {"Accept", "application/json, text/plain, */*"},
        {"Accept-Language", "en-US,en;q=0.9"},
        {"Accept-Encoding", "gzip, deflate, br"},
        {"X-Request-ID", "test-12345"},
        {"X-Custom-Header", "CustomValue"}
    };

    std::string request = createHttpRequest("GET", "/", "", headers);
    std::string response = client.SendRequest(request, MS_PER_SECOND);

    EXPECT_TRUE(ValidateHttpResponse(response, HTTP_STATUS_OK));

    client.DisConnect();
}

TEST_F(HttpServerTest, HandleEmptyBodyRequest)
{
    StartServer();

    TestHttpClient client;
    ASSERT_TRUE(client.Connect("127.0.0.1", testPort_));

    // 发送没有正文的POST请求
    std::string request = createHttpRequest("POST", "/echo", "");
    std::string response = client.SendRequest(request, MS_PER_SECOND);

    EXPECT_TRUE(ValidateHttpResponse(response, HTTP_STATUS_OK));
    EXPECT_NE(GetResponseBody(response).find("Echo: "), std::string::npos);

    client.DisConnect();
}

TEST_F(HttpServerTest, ServerHandlesConnectionClose)
{
    StartServer();

    // 建立连接并立即关闭
    TestHttpClient client;
    ASSERT_TRUE(client.Connect("127.0.0.1", testPort_));
    client.DisConnect();

    // 短暂等待让服务器处理关闭
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));

    // 建立新连接并发送请求
    TestHttpClient client2;
    ASSERT_TRUE(client2.Connect("127.0.0.1", testPort_));
    std::string request = createHttpRequest("GET", "/");
    std::string response = client2.SendRequest(request, MS_PER_SECOND);
    EXPECT_TRUE(ValidateHttpResponse(response, HTTP_STATUS_OK));

    client2.DisConnect();
}

TEST_F(HttpServerTest, HttpVersion11Compliance)
{
    StartServer();

    TestHttpClient client;
    ASSERT_TRUE(client.Connect("127.0.0.1", testPort_));

    // 使用HTTP/1.1
    std::string request = "GET / HTTP/1.1\r\n";
    request += "Host: localhost:" + std::to_string(testPort_) + "\r\n";
    request += "\r\n";

    std::string response = client.SendRequest(request, MS_PER_SECOND);

    EXPECT_TRUE(ValidateHttpResponse(response, HTTP_STATUS_OK));
    EXPECT_NE(response.find("HTTP/1.1"), std::string::npos);

    client.DisConnect();
}

TEST_F(HttpServerTest, HttpMethodSupportTest)
{
    StartServer();

    // 测试不同的HTTP方法
    std::vector<std::string> methods = {"GET", "POST", "PUT", "DELETE", "HEAD", "OPTIONS", "PATCH"};

    for (const auto& method : methods) {
        TestHttpClient client;
        if (client.Connect("127.0.0.1", testPort_)) {
            std::string request = method + " / HTTP/1.1\r\n";
            request += "Host: localhost:" + std::to_string(testPort_) + "\r\n";
            request += "\r\n";

            std::string response = client.SendRequest(request, MS_PER_SECOND);

            // 服务器应该响应（即使是不支持的方法）
            EXPECT_FALSE(response.empty());

            int statusCode = GetResponseStatusCode(response);
            // 应该是200（支持）或405（不支持）
            EXPECT_TRUE(statusCode == HTTP_STATUS_OK || statusCode == HTTP_STATUS_METHOD_NOT_ALLOWED);

            client.DisConnect();
        }
    }
}

TEST_F(HttpServerTest, RapidSequentialRequests)
{
    StartServer();

    // 快速连续发送多个请求
    for (int i = 0; i < HTTP_SIZE; i++) {
        TestHttpClient client;
        if (client.Connect("127.0.0.1", testPort_, WAIT_TIME_MS)) {
            std::string request = createHttpRequest("GET", "/", "Request " + std::to_string(i));
            std::string response = client.SendRequest(request, WAIT_TIME_MS);
            // 大多数请求应该成功
            if (i > 0) { // 跳过第一个可能因为连接建立慢的
                EXPECT_TRUE(ValidateHttpResponse(response, HTTP_STATUS_OK));
            }
            client.DisConnect();
        }
        // 短暂间隔
        std::this_thread::sleep_for(std::chrono::milliseconds(HTTP_SIZE));
    }
}

TEST_F(HttpServerTest, SendResponseAsyncBasic)
{
    StartServer();
    // 创建一个自定义路由处理异步响应
    Http::RouteMap async_routes;
    async_routes["/async-test"] = [](const Http::HttpRequest& request, Mcp::RequestContext& ctx) {
        // 立即返回，稍后异步发送响应
        std::thread([ctx]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));
            Http::HttpResponse response;
            response.statusCode = HTTP_STATUS_ACCEPTED;
            response.statusText = "Accepted";
            response.body = "Request is being processed asynchronously";
            ctx.httpSendFunc(response, ctx);
        }).detach();
    };

    StopServer();
    server_ = std::make_unique<HttpServer>("127.0.0.1", testPort_, tlsConfig_, async_routes);
    StartServer();

    TestHttpClient client;
    ASSERT_TRUE(client.Connect("127.0.0.1", testPort_));

    std::string request = createHttpRequest("GET", "/async-test");
    std::string response = client.SendRequest(request, MS_PER_SECOND); // 更长超时

    EXPECT_TRUE(ValidateHttpResponse(response, HTTP_STATUS_ACCEPTED));
    EXPECT_NE(GetResponseBody(response).find("asynchronously"), std::string::npos);

    client.DisConnect();
}
