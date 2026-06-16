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
    EXPECT_EQ(err.statusCode, INTERNAL_ERROR_CODE);
    EXPECT_EQ(err.statusCode, static_cast<int>(A2AErrorCode::JSONRPC_INTERNAL_ERROR));
    EXPECT_STREQ(err.what(), "internal failure");
}

TEST(ErrorTest, A2AServerError_CustomStatusCode)
{
    const int code = static_cast<int>(A2AErrorCode::TASK_NOT_FOUND);
    A2AServerError err("task missing", code);
    EXPECT_EQ(err.statusCode, code);
}

TEST(ErrorTest, MethodNotImplementedError_InheritsServerError)
{
    MethodNotImplementedError err("custom");
    EXPECT_NE(std::string(err.what()).find("Not Implemented"), std::string::npos);
    EXPECT_EQ(err.statusCode, INTERNAL_ERROR_CODE);
}

TEST(ErrorTest, A2AClientHTTPError_StoresStatusCode)
{
    A2AClientHTTPError err(404, "not found");
    EXPECT_EQ(err.statusCode, 404);
    EXPECT_NE(std::string(err.what()).find("404"), std::string::npos);
}

TEST(ErrorTest, A2AClientJSONError_Message)
{
    A2AClientJSONError err("invalid json");
    EXPECT_NE(std::string(err.what()).find("JSON Error"), std::string::npos);
}

TEST(ErrorTest, A2AClientTimeoutError_Message)
{
    A2AClientTimeoutError err("deadline exceeded");
    EXPECT_NE(std::string(err.what()).find("Timeout Error"), std::string::npos);
}

} // namespace A2A::Test
