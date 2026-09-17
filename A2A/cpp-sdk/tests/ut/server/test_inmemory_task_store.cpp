/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "tasks/inmemory_task_store.h"
#include "types.h"

using namespace A2A;
using namespace A2A::Server;

namespace {

class InMemoryTaskStoreTest : public ::testing::Test {
protected:
    std::shared_ptr<InMemoryTaskStore> store = std::make_shared<InMemoryTaskStore>();

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
};

TEST_F(InMemoryTaskStoreTest, Get_WhenTaskNotExist_ReturnsNullopt)
{
    auto result = store->Get("task-404", nullptr);

    EXPECT_FALSE(result != nullptr);
}

TEST_F(InMemoryTaskStoreTest, SaveThenGet_ReturnsStoredTask)
{
    Task task = MakeTask("task-1", "ctx-1", TaskState::WORKING);

    store->Save(task, nullptr);
    auto result = store->Get("task-1", nullptr);

    ASSERT_TRUE(result != nullptr);
    EXPECT_EQ(result->id, "task-1");
    EXPECT_EQ(result->contextId, "ctx-1");
    EXPECT_EQ(result->status.state, TaskState::WORKING);
    ASSERT_TRUE(result->status.timestamp.has_value());
    EXPECT_EQ(*result->status.timestamp, "2026-03-17T00:00:00Z");
}

TEST_F(InMemoryTaskStoreTest, Save_WhenSameTaskIdExists_ReplacesOldTask)
{
    Task oldTask = MakeTask("task-1", "ctx-1", TaskState::WORKING);
    Task newTask = MakeTask("task-1", "ctx-2", TaskState::COMPLETED);

    store->Save(oldTask, nullptr);
    store->Save(newTask, nullptr);

    auto result = store->Get("task-1", nullptr);

    ASSERT_TRUE(result != nullptr);
    EXPECT_EQ(result->id, "task-1");
    EXPECT_EQ(result->contextId, "ctx-2");
    EXPECT_EQ(result->status.state, TaskState::COMPLETED);
}

TEST_F(InMemoryTaskStoreTest, SaveAndGet_WithHistory_PreservesHistory)
{
    Task task = MakeTask("task-1", "ctx-1", TaskState::WORKING);
    task.history = std::vector<Message>{MakeMessage("msg-1"), MakeMessage("msg-2")};

    store->Save(task, nullptr);
    auto result = store->Get("task-1", nullptr);

    ASSERT_TRUE(result != nullptr);
    ASSERT_TRUE(result->history.has_value());
    ASSERT_EQ(result->history->size(), 2u);
    EXPECT_EQ((*result->history)[0].messageId, "msg-1");
    EXPECT_EQ((*result->history)[1].messageId, "msg-2");
}

TEST_F(InMemoryTaskStoreTest, SaveAndGet_WithArtifacts_PreservesArtifacts)
{
    Task task = MakeTask("task-1", "ctx-1", TaskState::WORKING);

    Artifact artifact;
    artifact.artifactId = "artifact-1";
    artifact.parts = {MakeTextPart("artifact text")};
    task.artifacts = std::vector<Artifact>{artifact};

    store->Save(task, nullptr);
    auto result = store->Get("task-1", nullptr);

    ASSERT_TRUE(result != nullptr);
    ASSERT_TRUE(result->artifacts.has_value());
    ASSERT_EQ(result->artifacts->size(), 1u);
    EXPECT_EQ((*result->artifacts)[0].artifactId, "artifact-1");
    ASSERT_EQ((*result->artifacts)[0].parts.size(), 1u);
    ASSERT_TRUE((*result->artifacts)[0].parts[0].text.has_value());
    EXPECT_EQ(*(*result->artifacts)[0].parts[0].text, "artifact text");
}

TEST_F(InMemoryTaskStoreTest, Get_ReturnsShared_NotCopyObject)
{
    Task task = MakeTask("task-1", "ctx-1", TaskState::WORKING);

    store->Save(task, nullptr);
    auto result1 = store->Get("task-1", nullptr);
    ASSERT_TRUE(result1 != nullptr);

    result1->contextId = "changed-ctx";
    result1->status.state = TaskState::FAILED;

    auto result2 = store->Get("task-1", nullptr);
    ASSERT_TRUE(result2 != nullptr);

    EXPECT_EQ(result2->contextId, "changed-ctx");
    EXPECT_EQ(result2->status.state, TaskState::FAILED);
}

TEST_F(InMemoryTaskStoreTest, Delete_WhenTaskExists_RemovesTask)
{
    Task task = MakeTask("task-1", "ctx-1", TaskState::WORKING);

    store->Save(task, nullptr);
    ASSERT_TRUE(store->Get("task-1", nullptr) != nullptr);

    store->Delete("task-1", nullptr);

    EXPECT_FALSE(store->Get("task-1", nullptr) != nullptr);
}

TEST_F(InMemoryTaskStoreTest, Delete_WhenTaskNotExist_DoesNothing)
{
    EXPECT_NO_THROW(store->Delete("task-404", nullptr));
    EXPECT_FALSE(store->Get("task-404", nullptr) != nullptr);
}

TEST_F(InMemoryTaskStoreTest, SaveMultipleTasks_GetByIdReturnsCorrectTask)
{
    Task task1 = MakeTask("task-1", "ctx-1", TaskState::WORKING);
    Task task2 = MakeTask("task-2", "ctx-2", TaskState::COMPLETED);

    store->Save(task1, nullptr);
    store->Save(task2, nullptr);

    auto result1 = store->Get("task-1", nullptr);
    auto result2 = store->Get("task-2", nullptr);

    ASSERT_TRUE(result1 != nullptr);
    ASSERT_TRUE(result2 != nullptr);

    EXPECT_EQ(result1->id, "task-1");
    EXPECT_EQ(result1->contextId, "ctx-1");
    EXPECT_EQ(result1->status.state, TaskState::WORKING);

    EXPECT_EQ(result2->id, "task-2");
    EXPECT_EQ(result2->contextId, "ctx-2");
    EXPECT_EQ(result2->status.state, TaskState::COMPLETED);
}

} // namespace