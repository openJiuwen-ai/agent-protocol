/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>

#include "client/http_client.h"

namespace Mcp::Http {
namespace {
using Net::TcpSocket;
namespace Script = Mcp::HttpClientTestSupport;

constexpr uint16_t SCRIPTED_SERVER_PORT = 80;
constexpr int REQUEST_TIMEOUT_MS = 1000;
constexpr int HTTP_STATUS_OK = 200;

const std::string COMPLETE_RESPONSE = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc";
const std::string PARTIAL_RESPONSE = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\na";

class HttpClientCallbacksTest : public ::testing::Test {
public:
    ~HttpClientCallbacksTest() override = default;

protected:
    void SetUp() override { Script::events.clear(); }
    void TearDown() override
    {
        TcpSocket::connection.reset();
        Script::events.clear();
    }
    std::optional<HttpResponse> Send()
    {
        HttpRequest request;
        request.method = "GET";
        request.url = "/";
        return HttpClient().SendRequest("scripted", SCRIPTED_SERVER_PORT, request, REQUEST_TIMEOUT_MS);
    }
    void ExpectCompleteResponse()
    {
        auto response = Send();
        ASSERT_TRUE(response.has_value());
        EXPECT_EQ(response->statusCode, HTTP_STATUS_OK);
        EXPECT_EQ(response->body, "abc");
        EXPECT_FALSE(Script::timerFired);
    }
};

TEST_F(HttpClientCallbacksTest, CompleteResponseOnRead)
{
    Script::events = {[] { TcpSocket::connection->Deliver(COMPLETE_RESPONSE); }};
    ExpectCompleteResponse();
}

TEST_F(HttpClientCallbacksTest, CompleteResponseAndEofBeforeRead)
{
    Script::events = {[] { TcpSocket::connection->Deliver(COMPLETE_RESPONSE, true); }};
    ExpectCompleteResponse();
}

TEST_F(HttpClientCallbacksTest, SplitResponseCompletedByEofBeforeRead)
{
    Script::events = {
        [] { TcpSocket::connection->Deliver(PARTIAL_RESPONSE); },
        [] { TcpSocket::connection->Deliver("bc", true); },
    };
    ExpectCompleteResponse();
}

TEST_F(HttpClientCallbacksTest, SplitResponseCompletedByRead)
{
    Script::events = {
        [] { TcpSocket::connection->Deliver(PARTIAL_RESPONSE); },
        [] { TcpSocket::connection->Deliver("bc"); },
    };
    ExpectCompleteResponse();
}

TEST_F(HttpClientCallbacksTest, PartialResponseAtEofFails)
{
    Script::events = {[] { TcpSocket::connection->Deliver(PARTIAL_RESPONSE, true); }};
    EXPECT_FALSE(Send().has_value());
    EXPECT_FALSE(Script::timerFired);
}

TEST_F(HttpClientCallbacksTest, PartialResponseThenCloseFails)
{
    Script::events = {
        [] { TcpSocket::connection->Deliver(PARTIAL_RESPONSE); },
        [] { TcpSocket::connection->Close(); },
    };
    EXPECT_FALSE(Send().has_value());
    EXPECT_FALSE(Script::timerFired);
}

TEST_F(HttpClientCallbacksTest, CloseWithoutResponseFails)
{
    Script::events = {[] { TcpSocket::connection->Close(); }};
    EXPECT_FALSE(Send().has_value());
    EXPECT_FALSE(Script::timerFired);
}

TEST_F(HttpClientCallbacksTest, InvalidLengthFailsWithoutWaitingForTimeout)
{
    Script::events = {[] {
        TcpSocket::connection->Deliver("HTTP/1.1 200 OK\r\nContent-Length: bad\r\n\r\n");
    }};
    EXPECT_FALSE(Send().has_value());
    EXPECT_FALSE(Script::timerFired);
}

TEST_F(HttpClientCallbacksTest, InvalidLengthAndEofBeforeReadFails)
{
    Script::events = {[] {
        TcpSocket::connection->Deliver("HTTP/1.1 200 OK\r\nContent-Length: bad\r\n\r\n", true);
    }};
    EXPECT_FALSE(Send().has_value());
    EXPECT_FALSE(Script::timerFired);
}

TEST_F(HttpClientCallbacksTest, ChunkedResponseFailsWithoutWaitingForTimeout)
{
    Script::events = {[] {
        TcpSocket::connection->Deliver("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                                       "3\r\nabc\r\n0\r\n\r\n");
    }};
    EXPECT_FALSE(Send().has_value());
    EXPECT_FALSE(Script::timerFired);
}
} // namespace
} // namespace Mcp::Http
