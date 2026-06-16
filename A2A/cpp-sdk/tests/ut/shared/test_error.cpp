/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>

#include "error.h"
#include "types.h"

namespace A2A::Test {

TEST(ErrorTest, A2AServerError_DefaultStatusCode)
{
    A2AServerError err("internal failure");
    EXPECT_EQ(err.statusCode, static_cast<int>(A2AErrorCode::JSONRPC_INTERNAL_ERROR));
    EXPECT_STREQ(err.what(), "internal failure");
}

TEST(ErrorTest, A2AServerError_CustomStatusCode)
{
    const int code = static_cast<int>(A2AErrorCode::TASK_NOT_FOUND);
    A2AServerError err("task missing", code);
    EXPECT_EQ(err.statusCode, code);
}

TEST(ErrorTest, MethodNotImplementedError_UsesMethodNotFoundCode)
{
    MethodNotImplementedError err("custom");
    EXPECT_NE(std::string(err.what()).find("Not Implemented"), std::string::npos);
    EXPECT_EQ(err.statusCode, static_cast<int>(A2AErrorCode::JSONRPC_METHOD_NOT_FOUND));
}

TEST(ErrorTest, A2AClientHTTPError_StoresStatusCode)
{
    A2AClientHTTPError err(404, "not found");
    EXPECT_EQ(err.statusCode, 404);
    EXPECT_EQ(err.errorCode, 404);
    EXPECT_NE(std::string(err.what()).find("\"code\":404"), std::string::npos);
}

TEST(ErrorTest, A2AClientJSONError_SerializesCode)
{
    const int code = static_cast<int>(A2AErrorCode::JSONRPC_PARSE_ERROR);
    A2AClientJSONError err(code, "invalid json");
    EXPECT_EQ(err.errorCode, code);
    EXPECT_NE(std::string(err.what()).find("\"code\":-32700"), std::string::npos);
}

TEST(ErrorTest, A2AClientTimeoutError_UsesRequestTimeoutCode)
{
    A2AClientTimeoutError err("deadline exceeded");
    EXPECT_EQ(err.errorCode, static_cast<int>(A2AErrorCode::A2A_REQUEST_TIMEOUT));
    EXPECT_NE(std::string(err.what()).find("\"code\":-32101"), std::string::npos);
}

TEST(ErrorTest, A2AClientException_MakeAndTryParse)
{
    auto exPtr = A2AClientException::Make(static_cast<int>(A2AErrorCode::A2A_INVALID_INPUT), "bad arg");
    try {
        std::rethrow_exception(exPtr);
    } catch (const A2AClientException& e) {
        A2AError parsed;
        ASSERT_TRUE(A2AClientException::TryParse(e, parsed));
        EXPECT_EQ(parsed.code, static_cast<int>(A2AErrorCode::A2A_INVALID_INPUT));
        ASSERT_TRUE(parsed.message.has_value());
        EXPECT_EQ(*parsed.message, "bad arg");
    }
}

} // namespace A2A::Test
