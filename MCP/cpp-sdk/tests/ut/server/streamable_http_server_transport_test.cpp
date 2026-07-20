/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <functional>
#include <regex>

#include "mcp_log.h"
#include "shared/http_common.h"
#include "shared/common_type.h"
#include "server/transport/streamable_http_server_transport.h"

namespace Mcp {

class MockTransportCallback : public TransportCallback {
public:
    ~MockTransportCallback() {}
    void OnMessageReceived(const JSONRPCMessage& message, RequestContext& ctx) override
    {
        messageCount++;
        lastCtx = &ctx;
    }

    void OnDisconnected(const std::string& reason) override
    {
        disconnectCount++;
        lastDisconnectReason = reason;
    }

    int messageCount = 0;
    int disconnectCount = 0;
    std::string lastDisconnectReason;
    RequestContext* lastCtx = nullptr;
};

class MockHttpSendFunc {
public:
    void operator()(const HttpResponse& response, const RequestContext& ctx)
    {
        responses.push_back(response);
        contexts.push_back(ctx);
    }

    std::vector<HttpResponse> responses;
    std::vector<RequestContext> contexts;

    void Clear()
    {
        responses.clear();
        contexts.clear();
    }

    HttpResponse lastResponse() const
    {
        return responses.empty() ? HttpResponse() : responses.back();
    }
    ~MockHttpSendFunc() {}
};

class StreamableHttpServerTransportTest : public ::testing::Test {
public:
    ~StreamableHttpServerTransportTest() {}
protected:
    void SetUp() override
    {
        mockHttpSendFunc.Clear();
        validSessionId = "test-session-12345-ABCDE";
        invalidSessionId = "test-session-©";
    }

    void TearDown() override
    {
        mockHttpSendFunc.Clear();
    }

    RequestContext CreateRequestContext(const std::string& method = "")
    {
        RequestContext ctx;
        ctx.method = method;
        ctx.httpSendFunc = [this](const HttpResponse& response, const RequestContext& context) {
            mockHttpSendFunc(response, context);
        };
        return ctx;
    }

    Http::HttpRequest CreateHttpRequest(
        const std::string& method = "POST",
        const std::unordered_map<std::string, std::string>& headers = {},
        const std::string& body = ""
    )
    {
        Http::HttpRequest request;
        request.method = method;
        request.headers = headers;
        request.body = body;
        return request;
    }

    std::string CreateInitializeRequestJson()
    {
        return R"({
            "jsonrpc": "2.0",
            "id": 1,
            "method": "initialize",
            "params": {
                "protocolVersion": ")" + std::string(DEFAULT_PROTOCOL_VERSION) + R"(",
                "capabilities": {},
                "clientInfo": {
                    "name": "TestClient",
                    "version": "1.0.0"
                }
            }
        })";
    }

    std::string CreateNonInitializeRequestJson()
    {
        return R"({
            "jsonrpc": "2.0",
            "id": 2,
            "method": "tools/list",
            "params": {}
        })";
    }

    std::string validSessionId;
    std::string invalidSessionId;
    MockHttpSendFunc mockHttpSendFunc;
};

TEST_F(StreamableHttpServerTransportTest, ConstructorValidSessionId)
{
    EXPECT_NO_THROW({
        StreamableHttpServerTransport transport(validSessionId, false);
    });
}

TEST_F(StreamableHttpServerTransportTest, ConstructorInvalidSessionId)
{
    EXPECT_THROW({
        StreamableHttpServerTransport transport(invalidSessionId, false);
    }, std::invalid_argument);
}

TEST_F(StreamableHttpServerTransportTest, ConstructorEmptySessionId)
{
    EXPECT_NO_THROW({
        StreamableHttpServerTransport transport("", false);
    });
}

TEST_F(StreamableHttpServerTransportTest, ConstructorWithJsonResponseEnabled)
{
    EXPECT_NO_THROW({
        StreamableHttpServerTransport transport(validSessionId, true);
    });
}

TEST_F(StreamableHttpServerTransportTest, ConstructorWithJsonResponseDisabled)
{
    EXPECT_NO_THROW({
        StreamableHttpServerTransport transport(validSessionId, false);
    });
}

TEST_F(StreamableHttpServerTransportTest, SetCallback)
{
    StreamableHttpServerTransport transport(validSessionId, false);
    auto callback = std::make_shared<MockTransportCallback>();

    EXPECT_NO_THROW(transport.SetCallback(callback));
}

