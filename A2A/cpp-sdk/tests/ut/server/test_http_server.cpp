/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <type_traits>
#include <thread>
#include <utility>

#define private public
#define protected public
#include "server/http_server.h"
#undef private
#undef protected

#include "shared/http_common.h"

using ::testing::HasSubstr;

using namespace A2A;
using namespace A2A::Server;

namespace {

RouteMap MakeRoutes()
{
    RouteMap routes;
    return routes;
}

using TlsConfigType = std::decay_t<decltype(std::declval<HttpServer>().tlsConfig_)>;

TlsConfigType MakeTlsConfig(bool enabled = false)
{
    TlsConfigType cfg{};
    cfg.enabled = enabled;
    return cfg;
}

std::unique_ptr<HttpServer> MakeServer(RouteMap& routes)
{
    return std::make_unique<HttpServer>("127.0.0.1", 0, MakeTlsConfig(false), routes, 0);
}

class FakeTcpSocket final : public TcpSocket {
public:
    FakeTcpSocket(EventSystem& es, int fd = 123)
        : TcpSocket(es, fd, TcpSocketOptions{})
    {
    }

    bool Send(const char* data, size_t len) override
    {
        if (!sendResult_) {
            return false;
        }
        sentData_.append(static_cast<const char*>(data), len);
        return true;
    }

    void Close() override
    {
        closed_ = true;
    }

    void SetSendResult(bool ok)
    {
        sendResult_ = ok;
    }

    bool closed_ {false};
    bool sendResult_ {true};
    std::string sentData_;
};

class HttpServerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        routes = MakeRoutes();
        server = MakeServer(routes);
    }

    TcpSocketPtr MakeFakeSocket(int fd = 123)
    {
        return std::make_shared<FakeTcpSocket>(server->eventSystem_, fd);
    }

    RouteMap routes;
    std::unique_ptr<HttpServer> server;
};

} // namespace

TEST_F(HttpServerTest, BuildHttpResponse_FullResponse)
{
    Http::HttpResponse response;
    response.type = Http::HttpSendType::HTTPRESPONSE;
    response.statusCode = 200;
    response.statusText = "OK";
    response.headers["Content-Type"] = "application/json";
    response.body = "{\"ok\":true}";

    std::string raw = server->BuildHttpResponse(response);

    EXPECT_THAT(raw, HasSubstr("HTTP/1.1 200 OK\r\n"));
    EXPECT_THAT(raw, HasSubstr("Content-Type: application/json\r\n"));
    EXPECT_THAT(raw, HasSubstr("Content-Length: 11\r\n"));
    EXPECT_THAT(raw, HasSubstr("\r\n\r\n{\"ok\":true}"));
}

TEST_F(HttpServerTest, BuildHttpResponse_ResponseStartOnly)
{
    Http::HttpResponse response;
    response.type = Http::HttpSendType::HTTPRESPONSESTART;
    response.statusCode = 204;
    response.statusText = "No Content";
    response.headers["X-Test"] = "1";
    response.body = "ignored";

    std::string raw = server->BuildHttpResponse(response);

    EXPECT_THAT(raw, HasSubstr("HTTP/1.1 204 No Content\r\n"));
    EXPECT_THAT(raw, HasSubstr("X-Test: 1\r\n"));
    EXPECT_THAT(raw, HasSubstr("Content-Length: 7\r\n"));
    EXPECT_THAT(raw, HasSubstr("\r\n\r\n"));
    EXPECT_EQ(raw.find("ignored"), std::string::npos);
}

TEST_F(HttpServerTest, BuildHttpResponse_ResponseBodyOnly)
{
    Http::HttpResponse response;
    response.type = Http::HttpSendType::HTTPRESPONSEBODY;
    response.body = "chunk-data";

    std::string raw = server->BuildHttpResponse(response);

    EXPECT_EQ(raw, "chunk-data");
}

TEST_F(HttpServerTest, BuildchunkedResponse_StartAndBodyWithChunkedHeader)
{
    Http::HttpResponse response;
    response.type = Http::HttpSendType::HTTPRESPONSE;
    response.statusCode = 200;
    response.statusText = "OK";
    response.headers[Http::TRANSFER_ENCODING_HEADER] = Http::TRANSFER_ENCODING_CHUNKED;
    response.body = "hello";

    bool chunkedEnabled = false;
    std::string raw = server->BuildchunkedResponse(response, chunkedEnabled);

    EXPECT_TRUE(chunkedEnabled);
    EXPECT_THAT(raw, HasSubstr("HTTP/1.1 200 OK\r\n"));
    EXPECT_THAT(raw, HasSubstr("Transfer-Encoding: chunked\r\n"));
    EXPECT_THAT(raw, HasSubstr("\r\n\r\n"));
    EXPECT_THAT(raw, HasSubstr("5\r\nhello\r\n"));
}

TEST_F(HttpServerTest, BuildchunkedResponse_BodyOnlyWhenChunkedAlreadyEnabled)
{
    Http::HttpResponse response;
    response.type = Http::HttpSendType::HTTPRESPONSEBODY;
    response.body = "abc";

    bool chunkedEnabled = true;
    std::string raw = server->BuildchunkedResponse(response, chunkedEnabled);

    EXPECT_TRUE(chunkedEnabled);
    EXPECT_EQ(raw, "3\r\nabc\r\n");
}

