/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#define private public
#define protected public
#include "tasks/task_manager.h"
#undef private
#undef protected

#include "error.h"
#include "tasks/inmemory_task_store.h"
#include "types.h"

using namespace A2A;
using namespace A2A::Server;

namespace {

class TaskManagerTest : public ::testing::Test {
protected:
    std::shared_ptr<TaskStore> taskStore = std::make_shared<InMemoryTaskStore>();

    std::vector<StreamEvent> capturedEvents;

    std::shared_ptr<TaskExecuteInfo> MakeExecuteInfo()
    {
        auto info = std::make_shared<TaskExecuteInfo>();
        info->eventCb.emplace_back([this](const StreamEvent& event) {
            capturedEvents.push_back(event);
        });
        return info;
    }

    void ClearCapturedEvents()
    {
        capturedEvents.clear();
    }

    static Part MakeTextPart(const std::string& text = "hello")
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
        msg.contextId = "ctx-1";
        msg.taskId = "task-1";
        return msg;
    }

    static Task MakeTask(const std::string& id = "task-1",
                        const std::string& contextId = "ctx-1",
                        TaskState state = TaskState::WORKING)
    {
        Task task;
        task.id = id;
        task.contextId = contextId;
        task.status.state = state;
        task.status.timestamp = "2026-03-17T00:00:00Z";
        return task;
    }

    static TaskStatusUpdateEvent MakeStatusEvent(const std::string& taskId = "task-1",
                                                const std::string& contextId = "ctx-1",
                                                TaskState state = TaskState::WORKING)
    {
        TaskStatusUpdateEvent ev;
        ev.taskId = taskId;
        ev.contextId = contextId;
        ev.status.state = state;
        ev.status.timestamp = "2026-03-17T00:00:00Z";
        return ev;
    }

    static TaskArtifactUpdateEvent MakeArtifactEvent(const std::string& taskId = "task-1",
                                                    const std::string& contextId = "ctx-1",
                                                    const std::string& artifactId = "artifact-1")
    {
        TaskArtifactUpdateEvent ev;
        ev.taskId = taskId;
        ev.contextId = contextId;

        Artifact artifact;
        artifact.artifactId = artifactId;
        artifact.parts = {MakeTextPart("artifact text")};
        ev.artifact = artifact;

        return ev;
    }
};

TEST_F(TaskManagerTest, Constructor_WithValidTaskStore_Succeeds)
{
    EXPECT_NO_THROW(TaskManager mgr(taskStore));
}

TEST_F(TaskManagerTest, RegisterTask_WithEmptyTaskId_Throws)
{
    TaskManager mgr(taskStore);
    auto info = MakeExecuteInfo();

    EXPECT_THROW(mgr.RegisterTask("", info), A2AServerError);
}

TEST_F(TaskManagerTest, RegisterTask_WithValidTaskId_Succeeds)
{
    TaskManager mgr(taskStore);
    auto info = MakeExecuteInfo();

    EXPECT_NO_THROW(mgr.RegisterTask("task-1", info));
}

TEST_F(TaskManagerTest, GetTask_WhenNotRegistered_ReturnsNullptr)
{
    TaskManager mgr(taskStore);

    auto result = mgr.GetTask("task-not-exist");

    EXPECT_EQ(result, nullptr);
}

TEST_F(TaskManagerTest, GetTask_WhenRegistered_ReturnsTaskFromStore)
{
    Task task = MakeTask("task-1", "ctx-1", TaskState::WORKING);
    taskStore->Save(task, nullptr);

    TaskManager mgr(taskStore);
    mgr.RegisterTask("task-1", MakeExecuteInfo());

    auto result = mgr.GetTask("task-1");

    ASSERT_TRUE(result != nullptr);
    EXPECT_EQ(result->id, "task-1");
    EXPECT_EQ(result->contextId, "ctx-1");
    EXPECT_EQ(result->status.state, TaskState::WORKING);
}

TEST_F(TaskManagerTest, GetContextId_WhenTaskNotFound_ReturnsEmpty)
{
    TaskManager mgr(taskStore);

    auto result = mgr.GetContextId("task-not-exist");

    EXPECT_EQ(result, "");
}

