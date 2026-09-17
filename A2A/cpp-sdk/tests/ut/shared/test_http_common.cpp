/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <unordered_map>
#include <string>

#include "http_common.h"

namespace A2A::Shared::Test {

using namespace A2A::Http;
using ::testing::ElementsAre;

// ===========================================================================
// TrimInPlace 测试
// ===========================================================================
TEST(TrimInPlaceTest, EmptyString)
{
    std::string str;
    TrimInPlace(str);
    EXPECT_TRUE(str.empty());
}

TEST(TrimInPlaceTest, OnlySpaces)
{
    std::string str = "   \t   ";
    TrimInPlace(str);
    EXPECT_TRUE(str.empty());
}

TEST(TrimInPlaceTest, LeadingSpaces)
{
    std::string str = "   \tHello";
    TrimInPlace(str);
    EXPECT_EQ(str, "Hello");
}

TEST(TrimInPlaceTest, TrailingSpaces)
{
    std::string str = "Hello   \t";
    TrimInPlace(str);
    EXPECT_EQ(str, "Hello");
}

TEST(TrimInPlaceTest, BothSidesSpaces)
{
    std::string str = "   \tHello World   \t";
    TrimInPlace(str);
    EXPECT_EQ(str, "Hello World");
}

TEST(TrimInPlaceTest, NoSpaces)
{
    std::string str = "Hello";
    TrimInPlace(str);
    EXPECT_EQ(str, "Hello");
}

TEST(TrimInPlaceTest, InternalSpaces)
{
    std::string str = "   Hello   World   ";
    TrimInPlace(str);
    EXPECT_EQ(str, "Hello   World");
}

// ===========================================================================
// ParseHeadersAndBody 测试
// ===========================================================================
class ParseHeadersAndBodyTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        headers.clear();
        body.clear();
        consumedBytes = 0;
    }

    std::unordered_map<std::string, std::string> headers;
    std::string body;
    std::size_t consumedBytes = 0;
};

TEST_F(ParseHeadersAndBodyTest, MissingHeaderEnd)
{
    std::string buffer = "Header1: value1\r\nHeader2: value2\r\n\r\nBody content";
    std::size_t headerEnd = std::string::npos;

    bool result = ParseHeadersAndBody(buffer, headerEnd, headers, body, consumedBytes);

    EXPECT_FALSE(result);
    EXPECT_EQ(consumedBytes, 0);
}

TEST_F(ParseHeadersAndBodyTest, HeaderEndTooClose)
{
    std::string buffer = "Header1: value1\r\n";
    std::size_t headerEnd = buffer.size() - 2; // 指向 \r\n 之前

    bool result = ParseHeadersAndBody(buffer, headerEnd, headers, body, consumedBytes);

    EXPECT_FALSE(result);
    EXPECT_EQ(consumedBytes, 0);
}

TEST_F(ParseHeadersAndBodyTest, SimpleHeaders)
{
    std::string jsonBody = "{\"key\":\"value\"}";
    std::string contentLength = std::to_string(jsonBody.length());

    std::string buffer =
        "Content-Type: application/json\r\n"
        "Content-Length: " + contentLength + "\r\n"
        "\r\n" +
        jsonBody;

    std::size_t headerEnd = buffer.find("\r\n\r\n");

    bool result = ParseHeadersAndBody(buffer, headerEnd, headers, body, consumedBytes);

    EXPECT_TRUE(result);
    EXPECT_EQ(headers.size(), 2);
    EXPECT_EQ(headers["Content-Type"], "application/json");
    EXPECT_EQ(headers["Content-Length"], contentLength);
    EXPECT_EQ(body, jsonBody);
    EXPECT_EQ(consumedBytes, buffer.size());
}

TEST_F(ParseHeadersAndBodyTest, HeadersWithCRLF)
{
    std::string bodyContent = "Body";
    std::string contentLength = std::to_string(bodyContent.length());

    std::string buffer = "Content-Type: application/json\r\n"
                        "X-Custom: value with spaces\r\n"
                        "Content-Length: " + contentLength + "\r\n"
                        "\r\n" +
                        bodyContent;
    std::size_t headerEnd = buffer.find("\r\n\r\n");

    bool result = ParseHeadersAndBody(buffer, headerEnd, headers, body, consumedBytes);

    EXPECT_TRUE(result);
    EXPECT_EQ(headers.size(), 3);
    EXPECT_EQ(headers["Content-Type"], "application/json");
    EXPECT_EQ(headers["X-Custom"], "value with spaces");
    EXPECT_EQ(headers["Content-Length"], contentLength);
    EXPECT_EQ(body, bodyContent);
    EXPECT_EQ(consumedBytes, buffer.size());
}