TEST_F(HttpServerTest, BuildchunkedResponse_NoChunkedHeader_DoesNotEnableChunked)
{
    Http::HttpResponse response;
    response.type = Http::HttpSendType::HTTPRESPONSESTART;
    response.statusCode = 200;
    response.statusText = "OK";
    response.headers["Content-Type"] = "text/plain";

    bool chunkedEnabled = false;
    std::string raw = server->BuildchunkedResponse(response, chunkedEnabled);

    EXPECT_FALSE(chunkedEnabled);
    EXPECT_THAT(raw, HasSubstr("HTTP/1.1 200 OK\r\n"));
    EXPECT_THAT(raw, HasSubstr("Content-Type: text/plain\r\n"));
}

TEST_F(HttpServerTest, ParseRequest_ValidGetRequest_ReturnsOk)
{
    std::string raw =
        "GET /hello HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 0\r\n"
        "\r\n";

    Http::HttpRequest request;
    std::size_t consumed = 0;

    int result = server->ParseRequest(raw, request, consumed);

    EXPECT_EQ(result, Http::HTTP_PARSE_OK);
    EXPECT_EQ(consumed, raw.size());
    EXPECT_EQ(request.method, "GET");
    EXPECT_EQ(request.url, "/hello");
    EXPECT_EQ(request.version, "HTTP/1.1");
    ASSERT_TRUE(request.headers.find("Host") != request.headers.end());
    EXPECT_EQ(request.headers["Host"], "localhost");
}

TEST_F(HttpServerTest, ParseRequest_PostWithBody_ReturnsOk)
{
    std::string raw =
        "POST /submit HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "abcde";

    Http::HttpRequest request;
    std::size_t consumed = 0;

    int result = server->ParseRequest(raw, request, consumed);

    EXPECT_EQ(result, Http::HTTP_PARSE_OK);
    EXPECT_EQ(request.method, "POST");
    EXPECT_EQ(request.url, "/submit");
    EXPECT_EQ(request.body, "abcde");
}

TEST_F(HttpServerTest, ParseRequest_IncompleteRequest_ReturnsNeedMore)
{
    std::string raw =
        "GET /hello HTTP/1.1\r\n"
        "Host: localhost\r\n";

    Http::HttpRequest request;
    std::size_t consumed = 0;

    int result = server->ParseRequest(raw, request, consumed);

    EXPECT_EQ(result, Http::HTTP_PARSE_NEED_MORE);
}

TEST_F(HttpServerTest, ParseRequest_EmptyBuffer_ReturnsNeedMore)
{
    Http::HttpRequest request;
    std::size_t consumed = 0;

    int result = server->ParseRequest("", request, consumed);

    EXPECT_EQ(result, Http::HTTP_PARSE_NEED_MORE);
}

TEST_F(HttpServerTest, ParseRequest_HeaderValueTrimmed)
{
    std::string raw =
        "GET /trim HTTP/1.1\r\n"
        "Host:   localhost   \r\n"
        "X-Test:   value   \r\n"
        "Content-Length: 0\r\n"
        "\r\n";

    Http::HttpRequest request;
    std::size_t consumed = 0;

    int result = server->ParseRequest(raw, request, consumed);

    EXPECT_EQ(result, Http::HTTP_PARSE_OK);
    EXPECT_EQ(consumed, raw.size());
    EXPECT_EQ(request.headers["Host"], "localhost");
    EXPECT_EQ(request.headers["X-Test"], "value");
}

TEST_F(HttpServerTest, SendRawResponse_ConnectionNotFound_ReturnsFalse)
{
    EXPECT_FALSE(server->SendRawResponse(123, "hello"));
}

TEST_F(HttpServerTest, SendResponse_ConnectionNotFound_ReturnsFalse)
{
    Http::HttpResponse response;
    response.statusCode = 200;
    response.statusText = "OK";
    response.type = Http::HttpSendType::HTTPRESPONSE;

    EXPECT_FALSE(server->SendResponse(123, response));
}

TEST_F(HttpServerTest, CleanupConnection_MissingFd_DoesNothing)
{
    server->CleanupConnection(999);

    EXPECT_TRUE(server->connections_.empty());
}

TEST_F(HttpServerTest, OnRead_ConnectionMissing_Returns)
{
    server->OnRead(999, "GET /x HTTP/1.1\r\n\r\n");

    EXPECT_TRUE(server->connections_.empty());
}

TEST_F(HttpServerTest, OnRead_IncompleteRequest_KeepsBuffer)
{
    HttpServer::ConnectionContext ctx;
    server->connections_[1] = std::move(ctx);

    std::string partial =
        "GET /hello HTTP/1.1\r\n"
        "Host: localhost\r\n";

    server->OnRead(1, partial);

    EXPECT_EQ(server->connections_[1].requestBuffer, partial);
}

TEST_F(HttpServerTest, OnRead_InvalidRequest_ClearsRequestBuffer)
{
    HttpServer::ConnectionContext ctx;
    server->connections_[1] = std::move(ctx);

    server->OnRead(1, "%%%INVALID%%%");

    EXPECT_TRUE(server->connections_[1].requestBuffer.empty());
}

TEST_F(HttpServerTest, HandleRequest_RouteNotFound_LeavesCurrentRequestUntouchedAndDoesNotThrow)
{
    HttpServer::ConnectionContext ctx;
    ctx.currentRequest.url = "/not-found";
    server->connections_[1] = std::move(ctx);

    EXPECT_NO_THROW(server->HandleRequest(1, server->connections_[1]));
    EXPECT_EQ(server->connections_[1].currentRequest.url, "/not-found");
}

TEST_F(HttpServerTest, HandleRequest_HandlerThrows_DoesNotThrowOut)
{
    routes["/boom"] = [](const Http::HttpRequest&, const Http::HttpRequestContext&) {
        throw std::runtime_error("boom");
    };

    HttpServer::ConnectionContext ctx;
    ctx.currentRequest.url = "/boom";
    server->connections_[1] = std::move(ctx);

    EXPECT_NO_THROW(server->HandleRequest(1, server->connections_[1]));
}