TEST_F(TaskManagerTest, GetContextId_WhenTaskExists_ReturnsContextId)
{
    Task task = MakeTask("task-1", "ctx-abc", TaskState::WORKING);
    taskStore->Save(task, nullptr);

    TaskManager mgr(taskStore);
    mgr.RegisterTask("task-1", MakeExecuteInfo());

    auto result = mgr.GetContextId("task-1");

    EXPECT_EQ(result, "ctx-abc");
}

TEST_F(TaskManagerTest, SaveTaskContextId_WithMatchingTaskEvent_Succeeds)
{
    TaskManager mgr(taskStore);
    mgr.RegisterTask("task-1", MakeExecuteInfo());
    EventType ev = MakeTask("task-1", "ctx-1", TaskState::WORKING);

    EXPECT_NO_THROW(mgr.SaveTaskContextId(ev));
}

TEST_F(TaskManagerTest, SaveTaskEvent_WithTask_SavesTaskDirectly)
{
    TaskManager mgr(taskStore);
    mgr.RegisterTask("task-1", MakeExecuteInfo());
    Task task = MakeTask("task-1", "ctx-1", TaskState::COMPLETED);

    mgr.SaveTaskEvent(EventType{task});

    auto stored = taskStore->Get("task-1", nullptr);
    ASSERT_TRUE(stored != nullptr);
    EXPECT_EQ(stored->id, "task-1");
    EXPECT_EQ(stored->status.state, TaskState::COMPLETED);
}

TEST_F(TaskManagerTest, SaveTaskEvent_WithStatusEventWithoutExistingTask_CreatesTask)
{
    TaskManager mgr(taskStore);
    mgr.RegisterTask("task-1", MakeExecuteInfo());
    TaskStatusUpdateEvent ev = MakeStatusEvent("task-1", "ctx-1", TaskState::WORKING);
    ev.metadata = std::string("{\"a\":1}");

    mgr.SaveTaskEvent(EventType{ev});

    auto stored = taskStore->Get("task-1", nullptr);
    ASSERT_TRUE(stored != nullptr);
    EXPECT_EQ(stored->id, "task-1");
    EXPECT_EQ(stored->contextId, "ctx-1");
    EXPECT_EQ(stored->status.state, TaskState::WORKING);
    ASSERT_TRUE(stored->metadata.has_value());
    EXPECT_EQ(*stored->metadata, "{\"a\":1}");
}

TEST_F(TaskManagerTest, SaveTaskEvent_WithStatusEvent_MergesMetadata)
{
    Task existing = MakeTask("task-1", "ctx-1", TaskState::WORKING);
    existing.metadata = std::string("{\"a\":1}");
    taskStore->Save(existing, nullptr);

    TaskManager mgr(taskStore);
    mgr.RegisterTask("task-1", MakeExecuteInfo());

    TaskStatusUpdateEvent ev = MakeStatusEvent("task-1", "ctx-1", TaskState::COMPLETED);
    ev.metadata = std::string("{\"b\":2}");

    mgr.SaveTaskEvent(EventType{ev});

    auto stored = taskStore->Get("task-1", nullptr);
    ASSERT_TRUE(stored != nullptr);
    ASSERT_TRUE(stored->metadata.has_value());
    EXPECT_NE(stored->metadata->find("\"a\":1"), std::string::npos);
    EXPECT_NE(stored->metadata->find("\"b\":2"), std::string::npos);
    EXPECT_EQ(stored->status.state, TaskState::COMPLETED);
}

TEST_F(TaskManagerTest, SaveTaskEvent_WithStatusEvent_MovesStatusMessageIntoHistory)
{
    Task existing = MakeTask("task-1", "ctx-1", TaskState::WORKING);
    Message msg = MakeMessage("msg-status");
    existing.status.message = msg;
    taskStore->Save(existing, nullptr);

    TaskManager mgr(taskStore);
    mgr.RegisterTask("task-1", MakeExecuteInfo());

    TaskStatusUpdateEvent ev = MakeStatusEvent("task-1", "ctx-1", TaskState::COMPLETED);

    mgr.SaveTaskEvent(EventType{ev});

    auto stored = taskStore->Get("task-1", nullptr);
    ASSERT_TRUE(stored != nullptr);
    ASSERT_TRUE(stored->history.has_value());
    ASSERT_EQ(stored->history->size(), 1u);
    EXPECT_EQ((*stored->history)[0].messageId, "msg-status");
    EXPECT_EQ(stored->status.state, TaskState::COMPLETED);
}