TEST_F(ParseHeadersAndBodyTest, HeadersWithLeadingTrailingSpaces)
{
    std::string expectedBody = "Body";
    std::string contentLength = std::to_string(expectedBody.length());

    std::string buffer = "  Content-Type : application/json  \r\n"
                        "  X-Custom : value  \r\n"
                        "Content-Length: " + contentLength + "\r\n"
                        "\r\n" +
                        expectedBody;
    std::size_t headerEnd = buffer.find("\r\n\r\n");

    bool result = ParseHeadersAndBody(buffer, headerEnd, headers, body, consumedBytes);

    EXPECT_TRUE(result);
    EXPECT_EQ(headers.size(), 3);
    EXPECT_EQ(headers["Content-Type"], "application/json");
    EXPECT_EQ(headers["X-Custom"], "value");
    EXPECT_EQ(headers["Content-Length"], contentLength);
    EXPECT_EQ(body, expectedBody);
    EXPECT_EQ(consumedBytes, buffer.size());
}

TEST_F(ParseHeadersAndBodyTest, EmptyHeaders)
{
    std::string expectedBody = "Body content only";
    std::string contentLength = std::to_string(expectedBody.length());

    std::string buffer = "Content-Length: " + contentLength + "\r\n\r\n" + expectedBody;
    std::size_t headerEnd = buffer.find("\r\n\r\n");

    bool result = ParseHeadersAndBody(buffer, headerEnd, headers, body, consumedBytes);

    EXPECT_TRUE(result);
    EXPECT_EQ(headers.size(), 1);  // 现在应该有一个 Content-Length 头部
    EXPECT_EQ(headers["Content-Length"], contentLength);
    EXPECT_EQ(body, expectedBody);
    EXPECT_EQ(consumedBytes, buffer.size());
}

TEST_F(ParseHeadersAndBodyTest, MissingColon)
{
    std::string buffer = "InvalidHeaderLine\r\nContent-Type: application/json\r\n\r\nBody";
    std::size_t headerEnd = buffer.find("\r\n\r\n");

    bool result = ParseHeadersAndBody(buffer, headerEnd, headers, body, consumedBytes);

    EXPECT_TRUE(result);
    EXPECT_EQ(headers.size(), 1); // 只有有效的那行被解析
    EXPECT_EQ(headers["Content-Type"], "application/json");
}

TEST_F(ParseHeadersAndBodyTest, ContentLengthMismatch)
{
    std::string buffer = "Content-Length: 20\r\n\r\nBody too short";
    std::size_t headerEnd = buffer.find("\r\n\r\n");

    bool result = ParseHeadersAndBody(buffer, headerEnd, headers, body, consumedBytes);

    EXPECT_FALSE(result); // 因为 body 长度小于 Content-Length
    EXPECT_EQ(consumedBytes, 0);
}

TEST_F(ParseHeadersAndBodyTest, ContentLengthExact)
{
    std::string jsonBody = "{\"key\":\"value\"}";
    std::string contentLength = std::to_string(jsonBody.length());

    std::string buffer = "Content-Length: " + contentLength + "\r\n\r\n" + jsonBody;
    std::size_t headerEnd = buffer.find("\r\n\r\n");

    bool result = ParseHeadersAndBody(buffer, headerEnd, headers, body, consumedBytes);

    EXPECT_TRUE(result);
    EXPECT_EQ(headers["Content-Length"], contentLength);
    EXPECT_EQ(body, jsonBody);
    EXPECT_EQ(body.length(), jsonBody.length());
}

