/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "server/request_context.h"
#include "server/server_call_context.h"
#include "types.h"

using namespace A2A;
using namespace A2A::Server;

namespace {

class RequestContextTest : public ::testing::Test {
protected:
    static Part MakeTextPart(const std::string& text)
    {
        Part p;
        p.text = text;
        p.mediaType = "text/plain";
        return p;
    }

    static Message MakeMessage(const std::string& messageId = "msg-1")
    {
        Message msg;
        msg.messageId = messageId;
        msg.role = Role::USER;
        msg.parts = {MakeTextPart("hello"), MakeTextPart("world")};
        return msg;
    }

    static MessageSendParams MakeParams()
    {
        MessageSendParams params;
        params.message = MakeMessage();
        return params;
    }

    static Task MakeTask(const std::string& taskId = "task-1",
                        const std::string& contextId = "ctx-1",
                        TaskState state = TaskState::WORKING)
    {
        Task task;
        task.id = taskId;
        task.contextId = contextId;
        task.status.state = state;
        task.status.timestamp = "2026-03-17T00:00:00Z";
        return task;
    }
};

TEST_F(RequestContextTest, ConstructorWithEmptyParam_AllGettersReturnDefaults)
{
    RequestContextParam param;
    RequestContext ctx(param);

    EXPECT_EQ(ctx.GetUserInput(), "");
    EXPECT_EQ(ctx.GetMessage(), nullptr);
    EXPECT_TRUE(ctx.GetRelatedTasks().empty());
    EXPECT_FALSE(ctx.GetCurrentTask());
    EXPECT_FALSE(ctx.GetTaskId().has_value());
    EXPECT_FALSE(ctx.GetContextId().has_value());
    EXPECT_EQ(ctx.GetConfiguration(), nullptr);
    EXPECT_EQ(ctx.GetCallContext(), nullptr);
    EXPECT_EQ(ctx.GetMetadata(), "");
    EXPECT_TRUE(ctx.GetRequestedExtensions().empty());
}

TEST_F(RequestContextTest, ConstructorWithExplicitIds_UsesProvidedIds)
{
    RequestContextParam param;
    param.request = MakeParams();
    param.taskId = "task-explicit";
    param.contextId = "ctx-explicit";

    RequestContext ctx(param);

    ASSERT_TRUE(ctx.GetTaskId().has_value());
    ASSERT_TRUE(ctx.GetContextId().has_value());
    EXPECT_EQ(*ctx.GetTaskId(), "task-explicit");
    EXPECT_EQ(*ctx.GetContextId(), "ctx-explicit");

    ASSERT_NE(ctx.GetMessage(), nullptr);
    ASSERT_TRUE(ctx.GetMessage()->taskId.has_value());
    ASSERT_TRUE(ctx.GetMessage()->contextId.has_value());
    EXPECT_EQ(*ctx.GetMessage()->taskId, "task-explicit");
    EXPECT_EQ(*ctx.GetMessage()->contextId, "ctx-explicit");
}

TEST_F(RequestContextTest, GetUserInput_UsesDefaultDelimiter)
{
    RequestContextParam param;
    param.request = MakeParams();

    RequestContext ctx(param);

    EXPECT_EQ(ctx.GetUserInput(), "hello\nworld");
}

TEST_F(RequestContextTest, GetUserInput_UsesCustomDelimiter)
{
    RequestContextParam param;
    param.request = MakeParams();

    RequestContext ctx(param);

    EXPECT_EQ(ctx.GetUserInput(" | "), "hello | world");
}

TEST_F(RequestContextTest, AttachRelatedTask_AppendsTask)
{
    RequestContextParam param;
    RequestContext ctx(param);

    auto t1 = MakeTask("task-1", "ctx-1");
    auto t2 = MakeTask("task-2", "ctx-2");

    ctx.AttachRelatedTask(t1);
    ctx.AttachRelatedTask(t2);

    ASSERT_EQ(ctx.GetRelatedTasks().size(), 2u);
    EXPECT_EQ(ctx.GetRelatedTasks()[0].id, "task-1");
    EXPECT_EQ(ctx.GetRelatedTasks()[1].id, "task-2");
}

TEST_F(RequestContextTest, GetConfiguration_WhenAbsent_ReturnsNullptr)
{
    RequestContextParam param;
    param.request = MakeParams();

    RequestContext ctx(param);

    EXPECT_EQ(ctx.GetConfiguration(), nullptr);
}

TEST_F(RequestContextTest, GetConfiguration_WhenPresent_ReturnsCopy)
{
    RequestContextParam param;
    param.request = MakeParams();

    MessageSendConfiguration cfg;
    cfg.acceptedOutputModes = {"text", "image"};
    cfg.historyLength = 3;
    cfg.returnImmediately = false;
    param.request->configuration = cfg;

    RequestContext ctx(param);

    auto out = ctx.GetConfiguration();
    ASSERT_NE(out, nullptr);
    ASSERT_TRUE(out->acceptedOutputModes.has_value());
    ASSERT_EQ(out->acceptedOutputModes->size(), 2u);
    EXPECT_EQ(out->acceptedOutputModes->at(0), "text");
    EXPECT_EQ(out->acceptedOutputModes->at(1), "image");
    ASSERT_TRUE(out->historyLength.has_value());
    EXPECT_EQ(*out->historyLength, 3);
    ASSERT_TRUE(out->returnImmediately.has_value());
    EXPECT_FALSE(*out->returnImmediately);
}

TEST_F(RequestContextTest, GetCallContext_ReturnsSamePointer)
{
    RequestContextParam param;
    param.callContext = std::make_shared<ServerCallContext>();

    RequestContext ctx(param);

    EXPECT_EQ(ctx.GetCallContext(), param.callContext);
}

TEST_F(RequestContextTest, GetMetadata_WhenAbsent_ReturnsEmptyString)
{
    RequestContextParam param;
    param.request = MakeParams();

    RequestContext ctx(param);

    EXPECT_EQ(ctx.GetMetadata(), "");
}

TEST_F(RequestContextTest, GetMetadata_WhenPresent_ReturnsValue)
{
    RequestContextParam param;
    param.request = MakeParams();
    param.request->metadata = "{\"trace\":\"1\"}";

    RequestContext ctx(param);

    EXPECT_EQ(ctx.GetMetadata(), "{\"trace\":\"1\"}");
}

TEST_F(RequestContextTest, AddActivatedExtension_WhenCallContextExists_InsertsIntoSet)
{
    RequestContextParam param;
    param.callContext = std::make_shared<ServerCallContext>();

    RequestContext ctx(param);

    ctx.AddActivatedExtension("ext://a");
    ctx.AddActivatedExtension("ext://b");
    ctx.AddActivatedExtension("ext://a");

    EXPECT_EQ(param.callContext->activatedExtensions.size(), 2u);
    EXPECT_TRUE(param.callContext->activatedExtensions.count("ext://a") > 0);
    EXPECT_TRUE(param.callContext->activatedExtensions.count("ext://b") > 0);
}

TEST_F(RequestContextTest, AddActivatedExtension_WhenCallContextMissing_DoesNothing)
{
    RequestContextParam param;
    RequestContext ctx(param);

    EXPECT_NO_THROW(ctx.AddActivatedExtension("ext://x"));
}

TEST_F(RequestContextTest, GetRequestedExtensions_WhenNoCallContext_ReturnsEmptySet)
{
    RequestContextParam param;
    RequestContext ctx(param);

    auto result = ctx.GetRequestedExtensions();

    EXPECT_TRUE(result.empty());
}

TEST_F(RequestContextTest, GetRequestedExtensions_WhenCallContextExists_ReturnsCopy)
{
    RequestContextParam param;
    param.callContext = std::make_shared<ServerCallContext>();
    param.callContext->requestedExtensions.insert("ext://1");
    param.callContext->requestedExtensions.insert("ext://2");

    RequestContext ctx(param);

    auto result = ctx.GetRequestedExtensions();

    EXPECT_EQ(result.size(), 2u);
    EXPECT_TRUE(result.count("ext://1") > 0);
    EXPECT_TRUE(result.count("ext://2") > 0);

    result.insert("ext://3");
    EXPECT_EQ(param.callContext->requestedExtensions.size(), 2u);
}

} // namespace