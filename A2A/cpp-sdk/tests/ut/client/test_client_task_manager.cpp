/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <variant>
#include <stdexcept>

#include "client/client_task_manager.h"
#include "types.h"

namespace A2A::Client::Test {

using namespace A2A;
using namespace A2A::Client;
using ::testing::_;
using ::testing::Return;
using ::testing::NiceMock;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::SizeIs;
using ::testing::Optional;
using ::testing::Field;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Create a basic Part with text content.
static Part MakeTestPart(const std::string& text = "test content", const std::string& mediaType = "text/plain")
{
    Part part;
    part.text = text;
    part.mediaType = mediaType;
    return part;
}

// Create a basic Message with minimal required fields.
static Message MakeTestMessage(
    const std::string& messageId = "msg-001",
    Role role = Role::AGENT,
    const std::string& text = "test message",
    const std::optional<std::string>& taskId = std::nullopt,
    const std::optional<std::string>& contextId = std::nullopt)
{
    Message msg;
    msg.messageId = messageId;
    msg.role = role;
    msg.parts = {MakeTestPart(text)};
    msg.taskId = taskId;
    msg.contextId = contextId;
    return msg;
}

// Create a basic Task with minimal required fields.
static Task MakeTestTask(const std::string& id = "task-001", const std::string& contextId = "ctx-001")
{
    Task task;
    task.id = id;
    task.contextId = contextId;
    task.status = TaskStatus{.message = std::nullopt, .state = TaskState::SUBMITTED, .timestamp = std::nullopt};
    return task;
}

// Create a basic TaskStatusUpdateEvent.
static TaskStatusUpdateEvent MakeStatusEvent(
    const std::string& taskId = "task-001",
    const std::string& contextId = "ctx-001",
    TaskState state = TaskState::COMPLETED,
    std::optional<Message> message = std::nullopt,
    std::optional<std::string> metadata = std::nullopt)
{
    TaskStatusUpdateEvent event;
    event.taskId = taskId;
    event.contextId = contextId;
    event.status = TaskStatus{.message = message, .state = state, .timestamp = std::nullopt};
    event.metadata = metadata;
    return event;
}

// Create a basic TaskArtifactUpdateEvent.
static TaskArtifactUpdateEvent MakeArtifactEvent(
    const std::string& taskId = "task-001",
    const std::string& contextId = "ctx-001",
    const std::string& artifactId = "artifact-001",
    std::vector<Part> parts = {},
    std::optional<bool> append = std::nullopt)
{
    TaskArtifactUpdateEvent event;
    event.taskId = taskId;
    event.contextId = contextId;
    event.artifact = Artifact{
        .artifactId = artifactId,
        .description = "test description",
        .extensions = std::nullopt,
        .metadata = std::nullopt,
        .name = "test-artifact",
        .parts = std::move(parts)
    };
    event.append = append;
    return event;
}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class ClientTaskManagerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        manager = std::make_unique<ClientTaskManager>();
    }

    std::unique_ptr<ClientTaskManager> manager;
};

// ===========================================================================
// Part I – GetTaskOrRaise
// ===========================================================================

TEST_F(ClientTaskManagerTest, GetTaskOrRaise_NoTask_Throws)
{
    EXPECT_THROW(manager->GetTaskOrRaise(), std::runtime_error);
}

TEST_F(ClientTaskManagerTest, GetTaskOrRaise_TaskSet_ReturnsReference)
{
    Task task = MakeTestTask();
    manager->SaveTaskEvent(task);

    Task& ref = manager->GetTaskOrRaise();

    EXPECT_EQ(ref.id, "task-001");
    EXPECT_EQ(ref.contextId, "ctx-001");

    ref.id = "modified-id";
    EXPECT_EQ(manager->GetTaskOrRaise().id, "modified-id");
}

// ===========================================================================
// Part II – SaveTaskEvent with Task variant
// ===========================================================================

