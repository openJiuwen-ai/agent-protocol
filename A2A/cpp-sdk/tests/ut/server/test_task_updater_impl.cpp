/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "server/task_updater.h"
#include "task_updater_impl.h"
#include "server/task_store.h"
#include "server/tasks/task_manager.h"
#include "types.h"

using namespace A2A;
using namespace A2A::Server;

namespace {

class MockTaskStore : public TaskStore {
public:
    void Save(const Task& task, std::shared_ptr<ServerCallContext> context) override
    {
        store_[task.id] = std::make_shared<Task>(task);
    }

    std::shared_ptr<Task> Get(const std::string& taskId, std::shared_ptr<ServerCallContext> context) override
    {
        if (auto it = store_.find(taskId); it != store_.end()) {
            return it->second;
        }
        return nullptr;
    }

    void Delete(const std::string& taskId, std::shared_ptr<ServerCallContext> context) override
    {
        store_.erase(taskId);
    }

private:
    std::unordered_map<std::string, std::shared_ptr<Task>> store_;
};

class TaskUpdaterTest : public ::testing::Test {
protected:
    std::shared_ptr<MockTaskStore> taskStore = std::make_shared<MockTaskStore>();
    std::shared_ptr<TaskManager> taskManager = std::make_shared<TaskManager>(taskStore);

    std::vector<StreamEvent> capturedEvents;
    std::shared_ptr<TaskExecuteInfo> executeInfo = std::make_shared<TaskExecuteInfo>();

    std::unique_ptr<TaskUpdater> updater = std::make_unique<TaskUpdaterImpl>(
        "task-1", "ctx-1", taskManager);

    void SetUp() override
    {
        executeInfo->eventCb.emplace_back([this](const StreamEvent& event) {
            capturedEvents.push_back(event);
        });
        taskManager->RegisterTask("task-1", executeInfo);
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
        msg.role = Role::AGENT;
        msg.parts = {MakeTextPart("hello")};
        msg.taskId = "task-1";
        msg.contextId = "ctx-1";
        return msg;
    }