TEST_F(ParseHeadersAndBodyTest, InvalidContentLength)
{
    std::string buffer = "Content-Length: invalid\r\n\r\nBody content";
    std::size_t headerEnd = buffer.find("\r\n\r\n");
    std::size_t bodyStart = headerEnd + 4;  // 两个 \r\n 的长度

    bool result = ParseHeadersAndBody(buffer, headerEnd, headers, body, consumedBytes);

    EXPECT_TRUE(result);
    EXPECT_EQ(headers["Content-Length"], "invalid");
    EXPECT_EQ(body, "");
    EXPECT_EQ(consumedBytes, bodyStart);
}

// ===========================================================================
// GetHeaderValue 测试
// ===========================================================================
TEST(GetHeaderValueTest, HeaderPresent)
{
    HttpResponse response;
    response.headers["Content-Type"] = "application/json";
    std::string result = GetHeaderValue(response, "Content-Type");
    EXPECT_EQ(result, "application/json");
}

TEST(GetHeaderValueTest, HeaderMissing)
{
    HttpResponse response;
    std::string result = GetHeaderValue(response, "Content-Type");
    EXPECT_TRUE(result.empty());
}

TEST(GetHeaderValueTest, EmptyHeaders)
{
    HttpResponse response;
    response.headers = {};
    std::string result = GetHeaderValue(response, "Content-Type");
    EXPECT_TRUE(result.empty());
}

TEST(GetHeaderValueTest, CaseInsensitive)
{
    HttpResponse response;
    response.headers["content-type"] = "application/json";
    std::string result = GetHeaderValue(response, "Content-Type");
    EXPECT_EQ(result, "application/json");
}

TEST(GetHeaderValueTest, DifferentHeader)
{
    HttpResponse response;
    response.headers["Content-Length"] = "1024";
    std::string result = GetHeaderValue(response, "Content-Length");
    EXPECT_EQ(result, "1024");
}

TEST(GetHeaderValueTest, CustomHeader)
{
    HttpResponse response;
    response.headers["X-Custom-Header"] = "custom-value";
    std::string result = GetHeaderValue(response, "X-Custom-Header");
    EXPECT_EQ(result, "custom-value");
}

TEST(GetHeaderValueTest, MultipleHeaders)
{
    HttpResponse response;
    response.headers["Content-Type"] = "application/json";
    response.headers["Content-Length"] = "1024";
    response.headers["X-Request-ID"] = "12345";

    EXPECT_EQ(GetHeaderValue(response, "Content-Type"), "application/json");
    EXPECT_EQ(GetHeaderValue(response, "Content-Length"), "1024");
    EXPECT_EQ(GetHeaderValue(response, "X-Request-ID"), "12345");
}

inline std::string GetContentType(const HttpResponse& response)
{
    return GetHeaderValue(response, Http::CONTENT_TYPE_HEADER);
}

TEST(GetContentTypeTest, ContentTypePresent)
{
    HttpResponse response;
    response.headers["Content-Type"] = "application/json";

    std::string result = GetContentType(response);
    EXPECT_EQ(result, "application/json");
}

TEST(GetContentTypeTest, ContentTypeMissing)
{
    HttpResponse response;

    std::string result = GetContentType(response);
    EXPECT_TRUE(result.empty());
}

TEST(GetContentTypeTest, EmptyHeaders)
{
    HttpResponse response;
    response.headers = {};

    std::string result = GetContentType(response);
    EXPECT_TRUE(result.empty());
}

TEST(GetContentTypeTest, CaseInsensitive)
{
    HttpResponse response;
    response.headers["content-type"] = "application/json";

    std::string result = GetContentType(response);
    EXPECT_EQ(result, "application/json");
}

// ===========================================================================
// ParseSseLine 测试
// ===========================================================================
class ParseSseLineTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        sseEvent = ServerSentEvent();
    }

    ServerSentEvent sseEvent;
};

TEST_F(ParseSseLineTest, EmptyLineWithData)
{
    sseEvent.data = "previous data";

    bool result = ParseSseLine("", sseEvent);
    EXPECT_TRUE(result);
    EXPECT_EQ(sseEvent.data, "previous data");
}

TEST_F(ParseSseLineTest, EventLine)
{
    bool result = ParseSseLine("event: userlogin", sseEvent);
    EXPECT_FALSE(result);
    EXPECT_EQ(sseEvent.event, "userlogin");
}

TEST_F(ParseSseLineTest, EventLineWithSpaces)
{
    bool result = ParseSseLine("event:   user login", sseEvent);
    EXPECT_FALSE(result);
    EXPECT_EQ(sseEvent.event, "user login");
}