TEST_F(ClientTaskManagerTest, SaveTaskEvent_Task_EmptyManager_SetsTask)
{
    Task task = MakeTestTask("new-task", "new-ctx");
    task.status = TaskStatus{
        .message = MakeTestMessage("msg-001", Role::AGENT, "working..."),
        .state = TaskState::WORKING,
        .timestamp = std::nullopt};

    manager->SaveTaskEvent(task);

    Task& current = manager->GetTaskOrRaise();
    EXPECT_EQ(current.id, "new-task");
    EXPECT_EQ(current.contextId, "new-ctx");
    EXPECT_EQ(current.status.state, TaskState::WORKING);
    ASSERT_TRUE(current.status.message.has_value());
    EXPECT_EQ(current.status.message->messageId, "msg-001");
}

TEST_F(ClientTaskManagerTest, SaveTaskEvent_Task_AlreadySet_Throws)
{
    manager->SaveTaskEvent(MakeTestTask());

    Task newTask = MakeTestTask("another-task");

    EXPECT_THROW(manager->SaveTaskEvent(newTask), std::runtime_error);

    EXPECT_EQ(manager->GetTaskOrRaise().id, "task-001");
}

// ===========================================================================
// Part III – SaveTaskEvent with TaskStatusUpdateEvent
// ===========================================================================

TEST_F(ClientTaskManagerTest, SaveTaskEvent_StatusEvent_NoTask_CreatesShell)
{
    auto event = MakeStatusEvent("shell-task", "shell-ctx", TaskState::COMPLETED);

    manager->SaveTaskEvent(event);

    Task& task = manager->GetTaskOrRaise();
    EXPECT_EQ(task.id, "shell-task");
    EXPECT_EQ(task.contextId, "shell-ctx");
    EXPECT_EQ(task.status.state, TaskState::COMPLETED);
    EXPECT_FALSE(task.artifacts.has_value());
}

TEST_F(ClientTaskManagerTest, SaveTaskEvent_StatusEvent_ExistingTask_UpdatesStatus)
{
    manager->SaveTaskEvent(MakeTestTask());

    auto statusMsg = MakeTestMessage("status-msg", Role::AGENT, "All done!");
    auto event = MakeStatusEvent(
        "task-001", "ctx-001",
        TaskState::COMPLETED,
        statusMsg,
        "metadata-v2");

    manager->SaveTaskEvent(event);

    Task& updated = manager->GetTaskOrRaise();
    EXPECT_EQ(updated.status.state, TaskState::COMPLETED);
    ASSERT_TRUE(updated.status.message.has_value());
    EXPECT_EQ(updated.status.message->messageId, "status-msg");
    EXPECT_EQ(updated.metadata, "metadata-v2");
}

TEST_F(ClientTaskManagerTest, SaveTaskEvent_StatusEvent_WithHistory_MovesMessageToHistory)
{
    Task task = MakeTestTask();
    task.status = TaskStatus{.message = MakeTestMessage("msg-step1", Role::AGENT, "step 1"),
        .state = TaskState::WORKING, .timestamp = std::nullopt};
    task.history = std::vector<Message>{MakeTestMessage("msg-init", Role::USER, "initial")};

    manager->SaveTaskEvent(task);

    auto event = MakeStatusEvent("task-001", "ctx-001", TaskState::WORKING,
        MakeTestMessage("msg-step2", Role::AGENT, "step 2"));
    manager->SaveTaskEvent(event);

    Task& updated = manager->GetTaskOrRaise();

    ASSERT_TRUE(updated.history.has_value());
    EXPECT_THAT(*updated.history, SizeIs(2));
    EXPECT_EQ((*updated.history)[0].messageId, "msg-init");
    EXPECT_EQ((*updated.history)[1].messageId, "msg-step2");

    ASSERT_TRUE(updated.status.message.has_value());
    EXPECT_EQ(updated.status.message->messageId, "msg-step2");
}