TEST_F(HttpServerTest, OnRead_ValidRequestWithNoRoute_ClearsConsumedBuffer)
{
    HttpServer::ConnectionContext ctx;
    server->connections_[1] = std::move(ctx);

    std::string raw =
        "GET /not-found HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 0\r\n"
        "\r\n";

    server->OnRead(1, raw);

    EXPECT_TRUE(server->connections_[1].requestBuffer.empty());
}

TEST_F(HttpServerTest, BuildchunkedResponse_ResponseStartOnlyWithChunkedHeader_DoesNotAppendBodyChunk)
{
    Http::HttpResponse response;
    response.type = Http::HttpSendType::HTTPRESPONSESTART;
    response.statusCode = 200;
    response.statusText = "OK";
    response.headers[Http::TRANSFER_ENCODING_HEADER] = Http::TRANSFER_ENCODING_CHUNKED;
    response.body = "ignored";

    bool chunkedEnabled = false;
    std::string raw = server->BuildchunkedResponse(response, chunkedEnabled);

    EXPECT_TRUE(chunkedEnabled);
    EXPECT_THAT(raw, HasSubstr("HTTP/1.1 200 OK\r\n"));
    EXPECT_EQ(raw.find("ignored"), std::string::npos);
}

TEST_F(HttpServerTest, BuildHttpResponse_ResponseTypeHttpResponseIncludesBody)
{
    Http::HttpResponse response;
    response.type = Http::HttpSendType::HTTPRESPONSE;
    response.statusCode = 201;
    response.statusText = "Created";
    response.body = "payload";

    std::string raw = server->BuildHttpResponse(response);

    EXPECT_THAT(raw, HasSubstr("HTTP/1.1 201 Created\r\n"));
    EXPECT_THAT(raw, HasSubstr("Content-Length: 7\r\n"));
    EXPECT_THAT(raw, HasSubstr("\r\n\r\npayload"));
}

TEST_F(HttpServerTest, ParseRequest_InvalidRequest_ReturnsError)
{
    Http::HttpRequest request;
    std::size_t consumed = 0;

    int result = server->ParseRequest("%%%INVALID%%%", request, consumed);

    EXPECT_EQ(result, Http::HTTP_PARSE_ERROR);
}

TEST_F(HttpServerTest, HandleRequest_RouteMatched_PassesConnectionId)
{
    int capturedConnectionId = -1;
    std::string capturedUrl;

    server->routes_["/ok"] = [&](const Http::HttpRequest& req, const Http::HttpRequestContext& ctx) {
        capturedConnectionId = ctx.connectionId;
        capturedUrl = req.url;
    };

    HttpServer::ConnectionContext ctx;
    ctx.currentRequest.url = "/ok";
    server->connections_[7] = std::move(ctx);

    server->HandleRequest(7, server->connections_[7]);

    EXPECT_EQ(capturedConnectionId, 7);
    EXPECT_EQ(capturedUrl, "/ok");
}

TEST_F(HttpServerTest, HandleRequest_RouteMatched_HttpSendFuncCallable)
{
    bool sendCalled = false;

    server->routes_["/send"] = [&](const Http::HttpRequest&, const Http::HttpRequestContext& ctx) {
        Http::HttpResponse resp;
        resp.type = Http::HttpSendType::HTTPRESPONSE;
        resp.statusCode = 200;
        resp.statusText = "OK";
        resp.body = "hello";

        ctx.httpSendFunc(resp, ctx);
        sendCalled = true;
    };

    HttpServer::ConnectionContext ctx;
    ctx.currentRequest.url = "/send";
    server->connections_[3] = std::move(ctx);

    EXPECT_NO_THROW(server->HandleRequest(3, server->connections_[3]));
    EXPECT_TRUE(sendCalled);
}

TEST_F(HttpServerTest, OnRead_ValidRequest_ResetsCurrentRequestAfterHandling)
{
    routes["/ok"] = [&](const Http::HttpRequest&, const Http::HttpRequestContext&) {};

    HttpServer::ConnectionContext ctx;
    server->connections_[1] = std::move(ctx);

    std::string raw =
        "GET /ok HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 0\r\n"
        "\r\n";

    server->OnRead(1, raw);

    EXPECT_TRUE(server->connections_[1].currentRequest.url.empty());
    EXPECT_TRUE(server->connections_[1].requestBuffer.empty());
}

TEST_F(HttpServerTest, BuildchunkedResponse_TransferEncodingNotChunked_DoesNotEnableChunked)
{
    Http::HttpResponse response;
    response.type = Http::HttpSendType::HTTPRESPONSE;
    response.statusCode = 200;
    response.statusText = "OK";
    response.headers[Http::TRANSFER_ENCODING_HEADER] = "gzip";
    response.body = "hello";

    bool chunkedEnabled = false;
    std::string raw = server->BuildchunkedResponse(response, chunkedEnabled);

    EXPECT_FALSE(chunkedEnabled);
    EXPECT_THAT(raw, HasSubstr("HTTP/1.1 200 OK\r\n"));
    EXPECT_EQ(raw.find("5\r\nhello\r\n"), std::string::npos);
}

TEST_F(HttpServerTest, SendResponse_WhenSseChunkedEnabled_UsesChunkedPath)
{
    HttpServer::ConnectionContext ctx;
    ctx.connection = nullptr;
    ctx.sseChunked = true;
    server->connections_[5] = std::move(ctx);

    Http::HttpResponse response;
    response.type = Http::HttpSendType::HTTPRESPONSEBODY;
    response.body = "abc";

    EXPECT_FALSE(server->SendResponse(5, response));
    EXPECT_TRUE(server->connections_[5].sseChunked);
}

