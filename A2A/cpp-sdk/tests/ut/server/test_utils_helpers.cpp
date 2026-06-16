/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#include "types.h"
#include "utils_helpers.h"

using namespace A2A;
using namespace A2A::Server;

namespace {

class UtilsHelpersTest : public ::testing::Test {
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
        msg.parts = {MakeTextPart("hello")};
        return msg;
    }

    static TaskArtifactUpdateEvent MakeArtifactEvent(const std::string& artifactId,
                                                    const std::vector<Part>& parts,
                                                    std::optional<bool> append = std::nullopt)
    {
        TaskArtifactUpdateEvent ev;
        ev.taskId = "task-1";
        ev.contextId = "ctx-1";
        ev.append = append;

        Artifact artifact;
        artifact.artifactId = artifactId;
        artifact.parts = parts;
        ev.artifact = artifact;
        return ev;
    }
};

TEST_F(UtilsHelpersTest, CreateTaskObj_WhenMessageHasNoContextId_GeneratesContextId)
{
    MessageSendParams params;
    params.message = MakeMessage("msg-1");

    auto task = CreateTaskObj(params);

    EXPECT_FALSE(task.id.empty());
    ASSERT_TRUE(params.message.contextId.has_value());
    EXPECT_FALSE(params.message.contextId->empty());
    EXPECT_EQ(task.contextId, *params.message.contextId);
    EXPECT_EQ(task.status.state, TaskState::SUBMITTED);

    ASSERT_TRUE(task.history.has_value());
    ASSERT_EQ(task.history->size(), 1u);
    EXPECT_EQ((*task.history)[0].messageId, "msg-1");
}

TEST_F(UtilsHelpersTest, CreateTaskObj_WhenMessageAlreadyHasContextId_ReusesIt)
{
    MessageSendParams params;
    params.message = MakeMessage("msg-2");
    params.message.contextId = "ctx-fixed";

    auto task = CreateTaskObj(params);

    EXPECT_FALSE(task.id.empty());
    ASSERT_TRUE(params.message.contextId.has_value());
    EXPECT_EQ(*params.message.contextId, "ctx-fixed");
    EXPECT_EQ(task.contextId, "ctx-fixed");
    EXPECT_EQ(task.status.state, TaskState::SUBMITTED);

    ASSERT_TRUE(task.history.has_value());
    ASSERT_EQ(task.history->size(), 1u);
    EXPECT_EQ((*task.history)[0].messageId, "msg-2");
}

TEST_F(UtilsHelpersTest, AppendArtifactToTask_WhenTaskHasNoArtifacts_CreatesListAndAddsArtifact)
{
    Task task;
    task.id = "task-1";
    task.contextId = "ctx-1";
    task.status.state = TaskState::WORKING;

    auto ev = MakeArtifactEvent("artifact-1", {MakeTextPart("p1")}, false);

    AppendArtifactToTask(task, ev);

    ASSERT_TRUE(task.artifacts.has_value());
    ASSERT_EQ(task.artifacts->size(), 1u);
    EXPECT_EQ((*task.artifacts)[0].artifactId, "artifact-1");
    ASSERT_EQ((*task.artifacts)[0].parts.size(), 1u);
    ASSERT_TRUE((*task.artifacts)[0].parts[0].text.has_value());
    EXPECT_EQ(*(*task.artifacts)[0].parts[0].text, "p1");
}

TEST_F(UtilsHelpersTest, AppendArtifactToTask_WhenAppendFalseAndArtifactExists_ReplacesArtifact)
{
    Task task;
    task.id = "task-1";
    task.contextId = "ctx-1";
    task.status.state = TaskState::WORKING;

    Artifact oldArtifact;
    oldArtifact.artifactId = "artifact-1";
    oldArtifact.parts = {MakeTextPart("old")};
    task.artifacts = std::vector<Artifact>{oldArtifact};

    auto ev = MakeArtifactEvent("artifact-1", {MakeTextPart("new")}, false);

    AppendArtifactToTask(task, ev);

    ASSERT_TRUE(task.artifacts.has_value());
    ASSERT_EQ(task.artifacts->size(), 1u);
    EXPECT_EQ((*task.artifacts)[0].artifactId, "artifact-1");
    ASSERT_EQ((*task.artifacts)[0].parts.size(), 1u);
    ASSERT_TRUE((*task.artifacts)[0].parts[0].text.has_value());
    EXPECT_EQ(*(*task.artifacts)[0].parts[0].text, "new");
}