TEST_F(StreamableHttpServerTransportTest, SetNullCallback)
{
    StreamableHttpServerTransport transport(validSessionId, false);

    EXPECT_NO_THROW(transport.SetCallback(nullptr));
}

TEST_F(StreamableHttpServerTransportTest, Listen)
{
    StreamableHttpServerTransport transport(validSessionId, false);

    EXPECT_NO_THROW(transport.Listen());
}

TEST_F(StreamableHttpServerTransportTest, Terminate)
{
    StreamableHttpServerTransport transport(validSessionId, false);

    EXPECT_NO_THROW(transport.Terminate());
}

TEST_F(StreamableHttpServerTransportTest, TerminateEmptySessionId)
{
    StreamableHttpServerTransport transport("", false);

    EXPECT_NO_THROW(transport.Terminate());
}

TEST_F(StreamableHttpServerTransportTest, HandleRequestNoHttpCallback)
{
    StreamableHttpServerTransport transport(validSessionId, false);
    RequestContext ctx = CreateRequestContext();
    ctx.httpSendFunc = nullptr;

    Http::HttpRequest request = CreateHttpRequest("POST");

    EXPECT_THROW({
        transport.HandleRequest(request, ctx);
    }, std::runtime_error);
}

TEST_F(StreamableHttpServerTransportTest, HandleRequestTerminatedSession)
{
    StreamableHttpServerTransport transport(validSessionId, false);
    transport.Terminate();

    RequestContext ctx = CreateRequestContext();
    Http::HttpRequest request = CreateHttpRequest("POST");

    transport.HandleRequest(request, ctx);

    EXPECT_FALSE(mockHttpSendFunc.responses.empty());
    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_NOT_FOUND);
}

TEST_F(StreamableHttpServerTransportTest, HandlePostRequestNoCallback)
{
    StreamableHttpServerTransport transport(validSessionId, false);

    RequestContext ctx = CreateRequestContext();
    Http::HttpRequest request = CreateHttpRequest("POST");
    request.body = CreateInitializeRequestJson();

    EXPECT_THROW({
        transport.HandleRequest(request, ctx);
    }, std::runtime_error);
}

TEST_F(StreamableHttpServerTransportTest, HandlePostRequestInvalidAcceptHeader)
{
    StreamableHttpServerTransport transport(validSessionId, false);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    std::unordered_map<std::string, std::string> headers = {
        {"accept", "application/xml"},
        {"content-type", "application/json"}
    };
    Http::HttpRequest request = CreateHttpRequest("POST", headers, CreateInitializeRequestJson());

    transport.HandleRequest(request, ctx);

    EXPECT_FALSE(mockHttpSendFunc.responses.empty());
    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_NOT_ACCEPTABLE);
    EXPECT_EQ(callback->messageCount, 0);
}

TEST_F(StreamableHttpServerTransportTest, JsonResponseModeAcceptsJsonOnlyAcceptHeader)
{
    StreamableHttpServerTransport transport(validSessionId, true, false);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    std::unordered_map<std::string, std::string> headers = {
        {"accept", "application/json"},
        {"content-type", "application/json"}
    };
    Http::HttpRequest request = CreateHttpRequest("POST", headers, CreateInitializeRequestJson());

    transport.HandleRequest(request, ctx);

    EXPECT_EQ(callback->messageCount, 1);
    EXPECT_TRUE(mockHttpSendFunc.responses.empty());
}

TEST_F(StreamableHttpServerTransportTest, JsonResponseModeRejectsWhenClientDoesNotAcceptJson)
{
    StreamableHttpServerTransport transport(validSessionId, true, false);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    std::unordered_map<std::string, std::string> headers = {
        {"accept", "text/event-stream"},
        {"content-type", "application/json"}
    };
    Http::HttpRequest request = CreateHttpRequest("POST", headers, CreateInitializeRequestJson());

    transport.HandleRequest(request, ctx);

    EXPECT_FALSE(mockHttpSendFunc.responses.empty());
    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_NOT_ACCEPTABLE);
    EXPECT_EQ(callback->messageCount, 0);
}