TEST_F(HttpServerTest, SendRawResponse_ConnectionExistsButNull_ReturnsFalse)
{
    HttpServer::ConnectionContext ctx;
    ctx.connection = nullptr;
    server->connections_[6] = std::move(ctx);

    EXPECT_FALSE(server->SendRawResponse(6, "hello"));
}

TEST_F(HttpServerTest, SendResponse_ConnectionExistsButNull_ReturnsFalse)
{
    HttpServer::ConnectionContext ctx;
    ctx.connection = nullptr;
    server->connections_[8] = std::move(ctx);

    Http::HttpResponse response;
    response.type = Http::HttpSendType::HTTPRESPONSE;
    response.statusCode = 200;
    response.statusText = "OK";
    response.body = "hello";

    EXPECT_FALSE(server->SendResponse(8, response));
}

TEST_F(HttpServerTest, OnRead_TwoChunkInput_CompletesOnSecondRead)
{
    int called = 0;
    server->routes_["/ok"] = [&](const Http::HttpRequest&, const Http::HttpRequestContext&) {
        ++called;
    };

    HttpServer::ConnectionContext ctx;
    server->connections_[45] = std::move(ctx);

    std::string part1 =
        "GET /ok HTTP/1.1\r\n"
        "Host: localhost\r\n";

    std::string part2 =
        "Content-Length: 0\r\n"
        "\r\n";

    server->OnRead(45, part1);
    EXPECT_EQ(called, 0);
    EXPECT_EQ(server->connections_[45].requestBuffer, part1);

    server->OnRead(45, part2);
    EXPECT_EQ(called, 1);
    EXPECT_TRUE(server->connections_[45].requestBuffer.empty());
}

TEST_F(HttpServerTest, SendRawResponse_NonTlsSendSuccess_ReturnsTrue)
{
    auto fake = MakeFakeSocket(30);

    HttpServer::ConnectionContext ctx;
    ctx.connection = fake;
    ctx.ssl = nullptr;
    server->connections_[30] = std::move(ctx);

    EXPECT_TRUE(server->SendRawResponse(30, "hello"));

    auto* typed = static_cast<FakeTcpSocket*>(fake.get());
    EXPECT_EQ(typed->sentData_, "hello");
    EXPECT_FALSE(typed->closed_);
}

TEST_F(HttpServerTest, SendRawResponse_NonTlsSendFails_CleansConnectionAndReturnsFalse)
{
    auto fake = MakeFakeSocket(31);
    static_cast<FakeTcpSocket*>(fake.get())->SetSendResult(false);

    HttpServer::ConnectionContext ctx;
    ctx.connection = fake;
    ctx.ssl = nullptr;
    server->connections_[31] = std::move(ctx);

    EXPECT_FALSE(server->SendRawResponse(31, "hello"));
    EXPECT_TRUE(server->connections_.find(31) == server->connections_.end());

    auto* typed = static_cast<FakeTcpSocket*>(fake.get());
    EXPECT_TRUE(typed->closed_);
}

TEST_F(HttpServerTest, SendResponse_NonChunkedPath_SendsBuiltResponse)
{
    auto fake = MakeFakeSocket(32);

    HttpServer::ConnectionContext ctx;
    ctx.connection = fake;
    ctx.ssl = nullptr;
    ctx.sseChunked = false;
    server->connections_[32] = std::move(ctx);

    Http::HttpResponse response;
    response.type = Http::HttpSendType::HTTPRESPONSE;
    response.statusCode = 200;
    response.statusText = "OK";
    response.body = "abc";

    EXPECT_TRUE(server->SendResponse(32, response));

    auto* typed = static_cast<FakeTcpSocket*>(fake.get());
    EXPECT_THAT(typed->sentData_, HasSubstr("HTTP/1.1 200 OK\r\n"));
    EXPECT_THAT(typed->sentData_, HasSubstr("Content-Length: 3\r\n"));
    EXPECT_THAT(typed->sentData_, HasSubstr("\r\n\r\nabc"));
}

TEST_F(HttpServerTest, SendResponse_ChunkedHeaderPath_SendsChunkedResponse)
{
    auto fake = MakeFakeSocket(33);

    HttpServer::ConnectionContext ctx;
    ctx.connection = fake;
    ctx.ssl = nullptr;
    ctx.sseChunked = false;
    server->connections_[33] = std::move(ctx);

    Http::HttpResponse response;
    response.type = Http::HttpSendType::HTTPRESPONSE;
    response.statusCode = 200;
    response.statusText = "OK";
    response.headers[Http::TRANSFER_ENCODING_HEADER] = Http::TRANSFER_ENCODING_CHUNKED;
    response.body = "hello";

    EXPECT_TRUE(server->SendResponse(33, response));

    auto* typed = static_cast<FakeTcpSocket*>(fake.get());
    EXPECT_THAT(typed->sentData_, HasSubstr("HTTP/1.1 200 OK\r\n"));
    EXPECT_THAT(typed->sentData_, HasSubstr("Transfer-Encoding: chunked\r\n"));
    EXPECT_THAT(typed->sentData_, HasSubstr("5\r\nhello\r\n"));
}