TEST_F(ClientTaskManagerTest, SaveTaskEvent_StatusEvent_NoHistory_CreatesHistory)
{
    Task task = MakeTestTask();

    task.status = TaskStatus{.message = MakeTestMessage("msg-first", Role::AGENT, "first message"),
        .state = TaskState::WORKING, .timestamp = std::nullopt};
    manager->SaveTaskEvent(task);

    auto event = MakeStatusEvent("task-001", "ctx-001", TaskState::WORKING,
        MakeTestMessage("msg-second", Role::AGENT, "second message"));
    manager->SaveTaskEvent(event);

    Task& updated = manager->GetTaskOrRaise();

    ASSERT_TRUE(updated.history.has_value());
    EXPECT_THAT(*updated.history, SizeIs(1));
    EXPECT_EQ((*updated.history)[0].messageId, "msg-second");
}

TEST_F(ClientTaskManagerTest, SaveTaskEvent_StatusEvent_Metadata_UpdatesOnlyIfEmpty)
{
    Task task = MakeTestTask();
    task.metadata = "original-meta";
    manager->SaveTaskEvent(task);

    auto event = MakeStatusEvent("task-001", "ctx-001", TaskState::WORKING, std::nullopt, "new-meta");
    manager->SaveTaskEvent(event);

    EXPECT_EQ(manager->GetTaskOrRaise().metadata, "original-meta");
}

TEST_F(ClientTaskManagerTest, SaveTaskEvent_StatusEvent_Metadata_SetsIfEmpty)
{
    Task task = MakeTestTask();
    manager->SaveTaskEvent(task);

    auto event = MakeStatusEvent("task-001", "ctx-001", TaskState::WORKING, std::nullopt, "first-meta");
    manager->SaveTaskEvent(event);

    EXPECT_EQ(manager->GetTaskOrRaise().metadata, "first-meta");
}

// ===========================================================================
// Part IV – SaveTaskEvent with TaskArtifactUpdateEvent
// ===========================================================================

TEST_F(ClientTaskManagerTest, SaveTaskEvent_ArtifactEvent_NoTask_CreatesShell)
{
    auto event = MakeArtifactEvent("artifact-task", "artifact-ctx", "art-001", {MakeTestPart("content")});

    manager->SaveTaskEvent(event);

    Task& task = manager->GetTaskOrRaise();
    EXPECT_EQ(task.id, "artifact-task");
    EXPECT_EQ(task.contextId, "artifact-ctx");
    ASSERT_TRUE(task.artifacts.has_value());
    EXPECT_THAT(*task.artifacts, SizeIs(1));
    EXPECT_EQ((*task.artifacts)[0].artifactId, "art-001");
}

TEST_F(ClientTaskManagerTest, SaveTaskEvent_ArtifactEvent_Replace_ExistingArtifact)
{
    Task task = MakeTestTask();
    task.artifacts = std::vector<Artifact>{
        Artifact{
            .artifactId = "art-001",
            .description = std::nullopt,
            .extensions = std::nullopt,
            .metadata = std::nullopt,
            .name = "old-name",
            .parts = {MakeTestPart("old")}}
    };
    manager->SaveTaskEvent(task);

    auto event = MakeArtifactEvent(
        "task-001", "ctx-001", "art-001",
        {MakeTestPart("new")},
        false);
    manager->SaveTaskEvent(event);

    Task& updated = manager->GetTaskOrRaise();
    ASSERT_TRUE(updated.artifacts.has_value());
    EXPECT_THAT(*updated.artifacts, SizeIs(1));
    ASSERT_FALSE((*updated.artifacts)[0].parts.empty());
    EXPECT_EQ((*updated.artifacts)[0].parts[0].text, "new");
}