TEST_F(UtilsHelpersTest, AppendArtifactToTask_WhenAppendFalseAndArtifactNotExists_AddsArtifact)
{
    Task task;
    task.id = "task-1";
    task.contextId = "ctx-1";
    task.status.state = TaskState::WORKING;
    task.artifacts = std::vector<Artifact>{};

    auto ev = MakeArtifactEvent("artifact-2", {MakeTextPart("content")}, false);

    AppendArtifactToTask(task, ev);

    ASSERT_TRUE(task.artifacts.has_value());
    ASSERT_EQ(task.artifacts->size(), 1u);
    EXPECT_EQ((*task.artifacts)[0].artifactId, "artifact-2");
}

TEST_F(UtilsHelpersTest, AppendArtifactToTask_WhenAppendTrueAndArtifactExists_AppendsParts)
{
    Task task;
    task.id = "task-1";
    task.contextId = "ctx-1";
    task.status.state = TaskState::WORKING;

    Artifact artifact;
    artifact.artifactId = "artifact-1";
    artifact.parts = {MakeTextPart("p1")};
    task.artifacts = std::vector<Artifact>{artifact};

    auto ev = MakeArtifactEvent("artifact-1", {MakeTextPart("p2"), MakeTextPart("p3")}, true);

    AppendArtifactToTask(task, ev);

    ASSERT_TRUE(task.artifacts.has_value());
    ASSERT_EQ(task.artifacts->size(), 1u);
    ASSERT_EQ((*task.artifacts)[0].parts.size(), 3u);
    EXPECT_EQ(*(*task.artifacts)[0].parts[0].text, "p1");
    EXPECT_EQ(*(*task.artifacts)[0].parts[1].text, "p2");
    EXPECT_EQ(*(*task.artifacts)[0].parts[2].text, "p3");
}

TEST_F(UtilsHelpersTest, AppendArtifactToTask_WhenAppendTrueAndArtifactNotExists_IgnoresChunk)
{
    Task task;
    task.id = "task-1";
    task.contextId = "ctx-1";
    task.status.state = TaskState::WORKING;
    task.artifacts = std::vector<Artifact>{};

    auto ev = MakeArtifactEvent("artifact-missing", {MakeTextPart("chunk")}, true);

    AppendArtifactToTask(task, ev);

    ASSERT_TRUE(task.artifacts.has_value());
    EXPECT_TRUE(task.artifacts->empty());
}

TEST_F(UtilsHelpersTest, AppendArtifactToTask_WhenAppendMissing_TreatedAsFalse)
{
    Task task;
    task.id = "task-1";
    task.contextId = "ctx-1";
    task.status.state = TaskState::WORKING;
    task.artifacts = std::vector<Artifact>{};

    auto ev = MakeArtifactEvent("artifact-1", {MakeTextPart("content")}, std::nullopt);

    AppendArtifactToTask(task, ev);

    ASSERT_TRUE(task.artifacts.has_value());
    ASSERT_EQ(task.artifacts->size(), 1u);
    EXPECT_EQ((*task.artifacts)[0].artifactId, "artifact-1");
}

TEST_F(UtilsHelpersTest, AreModalitiesCompatible_WhenClientModesMissing_ReturnsTrue)
{
    std::optional<std::vector<std::string>> server = std::vector<std::string>{"text", "image"};
    std::optional<std::vector<std::string>> client = std::nullopt;

    EXPECT_TRUE(AreModalitiesCompatible(server, client));
}

TEST_F(UtilsHelpersTest, AreModalitiesCompatible_WhenClientModesEmpty_ReturnsTrue)
{
    std::optional<std::vector<std::string>> server = std::vector<std::string>{"text"};
    std::optional<std::vector<std::string>> client = std::vector<std::string>{};

    EXPECT_TRUE(AreModalitiesCompatible(server, client));
}

TEST_F(UtilsHelpersTest, AreModalitiesCompatible_WhenServerModesMissing_ReturnsTrue)
{
    std::optional<std::vector<std::string>> server = std::nullopt;
    std::optional<std::vector<std::string>> client = std::vector<std::string>{"text"};

    EXPECT_TRUE(AreModalitiesCompatible(server, client));
}

TEST_F(UtilsHelpersTest, AreModalitiesCompatible_WhenServerModesEmpty_ReturnsTrue)
{
    std::optional<std::vector<std::string>> server = std::vector<std::string>{};
    std::optional<std::vector<std::string>> client = std::vector<std::string>{"text"};

    EXPECT_TRUE(AreModalitiesCompatible(server, client));
}

