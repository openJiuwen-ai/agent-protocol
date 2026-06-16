/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>

#include <optional>
#include <string>

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

} // namespace
} // namespace A2A::Transport