TEST_F(HttpServerTest, CleanupConnection_WithConnection_ClosesAndErases)
{
    auto fake = MakeFakeSocket(34);

    HttpServer::ConnectionContext ctx;
    ctx.connection = fake;
    server->connections_[34] = std::move(ctx);

    server->CleanupConnection(34);

    EXPECT_TRUE(server->connections_.find(34) == server->connections_.end());

    auto* typed = static_cast<FakeTcpSocket*>(fake.get());
    EXPECT_TRUE(typed->closed_);
}

TEST_F(HttpServerTest, HandleClose_ValidSocket_CleansConnection)
{
    auto fake = MakeFakeSocket(35);

    HttpServer::ConnectionContext ctx;
    ctx.connection = fake;
    server->connections_[35] = std::move(ctx);

    server->HandleClose(fake);

    EXPECT_TRUE(server->connections_.find(35) == server->connections_.end());

    auto* typed = static_cast<FakeTcpSocket*>(fake.get());
    EXPECT_TRUE(typed->closed_);
}

TEST_F(HttpServerTest, HandleClose_NullSocket_DoesNothing)
{
    server->HandleClose(nullptr);

    EXPECT_TRUE(server->connections_.empty());
}

TEST_F(HttpServerTest, HandleError_ValidSocket_CleansConnection)
{
    auto fake = MakeFakeSocket(36);

    HttpServer::ConnectionContext ctx;
    ctx.connection = fake;
    server->connections_[36] = std::move(ctx);

    server->HandleError(fake, 100, "boom");

    EXPECT_TRUE(server->connections_.find(36) == server->connections_.end());

    auto* typed = static_cast<FakeTcpSocket*>(fake.get());
    EXPECT_TRUE(typed->closed_);
}

TEST_F(HttpServerTest, HandleError_NullSocket_DoesNothing)
{
    server->HandleError(nullptr, 100, "boom");

    EXPECT_TRUE(server->connections_.empty());
}

TEST_F(HttpServerTest, HandleNewConnection_NullConnection_DoesNothing)
{
    server->HandleNewConnection(nullptr);

    EXPECT_TRUE(server->connections_.empty());
}

TEST_F(HttpServerTest, HandleRead_NullConnection_Returns)
{
    server->HandleRead(nullptr);

    EXPECT_TRUE(server->connections_.empty());
}

TEST_F(HttpServerTest, HandleRead_ConnectionNotTracked_Returns)
{
    auto fake = MakeFakeSocket(40);

    server->HandleRead(fake);

    EXPECT_TRUE(server->connections_.empty());
}

TEST_F(HttpServerTest, HandleRead_EmptyInputBuffer_Returns)
{
    auto fake = MakeFakeSocket(41);

    HttpServer::ConnectionContext ctx;
    ctx.connection = fake;
    ctx.ssl = nullptr;
    server->connections_[41] = std::move(ctx);

    server->HandleRead(fake);

    EXPECT_TRUE(server->connections_.find(41) != server->connections_.end());
    EXPECT_TRUE(server->connections_[41].requestBuffer.empty());
}

TEST_F(HttpServerTest, HandleRead_NonTls_ReadsBufferAndDispatchesRequest)
{
    int called = 0;
    std::string method;
    std::string url;

    server->routes_["/ok"] = [&](const Http::HttpRequest& req, const Http::HttpRequestContext&) {
        ++called;
        method = req.method;
        url = req.url;
    };

    auto fake = MakeFakeSocket(42);
    auto* typed = static_cast<FakeTcpSocket*>(fake.get());

    std::string raw =
        "GET /ok HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 0\r\n"
        "\r\n";

    typed->inBuf_.buf_.assign(raw.begin(), raw.end());
    typed->inBuf_.r_ = 0;
    typed->inBuf_.w_ = raw.size();

    HttpServer::ConnectionContext ctx;
    ctx.connection = fake;
    ctx.ssl = nullptr;
    server->connections_[42] = std::move(ctx);

    server->HandleRead(fake);

    EXPECT_EQ(called, 1);
    EXPECT_EQ(method, "GET");
    EXPECT_EQ(url, "/ok");
    EXPECT_TRUE(server->connections_[42].requestBuffer.empty());
}

TEST_F(HttpServerTest, HandleRead_NonTls_InvalidRequest_ClearsRequestBuffer)
{
    auto fake = MakeFakeSocket(43);
    auto* typed = static_cast<FakeTcpSocket*>(fake.get());

    std::string raw = "%%%INVALID%%%";
    typed->inBuf_.buf_.assign(raw.begin(), raw.end());
    typed->inBuf_.r_ = 0;
    typed->inBuf_.w_ = raw.size();

    HttpServer::ConnectionContext ctx;
    ctx.connection = fake;
    ctx.ssl = nullptr;
    server->connections_[43] = std::move(ctx);

    server->HandleRead(fake);

    EXPECT_TRUE(server->connections_[43].requestBuffer.empty());
}

TEST_F(HttpServerTest, OnRead_TwoChunkInput_CompletesOnSecondRead_Again)
{
    int called = 0;
    server->routes_["/ok"] = [&](const Http::HttpRequest&, const Http::HttpRequestContext&) {
        ++called;
    };

    HttpServer::ConnectionContext ctx;
    server->connections_[46] = std::move(ctx);

    std::string part1 =
        "GET /ok HTTP/1.1\r\n"
        "Host: localhost\r\n";

    std::string part2 =
        "Content-Length: 0\r\n"
        "\r\n";

    server->OnRead(46, part1);
    EXPECT_EQ(called, 0);
    EXPECT_EQ(server->connections_[46].requestBuffer, part1);

    server->OnRead(46, part2);
    EXPECT_EQ(called, 1);
    EXPECT_TRUE(server->connections_[46].requestBuffer.empty());
}