TEST_F(ClientTaskManagerTest, SaveTaskEvent_ArtifactEvent_Replace_NewArtifact)
{
    Task task = MakeTestTask();
    task.artifacts = std::vector<Artifact>{
        Artifact{
            .artifactId = "art-001",
            .description = std::nullopt,
            .extensions = std::nullopt,
            .metadata = std::nullopt,
            .name = "existing",
            .parts = {}}
    };
    manager->SaveTaskEvent(task);

    auto event = MakeArtifactEvent(
        "task-001", "ctx-001", "art-002",
        {MakeTestPart("new-artifact")},
        false);
    manager->SaveTaskEvent(event);

    Task& updated = manager->GetTaskOrRaise();
    ASSERT_TRUE(updated.artifacts.has_value());
    EXPECT_THAT(*updated.artifacts, SizeIs(2));

    auto it = std::find_if(updated.artifacts->begin(), updated.artifacts->end(),
        [](const Artifact& a) { return a.artifactId == "art-002"; });
    ASSERT_NE(it, updated.artifacts->end());
    ASSERT_FALSE(it->parts.empty());
    EXPECT_EQ(it->parts[0].text, "new-artifact");
}

TEST_F(ClientTaskManagerTest, SaveTaskEvent_ArtifactEvent_Append_ExistingArtifact)
{
    Task task = MakeTestTask();
    task.artifacts = std::vector<Artifact>{
        Artifact{
            .artifactId = "art-001",
            .description = std::nullopt,
            .extensions = std::nullopt,
            .metadata = std::nullopt,
            .name = "test",
            .parts = {MakeTestPart("part1")}}
    };
    manager->SaveTaskEvent(task);

    auto event = MakeArtifactEvent(
        "task-001", "ctx-001", "art-001",
        {MakeTestPart("part2"), MakeTestPart("part3")},
        true);
    manager->SaveTaskEvent(event);

    Task& updated = manager->GetTaskOrRaise();
    ASSERT_TRUE(updated.artifacts.has_value());
    EXPECT_THAT(*updated.artifacts, SizeIs(1));
    EXPECT_THAT((*updated.artifacts)[0].parts, SizeIs(3));
    EXPECT_EQ((*updated.artifacts)[0].parts[0].text, "part1");
    EXPECT_EQ((*updated.artifacts)[0].parts[1].text, "part2");
    EXPECT_EQ((*updated.artifacts)[0].parts[2].text, "part3");
}

TEST_F(ClientTaskManagerTest, SaveTaskEvent_ArtifactEvent_Append_NewArtifact)
{
    Task task = MakeTestTask();
    task.artifacts = std::vector<Artifact>{};
    manager->SaveTaskEvent(task);

    auto event = MakeArtifactEvent(
        "task-001", "ctx-001", "art-new",
        {MakeTestPart("first-part")},
        true);
    manager->SaveTaskEvent(event);

    Task& updated = manager->GetTaskOrRaise();
    ASSERT_TRUE(updated.artifacts.has_value());
    EXPECT_THAT(*updated.artifacts, SizeIs(1));
    EXPECT_EQ((*updated.artifacts)[0].artifactId, "art-new");
    ASSERT_FALSE((*updated.artifacts)[0].parts.empty());
    EXPECT_EQ((*updated.artifacts)[0].parts[0].text, "first-part");
}

TEST_F(ClientTaskManagerTest, SaveTaskEvent_ArtifactEvent_NullArtifacts_InitializesVector)
{
    Task task = MakeTestTask();
    manager->SaveTaskEvent(task);

    auto event = MakeArtifactEvent("task-001", "ctx-001", "art-001", {MakeTestPart("content")});
    manager->SaveTaskEvent(event);

    Task& updated = manager->GetTaskOrRaise();
    ASSERT_TRUE(updated.artifacts.has_value());
    EXPECT_THAT(*updated.artifacts, SizeIs(1));
}

// ===========================================================================
// Part V – UpdateWithMessage
// ===========================================================================
TEST_F(ClientTaskManagerTest, UpdateWithMessage_WithStatusMessage_MovesToHistory)
{
    Task task = MakeTestTask();
    task.status = TaskStatus{.message = MakeTestMessage("msg-pending", Role::AGENT, "pending message"),
        .state = TaskState::WORKING, .timestamp = std::nullopt};

    Message newMsg = MakeTestMessage("msg-user", Role::USER, "user input");

    Task result = manager->UpdateWithMessage(newMsg, task);

    ASSERT_TRUE(result.history.has_value());
    EXPECT_THAT(*result.history, SizeIs(2));
    EXPECT_EQ((*result.history)[0].messageId, "msg-pending");
    EXPECT_EQ((*result.history)[1].messageId, "msg-user");

    EXPECT_EQ(manager->GetTaskOrRaise().id, task.id);
}