TEST_F(StreamableHttpServerTransportTest, SseResponseModeRejectsJsonOnlyAcceptHeader)
{
    StreamableHttpServerTransport transport(validSessionId, false, false);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    std::unordered_map<std::string, std::string> headers = {
        {"accept", "application/json"},
        {"content-type", "application/json"}
    };
    Http::HttpRequest request = CreateHttpRequest("POST", headers, CreateInitializeRequestJson());

    transport.HandleRequest(request, ctx);

    EXPECT_FALSE(mockHttpSendFunc.responses.empty());
    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_NOT_ACCEPTABLE);
    EXPECT_EQ(callback->messageCount, 0);
}

TEST_F(StreamableHttpServerTransportTest, StatelessSsePostStartsEventStreamAndDeliversRequest)
{
    StreamableHttpServerTransport transport("", false, true);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    std::unordered_map<std::string, std::string> headers = {
        {"accept", "application/json, text/event-stream"},
        {"content-type", "application/json"}
    };
    Http::HttpRequest request = CreateHttpRequest("POST", headers, CreateNonInitializeRequestJson());

    transport.HandleRequest(request, ctx);

    EXPECT_EQ(callback->messageCount, 1);
    ASSERT_FALSE(mockHttpSendFunc.responses.empty());
    EXPECT_EQ(mockHttpSendFunc.responses.front().statusCode, Http::HTTP_STATUS_OK);
    EXPECT_EQ(mockHttpSendFunc.responses.front().headers[Http::CONTENT_TYPE_HEADER], Http::CONTENT_TYPE_SSE);
}

TEST_F(StreamableHttpServerTransportTest, StatelessModeRejectsGetSseStream)
{
    StreamableHttpServerTransport transport("", true, true);

    RequestContext ctx = CreateRequestContext();
    std::unordered_map<std::string, std::string> headers = {
        {"accept", "text/event-stream"},
        {"content-type", "application/json"}
    };
    Http::HttpRequest request = CreateHttpRequest("GET", headers, "");

    transport.HandleRequest(request, ctx);

    ASSERT_FALSE(mockHttpSendFunc.responses.empty());
    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_METHOD_NOT_ALLOWED);
}

TEST_F(StreamableHttpServerTransportTest, StatelessModeDeleteIsMethodNotAllowed)
{
    StreamableHttpServerTransport transport("", true, true);

    RequestContext ctx = CreateRequestContext();
    std::unordered_map<std::string, std::string> headers = {
        {"accept", "application/json"},
        {"content-type", "application/json"}
    };
    Http::HttpRequest request = CreateHttpRequest("DELETE", headers, "");

    transport.HandleRequest(request, ctx);

    EXPECT_FALSE(mockHttpSendFunc.responses.empty());
    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_METHOD_NOT_ALLOWED);
}

TEST_F(StreamableHttpServerTransportTest, StatelessPostAcceptsJsonOnlyAndSkipsSessionIdValidation)
{
    StreamableHttpServerTransport transport("", true, true);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    std::unordered_map<std::string, std::string> headers = {
        {"accept", "application/json"},
        {"content-type", "application/json"}
    };
    Http::HttpRequest request = CreateHttpRequest("POST", headers, CreateNonInitializeRequestJson());

    transport.HandleRequest(request, ctx);

    EXPECT_EQ(callback->messageCount, 1);
    EXPECT_TRUE(mockHttpSendFunc.responses.empty());
}

TEST_F(StreamableHttpServerTransportTest, StatelessPostRejectsWhenClientDoesNotAcceptJson)
{
    StreamableHttpServerTransport transport(validSessionId, true, true);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    std::unordered_map<std::string, std::string> headers = {
        {"accept", "text/event-stream"},
        {"content-type", "application/json"}
    };
    Http::HttpRequest request = CreateHttpRequest("POST", headers, CreateInitializeRequestJson());

    transport.HandleRequest(request, ctx);

    EXPECT_FALSE(mockHttpSendFunc.responses.empty());
    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_NOT_ACCEPTABLE);
    EXPECT_EQ(callback->messageCount, 0);
}

TEST_F(StreamableHttpServerTransportTest, HandlePostRequestMissingContentType)
{
    StreamableHttpServerTransport transport(validSessionId, false);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    std::unordered_map<std::string, std::string> headers = {
        {"accept", "application/json, text/event-stream"}
    };
    Http::HttpRequest request = CreateHttpRequest("POST", headers, CreateInitializeRequestJson());

    transport.HandleRequest(request, ctx);

    EXPECT_FALSE(mockHttpSendFunc.responses.empty());
    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE);
    EXPECT_EQ(callback->messageCount, 0);
}