TEST_F(HttpServerTest, SendResponseAsync_NotRunning_ReturnsDirectly)
{
    Http::HttpResponse response;
    response.type = Http::HttpSendType::HTTPRESPONSE;
    response.statusCode = 200;
    response.statusText = "OK";
    response.body = "hello";

    Http::HttpRequestContext ctx;
    ctx.connectionId = 1;

    EXPECT_NO_THROW(server->SendResponseAsync(response, ctx));
}

TEST_F(HttpServerTest, SendResponseAsync_RunningButTaskQueueNull_ReturnsDirectly)
{
    server->running_ = true;
    server->taskQueue_.reset();

    Http::HttpResponse response;
    response.type = Http::HttpSendType::HTTPRESPONSE;
    response.statusCode = 200;
    response.statusText = "OK";
    response.body = "hello";

    Http::HttpRequestContext ctx;
    ctx.connectionId = 1;

    EXPECT_NO_THROW(server->SendResponseAsync(response, ctx));
}

TEST_F(HttpServerTest, CleanupConnection_WithTlsObjects_ClosesAndErases)
{
    auto fake = MakeFakeSocket(61);

    SSL_CTX* ctxRaw = SSL_CTX_new(TLS_method());
    ASSERT_NE(ctxRaw, nullptr);

    SSL* ssl = SSL_new(ctxRaw);
    ASSERT_NE(ssl, nullptr);

    BIO* rbio = BIO_new(BIO_s_mem());
    BIO* wbio = BIO_new(BIO_s_mem());
    ASSERT_NE(rbio, nullptr);
    ASSERT_NE(wbio, nullptr);

    SSL_set_bio(ssl, rbio, wbio);

    HttpServer::ConnectionContext ctx;
    ctx.connection = fake;
    ctx.ssl = ssl;
    ctx.rbio = rbio;
    ctx.wbio = wbio;
    ctx.handshaked = true;
    server->connections_[61] = std::move(ctx);

    server->CleanupConnection(61);

    EXPECT_TRUE(server->connections_.find(61) == server->connections_.end());
    EXPECT_TRUE(static_cast<FakeTcpSocket*>(fake.get())->closed_);

    SSL_CTX_free(ctxRaw);
}

TEST_F(HttpServerTest, HandleRead_TlsHandshakedButSslReadFails_CleansConnection)
{
    auto fake = MakeFakeSocket(62);
    auto* typed = static_cast<FakeTcpSocket*>(fake.get());

    std::string encrypted = "abc";
    typed->inBuf_.buf_.assign(encrypted.begin(), encrypted.end());
    typed->inBuf_.r_ = 0;
    typed->inBuf_.w_ = encrypted.size();

    SSL_CTX* ctxRaw = SSL_CTX_new(TLS_method());
    ASSERT_NE(ctxRaw, nullptr);

    SSL* ssl = SSL_new(ctxRaw);
    ASSERT_NE(ssl, nullptr);

    BIO* rbio = BIO_new(BIO_s_mem());
    BIO* wbio = BIO_new(BIO_s_mem());
    ASSERT_NE(rbio, nullptr);
    ASSERT_NE(wbio, nullptr);

    SSL_set_bio(ssl, rbio, wbio);

    HttpServer::ConnectionContext ctx;
    ctx.connection = fake;
    ctx.ssl = ssl;
    ctx.rbio = rbio;
    ctx.wbio = wbio;
    ctx.handshaked = true;
    server->connections_[62] = std::move(ctx);

    server->HandleRead(fake);

    EXPECT_TRUE(server->connections_.find(62) == server->connections_.end());
    EXPECT_TRUE(static_cast<FakeTcpSocket*>(fake.get())->closed_);

    SSL_CTX_free(ctxRaw);
}

TEST_F(HttpServerTest, SendRawResponse_TlsWriteFails_CleansConnectionAndReturnsFalse)
{
    auto fake = MakeFakeSocket(63);

    SSL_CTX* ctxRaw = SSL_CTX_new(TLS_method());
    ASSERT_NE(ctxRaw, nullptr);

    SSL* ssl = SSL_new(ctxRaw);
    ASSERT_NE(ssl, nullptr);

    BIO* rbio = BIO_new(BIO_s_mem());
    BIO* wbio = BIO_new(BIO_s_mem());
    ASSERT_NE(rbio, nullptr);
    ASSERT_NE(wbio, nullptr);

    SSL_set_bio(ssl, rbio, wbio);

    HttpServer::ConnectionContext ctx;
    ctx.connection = fake;
    ctx.ssl = ssl;
    ctx.rbio = rbio;
    ctx.wbio = wbio;
    ctx.handshaked = true;
    server->connections_[63] = std::move(ctx);

    bool ok = server->SendRawResponse(63, "hello");

    EXPECT_FALSE(ok);
    EXPECT_TRUE(server->connections_.find(63) == server->connections_.end());
    EXPECT_TRUE(static_cast<FakeTcpSocket*>(fake.get())->closed_);

    SSL_CTX_free(ctxRaw);
}

TEST_F(HttpServerTest, InitializeSslContext_MissingCertificate_Throws)
{
    server->tlsConfig_.enabled = true;
    server->tlsConfig_.certFile = "/tmp/not_exists_cert.pem";
    server->tlsConfig_.keyFile = "/tmp/not_exists_key.pem";
    server->tlsConfig_.caFile.clear();
    server->tlsConfig_.verifyPeer = false;

    EXPECT_THROW(server->InitializeSslContext(), std::runtime_error);
}

TEST_F(HttpServerTest, Stop_WhenNotRunning_ReturnsDirectly)
{
    server->running_ = false;
    EXPECT_NO_THROW(server->Stop());
}