TEST_F(TaskManagerTest, SaveTaskEvent_WithArtifactEvent_AppendsArtifact)
{
    TaskManager mgr(taskStore);
    mgr.RegisterTask("task-1", MakeExecuteInfo());
    TaskArtifactUpdateEvent ev = MakeArtifactEvent("task-1", "ctx-1", "artifact-1");

    mgr.SaveTaskEvent(EventType{ev});

    auto stored = taskStore->Get("task-1", nullptr);
    ASSERT_TRUE(stored != nullptr);
    ASSERT_TRUE(stored->artifacts.has_value());
    ASSERT_EQ(stored->artifacts->size(), 1u);
    EXPECT_EQ((*stored->artifacts)[0].artifactId, "artifact-1");
}

TEST_F(TaskManagerTest, EnsureTask_WhenStoredTaskExists_LoadsStoredTask)
{
    Task existing = MakeTask("task-1", "ctx-1", TaskState::WORKING);
    taskStore->Save(existing, nullptr);

    TaskManager mgr(taskStore);
    mgr.RegisterTask("task-1", MakeExecuteInfo());

    auto result = mgr.EnsureTask(std::variant<TaskStatusUpdateEvent, TaskArtifactUpdateEvent>{
        MakeStatusEvent("task-1", "ctx-1", TaskState::COMPLETED)});

    EXPECT_EQ(result.id, "task-1");
    EXPECT_EQ(result.status.state, TaskState::WORKING);
}

TEST_F(TaskManagerTest, EnsureTask_WhenNoStoredTask_CreatesFromStatusEvent)
{
    TaskManager mgr(taskStore);
    mgr.RegisterTask("task-new", MakeExecuteInfo());

    auto result = mgr.EnsureTask(std::variant<TaskStatusUpdateEvent, TaskArtifactUpdateEvent>{
        MakeStatusEvent("task-new", "ctx-new", TaskState::WORKING)});

    EXPECT_EQ(result.id, "task-new");
    EXPECT_EQ(result.contextId, "ctx-new");
    EXPECT_EQ(result.status.state, TaskState::SUBMITTED);
}

TEST_F(TaskManagerTest, Process_WithStatusEvent_CallsCallbackAndSavesTask)
{
    TaskManager mgr(taskStore);
    mgr.RegisterTask("task-1", MakeExecuteInfo());
    TaskStatusUpdateEvent ev = MakeStatusEvent("task-1", "ctx-1", TaskState::WORKING);

    mgr.Process("task-1", ev);

    ASSERT_EQ(capturedEvents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<TaskStatusUpdateEvent>(capturedEvents[0]));

    auto stored = taskStore->Get("task-1", nullptr);
    ASSERT_TRUE(stored != nullptr);
    EXPECT_EQ(stored->status.state, TaskState::WORKING);
}

TEST_F(TaskManagerTest, Process_WithArtifactEvent_CallsCallbackAndSavesTask)
{
    TaskManager mgr(taskStore);
    mgr.RegisterTask("task-1", MakeExecuteInfo());
    TaskArtifactUpdateEvent ev = MakeArtifactEvent("task-1", "ctx-1", "artifact-1");

    mgr.Process("task-1", ev);

    ASSERT_EQ(capturedEvents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<TaskArtifactUpdateEvent>(capturedEvents[0]));
}

TEST_F(TaskManagerTest, Process_WithMessage_CallsCallback)
{
    TaskManager mgr(taskStore);
    mgr.RegisterTask("task-1", MakeExecuteInfo());
    Message msg = MakeMessage("msg-1");

    mgr.Process("task-1", msg);

    ASSERT_EQ(capturedEvents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<Message>(capturedEvents[0]));
    EXPECT_EQ(std::get<Message>(capturedEvents[0]).messageId, "msg-1");
}

TEST_F(TaskManagerTest, Process_WhenCallbackNull_DoesNotCallCallback)
{
    auto info = std::make_shared<TaskExecuteInfo>();
    info->eventCb = {};

    TaskManager mgr(taskStore);
    mgr.RegisterTask("task-1", info);
    TaskStatusUpdateEvent ev = MakeStatusEvent("task-1", "ctx-1", TaskState::WORKING);

    EXPECT_NO_THROW(mgr.Process("task-1", ev));
    EXPECT_EQ(capturedEvents.size(), 0u);
}