TEST_F(StreamableHttpServerTransportTest, HandlePostRequestInvalidContentType)
{
    StreamableHttpServerTransport transport(validSessionId, false);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    std::unordered_map<std::string, std::string> headers = {
        {"accept", "application/json, text/event-stream"},
        {"content-type", "application/xml"}
    };
    Http::HttpRequest request = CreateHttpRequest("POST", headers, CreateInitializeRequestJson());

    transport.HandleRequest(request, ctx);

    EXPECT_FALSE(mockHttpSendFunc.responses.empty());
    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE);
    EXPECT_EQ(callback->messageCount, 0);
}

TEST_F(StreamableHttpServerTransportTest, HandlePostRequestInvalidJson)
{
    StreamableHttpServerTransport transport(validSessionId, false);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    std::unordered_map<std::string, std::string> headers = {
        {"accept", "application/json, text/event-stream"},
        {"content-type", "application/json"}
    };
    Http::HttpRequest request = CreateHttpRequest("POST", headers, "invalid json");

    transport.HandleRequest(request, ctx);

    EXPECT_FALSE(mockHttpSendFunc.responses.empty());
    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_BAD_REQUEST);
    EXPECT_EQ(callback->messageCount, 0);
}

TEST_F(StreamableHttpServerTransportTest, HandlePostRequestInitializeNoSessionId)
{
    StreamableHttpServerTransport transport("", false); // 空session ID
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    std::unordered_map<std::string, std::string> headers = {
        {"accept", "application/json, text/event-stream"},
        {"content-type", "application/json"}
    };
    Http::HttpRequest request = CreateHttpRequest("POST", headers, CreateInitializeRequestJson());

    transport.HandleRequest(request, ctx);

    // 初始化请求，没有session ID应该通过
    EXPECT_EQ(callback->messageCount, 1);
}

TEST_F(StreamableHttpServerTransportTest, HandlePostRequestInitializeWithSessionId)
{
    StreamableHttpServerTransport transport(validSessionId, false);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    std::unordered_map<std::string, std::string> headers = {
        {"accept", "application/json, text/event-stream"},
        {"content-type", "application/json"},
        {"mcp-session-id", validSessionId}
    };
    Http::HttpRequest request = CreateHttpRequest("POST", headers, CreateInitializeRequestJson());

    transport.HandleRequest(request, ctx);

    // 初始化请求，有session ID应该通过
    EXPECT_EQ(callback->messageCount, 1);
}

TEST_F(StreamableHttpServerTransportTest, HandlePostRequestNonInitializeMissingSessionId)
{
    StreamableHttpServerTransport transport(validSessionId, false);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    std::unordered_map<std::string, std::string> headers = {
        {"accept", "application/json, text/event-stream"},
        {"content-type", "application/json"}
        // 没有mcp-session-id
    };
    Http::HttpRequest request = CreateHttpRequest("POST", headers, CreateNonInitializeRequestJson());

    transport.HandleRequest(request, ctx);

    EXPECT_FALSE(mockHttpSendFunc.responses.empty());
    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_BAD_REQUEST);
    EXPECT_EQ(callback->messageCount, 0);
}

TEST_F(StreamableHttpServerTransportTest, HandlePostRequestNonInitializeInvalidSessionId)
{
    StreamableHttpServerTransport transport(validSessionId, false);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    std::unordered_map<std::string, std::string> headers = {
        {"accept", "application/json, text/event-stream"},
        {"content-type", "application/json"},
        {"mcp-session-id", "wrong-session-id"}
    };
    Http::HttpRequest request = CreateHttpRequest("POST", headers, CreateNonInitializeRequestJson());

    transport.HandleRequest(request, ctx);

    EXPECT_FALSE(mockHttpSendFunc.responses.empty());
    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_NOT_FOUND);
    EXPECT_EQ(callback->messageCount, 0);
}

TEST_F(StreamableHttpServerTransportTest, HandlePostRequestNonInitializeValidSessionId)
{
    StreamableHttpServerTransport transport(validSessionId, false);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    std::unordered_map<std::string, std::string> headers = {
        {"accept", "application/json, text/event-stream"},
        {"content-type", "application/json"},
        {"mcp-session-id", validSessionId}
    };
    Http::HttpRequest request = CreateHttpRequest("POST", headers, CreateNonInitializeRequestJson());

    transport.HandleRequest(request, ctx);

    EXPECT_EQ(callback->messageCount, 1);
}

