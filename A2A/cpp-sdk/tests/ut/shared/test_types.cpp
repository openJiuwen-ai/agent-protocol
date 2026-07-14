/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "shared/jsonrpc.h"
#include "types.h"

namespace A2A::Test {

using json = nlohmann::json;

TEST(TypesTest, JsonRpcConstants)
{
    EXPECT_STREQ(JSONRPC_VERSION, "2.0");
    EXPECT_STREQ(JSONRPC_TRANSPORT, "JSONRPC");
}

TEST(TypesTest, A2AErrorCode_JsonRpcRange)
{
    EXPECT_EQ(static_cast<int>(A2AErrorCode::JSONRPC_PARSE_ERROR), -32700);
    EXPECT_EQ(static_cast<int>(A2AErrorCode::JSONRPC_INTERNAL_ERROR), -32603);
    EXPECT_EQ(static_cast<int>(A2AErrorCode::TASK_NOT_FOUND), -32001);
    EXPECT_EQ(static_cast<int>(A2AErrorCode::A2A_BAD_ALLOC), -32110);
}

TEST(TypesTest, RoleRoundTripViaJson)
{
    json roleJson = Role::AGENT;
    EXPECT_EQ(roleJson.get<std::string>(), "ROLE_AGENT");

    Role parsed = roleJson.get<Role>();
    EXPECT_EQ(parsed, Role::AGENT);
}

TEST(TypesTest, MessageSerializeDeserialize)
{
    Message msg;
    msg.messageId = "msg-1";
    msg.role = Role::USER;
    msg.contextId = "ctx-1";

    Part part;
    part.text = "hello";
    part.mediaType = "text/plain";
    msg.parts.push_back(part);

    json j = msg;
    Message restored = j.get<Message>();

    EXPECT_EQ(restored.messageId, "msg-1");
    EXPECT_EQ(restored.role, Role::USER);
    ASSERT_EQ(restored.parts.size(), 1u);
    ASSERT_TRUE(restored.parts[0].text.has_value());
    EXPECT_EQ(*restored.parts[0].text, "hello");
}

TEST(TypesTest, TaskStatusDefaults)
{
    TaskStatus status;
    status.state = TaskState::WORKING;

    EXPECT_EQ(status.state, TaskState::WORKING);
    EXPECT_FALSE(status.message.has_value());
    EXPECT_FALSE(status.timestamp.has_value());
}

TEST(TypesTest, PredefinedErrors_HaveExpectedCodes)
{
    EXPECT_EQ(TaskNotFoundError().code, static_cast<int>(A2AErrorCode::TASK_NOT_FOUND));
    EXPECT_EQ(MethodNotFoundError().code, static_cast<int>(A2AErrorCode::JSONRPC_METHOD_NOT_FOUND));
    EXPECT_EQ(InvalidParamsError().code, static_cast<int>(A2AErrorCode::JSONRPC_INVALID_PARAMS));
    EXPECT_EQ(VersionNotSupportedError().code, static_cast<int>(A2AErrorCode::VERSION_NOT_SUPPORTED_ERROR));
}

TEST(TypesTest, AgentCard_RoundTripViaJson)
{
    AgentCard card;
    card.name = "TestAgent";
    card.description = "desc";
    card.version = "1.0";
    card.defaultInputModes = {"text"};
    card.defaultOutputModes = {"text"};
    card.capabilities.streaming = true;

    AgentInterface iface;
    iface.url = "http://127.0.0.1:8080/jsonrpc";
    iface.protocolBinding = "JSONRPC";
    iface.protocolVersion = "1.0";
    card.supportedInterfaces = {iface};

    json j = card;
    AgentCard restored = j.get<AgentCard>();

    EXPECT_EQ(restored.name, "TestAgent");
    EXPECT_EQ(restored.version, "1.0");
    ASSERT_EQ(restored.supportedInterfaces.size(), 1u);
    EXPECT_EQ(restored.supportedInterfaces[0].url, iface.url);
    EXPECT_EQ(restored.supportedInterfaces[0].protocolVersion, "1.0");
}

} // namespace A2A::Test