    const StreamEvent& GetLastEvent()
    {
        return capturedEvents.back();
    }
};

TEST_F(TaskUpdaterTest, UpdateStatus_TriggersStatusEventCallback)
{
    auto msg = MakeMessage("msg-status");

    updater->UpdateStatus(TaskState::WORKING, msg, std::string("2026-03-17T00:00:00Z"),
        std::string("{\"k\":\"v\"}"));

    ASSERT_EQ(capturedEvents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<TaskStatusUpdateEvent>(capturedEvents[0]));

    const auto& out = std::get<TaskStatusUpdateEvent>(capturedEvents[0]);
    EXPECT_EQ(out.taskId, "task-1");
    EXPECT_EQ(out.contextId, "ctx-1");
    ASSERT_TRUE(out.metadata.has_value());
    EXPECT_EQ(*out.metadata, "{\"k\":\"v\"}");
    ASSERT_TRUE(out.status.message.has_value());
    EXPECT_EQ(out.status.message->messageId, "msg-status");
    EXPECT_EQ(out.status.state, TaskState::WORKING);
    ASSERT_TRUE(out.status.timestamp.has_value());
    EXPECT_EQ(*out.status.timestamp, "2026-03-17T00:00:00Z");
}

TEST_F(TaskUpdaterTest, UpdateStatus_WithoutTimestamp_GeneratesTimestamp)
{
    updater->UpdateStatus(TaskState::WORKING, std::nullopt, std::nullopt, std::nullopt);

    ASSERT_EQ(capturedEvents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<TaskStatusUpdateEvent>(capturedEvents[0]));

    const auto& out = std::get<TaskStatusUpdateEvent>(capturedEvents[0]);
    ASSERT_TRUE(out.status.timestamp.has_value());
    EXPECT_FALSE(out.status.timestamp->empty());
    EXPECT_EQ(out.status.timestamp->back(), 'Z');
}

TEST_F(TaskUpdaterTest, UpdateStatus_FinalStateForcesFinalTrue)
{
    updater->UpdateStatus(TaskState::COMPLETED, std::nullopt, std::nullopt, std::nullopt);

    ASSERT_EQ(capturedEvents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<TaskStatusUpdateEvent>(capturedEvents[0]));

    const auto& out = std::get<TaskStatusUpdateEvent>(capturedEvents[0]);
    EXPECT_EQ(out.status.state, TaskState::COMPLETED);
}

TEST_F(TaskUpdaterTest, UpdateStatus_AfterTerminalState_Ignored)
{
    updater->Complete(std::nullopt);
    ClearCapturedEvents();
    EXPECT_NO_THROW(updater->UpdateStatus(TaskState::WORKING, std::nullopt, std::nullopt, std::nullopt));
    EXPECT_EQ(capturedEvents.size(), 0u);
}

TEST_F(TaskUpdaterTest, AddArtifact_WithExplicitArtifactId_TriggersArtifactEventCallback)
{
    TaskArtifactParam p;
    p.artifactId = std::string("artifact-1");
    p.parts = {MakeTextPart("artifact-text")};
    p.name = std::string("artifact-name");
    p.metadata = std::string("{\"m\":1}");
    p.append = true;
    p.lastChunk = false;

    updater->AddArtifact(p);

    ASSERT_EQ(capturedEvents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<TaskArtifactUpdateEvent>(capturedEvents[0]));

    const auto& out = std::get<TaskArtifactUpdateEvent>(capturedEvents[0]);
    EXPECT_EQ(out.taskId, "task-1");
    EXPECT_EQ(out.contextId, "ctx-1");
    EXPECT_EQ(out.artifact.artifactId, "artifact-1");
    ASSERT_EQ(out.artifact.parts.size(), 1u);
    ASSERT_TRUE(out.artifact.parts[0].text.has_value());
    EXPECT_EQ(*out.artifact.parts[0].text, "artifact-text");
    ASSERT_TRUE(out.artifact.name.has_value());
    EXPECT_EQ(*out.artifact.name, "artifact-name");
    ASSERT_TRUE(out.artifact.metadata.has_value());
    EXPECT_EQ(*out.artifact.metadata, "{\"m\":1}");
    ASSERT_TRUE(out.append.has_value());
    EXPECT_TRUE(*out.append);
    ASSERT_TRUE(out.lastChunk.has_value());
    EXPECT_FALSE(*out.lastChunk);
    ASSERT_TRUE(out.metadata.has_value());
    EXPECT_EQ(*out.metadata, "{\"m\":1}");
}

TEST_F(TaskUpdaterTest, AddArtifact_WithoutArtifactId_GeneratesArtifactId)
{
    TaskArtifactParam p;
    p.parts = {MakeTextPart("artifact-text")};

    updater->AddArtifact(p);

    ASSERT_EQ(capturedEvents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<TaskArtifactUpdateEvent>(capturedEvents[0]));

    const auto& out = std::get<TaskArtifactUpdateEvent>(capturedEvents[0]);
    EXPECT_FALSE(out.artifact.artifactId.empty());
    ASSERT_EQ(out.artifact.parts.size(), 1u);
}

TEST_F(TaskUpdaterTest, Complete_TriggersCompletedStatusCallback)
{
    auto msg = MakeMessage("msg-complete");

    updater->Complete(msg);

    ASSERT_EQ(capturedEvents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<TaskStatusUpdateEvent>(capturedEvents[0]));

    const auto& out = std::get<TaskStatusUpdateEvent>(capturedEvents[0]);
    EXPECT_EQ(out.status.state, TaskState::COMPLETED);
    ASSERT_TRUE(out.status.message.has_value());
    EXPECT_EQ(out.status.message->messageId, "msg-complete");
}

TEST_F(TaskUpdaterTest, Failed_TriggersFailedStatusCallback)
{
    updater->Failed(std::nullopt);

    ASSERT_EQ(capturedEvents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<TaskStatusUpdateEvent>(capturedEvents[0]));
    EXPECT_EQ(std::get<TaskStatusUpdateEvent>(capturedEvents[0]).status.state, TaskState::FAILED);
}

TEST_F(TaskUpdaterTest, Reject_TriggersRejectedStatusCallback)
{
    updater->Reject(std::nullopt);

    ASSERT_EQ(capturedEvents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<TaskStatusUpdateEvent>(capturedEvents[0]));
    EXPECT_EQ(std::get<TaskStatusUpdateEvent>(capturedEvents[0]).status.state, TaskState::REJECTED);
}

TEST_F(TaskUpdaterTest, Submit_TriggersSubmittedStatusCallback)
{
    updater->Submit(std::nullopt);

    ASSERT_EQ(capturedEvents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<TaskStatusUpdateEvent>(capturedEvents[0]));

    const auto& out = std::get<TaskStatusUpdateEvent>(capturedEvents[0]);
    EXPECT_EQ(out.status.state, TaskState::SUBMITTED);
}

TEST_F(TaskUpdaterTest, StartWork_TriggersWorkingStatusCallback)
{
    updater->StartWork(std::nullopt);

    ASSERT_EQ(capturedEvents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<TaskStatusUpdateEvent>(capturedEvents[0]));
    EXPECT_EQ(std::get<TaskStatusUpdateEvent>(capturedEvents[0]).status.state, TaskState::WORKING);
}

TEST_F(TaskUpdaterTest, Cancel_TriggersCanceledStatusAndBecomesTerminal)
{
    updater->Cancel(std::nullopt);

    ASSERT_EQ(capturedEvents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<TaskStatusUpdateEvent>(capturedEvents[0]));

    const auto& out = std::get<TaskStatusUpdateEvent>(capturedEvents[0]);
    EXPECT_EQ(out.status.state, TaskState::CANCELED);

    ClearCapturedEvents();
    EXPECT_NO_THROW(updater->Submit(std::nullopt));
    EXPECT_EQ(capturedEvents.size(), 0u);
}

TEST_F(TaskUpdaterTest, RequiresInput_TriggersInputRequiredStatusCallback)
{
    updater->RequiresInput(std::nullopt);

    ASSERT_EQ(capturedEvents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<TaskStatusUpdateEvent>(capturedEvents[0]));

    const auto& out = std::get<TaskStatusUpdateEvent>(capturedEvents[0]);
    EXPECT_EQ(out.status.state, TaskState::INPUT_REQUIRED);
}

TEST_F(TaskUpdaterTest, RequiresAuth_TriggersAuthRequiredStatusCallback)
{
    updater->RequiresAuth(std::nullopt);

    ASSERT_EQ(capturedEvents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<TaskStatusUpdateEvent>(capturedEvents[0]));

    const auto& out = std::get<TaskStatusUpdateEvent>(capturedEvents[0]);
    EXPECT_EQ(out.status.state, TaskState::AUTH_REQUIRED);
}

TEST_F(TaskUpdaterTest, NewAgentMessage_FillsExpectedFields)
{
    std::vector<Part> parts{MakeTextPart("agent says hi")};

    auto msg = updater->NewAgentMessage(parts, std::string("{\"trace\":\"1\"}"));

    EXPECT_EQ(msg.role, Role::AGENT);
    EXPECT_EQ(msg.parts.size(), 1u);
    ASSERT_TRUE(msg.parts[0].text.has_value());
    EXPECT_EQ(*msg.parts[0].text, "agent says hi");
    EXPECT_FALSE(msg.messageId.empty());
    ASSERT_TRUE(msg.taskId.has_value());
    EXPECT_EQ(*msg.taskId, "task-1");
    ASSERT_TRUE(msg.contextId.has_value());
    EXPECT_EQ(*msg.contextId, "ctx-1");
    ASSERT_TRUE(msg.metadata.has_value());
    EXPECT_EQ(*msg.metadata, "{\"trace\":\"1\"}");
}

TEST_F(TaskUpdaterTest, SendResponseMessage_TriggersMessageCallback)
{
    auto msg = MakeMessage("msg-response");

    updater->SendResponseMessage(msg);

    ASSERT_EQ(capturedEvents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<Message>(capturedEvents[0]));

    const auto& out = std::get<Message>(capturedEvents[0]);
    EXPECT_EQ(out.messageId, "msg-response");
    EXPECT_EQ(out.role, Role::AGENT);
}

} // namespace