TEST_F(StreamableHttpServerTransportTest, HandlePostRequestUnsupportedProtocolVersion)
{
    StreamableHttpServerTransport transport(validSessionId, false);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    std::unordered_map<std::string, std::string> headers = {
        {"accept", "application/json, text/event-stream"},
        {"content-type", "application/json"},
        {"mcp-session-id", validSessionId},
        {"mcp-protocol-version", "unsupported-version"}
    };
    Http::HttpRequest request = CreateHttpRequest("POST", headers, CreateNonInitializeRequestJson());

    transport.HandleRequest(request, ctx);

    EXPECT_FALSE(mockHttpSendFunc.responses.empty());
    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_BAD_REQUEST);
    EXPECT_EQ(callback->messageCount, 0);
}

TEST_F(StreamableHttpServerTransportTest, HandlePostRequestSupportedProtocolVersion)
{
    StreamableHttpServerTransport transport(validSessionId, false);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    std::unordered_map<std::string, std::string> headers = {
        {"accept", "application/json, text/event-stream"},
        {"content-type", "application/json"},
        {"mcp-session-id", validSessionId},
        {"mcp-protocol-version", DEFAULT_PROTOCOL_VERSION}
    };
    Http::HttpRequest request = CreateHttpRequest("POST", headers, CreateNonInitializeRequestJson());

    transport.HandleRequest(request, ctx);

    EXPECT_EQ(callback->messageCount, 1);
}

// ============ GET请求测试 ============
TEST_F(StreamableHttpServerTransportTest, HandleGetRequestNoAcceptHeader)
{
    StreamableHttpServerTransport transport(validSessionId, false);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    Http::HttpRequest request = CreateHttpRequest("GET");

    transport.HandleRequest(request, ctx);

    EXPECT_FALSE(mockHttpSendFunc.responses.empty());
    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_NOT_ACCEPTABLE);
}

TEST_F(StreamableHttpServerTransportTest, HandleGetRequestNoSseAccept)
{
    StreamableHttpServerTransport transport(validSessionId, false);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    std::unordered_map<std::string, std::string> headers = {
        {"accept", "application/json"}
    };
    Http::HttpRequest request = CreateHttpRequest("GET", headers);

    transport.HandleRequest(request, ctx);

    EXPECT_FALSE(mockHttpSendFunc.responses.empty());
    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_NOT_ACCEPTABLE);
}

TEST_F(StreamableHttpServerTransportTest, HandleGetRequestMissingSessionId)
{
    StreamableHttpServerTransport transport(validSessionId, false);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    std::unordered_map<std::string, std::string> headers = {
        {"accept", "text/event-stream"}
    };
    Http::HttpRequest request = CreateHttpRequest("GET", headers);

    transport.HandleRequest(request, ctx);

    EXPECT_FALSE(mockHttpSendFunc.responses.empty());
    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_BAD_REQUEST);
}

TEST_F(StreamableHttpServerTransportTest, HandleGetRequestInvalidSessionId)
{
    StreamableHttpServerTransport transport(validSessionId, false);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    std::unordered_map<std::string, std::string> headers = {
        {"accept", "text/event-stream"},
        {"mcp-session-id", "wrong-session-id"}
    };
    Http::HttpRequest request = CreateHttpRequest("GET", headers);

    transport.HandleRequest(request, ctx);

    EXPECT_FALSE(mockHttpSendFunc.responses.empty());
    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_BAD_REQUEST);
}

TEST_F(StreamableHttpServerTransportTest, HandleGetRequestUnsupportedProtocolVersion)
{
    StreamableHttpServerTransport transport(validSessionId, false);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    std::unordered_map<std::string, std::string> headers = {
        {"accept", "text/event-stream"},
        {"mcp-session-id", validSessionId},
        {"mcp-protocol-version", "unsupported-version"}
    };
    Http::HttpRequest request = CreateHttpRequest("GET", headers);

    transport.HandleRequest(request, ctx);

    EXPECT_FALSE(mockHttpSendFunc.responses.empty());
    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_BAD_REQUEST);
}