TEST_F(TaskManagerTest, EnsureTaskForEvent_WhenStoreEmpty_CreatesTaskFromStatusEvent)
{
    TaskManager mgr(taskStore);
    mgr.RegisterTask("task-a", MakeExecuteInfo());
    EventType ev = MakeStatusEvent("task-a", "ctx-a", TaskState::WORKING);

    auto task = mgr.EnsureTaskForEvent(ev);

    EXPECT_EQ(task.id, "task-a");
    EXPECT_EQ(task.contextId, "ctx-a");
    EXPECT_EQ(task.status.state, TaskState::SUBMITTED);
}

TEST_F(TaskManagerTest, EnsureTaskForEvent_WhenStoreEmpty_CreatesTaskFromArtifactEvent)
{
    TaskManager mgr(taskStore);
    mgr.RegisterTask("task-a", MakeExecuteInfo());
    EventType ev = MakeArtifactEvent("task-a", "ctx-a", "artifact-a");

    auto task = mgr.EnsureTaskForEvent(ev);

    EXPECT_EQ(task.id, "task-a");
    EXPECT_EQ(task.contextId, "ctx-a");
    EXPECT_EQ(task.status.state, TaskState::SUBMITTED);
}

TEST_F(TaskManagerTest, SaveTask_SavesTaskToStore)
{
    TaskManager mgr(taskStore);
    mgr.RegisterTask("task-1", MakeExecuteInfo());
    Task task = MakeTask("task-1", "ctx-1", TaskState::WORKING);

    mgr.SaveTask(task);

    auto stored = taskStore->Get("task-1", nullptr);
    ASSERT_TRUE(stored != nullptr);
    EXPECT_EQ(stored->id, "task-1");
    EXPECT_EQ(stored->status.state, TaskState::WORKING);
}

TEST_F(TaskManagerTest, UpdateWithMessage_WhenStatusMessageExists_MovesItAndAppendsNewMessage)
{
    TaskManager mgr(taskStore);
    mgr.RegisterTask("task-1", MakeExecuteInfo());
    Task task = MakeTask("task-1", "ctx-1", TaskState::WORKING);
    task.status.message = MakeMessage("msg-status");
    Message newMsg = MakeMessage("msg-new");

    auto result = mgr.UpdateWithMessage(newMsg, task);

    ASSERT_TRUE(result.history.has_value());
    ASSERT_EQ(result.history->size(), 2u);
    EXPECT_EQ(result.history.value()[0].messageId, "msg-status");
    EXPECT_EQ(result.history.value()[1].messageId, "msg-new");
    EXPECT_FALSE(result.status.message.has_value());
}

TEST_F(TaskManagerTest, UpdateWithMessage_WhenHistoryEmpty_CreatesHistory)
{
    TaskManager mgr(taskStore);
    mgr.RegisterTask("task-1", MakeExecuteInfo());
    Task task = MakeTask("task-1", "ctx-1", TaskState::WORKING);
    Message newMsg = MakeMessage("msg-only");

    auto result = mgr.UpdateWithMessage(newMsg, task);

    ASSERT_TRUE(result.history.has_value());
    ASSERT_EQ(result.history->size(), 1u);
    EXPECT_EQ(result.history.value()[0].messageId, "msg-only");
}

TEST_F(TaskManagerTest, SetEventCallback_UpdatesCallback)
{
    std::vector<StreamEvent> newEvents;
    TaskManager mgr(taskStore);
    mgr.RegisterTask("task-1", MakeExecuteInfo());

    mgr.AddEventCallback("task-1", [&newEvents](const StreamEvent& event) {
        newEvents.push_back(event);
    });

    TaskStatusUpdateEvent ev = MakeStatusEvent("task-1", "ctx-1", TaskState::WORKING);
    mgr.Process("task-1", ev);

    EXPECT_EQ(newEvents.size(), 1u);
    EXPECT_EQ(capturedEvents.size(), 1u);
}

TEST_F(TaskManagerTest, ExchangeMessageSent_SetsAndReturnsPreviousValue)
{
    auto info = std::make_shared<TaskExecuteInfo>();
    info->messageSent = false;

    TaskManager mgr(taskStore);
    mgr.RegisterTask("task-1", info);

    EXPECT_FALSE(mgr.ExchangeMessageSent("task-1", true));
    EXPECT_TRUE(mgr.ExchangeMessageSent("task-1", false));
}

} // namespace