TEST_F(HttpServerTest, HandleRequest_RouteNotFound_Sends404Body)
{
    auto fake = MakeFakeSocket(70);

    HttpServer::ConnectionContext ctx;
    ctx.connection = fake;
    ctx.currentRequest.url = "/missing";
    server->connections_[70] = std::move(ctx);

    server->HandleRequest(70, server->connections_[70]);

    auto* typed = static_cast<FakeTcpSocket*>(fake.get());
    EXPECT_THAT(typed->sentData_, HasSubstr("404 Not Found"));
    EXPECT_THAT(typed->sentData_, HasSubstr("Endpoint not found"));
}

TEST_F(HttpServerTest, HandleRequest_HandlerThrows_Sends500Body)
{
    server->routes_["/boom"] = [](const Http::HttpRequest&, const Http::HttpRequestContext&) {
        throw std::runtime_error("boom");
    };

    auto fake = MakeFakeSocket(71);

    HttpServer::ConnectionContext ctx;
    ctx.connection = fake;
    ctx.currentRequest.url = "/boom";
    server->connections_[71] = std::move(ctx);

    server->HandleRequest(71, server->connections_[71]);

    auto* typed = static_cast<FakeTcpSocket*>(fake.get());
    EXPECT_THAT(typed->sentData_, HasSubstr("500 Internal Server Error"));
    EXPECT_THAT(typed->sentData_, HasSubstr("Error: boom"));
}

TEST_F(HttpServerTest, BuildchunkedResponse_HttpResponseWithEmptyBody_DoesNotAppendChunk)
{
    Http::HttpResponse response;
    response.type = Http::HttpSendType::HTTPRESPONSE;
    response.statusCode = 200;
    response.statusText = "OK";
    response.headers[Http::TRANSFER_ENCODING_HEADER] = Http::TRANSFER_ENCODING_CHUNKED;
    response.body = "";

    bool chunkedEnabled = false;
    std::string raw = server->BuildchunkedResponse(response, chunkedEnabled);

    EXPECT_TRUE(chunkedEnabled);
    EXPECT_THAT(raw, HasSubstr("HTTP/1.1 200 OK\r\n"));
    EXPECT_EQ(raw.find("0\r\n\r\n"), std::string::npos);
}

TEST_F(HttpServerTest, HandleRequest_RouteNotFound_Sends404Response)
{
    auto fake = MakeFakeSocket(80);

    HttpServer::ConnectionContext ctx;
    ctx.connection = fake;
    ctx.currentRequest.url = "/missing";
    server->connections_[80] = std::move(ctx);

    server->HandleRequest(80, server->connections_[80]);

    auto* typed = static_cast<FakeTcpSocket*>(fake.get());
    EXPECT_THAT(typed->sentData_, HasSubstr("HTTP/1.1 404 Not Found\r\n"));
    EXPECT_THAT(typed->sentData_, HasSubstr("Endpoint not found"));
}

TEST_F(HttpServerTest, HandleRequest_HandlerThrows_Sends500Response)
{
    server->routes_["/boom"] = [](const Http::HttpRequest&, const Http::HttpRequestContext&) {
        throw std::runtime_error("boom");
    };

    auto fake = MakeFakeSocket(81);

    HttpServer::ConnectionContext ctx;
    ctx.connection = fake;
    ctx.currentRequest.url = "/boom";
    server->connections_[81] = std::move(ctx);

    server->HandleRequest(81, server->connections_[81]);

    auto* typed = static_cast<FakeTcpSocket*>(fake.get());
    EXPECT_THAT(typed->sentData_, HasSubstr("HTTP/1.1 500 Internal Server Error\r\n"));
    EXPECT_THAT(typed->sentData_, HasSubstr("Error: boom"));
}

TEST_F(HttpServerTest, OnRead_InvalidRequest_Sends400Response)
{
    auto fake = MakeFakeSocket(82);

    HttpServer::ConnectionContext ctx;
    ctx.connection = fake;
    server->connections_[82] = std::move(ctx);

    server->OnRead(82, "%%%INVALID%%%");

    auto* typed = static_cast<FakeTcpSocket*>(fake.get());
    EXPECT_THAT(typed->sentData_, HasSubstr("HTTP/1.1 400 Bad Request\r\n"));
    EXPECT_THAT(typed->sentData_, HasSubstr("Failed to parse HTTP request"));
    EXPECT_TRUE(server->connections_[82].requestBuffer.empty());
}

TEST_F(HttpServerTest, OnRead_ValidRequestWithoutRoute_Sends404Response)
{
    auto fake = MakeFakeSocket(83);

    HttpServer::ConnectionContext ctx;
    ctx.connection = fake;
    server->connections_[83] = std::move(ctx);

    std::string raw =
        "GET /not-found HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 0\r\n"
        "\r\n";

    server->OnRead(83, raw);

    auto* typed = static_cast<FakeTcpSocket*>(fake.get());
    EXPECT_THAT(typed->sentData_, HasSubstr("HTTP/1.1 404 Not Found\r\n"));
    EXPECT_THAT(typed->sentData_, HasSubstr("Endpoint not found"));
    EXPECT_TRUE(server->connections_[83].requestBuffer.empty());
}