TEST_F(StreamableHttpServerTransportTest, HandleGetRequestValid)
{
    StreamableHttpServerTransport transport(validSessionId, false);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    std::unordered_map<std::string, std::string> headers = {
        {"accept", "text/event-stream"},
        {"mcp-session-id", validSessionId},
        {"mcp-protocol-version", DEFAULT_PROTOCOL_VERSION}
    };
    Http::HttpRequest request = CreateHttpRequest("GET", headers);

    transport.HandleRequest(request, ctx);

    EXPECT_FALSE(mockHttpSendFunc.responses.empty());
    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_OK);
    EXPECT_EQ(mockHttpSendFunc.lastResponse().headers[Http::CONTENT_TYPE_HEADER], Http::CONTENT_TYPE_SSE);
}

TEST_F(StreamableHttpServerTransportTest, HandleGetRequestDuplicateStream)
{
    StreamableHttpServerTransport transport(validSessionId, false);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx1 = CreateRequestContext();
    std::unordered_map<std::string, std::string> headers = {
        {"accept", "text/event-stream"},
        {"mcp-session-id", validSessionId},
        {"mcp-protocol-version", DEFAULT_PROTOCOL_VERSION}
    };
    Http::HttpRequest request = CreateHttpRequest("GET", headers);

    // 第一次GET请求
    transport.HandleRequest(request, ctx1);
    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_OK);

    // 第二次GET请求（应该冲突）
    RequestContext ctx2 = CreateRequestContext();
    mockHttpSendFunc.Clear();
    transport.HandleRequest(request, ctx2);

    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_CONFLICT);
}

TEST_F(StreamableHttpServerTransportTest, HandleGetRequestEmptySessionIdTransport)
{
    StreamableHttpServerTransport transport("", false); // 空session ID
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    std::unordered_map<std::string, std::string> headers = {
        {"accept", "text/event-stream"},
        {"mcp-protocol-version", DEFAULT_PROTOCOL_VERSION}
    };
    Http::HttpRequest request = CreateHttpRequest("GET", headers);

    transport.HandleRequest(request, ctx);

    EXPECT_FALSE(mockHttpSendFunc.responses.empty());
    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_OK);
}

// ============ DELETE请求测试 ============
TEST_F(StreamableHttpServerTransportTest, HandleDeleteRequestEmptySessionIdTransport)
{
    StreamableHttpServerTransport transport("", false);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    Http::HttpRequest request = CreateHttpRequest("DELETE");

    transport.HandleRequest(request, ctx);

    EXPECT_FALSE(mockHttpSendFunc.responses.empty());
    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_METHOD_NOT_ALLOWED);
}

TEST_F(StreamableHttpServerTransportTest, HandleDeleteRequestMissingSessionId)
{
    StreamableHttpServerTransport transport(validSessionId, false);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    Http::HttpRequest request = CreateHttpRequest("DELETE");

    transport.HandleRequest(request, ctx);

    EXPECT_FALSE(mockHttpSendFunc.responses.empty());
    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_BAD_REQUEST);
}

TEST_F(StreamableHttpServerTransportTest, HandleDeleteRequestInvalidSessionId)
{
    StreamableHttpServerTransport transport(validSessionId, false);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    std::unordered_map<std::string, std::string> headers = {
        {"mcp-session-id", "wrong-session-id"}
    };
    Http::HttpRequest request = CreateHttpRequest("DELETE", headers);

    transport.HandleRequest(request, ctx);

    EXPECT_FALSE(mockHttpSendFunc.responses.empty());
    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_BAD_REQUEST);
}

TEST_F(StreamableHttpServerTransportTest, HandleDeleteRequestValid)
{
    StreamableHttpServerTransport transport(validSessionId, false);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    std::unordered_map<std::string, std::string> headers = {
        {"mcp-session-id", validSessionId}
    };
    Http::HttpRequest request = CreateHttpRequest("DELETE", headers);

    transport.HandleRequest(request, ctx);

    EXPECT_FALSE(mockHttpSendFunc.responses.empty());
    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_OK);
}

// ============ 不支持的方法测试 ============
TEST_F(StreamableHttpServerTransportTest, HandleUnsupportedMethod)
{
    StreamableHttpServerTransport transport(validSessionId, false);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    Http::HttpRequest request = CreateHttpRequest("PUT");

    transport.HandleRequest(request, ctx);

    EXPECT_FALSE(mockHttpSendFunc.responses.empty());
    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_METHOD_NOT_ALLOWED);
    EXPECT_EQ(mockHttpSendFunc.lastResponse().headers["Allow"], "GET, POST, DELETE");
}