TEST_F(ClientTaskManagerTest, UpdateWithMessage_NoHistory_CreatesHistory)
{
    Task task = MakeTestTask();
    Message newMsg = MakeTestMessage("msg-first", Role::USER, "first message");

    Task result = manager->UpdateWithMessage(newMsg, task);

    ASSERT_TRUE(result.history.has_value());
    EXPECT_THAT(*result.history, SizeIs(1));
    EXPECT_EQ((*result.history)[0].messageId, "msg-first");
}

TEST_F(ClientTaskManagerTest, UpdateWithMessage_WithHistory_Appends)
{
    Task task = MakeTestTask();
    task.history = std::vector<Message>{
        MakeTestMessage("msg-1", Role::USER, "msg1"),
        MakeTestMessage("msg-2", Role::AGENT, "msg2")
    };

    Message newMsg = MakeTestMessage("msg-3", Role::USER, "msg3");

    Task result = manager->UpdateWithMessage(newMsg, task);

    ASSERT_TRUE(result.history.has_value());
    EXPECT_THAT(*result.history, SizeIs(3));
    EXPECT_EQ((*result.history)[2].messageId, "msg-3");
}

TEST_F(ClientTaskManagerTest, UpdateWithMessage_UpdatesManagerState)
{
    Task task = MakeTestTask("original");
    manager->SaveTaskEvent(task);

    Task modifiedTask = MakeTestTask("modified");
    modifiedTask.status = TaskStatus{.message = std::nullopt, .state = TaskState::COMPLETED, .timestamp = std::nullopt};

    manager->UpdateWithMessage(MakeTestMessage("msg-test", Role::USER, "test"), modifiedTask);

    Task& current = manager->GetTaskOrRaise();
    EXPECT_EQ(current.id, "modified");
    EXPECT_EQ(current.status.state, TaskState::COMPLETED);
}

// ===========================================================================
// Part VI – SaveTask (via public SaveTaskEvent)
// ===========================================================================

TEST_F(ClientTaskManagerTest, SaveTask_SetsCurrentTask)
{
    Task task = MakeTestTask("saved-task", "saved-ctx");
    task.status = TaskStatus{.message = std::nullopt, .state = TaskState::COMPLETED, .timestamp = std::nullopt};

    manager->SaveTaskEvent(task);

    Task& current = manager->GetTaskOrRaise();
    EXPECT_EQ(current.id, "saved-task");
    EXPECT_EQ(current.contextId, "saved-ctx");
    EXPECT_EQ(current.status.state, TaskState::COMPLETED);
}

TEST_F(ClientTaskManagerTest, SaveTask_Overwrite_Throws)
{
    manager->SaveTaskEvent(MakeTestTask("original"));

    Task newTask = MakeTestTask("replacement");
    newTask.metadata = "new-meta";

    EXPECT_THROW(manager->SaveTaskEvent(newTask), std::runtime_error);

    // Check the original task remains the same
    Task& current = manager->GetTaskOrRaise();
    EXPECT_EQ(current.id, "original");
    EXPECT_EQ(current.metadata, std::nullopt);
}

// ===========================================================================
// Part VII – Integration scenarios
// ===========================================================================