TEST_F(UtilsHelpersTest, AreModalitiesCompatible_WhenThereIsOverlap_ReturnsTrue)
{
    std::optional<std::vector<std::string>> server = std::vector<std::string>{"image", "text"};
    std::optional<std::vector<std::string>> client = std::vector<std::string>{"audio", "text"};

    EXPECT_TRUE(AreModalitiesCompatible(server, client));
}

TEST_F(UtilsHelpersTest, AreModalitiesCompatible_WhenNoOverlap_ReturnsFalse)
{
    std::optional<std::vector<std::string>> server = std::vector<std::string>{"image", "video"};
    std::optional<std::vector<std::string>> client = std::vector<std::string>{"audio", "text"};

    EXPECT_FALSE(AreModalitiesCompatible(server, client));
}

TEST_F(UtilsHelpersTest, BuildTextArtifact_SetsTextPartAndArtifactId)
{
    auto artifact = BuildTextArtifact("hello text", "artifact-text-1");

    EXPECT_EQ(artifact.artifactId, "artifact-text-1");
    ASSERT_EQ(artifact.parts.size(), 1u);
    ASSERT_TRUE(artifact.parts[0].text.has_value());
    EXPECT_EQ(*artifact.parts[0].text, "hello text");
    ASSERT_TRUE(artifact.parts[0].mediaType.has_value());
    EXPECT_EQ(*artifact.parts[0].mediaType, "text/plain");
}

TEST(UtilsHelpersStateTest, IsFinal_RecognizesTerminalStates)
{
    EXPECT_TRUE(IsFinal(TaskState::COMPLETED));
    EXPECT_TRUE(IsFinal(TaskState::CANCELED));
    EXPECT_TRUE(IsFinal(TaskState::FAILED));
    EXPECT_TRUE(IsFinal(TaskState::REJECTED));
    EXPECT_FALSE(IsFinal(TaskState::WORKING));
}

TEST(UtilsHelpersStateTest, IsInterrupted_RecognizesInteractiveStates)
{
    EXPECT_TRUE(IsInterrupted(TaskState::INPUT_REQUIRED));
    EXPECT_TRUE(IsInterrupted(TaskState::AUTH_REQUIRED));
    EXPECT_FALSE(IsInterrupted(TaskState::SUBMITTED));
}

TEST(UtilsHelpersStateTest, IsFinalOrInterrupted_CombinesBoth)
{
    EXPECT_TRUE(IsFinalOrInterrupted(TaskState::COMPLETED));
    EXPECT_TRUE(IsFinalOrInterrupted(TaskState::INPUT_REQUIRED));
    EXPECT_FALSE(IsFinalOrInterrupted(TaskState::WORKING));
}

TEST(UtilsHelpersStateTest, ValidateOrThrow_ThrowsWhenExpressionFalse)
{
    EXPECT_NO_THROW(ValidateOrThrow(true, "ok"));
    EXPECT_THROW(ValidateOrThrow(false, "bad input"), A2AServerError);
}

TEST(UtilsHelpersStateTest, MakeError_BuildsJsonRpcErrorObject)
{
    auto err = MakeError("req-1", -32001, "task not found");
    EXPECT_EQ(err["jsonrpc"].get<std::string>(), "2.0");
    EXPECT_EQ(err["id"].get<std::string>(), "req-1");
    EXPECT_EQ(err["error"]["code"].get<int>(), -32001);
    EXPECT_EQ(err["error"]["message"].get<std::string>(), "task not found");
}

TEST(UtilsHelpersStateTest, IsFinalEvent_MessageIsFinal)
{
    Message msg;
    msg.messageId = "m1";
    msg.role = Role::USER;
    StreamEvent ev = msg;
    EXPECT_TRUE(IsFinalEvent(ev));
}

TEST(UtilsHelpersStateTest, IsFinalEvent_CompletedTaskIsFinal)
{
    Task task;
    task.id = "t1";
    task.contextId = "c1";
    task.status.state = TaskState::COMPLETED;
    StreamEvent ev = task;
    EXPECT_TRUE(IsFinalEvent(ev));
}

TEST(UtilsHelpersStateTest, IsFinalEvent_ArtifactUpdateIsNotFinal)
{
    TaskArtifactUpdateEvent ev;
    ev.taskId = "t1";
    ev.contextId = "c1";
    ev.artifact.artifactId = "a1";
    StreamEvent event = ev;
    EXPECT_FALSE(IsFinalEvent(event));
}

} // namespace