// ============ 边缘情况测试 ============
TEST_F(StreamableHttpServerTransportTest, EmptyJsonBody)
{
    StreamableHttpServerTransport transport(validSessionId, false);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    std::unordered_map<std::string, std::string> headers = {
        {"accept", "application/json, text/event-stream"},
        {"content-type", "application/json"},
        {"mcp-session-id", validSessionId}
    };
    Http::HttpRequest request = CreateHttpRequest("POST", headers, "");

    transport.HandleRequest(request, ctx);

    EXPECT_FALSE(mockHttpSendFunc.responses.empty());
    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_BAD_REQUEST);
}

TEST_F(StreamableHttpServerTransportTest, WhitespaceJsonBody)
{
    StreamableHttpServerTransport transport(validSessionId, false);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    std::unordered_map<std::string, std::string> headers = {
        {"accept", "application/json, text/event-stream"},
        {"content-type", "application/json"},
        {"mcp-session-id", validSessionId}
    };
    Http::HttpRequest request = CreateHttpRequest("POST", headers, "   ");

    transport.HandleRequest(request, ctx);

    EXPECT_FALSE(mockHttpSendFunc.responses.empty());
    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_BAD_REQUEST);
}

TEST_F(StreamableHttpServerTransportTest, CaseInsensitiveHeaders)
{
    StreamableHttpServerTransport transport(validSessionId, false);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    RequestContext ctx = CreateRequestContext();
    // 使用不同大小写的header名称
    std::unordered_map<std::string, std::string> headers = {
        {"Accept", "application/json, text/event-stream"},
        {"Content-Type", "application/json"},
        {"MCP-SESSION-ID", validSessionId}
    };
    Http::HttpRequest request = CreateHttpRequest("POST", headers, CreateNonInitializeRequestJson());

    // 注意：原代码使用小写查找，所以这些大写header不会被识别
    transport.HandleRequest(request, ctx);

    // 应该失败，因为找不到小写的header
    EXPECT_FALSE(mockHttpSendFunc.responses.empty());
    EXPECT_NE(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_OK);
}

// ============ 综合测试 ============
TEST_F(StreamableHttpServerTransportTest, CompleteWorkflow)
{
    StreamableHttpServerTransport transport(validSessionId, false);
    auto callback = std::make_shared<MockTransportCallback>();
    transport.SetCallback(callback);

    // 1. POST初始化请求
    RequestContext ctx1 = CreateRequestContext();
    std::unordered_map<std::string, std::string> postHeaders = {
        {"accept", "application/json, text/event-stream"},
        {"content-type", "application/json"}
    };
    Http::HttpRequest postRequest = CreateHttpRequest("POST", postHeaders, CreateInitializeRequestJson());

    transport.HandleRequest(postRequest, ctx1);
    EXPECT_EQ(callback->messageCount, 1);

    // 2. GET请求建立SSE流
    mockHttpSendFunc.Clear();
    RequestContext ctx2 = CreateRequestContext();
    std::unordered_map<std::string, std::string> getHeaders = {
        {"accept", "text/event-stream"},
        {"mcp-session-id", validSessionId},
        {"mcp-protocol-version", DEFAULT_PROTOCOL_VERSION}
    };
    Http::HttpRequest getRequest = CreateHttpRequest("GET", getHeaders);

    transport.HandleRequest(getRequest, ctx2);
    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_OK);

    // 3. DELETE请求终止会话
    mockHttpSendFunc.Clear();
    RequestContext ctx3 = CreateRequestContext();
    std::unordered_map<std::string, std::string> deleteHeaders = {
        {"mcp-session-id", validSessionId}
    };
    Http::HttpRequest deleteRequest = CreateHttpRequest("DELETE", deleteHeaders);

    transport.HandleRequest(deleteRequest, ctx3);
    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_OK);

    // 4. 会话终止后请求应该失败
    mockHttpSendFunc.Clear();
    RequestContext ctx4 = CreateRequestContext();
    Http::HttpRequest postRequest2 = CreateHttpRequest("POST", postHeaders, CreateNonInitializeRequestJson());

    transport.HandleRequest(postRequest2, ctx4);
    EXPECT_EQ(mockHttpSendFunc.lastResponse().statusCode, Http::HTTP_STATUS_NOT_FOUND);
}

} // namespace Mcp