TEST_F(ParseSseLineTest, IdLine)
{
    bool result = ParseSseLine("id: 12345", sseEvent);
    EXPECT_FALSE(result);
    EXPECT_EQ(sseEvent.id, "12345");
}

TEST_F(ParseSseLineTest, IdLineWithSpaces)
{
    bool result = ParseSseLine("id:   123-45", sseEvent);
    EXPECT_FALSE(result);
    EXPECT_EQ(sseEvent.id, "123-45");
}

TEST_F(ParseSseLineTest, DataLine)
{
    bool result = ParseSseLine("data: Hello world", sseEvent);
    EXPECT_FALSE(result);
    EXPECT_EQ(sseEvent.data, "Hello world");
}

TEST_F(ParseSseLineTest, DataLineWithSpaces)
{
    bool result = ParseSseLine("data:   Hello   world", sseEvent);
    EXPECT_FALSE(result);
    EXPECT_EQ(sseEvent.data, "Hello   world");
}

TEST_F(ParseSseLineTest, MultipleLines)
{
    ParseSseLine("event: message", sseEvent);
    ParseSseLine("id: 001", sseEvent);
    ParseSseLine("data: First line", sseEvent);
    ParseSseLine("data: Second line", sseEvent);

    bool result = ParseSseLine("", sseEvent);

    EXPECT_TRUE(result);
    EXPECT_EQ(sseEvent.event, "message");
    EXPECT_EQ(sseEvent.id, "001");
    EXPECT_EQ(sseEvent.data, "First line\nSecond line");
}

// ===========================================================================
// 综合测试 - 完整的 SSE 消息解析
// ===========================================================================
TEST_F(ParseSseLineTest, CompleteSseMessage)
{
    std::vector<std::string> lines = {
        "event: notification",
        "id: 123",
        "data: Hello",
        "data: World",
        ""
    };

    std::vector<ServerSentEvent> events;
    ServerSentEvent currentEvent;

    for (const auto& line : lines) {
        bool isEventEnd = ParseSseLine(line, currentEvent);
        if (isEventEnd) {
            events.push_back(currentEvent);
            currentEvent = ServerSentEvent();
        }
    }

    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].event, "notification");
    EXPECT_EQ(events[0].id, "123");
    // 追加模式下，应该是两行用换行符连接
    EXPECT_EQ(events[0].data, "Hello\nWorld");
}

// ===========================================================================
// 综合测试 - 完整的 HTTP 消息解析
// ===========================================================================
TEST_F(ParseHeadersAndBodyTest, CompleteHttpMessage)
{
    std::string jsonBody = "{\"message\": \"Hello World\"}";
    std::string contentLength = std::to_string(jsonBody.length());

    std::string httpMessage =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " + contentLength + "\r\n"
        "X-Custom-Header: test value\r\n"
        "\r\n" +
        jsonBody;

    std::size_t headerEnd = httpMessage.find("\r\n\r\n");

    bool result = ParseHeadersAndBody(httpMessage, headerEnd, headers, body, consumedBytes);

    EXPECT_TRUE(result);
    EXPECT_EQ(headers.size(), 3);
    EXPECT_EQ(headers["Content-Type"], "application/json");
    EXPECT_EQ(headers["Content-Length"], contentLength);
    EXPECT_EQ(headers["X-Custom-Header"], "test value");
    EXPECT_EQ(body, jsonBody);
    EXPECT_EQ(consumedBytes, httpMessage.size());
}

// ===========================================================================
// 边界条件测试
// ===========================================================================
TEST(TrimInPlaceTest, VeryLongString)
{
    std::string str(10000, ' ');
    str += "content";
    str += std::string(10000, ' ');

    TrimInPlace(str);
    EXPECT_EQ(str, "content");
}

TEST_F(ParseHeadersAndBodyTest, VeryLargeContentLength)
{
    std::string buffer = "Content-Length: 9999999\r\n\r\nSmall body";
    std::size_t headerEnd = buffer.find("\r\n\r\n");

    bool result = ParseHeadersAndBody(buffer, headerEnd, headers, body, consumedBytes);
    EXPECT_FALSE(result);
}

} // namespace A2A::Http::Test