/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "shared/http_common.h"

#include <gtest/gtest.h>
#include <string>
#include <unordered_map>

static constexpr int HEADER_SIZE = 2;
static constexpr int ARGS_NUM = 4;
static constexpr int REQ_ID = 12345;

namespace Mcp::Http {

TEST(HttpCommonTest, TrimInPlace_EmptyString)
{
    std::string str = "";
    TrimInPlace(str);
    EXPECT_EQ(str, "");
}

TEST(HttpCommonTest, TrimInPlace_OnlySpaces)
{
    std::string str = "   \t  ";
    TrimInPlace(str);
    EXPECT_EQ(str, "");
}

TEST(HttpCommonTest, TrimInPlace_LeadingSpaces)
{
    std::string str = "  \thello";
    TrimInPlace(str);
    EXPECT_EQ(str, "hello");
}

TEST(HttpCommonTest, TrimInPlace_TrailingSpaces)
{
    std::string str = "hello  \t";
    TrimInPlace(str);
    EXPECT_EQ(str, "hello");
}

TEST(HttpCommonTest, TrimInPlace_BothEndsSpaces)
{
    std::string str = "  \thello world  \t";
    TrimInPlace(str);
    EXPECT_EQ(str, "hello world");
}

TEST(HttpCommonTest, TrimInPlace_NoSpaces)
{
    std::string str = "hello";
    TrimInPlace(str);
    EXPECT_EQ(str, "hello");
}

TEST(HttpCommonTest, TrimInPlace_MiddleSpacesPreserved)
{
    std::string str = "  hello  world  ";
    TrimInPlace(str);
    EXPECT_EQ(str, "hello  world");
}

TEST(HttpCommonTest, ParseHeadersAndBody_CompleteMessageWithContentLength)
{
    std::string buffer = "Content-Length: 11\r\n"
                         "Content-Type: text/plain\r\n"
                         "\r\n"
                         "Hello World";

    std::size_t headerEnd = buffer.find("\r\n\r\n");
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    std::size_t consumedBytes = 0;

    EXPECT_TRUE(ParseHeadersAndBody(buffer, headerEnd, headers, body, consumedBytes));

    EXPECT_EQ(headers.size(), HEADER_SIZE);
    EXPECT_EQ(headers["Content-Length"], "11");
    EXPECT_EQ(headers["Content-Type"], "text/plain");
    EXPECT_EQ(body, "Hello World");
    EXPECT_EQ(consumedBytes, buffer.size());
}

TEST(HttpCommonTest, ParseHeadersAndBody_IncompleteHeader)
{
    std::string buffer = "Content-Length: 11\r\n"
                         "Content-Type: text/plain";

    std::size_t headerEnd = buffer.find("\r\n\r\n"); // npos
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    std::size_t consumedBytes = 0;

    EXPECT_FALSE(ParseHeadersAndBody(buffer, headerEnd, headers, body, consumedBytes));
    EXPECT_EQ(consumedBytes, 0);
    EXPECT_TRUE(headers.empty());
    EXPECT_TRUE(body.empty());
}

TEST(HttpCommonTest, ParseHeadersAndBody_IncompleteBody)
{
    std::string buffer = "Content-Length: 20\r\n"
                         "\r\n"
                         "Hello World"; // Only 11 bytes, not 20

    std::size_t headerEnd = buffer.find("\r\n\r\n");
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    std::size_t consumedBytes = 0;

    EXPECT_FALSE(ParseHeadersAndBody(buffer, headerEnd, headers, body, consumedBytes));
    EXPECT_EQ(headers.size(), 1);
    EXPECT_EQ(headers["Content-Length"], "20");
    EXPECT_TRUE(body.empty());
    EXPECT_EQ(consumedBytes, 0);
}

TEST(HttpCommonTest, ParseHeadersAndBody_NoContentLength)
{
    std::string buffer = "Content-Type: text/plain\r\n"
                         "\r\n"
                         "Some body data";

    std::size_t headerEnd = buffer.find("\r\n\r\n");
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    std::size_t consumedBytes = 0;

    EXPECT_TRUE(ParseHeadersAndBody(buffer, headerEnd, headers, body, consumedBytes));
    EXPECT_EQ(headers.size(), 1);
    EXPECT_EQ(headers["Content-Type"], "text/plain");
    EXPECT_EQ(body, "");
    EXPECT_EQ(consumedBytes, headerEnd + HTTP_HEADER_BODY_SEPARATOR_LENGTH);
}

TEST(HttpCommonTest, ParseHeadersAndBody_EmptyBody)
{
    std::string buffer = "Content-Length: 0\r\n"
                         "\r\n";

    std::size_t headerEnd = buffer.find("\r\n\r\n");
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    std::size_t consumedBytes = 0;

    EXPECT_TRUE(ParseHeadersAndBody(buffer, headerEnd, headers, body, consumedBytes));
    EXPECT_EQ(headers.size(), 1);
    EXPECT_EQ(headers["Content-Length"], "0");
    EXPECT_EQ(body, "");
    EXPECT_EQ(consumedBytes, buffer.size());
}

TEST(HttpCommonTest, ParseHeadersAndBody_InvalidContentLength)
{
    std::string buffer = "Content-Length: abc\r\n"
                         "\r\n"
                         "Some body";

    std::size_t headerEnd = buffer.find("\r\n\r\n");
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    std::size_t consumedBytes = 0;

    EXPECT_TRUE(ParseHeadersAndBody(buffer, headerEnd, headers, body, consumedBytes));
    EXPECT_EQ(headers.size(), 1);
    EXPECT_EQ(headers["Content-Length"], "abc");
    EXPECT_EQ(body, "");
    EXPECT_EQ(consumedBytes, headerEnd + HTTP_HEADER_BODY_SEPARATOR_LENGTH);
}

TEST(HttpCommonTest, ParseHeadersAndBody_MultipleHeaders)
{
    std::string buffer = "Host: localhost:8080\r\n"
                         "User-Agent: TestClient\r\n"
                         "Accept: */*\r\n"
                         "Content-Length: 5\r\n"
                         "\r\n"
                         "12345";

    std::size_t headerEnd = buffer.find("\r\n\r\n");
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    std::size_t consumedBytes = 0;

    EXPECT_TRUE(ParseHeadersAndBody(buffer, headerEnd, headers, body, consumedBytes));
    EXPECT_EQ(headers.size(), ARGS_NUM);
    EXPECT_EQ(headers["Host"], "localhost:8080");
    EXPECT_EQ(headers["User-Agent"], "TestClient");
    EXPECT_EQ(headers["Accept"], "*/*");
    EXPECT_EQ(headers["Content-Length"], "5");
    EXPECT_EQ(body, "12345");
    EXPECT_EQ(consumedBytes, buffer.size());
}

TEST(HttpCommonTest, ParseHeadersAndBody_MalformedHeaderNoColon)
{
    std::string buffer = "Content-Length 11\r\n"  // Missing colon
                         "Content-Type: text/plain\r\n"
                         "\r\n"
                         "Hello";

    std::size_t headerEnd = buffer.find("\r\n\r\n");
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    std::size_t consumedBytes = 0;

    EXPECT_TRUE(ParseHeadersAndBody(buffer, headerEnd, headers, body, consumedBytes));
    EXPECT_EQ(headers.size(), 1);
    EXPECT_EQ(headers["Content-Type"], "text/plain");
    EXPECT_EQ(body, "");
    EXPECT_EQ(consumedBytes, headerEnd + HTTP_HEADER_BODY_SEPARATOR_LENGTH);
}

TEST(HttpCommonTest, ParseHeadersAndBody_BodyExactlyContentLength)
{
    std::string buffer = "Content-Length: 3\r\n"
                         "\r\n"
                         "xyz";

    std::size_t headerEnd = buffer.find("\r\n\r\n");
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    std::size_t consumedBytes = 0;

    EXPECT_TRUE(ParseHeadersAndBody(buffer, headerEnd, headers, body, consumedBytes));
    EXPECT_EQ(headers.size(), 1);
    EXPECT_EQ(headers["Content-Length"], "3");
    EXPECT_EQ(body, "xyz");
    EXPECT_EQ(consumedBytes, buffer.size());
}

TEST(HttpCommonTest, HttpResponse_DefaultConstructor)
{
    HttpResponse response;

    EXPECT_FALSE(response.success);
    EXPECT_EQ(response.statusCode, HTTP_STATUS_OK);
    EXPECT_EQ(response.statusText, "OK");
    EXPECT_TRUE(response.errorMessage.empty());
    EXPECT_TRUE(response.body.empty());
    EXPECT_EQ(response.headers.size(), HEADER_SIZE);
    EXPECT_EQ(response.headers[CONTENT_TYPE_HEADER], "text/plain");
    EXPECT_EQ(response.headers["Connection"], "keep-alive");
    EXPECT_EQ(response.userData.requestId, 0);
    EXPECT_TRUE(response.userData.method.empty());
}

TEST(HttpCommonTest, HttpResponse_UserData)
{
    HttpResponse response;
    response.userData.requestId = REQ_ID;
    response.userData.method = "GET";

    EXPECT_EQ(response.userData.requestId, REQ_ID);
    EXPECT_EQ(response.userData.method, "GET");
}

TEST(HttpCommonTest, HttpRequest_DefaultValues)
{
    HttpRequest request;

    EXPECT_TRUE(request.method.empty());
    EXPECT_TRUE(request.url.empty());
    EXPECT_TRUE(request.version.empty());
    EXPECT_TRUE(request.headers.empty());
    EXPECT_TRUE(request.body.empty());
    EXPECT_TRUE(request.queryParams.empty());
    EXPECT_TRUE(request.pathParams.empty());
}

} // namespace Mcp::Http