TEST_F(ClientTaskManagerTest, FullWorkflow_CompleteLifecycle)
{
    Task initialTask = MakeTestTask("workflow-task");
    manager->SaveTaskEvent(initialTask);

    auto statusEvent = MakeStatusEvent("workflow-task", "ctx-001", TaskState::WORKING,
        MakeTestMessage("msg-proc", Role::AGENT, "Processing..."));
    manager->SaveTaskEvent(statusEvent);

    auto artifactEvent1 = MakeArtifactEvent(
        "workflow-task", "ctx-001", "output.txt",
        {MakeTestPart("Line 1\n")},
        true);
    manager->SaveTaskEvent(artifactEvent1);

    auto artifactEvent2 = MakeArtifactEvent(
        "workflow-task", "ctx-001", "output.txt",
        {MakeTestPart("Line 2\n")},
        true);
    manager->SaveTaskEvent(artifactEvent2);

    auto finalStatus = MakeStatusEvent("workflow-task", "ctx-001", TaskState::COMPLETED,
        MakeTestMessage("msg-done", Role::AGENT, "Done!"));
    manager->SaveTaskEvent(finalStatus);

    Message userMsg = MakeTestMessage("msg-thanks", Role::USER, "Thanks!");
    Task& current = manager->GetTaskOrRaise();
    manager->UpdateWithMessage(userMsg, current);

    Task& result = manager->GetTaskOrRaise();
    EXPECT_EQ(result.id, "workflow-task");
    EXPECT_EQ(result.status.state, TaskState::COMPLETED);

    ASSERT_TRUE(result.status.message.has_value());
    EXPECT_EQ(result.status.message->messageId, "msg-done");

    ASSERT_TRUE(result.artifacts.has_value());
    EXPECT_THAT(*result.artifacts, SizeIs(1));
    EXPECT_EQ((*result.artifacts)[0].artifactId, "output.txt");
    EXPECT_THAT((*result.artifacts)[0].parts, SizeIs(2));
    EXPECT_EQ((*result.artifacts)[0].parts[0].text, "Line 1\n");
    EXPECT_EQ((*result.artifacts)[0].parts[1].text, "Line 2\n");

    ASSERT_TRUE(result.history.has_value());
    EXPECT_THAT(*result.history, SizeIs(4));
    if (result.history) {
        EXPECT_EQ((*result.history)[0].messageId, "msg-proc");
        EXPECT_EQ((*result.history)[1].messageId, "msg-done");
        EXPECT_EQ((*result.history)[2].messageId, "msg-done");
        EXPECT_EQ((*result.history)[3].messageId, "msg-thanks");
    }
}

TEST_F(ClientTaskManagerTest, MultipleStatusEvents_HistoryManagement)
{
    manager->SaveTaskEvent(
        MakeStatusEvent("t1", "c1", TaskState::SUBMITTED, MakeTestMessage("m1", Role::AGENT, "Step 1")));
    manager->SaveTaskEvent(
        MakeStatusEvent("t1", "c1", TaskState::WORKING, MakeTestMessage("m2", Role::AGENT, "Step 2")));
    manager->SaveTaskEvent(
        MakeStatusEvent("t1", "c1", TaskState::COMPLETED, MakeTestMessage("m3", Role::AGENT, "Step 3")));

    Task& task = manager->GetTaskOrRaise();
    ASSERT_TRUE(task.history.has_value());
    EXPECT_THAT(*task.history, SizeIs(3));
    EXPECT_EQ((*task.history)[0].messageId, "m1");
    EXPECT_EQ((*task.history)[1].messageId, "m2");
    EXPECT_EQ((*task.history)[2].messageId, "m3");
}

TEST_F(ClientTaskManagerTest, MultipleArtifacts_DifferentIds_SeparateEntries)
{
    manager->SaveTaskEvent(MakeTestTask());

    manager->SaveTaskEvent(MakeArtifactEvent("t1", "c1", "art-A", {MakeTestPart("A")}));
    manager->SaveTaskEvent(MakeArtifactEvent("t1", "c1", "art-B", {MakeTestPart("B")}));
    manager->SaveTaskEvent(MakeArtifactEvent("t1", "c1", "art-C", {MakeTestPart("C")}));

    Task& task = manager->GetTaskOrRaise();
    ASSERT_TRUE(task.artifacts.has_value());
    EXPECT_THAT(*task.artifacts, SizeIs(3));

    std::vector<std::string> ids;
    for (const auto& art : *task.artifacts) {
        ids.push_back(art.artifactId);
    }
    EXPECT_THAT(ids, ::testing::UnorderedElementsAre("art-A", "art-B", "art-C"));
}

