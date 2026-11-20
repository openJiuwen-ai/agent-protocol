/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#include "stream_server_emitter.h"
#include "http_common.h"
#include "http_server_transport.h"

namespace A2A::Transport {
namespace {

class StreamServerEmitterTest : public ::testing::Test {
protected:
    A2A::Http::HttpRequestContext ctx_{};
};

TEST_F(StreamServerEmitterTest, WriteData_HttpSendFuncExists_SendsSseFormattedBody)
{
    ctx_.connectionId = 123;
    ctx_.method = "GET";

    std::optional<A2A::Http::HttpResponse> capturedResponse;
    bool called = false;

    ctx_.httpSendFunc = [&](const A2A::Http::HttpResponse& response,
                            const A2A::Http::HttpRequestContext& context) {
        called = true;
        capturedResponse = response;
        EXPECT_EQ(context.connectionId, 123);
        EXPECT_EQ(context.method, "GET");
    };

    StreamServerEmitter emitter(ctx_);

    emitter.WriteStreamingData("payload");

    ASSERT_TRUE(called);
    ASSERT_TRUE(capturedResponse.has_value());
    EXPECT_EQ(capturedResponse->body, "data: payload\r\n\r\n");
    EXPECT_EQ(capturedResponse->type, A2A::Http::HttpSendType::HTTPRESPONSEBODY);
}

TEST_F(StreamServerEmitterTest, WriteData_HttpSendFuncIsNull_DoesNothing)
{
    StreamServerEmitter emitter(ctx_);

    EXPECT_NO_THROW(emitter.WriteStreamingData("payload"));
}

TEST_F(StreamServerEmitterTest, WriteDone_Called_SendEmpty)
{
    bool called = false;
    std::optional<A2A::Http::HttpResponse> capturedResponse;
    ctx_.httpSendFunc = [&](const A2A::Http::HttpResponse& response,
                            const A2A::Http::HttpRequestContext&) {
        called = true;
        capturedResponse = response;
    };

    StreamServerEmitter emitter(ctx_);

    emitter.WriteDone();

    EXPECT_TRUE(called);
    EXPECT_EQ(capturedResponse->body, "");
}

TEST_F(StreamServerEmitterTest, WriteNonStreamingData_StreamNotStarted_SendsOneShotResponse)
{
    std::optional<A2A::Http::HttpResponse> capturedResponse;
    ctx_.httpSendFunc = [&](const A2A::Http::HttpResponse& response,
                            const A2A::Http::HttpRequestContext&) {
        capturedResponse = response;
    };

    StreamServerEmitter emitter(ctx_);

    emitter.WriteNonStreamingData(R"({"jsonrpc":"2.0","id":1,"result":{}})");

    ASSERT_TRUE(capturedResponse.has_value());
    EXPECT_EQ(capturedResponse->type, A2A::Http::HttpSendType::HTTPRESPONSE);
    EXPECT_FALSE(capturedResponse->streaming);
    EXPECT_EQ(capturedResponse->headers.at(A2A::Http::CONTENT_TYPE_HEADER), A2A::Http::CONTENT_TYPE_JSON);
    EXPECT_EQ(capturedResponse->body, R"({"jsonrpc":"2.0","id":1,"result":{}})");
}

TEST_F(StreamServerEmitterTest, WriteNonStreamingData_StreamNotStarted_SecondWriteDropped)
{
    int sendCount = 0;
    ctx_.httpSendFunc = [&](const A2A::Http::HttpResponse&, const A2A::Http::HttpRequestContext&) {
        ++sendCount;
    };

    StreamServerEmitter emitter(ctx_);

    emitter.WriteNonStreamingData("first");
    emitter.WriteNonStreamingData("second");

    EXPECT_EQ(sendCount, 1);
}

TEST_F(StreamServerEmitterTest, WriteNonStreamingData_StreamStarted_DeliveredAsDataEvent)
{
    // The streaming request path sends the chunked response head before the handler runs, so
    // the emitter is constructed with streamStarted=true. A one-shot response here would write
    // a second status line and Content-Length into the open chunked stream; instead the payload
    // must go out as an SSE data: event like any other stream write.
    std::vector<A2A::Http::HttpResponse> sent;
    ctx_.httpSendFunc = [&](const A2A::Http::HttpResponse& response,
                            const A2A::Http::HttpRequestContext&) {
        sent.push_back(response);
    };

    StreamServerEmitter emitter(ctx_, {}, true);

    emitter.WriteNonStreamingData(R"({"jsonrpc":"2.0","id":1,"result":{}})");

    ASSERT_EQ(sent.size(), 1U);
    EXPECT_EQ(sent[0].type, A2A::Http::HttpSendType::HTTPRESPONSEBODY);
    EXPECT_EQ(sent[0].body, "data: " + std::string(R"({"jsonrpc":"2.0","id":1,"result":{}})") +
        "\r\n\r\n");

    // The stream stays usable: the terminating chunk is still written after the event.
    emitter.WriteDone();
    ASSERT_EQ(sent.size(), 2U);
    EXPECT_EQ(sent[1].body, "");
}

TEST_F(StreamServerEmitterTest, WriteNonStreamingData_StreamStartedAfterDone_Dropped)
{
    // streamDone_ wins over streamStarted_: once the terminating chunk closed the stream,
    // nothing may be written, not even a converted data: event.
    int sendCount = 0;
    ctx_.httpSendFunc = [&](const A2A::Http::HttpResponse&, const A2A::Http::HttpRequestContext&) {
        ++sendCount;
    };

    StreamServerEmitter emitter(ctx_, {}, true);

    emitter.WriteDone();
    emitter.WriteNonStreamingData("late");

    EXPECT_EQ(sendCount, 1);
}

} // namespace
} // namespace A2A::Transport