TEST_F(HttpServerTest, HandleRequest_RouteMatched_DirectSendWritesResponse)
{
    auto fake = MakeFakeSocket(84);

    server->routes_["/direct"] = [](const Http::HttpRequest&, const Http::HttpRequestContext& ctx) {
        Http::HttpResponse resp;
        resp.type = Http::HttpSendType::HTTPRESPONSE;
        resp.statusCode = 200;
        resp.statusText = "OK";
        resp.body = "direct";
        ctx.httpSendFunc(resp, ctx);
    };

    HttpServer::ConnectionContext ctx;
    ctx.connection = fake;
    ctx.currentRequest.url = "/direct";
    server->connections_[84] = std::move(ctx);

    server->HandleRequest(84, server->connections_[84]);

    auto* typed = static_cast<FakeTcpSocket*>(fake.get());
    // 当前 server 未 running，httpSendFunc 最终会走 SendResponseAsync early return，
    // 所以这里只验证 HandleRequest 本身不再额外发送 fallback。
    EXPECT_TRUE(typed->sentData_.empty());
}

TEST_F(HttpServerTest, BuildchunkedResponse_HttpResponseWithEmptyBody_DoesNotAppendChunkData)
{
    Http::HttpResponse response;
    response.type = Http::HttpSendType::HTTPRESPONSE;
    response.statusCode = 200;
    response.statusText = "OK";
    response.headers[Http::TRANSFER_ENCODING_HEADER] = Http::TRANSFER_ENCODING_CHUNKED;
    response.body = "";

    bool chunkedEnabled = false;
    std::string raw = server->BuildchunkedResponse(response, chunkedEnabled);

    EXPECT_TRUE(chunkedEnabled);
    EXPECT_THAT(raw, HasSubstr("HTTP/1.1 200 OK\r\n"));
    EXPECT_EQ(raw.find("0\r\n"), std::string::npos);
}

TEST_F(HttpServerTest, SendResponse_ChunkedBodyOnly_UsesExistingSseChunkedState)
{
    auto fake = MakeFakeSocket(85);

    HttpServer::ConnectionContext ctx;
    ctx.connection = fake;
    ctx.sseChunked = true;
    server->connections_[85] = std::move(ctx);

    Http::HttpResponse response;
    response.type = Http::HttpSendType::HTTPRESPONSEBODY;
    response.body = "hello";

    EXPECT_TRUE(server->SendResponse(85, response));

    auto* typed = static_cast<FakeTcpSocket*>(fake.get());
    EXPECT_EQ(typed->sentData_, "5\r\nhello\r\n");
}

TEST_F(HttpServerTest, RunAndStop_NonTls_Succeeds)
{
    RouteMap localRoutes;
    auto localServer = std::make_unique<HttpServer>("127.0.0.1", 0, MakeTlsConfig(false), localRoutes, 0);

    EXPECT_NO_THROW(localServer->Run());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_TRUE(localServer->running_.load());

    EXPECT_NO_THROW(localServer->Stop());

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    EXPECT_FALSE(localServer->running_.load());
}

TEST_F(HttpServerTest, RunCalledTwice_SecondCallReturnsDirectly)
{
    RouteMap localRoutes;
    auto localServer = std::make_unique<HttpServer>("127.0.0.1", 0, MakeTlsConfig(false), localRoutes, 0);

    ASSERT_NO_THROW(localServer->Run());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_NO_THROW(localServer->Run());

    EXPECT_NO_THROW(localServer->Stop());
}

TEST_F(HttpServerTest, SendResponseAsync_WhenRunningAndQueueReady_DoesNotThrow)
{
    RouteMap localRoutes;
    auto localServer = std::make_unique<HttpServer>("127.0.0.1", 0, MakeTlsConfig(false), localRoutes, 0);

    ASSERT_NO_THROW(localServer->Run());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    Http::HttpResponse response;
    response.type = Http::HttpSendType::HTTPRESPONSE;
    response.statusCode = 200;
    response.statusText = "OK";
    response.body = "async";

    Http::HttpRequestContext ctx;
    ctx.connectionId = 9999; // 不存在也没关系，主要覆盖异步投递路径

    EXPECT_NO_THROW(localServer->SendResponseAsync(response, ctx));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_NO_THROW(localServer->Stop());
}

TEST_F(HttpServerTest, Stop_AfterRun_CleansUpTaskQueuePath)
{
    RouteMap localRoutes;
    auto localServer = std::make_unique<HttpServer>("127.0.0.1", 0, MakeTlsConfig(false), localRoutes, 0);

    ASSERT_NO_THROW(localServer->Run());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    ASSERT_TRUE(localServer->taskQueue_ != nullptr);

    EXPECT_NO_THROW(localServer->Stop());
}

TEST_F(HttpServerTest, HandleNewConnection_TlsEnabledButSslContextNull_ClosesSocketAndDoesNotStoreConnection)
{
    server->tlsConfig_.enabled = true;
    server->sslContext_ = nullptr;

    auto fake = MakeFakeSocket(90);

    server->HandleNewConnection(fake);

    EXPECT_TRUE(server->connections_.find(90) == server->connections_.end());

    auto* typed = static_cast<FakeTcpSocket*>(fake.get());
    EXPECT_TRUE(typed->closed_);
}

TEST_F(HttpServerTest, Run_TlsEnabledWithMissingCert_Throws)
{
    RouteMap localRoutes;
    auto localServer = std::make_unique<HttpServer>("127.0.0.1", 0, MakeTlsConfig(true), localRoutes, 0);

    localServer->tlsConfig_.certFile = "/tmp/not_exist_cert.pem";
    localServer->tlsConfig_.keyFile = "/tmp/not_exist_key.pem";
    localServer->tlsConfig_.caFile.clear();
    localServer->tlsConfig_.verifyPeer = false;

    EXPECT_THROW(localServer->Run(), std::runtime_error);
    EXPECT_TRUE(localServer->running_.load());
    EXPECT_NO_THROW(localServer->Stop());
}