// ===========================================================================
// Part VIII – Edge cases
// ===========================================================================

TEST_F(ClientTaskManagerTest, EdgeCase_EmptyStringIds_Accepted)
{
    Task task;
    task.id = "";
    task.contextId = "";
    task.status = TaskStatus{.message = std::nullopt, .state = TaskState::SUBMITTED, .timestamp = std::nullopt};

    EXPECT_NO_THROW(manager->SaveTaskEvent(task));
    EXPECT_EQ(manager->GetTaskOrRaise().id, "");
}

TEST_F(ClientTaskManagerTest, EdgeCase_NullOptionals_DoesNotCrash)
{
    TaskStatusUpdateEvent event;
    event.taskId = "t1";
    event.contextId = "c1";
    event.status = TaskStatus{.message = std::nullopt, .state = TaskState::WORKING, .timestamp = std::nullopt};

    EXPECT_NO_THROW(manager->SaveTaskEvent(event));

    Task& task = manager->GetTaskOrRaise();
    EXPECT_FALSE(task.status.message.has_value());
    EXPECT_FALSE(task.metadata.has_value());
}

TEST_F(ClientTaskManagerTest, EdgeCase_EmptyMessage_UpdatesHistory)
{
    Task task = MakeTestTask();
    Message emptyMsg;
    emptyMsg.messageId = "empty-msg";

    Task result = manager->UpdateWithMessage(emptyMsg, task);

    ASSERT_TRUE(result.history.has_value());
    EXPECT_THAT(*result.history, SizeIs(1));
    EXPECT_EQ((*result.history)[0].messageId, "empty-msg");
}

TEST_F(ClientTaskManagerTest, EdgeCase_MinimalTask_GetTaskOrRaiseWorks)
{
    Task minimalTask;
    minimalTask.id = "minimal";
    minimalTask.contextId = "ctx";
    minimalTask.status = TaskStatus{.message = std::nullopt, .state = TaskState::SUBMITTED, .timestamp = std::nullopt};

    manager->SaveTaskEvent(minimalTask);

    EXPECT_NO_THROW({
        Task& ref = manager->GetTaskOrRaise();
        EXPECT_EQ(ref.id, "minimal");
    });
}

TEST_F(ClientTaskManagerTest, EdgeCase_EmptyArtifactParts_Handled)
{
    Task task = MakeTestTask();
    manager->SaveTaskEvent(task);

    auto event = MakeArtifactEvent("t1", "c1", "empty-art", {});
    EXPECT_NO_THROW(manager->SaveTaskEvent(event));

    Task& updated = manager->GetTaskOrRaise();
    ASSERT_TRUE(updated.artifacts.has_value());
    EXPECT_THAT(*updated.artifacts, SizeIs(1));
    EXPECT_TRUE((*updated.artifacts)[0].parts.empty());
}

TEST_F(ClientTaskManagerTest, EdgeCase_HistoryOrder_Preserved)
{
    Task task = MakeTestTask();
    task.history = std::vector<Message>{
        MakeTestMessage("m1", Role::USER, "first"),
        MakeTestMessage("m2", Role::AGENT, "second")
    };
    manager->SaveTaskEvent(task);

    manager->SaveTaskEvent(
        MakeStatusEvent("task-001", "ctx-001", TaskState::WORKING, MakeTestMessage("m3", Role::AGENT, "third")));

    Task& updated = manager->GetTaskOrRaise();
    ASSERT_TRUE(updated.history.has_value());
    EXPECT_THAT(*updated.history, SizeIs(3));
    EXPECT_EQ((*updated.history)[0].messageId, "m1");
    EXPECT_EQ((*updated.history)[1].messageId, "m2");
    EXPECT_EQ((*updated.history)[2].messageId, "m3");
}

}