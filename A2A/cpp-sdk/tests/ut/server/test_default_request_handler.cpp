/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#define private public
#define protected public
#include "server/default_request_handler.h"
#undef private
#undef protected

#include "error.h"
#include "server/agent_executor.h"
#include "server/task_store.h"
#include "tasks/push_notification_config_store.h"
#include "tasks/push_notification_sender.h"
#include "types.h"

using ::testing::_;
using ::testing::A;
using ::testing::ByMove;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::Throw;
using ::testing::IsNull;

using namespace A2A;
using namespace A2A::Server;

namespace {

class MockTaskStore : public TaskStore {
public:
    MOCK_METHOD(void, Save,
        (const Task&, std::shared_ptr<ServerCallContext>), (override));

    MOCK_METHOD(std::shared_ptr<Task>, Get,
        (const std::string&, std::shared_ptr<ServerCallContext>), (override));

    MOCK_METHOD(void, Delete,
        (const std::string&, std::shared_ptr<ServerCallContext>), (override));
};

class MockPushNotificationConfigStore : public PushNotificationConfigStore {
public:
    MOCK_METHOD(void, SetInfo,
        (const std::string&, PushNotificationConfig), (override));

    MOCK_METHOD(std::vector<PushNotificationConfig>, GetInfo,
        (const std::string&), (override));

    MOCK_METHOD(void, DeleteInfo,
        (const std::string&, const std::optional<std::string>&), (override));
};

class MockPushNotificationSender : public PushNotificationSender {
public:
    MOCK_METHOD(void, SendNotification, (const std::shared_ptr<Task>& task), (override));
};

class MockAgentExecutor : public AgentExecutor {
public:
    MOCK_METHOD(void, Execute,
        (std::shared_ptr<RequestContext>, std::shared_ptr<TaskUpdater>), (override));

    MOCK_METHOD(void, Cancel,
        (std::shared_ptr<RequestContext>, std::shared_ptr<TaskUpdater>), (override));
};

std::shared_ptr<ServerCallContext> MakeCtx()
{
    return std::make_shared<ServerCallContext>();
}

std::shared_ptr<AgentCard> MakeAgentCard()
{
    auto card = std::make_shared<AgentCard>();
    card->name = "ut-agent";
    return card;
}

Task MakeTask(const std::string& id,
    const std::string& contextId,
    TaskState state = TaskState::WORKING)
{
    Task task;
    task.id = id;
    task.contextId = contextId;

    TaskStatus status;
    status.state = state;
    task.status = status;
    return task;
}

Message MakeMessage(const std::optional<std::string>& taskId = std::nullopt,
    const std::optional<std::string>& contextId = std::nullopt)
{
    Message msg;
    msg.taskId = taskId;
    msg.contextId = contextId;
    return msg;
}

MessageSendParams MakeSendParams(const Message& msg)
{
    MessageSendParams params;
    params.message = msg;
    return params;
}

class DefaultRequestHandlerTest : public ::testing::Test {
protected:
    std::shared_ptr<NiceMock<MockAgentExecutor>> executor_ =
        std::make_shared<NiceMock<MockAgentExecutor>>();
    std::shared_ptr<NiceMock<MockTaskStore>> taskStore_ =
        std::make_shared<NiceMock<MockTaskStore>>();
    std::shared_ptr<NiceMock<MockPushNotificationConfigStore>> pushConfigStore_ =
        std::make_shared<NiceMock<MockPushNotificationConfigStore>>();
    std::shared_ptr<NiceMock<MockPushNotificationSender>> pushSender_ =
        std::make_shared<NiceMock<MockPushNotificationSender>>();
    std::shared_ptr<ServerCallContext> ctx_ = MakeCtx();

    std::unique_ptr<DefaultRequestHandler> MakeHandler(bool withExecutor = true, bool withQueue = true)
    {
        auto handler = std::make_unique<DefaultRequestHandler>(
            withExecutor ? executor_ : nullptr,
            MakeAgentCard(),
            taskStore_);

        // 依赖 #define private public
        handler->pushConfigStore_ = pushConfigStore_;
        handler->pushSender_ = pushSender_;
        return handler;
    }
};

TEST_F(DefaultRequestHandlerTest, DetermineTaskId_NoTaskId_GeneratesNewId)
{
    auto handler = MakeHandler();

    MessageSendParams params = MakeSendParams(MakeMessage(std::nullopt, std::nullopt));
    std::shared_ptr<Task> existingTask = nullptr;

    std::string taskId = handler->DetermineTaskId(params, ctx_, existingTask);

    EXPECT_TRUE(existingTask == nullptr);
    EXPECT_THAT(taskId, HasSubstr("task-"));
}

TEST_F(DefaultRequestHandlerTest, DetermineTaskId_ExistingTask_ReturnsSameId)
{
    auto handler = MakeHandler();

    Task stored = MakeTask("task-123", "ctx-1");
    EXPECT_CALL(*taskStore_, Get("task-123", _))
        .WillOnce(Return(std::make_shared<Task>(stored)));

    MessageSendParams params = MakeSendParams(MakeMessage("task-123", "ctx-1"));
    std::shared_ptr<Task> existingTask = nullptr;

    std::string taskId = handler->DetermineTaskId(params, ctx_, existingTask);

    EXPECT_EQ(taskId, "task-123");
    ASSERT_TRUE(existingTask != nullptr);
    EXPECT_EQ(existingTask->id, "task-123");
}

TEST_F(DefaultRequestHandlerTest, DetermineTaskId_ContextMismatch_Throws)
{
    auto handler = MakeHandler();

    Task stored = MakeTask("task-123", "ctx-old");
    EXPECT_CALL(*taskStore_, Get("task-123", _))
        .WillOnce(Return(std::make_shared<Task>(stored)));

    MessageSendParams params = MakeSendParams(MakeMessage("task-123", "ctx-new"));
    std::shared_ptr<Task> existingTask = nullptr;

    EXPECT_THROW(handler->DetermineTaskId(params, ctx_, existingTask), A2AServerError);
}

TEST_F(DefaultRequestHandlerTest, DetermineTaskId_TaskNotFound_Throws)
{
    auto handler = MakeHandler();

    EXPECT_CALL(*taskStore_, Get("task-404", _))
        .WillOnce(Return(nullptr));

    MessageSendParams params = MakeSendParams(MakeMessage("task-404", "ctx-1"));
    std::shared_ptr<Task> existingTask = nullptr;

    EXPECT_THROW(handler->DetermineTaskId(params, ctx_, existingTask), A2AServerError);
}

TEST_F(DefaultRequestHandlerTest, OnGetTask_TaskNotFound_Throws)
{
    auto handler = MakeHandler();

    EXPECT_CALL(*taskStore_, Get("not-found", _))
        .WillOnce(Return(nullptr));

    TaskQueryParams params;
    params.id = "not-found";

    EXPECT_THROW(handler->OnGetTask(params, ctx_), A2AServerError);
}

TEST_F(DefaultRequestHandlerTest, OnGetTask_ReturnsStoredTask)
{
    auto handler = MakeHandler();

    Task stored = MakeTask("task-1", "ctx-1");
    EXPECT_CALL(*taskStore_, Get("task-1", _))
        .WillOnce(Return(std::make_shared<Task>(stored)));

    TaskQueryParams params;
    params.id = "task-1";

    auto result = handler->OnGetTask(params, ctx_);

    EXPECT_EQ(result.id, "task-1");
    EXPECT_EQ(result.contextId, "ctx-1");
    EXPECT_EQ(result.status.state, TaskState::WORKING);
}

TEST_F(DefaultRequestHandlerTest, OnGetTask_HistoryLength_TrimsLatestMessages)
{
    auto handler = MakeHandler();

    Task stored = MakeTask("task-1", "ctx-1");
    stored.history = std::vector<Message>{
        MakeMessage(std::nullopt, "ctx-1"),
        MakeMessage(std::nullopt, "ctx-1"),
        MakeMessage(std::nullopt, "ctx-1")
    };

    EXPECT_CALL(*taskStore_, Get("task-1", _))
        .WillOnce(Return(std::make_shared<Task>(stored)));

    TaskQueryParams params;
    params.id = "task-1";
    params.historyLength = 2;

    auto result = handler->OnGetTask(params, ctx_);

    ASSERT_TRUE(result.history.has_value());
    EXPECT_EQ(result.history->size(), 2u);
}

TEST_F(DefaultRequestHandlerTest, OnCancelTask_TaskNotFound_Throws)
{
    auto handler = MakeHandler();

    EXPECT_CALL(*taskStore_, Get("task-404", _))
        .WillOnce(Return(nullptr));

    TaskIdParams params;
    params.id = "task-404";

    EXPECT_THROW(handler->OnCancelTask(params, ctx_), A2AServerError);
}

TEST_F(DefaultRequestHandlerTest, OnCancelTask_FinalState_Throws)
{
    auto handler = MakeHandler();

    Task stored = MakeTask("task-1", "ctx-1", TaskState::COMPLETED);
    EXPECT_CALL(*taskStore_, Get("task-1", _))
        .WillOnce(Return(std::make_shared<Task>(stored)));

    TaskIdParams params;
    params.id = "task-1";

    EXPECT_THROW(handler->OnCancelTask(params, ctx_), A2AServerError);
}

TEST_F(DefaultRequestHandlerTest, OnCancelTask_NoQueueOrExecutor_MarksCanceled)
{
    auto handler = MakeHandler(false, false);

    Task stored = MakeTask("task-1", "ctx-1", TaskState::WORKING);

    EXPECT_CALL(*taskStore_, Get("task-1", _))
        .WillOnce(Return(std::make_shared<Task>(stored)));

    TaskIdParams params;
    params.id = "task-1";

    auto result = handler->OnCancelTask(params, ctx_);

    EXPECT_EQ(result.status.state, TaskState::CANCELED);
}

TEST_F(DefaultRequestHandlerTest, OnSetTaskPushNotificationConfig_NoStore_Throws)
{
    auto handler = MakeHandler();
    handler->pushConfigStore_ = nullptr;

    Task stored = MakeTask("task-1", "ctx-1");
    EXPECT_CALL(*taskStore_, Get("task-1", _))
        .WillRepeatedly(Return(std::make_shared<Task>(stored)));

    TaskPushNotificationConfig cfg;
    cfg.taskId = "task-1";
    cfg.pushNotificationConfig.url = "http://callback.test";

    EXPECT_THROW(handler->OnSetTaskPushNotificationConfig(cfg, ctx_), A2AServerError);
}

TEST_F(DefaultRequestHandlerTest, OnSetTaskPushNotificationConfig_TaskNotFound_Throws)
{
    auto handler = MakeHandler();

    EXPECT_CALL(*taskStore_, Get("task-1", _))
        .WillOnce(Return(nullptr));

    TaskPushNotificationConfig cfg;
    cfg.taskId = "task-1";
    cfg.pushNotificationConfig.url = "http://callback.test";

    EXPECT_THROW(handler->OnSetTaskPushNotificationConfig(cfg, ctx_), A2AServerError);
}

TEST_F(DefaultRequestHandlerTest, OnSetTaskPushNotificationConfig_Success)
{
    auto handler = MakeHandler();

    Task stored = MakeTask("task-1", "ctx-1");
    EXPECT_CALL(*taskStore_, Get("task-1", _))
        .WillOnce(Return(std::make_shared<Task>(stored)));

    EXPECT_CALL(*pushConfigStore_, SetInfo("task-1", _))
        .Times(1);

    TaskPushNotificationConfig cfg;
    cfg.taskId = "task-1";
    cfg.pushNotificationConfig.url = "http://callback.test";

    handler->OnSetTaskPushNotificationConfig(cfg, ctx_);
}

TEST_F(DefaultRequestHandlerTest, OnGetTaskPushNotificationConfig_NoStoredConfig_ReturnsFallback)
{
    auto handler = MakeHandler();

    Task stored = MakeTask("task-1", "ctx-1");
    EXPECT_CALL(*taskStore_, Get("task-1", _))
        .WillOnce(Return(std::make_shared<Task>(stored)));

    EXPECT_CALL(*pushConfigStore_, GetInfo("task-1"))
        .WillOnce(Return(std::vector<PushNotificationConfig>{}));

    GetTaskPushNotificationConfigParams params;
    params.id = "task-1";

    auto result = handler->OnGetTaskPushNotificationConfig(params, ctx_);

    EXPECT_EQ(result.taskId, "task-1");
}

TEST_F(DefaultRequestHandlerTest, OnListTaskPushNotificationConfigs_ReturnsAllStoredConfigs)
{
    auto handler = MakeHandler();

    Task stored = MakeTask("task-1", "ctx-1");
    EXPECT_CALL(*taskStore_, Get("task-1", _))
        .WillOnce(Return(std::make_shared<Task>(stored)));

    PushNotificationConfig cfg1;
    cfg1.url = "http://cb1.test";
    PushNotificationConfig cfg2;
    cfg2.url = "http://cb2.test";

    EXPECT_CALL(*pushConfigStore_, GetInfo("task-1"))
        .WillOnce(Return(std::vector<PushNotificationConfig>{cfg1, cfg2}));

    ListTaskPushNotificationConfigParams params;
    params.id = "task-1";

    auto result = handler->OnListTaskPushNotificationConfigs(params, ctx_);

    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].pushNotificationConfig.url, "http://cb1.test");
    EXPECT_EQ(result[1].pushNotificationConfig.url, "http://cb2.test");
}

TEST_F(DefaultRequestHandlerTest, OnDeleteTaskPushNotificationConfig_Success)
{
    auto handler = MakeHandler();

    Task stored = MakeTask("task-1", "ctx-1");
    EXPECT_CALL(*taskStore_, Get("task-1", _))
        .WillOnce(Return(std::make_shared<Task>(stored)));

    EXPECT_CALL(*pushConfigStore_, DeleteInfo("task-1", _))
        .Times(1);

    DeleteTaskPushNotificationConfigParams params;
    params.id = "task-1";
    params.pushNotificationConfigId = "cfg-1";

    handler->OnDeleteTaskPushNotificationConfig(params, ctx_);
}

TEST_F(DefaultRequestHandlerTest, OnResubscribeToTask_TaskNotFound_Throws)
{
    auto handler = MakeHandler();

    EXPECT_CALL(*taskStore_, Get("task-1", _))
        .WillOnce(Return(nullptr));

    TaskIdParams params;
    params.id = "task-1";

    auto emit = [](const StreamEvent&) {};

    EXPECT_THROW(handler->OnResubscribeToTask(params, emit, ctx_), A2AServerError);
}

TEST_F(DefaultRequestHandlerTest, OnCancelTask_WithQueueAndExecutor_CallsCancelAndSavesCanceled)
{
    auto handler = MakeHandler(true, true);

    Task stored = MakeTask("task-1", "ctx-1", TaskState::WORKING);

    EXPECT_CALL(*taskStore_, Get("task-1", _))
        .WillOnce(Return(std::make_shared<Task>(stored)));

    EXPECT_CALL(*executor_, Cancel(_, _))
        .Times(1);

    TaskIdParams params;
    params.id = "task-1";

    auto result = handler->OnCancelTask(params, ctx_);

    EXPECT_EQ(result.id, "task-1");
    EXPECT_EQ(result.status.state, TaskState::CANCELED);
}

TEST_F(DefaultRequestHandlerTest, OnGetTaskPushNotificationConfig_NoStore_Throws)
{
    auto handler = MakeHandler();
    handler->pushConfigStore_ = nullptr;

    GetTaskPushNotificationConfigParams params;
    params.id = "task-1";

    EXPECT_THROW(handler->OnGetTaskPushNotificationConfig(params, ctx_), A2AServerError);
}

TEST_F(DefaultRequestHandlerTest, OnGetTaskPushNotificationConfig_TaskNotFound_Throws)
{
    auto handler = MakeHandler();

    EXPECT_CALL(*taskStore_, Get("task-1", _))
        .WillOnce(Return(nullptr));

    GetTaskPushNotificationConfigParams params;
    params.id = "task-1";

    EXPECT_THROW(handler->OnGetTaskPushNotificationConfig(params, ctx_), A2AServerError);
}

TEST_F(DefaultRequestHandlerTest, OnGetTaskPushNotificationConfig_ReturnsStoredFirstConfig)
{
    auto handler = MakeHandler();

    Task stored = MakeTask("task-1", "ctx-1");
    EXPECT_CALL(*taskStore_, Get("task-1", _))
        .WillOnce(Return(std::make_shared<Task>(stored)));

    PushNotificationConfig cfg1;
    cfg1.url = "http://first.test";
    PushNotificationConfig cfg2;
    cfg2.url = "http://second.test";

    EXPECT_CALL(*pushConfigStore_, GetInfo("task-1"))
        .WillOnce(Return(std::vector<PushNotificationConfig>{cfg1, cfg2}));

    GetTaskPushNotificationConfigParams params;
    params.id = "task-1";

    auto result = handler->OnGetTaskPushNotificationConfig(params, ctx_);

    EXPECT_EQ(result.taskId, "task-1");
    EXPECT_EQ(result.pushNotificationConfig.url, "http://first.test");
}

TEST_F(DefaultRequestHandlerTest, OnListTaskPushNotificationConfigs_NoStore_Throws)
{
    auto handler = MakeHandler();
    handler->pushConfigStore_ = nullptr;

    ListTaskPushNotificationConfigParams params;
    params.id = "task-1";

    EXPECT_THROW(handler->OnListTaskPushNotificationConfigs(params, ctx_), A2AServerError);
}

TEST_F(DefaultRequestHandlerTest, OnListTaskPushNotificationConfigs_TaskNotFound_Throws)
{
    auto handler = MakeHandler();

    EXPECT_CALL(*taskStore_, Get("task-1", _))
        .WillOnce(Return(nullptr));

    ListTaskPushNotificationConfigParams params;
    params.id = "task-1";

    EXPECT_THROW(handler->OnListTaskPushNotificationConfigs(params, ctx_), A2AServerError);
}

TEST_F(DefaultRequestHandlerTest, OnDeleteTaskPushNotificationConfig_NoStore_Throws)
{
    auto handler = MakeHandler();
    handler->pushConfigStore_ = nullptr;

    DeleteTaskPushNotificationConfigParams params;
    params.id = "task-1";
    params.pushNotificationConfigId = "cfg-1";

    EXPECT_THROW(handler->OnDeleteTaskPushNotificationConfig(params, ctx_), A2AServerError);
}

TEST_F(DefaultRequestHandlerTest, OnDeleteTaskPushNotificationConfig_TaskNotFound_Throws)
{
    auto handler = MakeHandler();

    EXPECT_CALL(*taskStore_, Get("task-1", _))
        .WillOnce(Return(nullptr));

    DeleteTaskPushNotificationConfigParams params;
    params.id = "task-1";
    params.pushNotificationConfigId = "cfg-1";

    EXPECT_THROW(handler->OnDeleteTaskPushNotificationConfig(params, ctx_), A2AServerError);
}

TEST_F(DefaultRequestHandlerTest, OnGetTask_HistoryLengthLargerThanHistory_NoTrim)
{
    auto handler = MakeHandler();

    Task stored = MakeTask("task-1", "ctx-1");
    stored.history = std::vector<Message>{
        MakeMessage(std::nullopt, "ctx-1"),
        MakeMessage(std::nullopt, "ctx-1")
    };

    EXPECT_CALL(*taskStore_, Get("task-1", _))
        .WillOnce(Return(std::make_shared<Task>(stored)));

    TaskQueryParams params;
    params.id = "task-1";
    params.historyLength = 10;

    auto result = handler->OnGetTask(params, ctx_);

    ASSERT_TRUE(result.history.has_value());
    EXPECT_EQ(result.history->size(), 2u);
}

TEST_F(DefaultRequestHandlerTest, OnGetCard_ReturnsAgentCard)
{
    auto handler = MakeHandler();
    auto card = handler->OnGetCard(ctx_);
    EXPECT_EQ(card.name, "ut-agent");
}

TEST_F(DefaultRequestHandlerTest, InitializeStreamingTask_NewTask_SavesSubmittedTask)
{
    auto handler = MakeHandler();

    MessageSendParams params = MakeSendParams(MakeMessage(std::nullopt, "ctx-1"));

    std::shared_ptr<Task> existingTask = nullptr;
    std::string taskId;

    Task saved;
    EXPECT_CALL(*taskStore_, Save(_, _))
        .WillOnce(DoAll(SaveArg<0>(&saved)));

    handler->InitializeStreamingTask(params, ctx_, existingTask, taskId);

    EXPECT_THAT(taskId, HasSubstr("task-"));
    EXPECT_EQ(saved.id, taskId);
    EXPECT_EQ(saved.contextId, "ctx-1");
    EXPECT_EQ(saved.status.state, TaskState::SUBMITTED);
}

TEST_F(DefaultRequestHandlerTest, InitializeStreamingTask_ExistingTask_UpdatesTask)
{
    auto handler = MakeHandler();

    Task existing = MakeTask("task-1", "ctx-1", TaskState::INPUT_REQUIRED);
    MessageSendParams params = MakeSendParams(MakeMessage("task-1", "ctx-1"));

    std::shared_ptr<Task> existingTask = nullptr;
    std::string taskId;

    EXPECT_CALL(*taskStore_, Get("task-1", _))
        .WillOnce(Return(std::make_shared<Task>(existing)));

    EXPECT_CALL(*taskStore_, Save(_, _))
        .Times(1);

    handler->InitializeStreamingTask(params, ctx_, existingTask, taskId);

    ASSERT_TRUE(existingTask != nullptr);
    EXPECT_EQ(existingTask->id, "task-1");
    EXPECT_EQ(taskId, "task-1");
}

// ========== GetRelatedTasksFromReferenceTaskIds Tests ==========
TEST_F(DefaultRequestHandlerTest, GetRelatedTasksFromReferenceTaskIds_NoReferenceTaskIds_ReturnsEmpty)
{
    auto handler = MakeHandler();

    Message msg = MakeMessage(std::nullopt, "ctx-1");
    msg.referenceTaskIds = std::nullopt;
    MessageSendParams params = MakeSendParams(msg);

    auto result = handler->GetRelatedTasksFromReferenceTaskIds(params, ctx_);

    EXPECT_TRUE(result.empty());
}

TEST_F(DefaultRequestHandlerTest, GetRelatedTasksFromReferenceTaskIds_EmptyReferenceTaskIds_ReturnsEmpty)
{
    auto handler = MakeHandler();

    Message msg = MakeMessage(std::nullopt, "ctx-1");
    msg.referenceTaskIds = std::vector<std::string>{};
    MessageSendParams params = MakeSendParams(msg);

    auto result = handler->GetRelatedTasksFromReferenceTaskIds(params, ctx_);

    EXPECT_TRUE(result.empty());
}

TEST_F(DefaultRequestHandlerTest, GetRelatedTasksFromReferenceTaskIds_SingleValidTaskId_ReturnsTask)
{
    auto handler = MakeHandler();

    Task referencedTask = MakeTask("ref-task-1", "ctx-1");

    EXPECT_CALL(*taskStore_, Get("ref-task-1", _))
        .WillOnce(Return(std::make_shared<Task>(referencedTask)));

    Message msg = MakeMessage(std::nullopt, "ctx-1");
    msg.referenceTaskIds = std::vector<std::string>{"ref-task-1"};
    MessageSendParams params = MakeSendParams(msg);

    auto result = handler->GetRelatedTasksFromReferenceTaskIds(params, ctx_);

    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].id, "ref-task-1");
    EXPECT_EQ(result[0].contextId, "ctx-1");
}

TEST_F(DefaultRequestHandlerTest, GetRelatedTasksFromReferenceTaskIds_MultipleValidTaskIds_ReturnsAllTasks)
{
    auto handler = MakeHandler();

    Task task1 = MakeTask("ref-task-1", "ctx-1");
    Task task2 = MakeTask("ref-task-2", "ctx-1");
    Task task3 = MakeTask("ref-task-3", "ctx-2");

    EXPECT_CALL(*taskStore_, Get("ref-task-1", _))
        .WillOnce(Return(std::make_shared<Task>(task1)));
    EXPECT_CALL(*taskStore_, Get("ref-task-2", _))
        .WillOnce(Return(std::make_shared<Task>(task2)));
    EXPECT_CALL(*taskStore_, Get("ref-task-3", _))
        .WillOnce(Return(std::make_shared<Task>(task3)));

    Message msg = MakeMessage(std::nullopt, "ctx-1");
    msg.referenceTaskIds = std::vector<std::string>{"ref-task-1", "ref-task-2", "ref-task-3"};
    MessageSendParams params = MakeSendParams(msg);

    auto result = handler->GetRelatedTasksFromReferenceTaskIds(params, ctx_);

    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0].id, "ref-task-1");
    EXPECT_EQ(result[1].id, "ref-task-2");
    EXPECT_EQ(result[2].id, "ref-task-3");
}

TEST_F(DefaultRequestHandlerTest, GetRelatedTasksFromReferenceTaskIds_WithEmptyTaskIds_SkipsEmptyStrings)
{
    auto handler = MakeHandler();

    Task validTask = MakeTask("valid-task", "ctx-1");

    EXPECT_CALL(*taskStore_, Get("valid-task", _))
        .WillOnce(Return(std::make_shared<Task>(validTask)));

    Message msg = MakeMessage(std::nullopt, "ctx-1");
    msg.referenceTaskIds = std::vector<std::string>{"", "valid-task", ""};
    MessageSendParams params = MakeSendParams(msg);

    auto result = handler->GetRelatedTasksFromReferenceTaskIds(params, ctx_);

    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].id, "valid-task");
}

TEST_F(DefaultRequestHandlerTest, GetRelatedTasksFromReferenceTaskIds_TaskNotFound_SkipsAndReturnsEmpty)
{
    auto handler = MakeHandler();

    EXPECT_CALL(*taskStore_, Get("non-existent-task", _))
        .WillOnce(Return(nullptr));

    Message msg = MakeMessage(std::nullopt, "ctx-1");
    msg.referenceTaskIds = std::vector<std::string>{"non-existent-task"};
    MessageSendParams params = MakeSendParams(msg);

    auto result = handler->GetRelatedTasksFromReferenceTaskIds(params, ctx_);

    EXPECT_TRUE(result.empty());
}

TEST_F(DefaultRequestHandlerTest,
    GetRelatedTasksFromReferenceTaskIds_MultipleTasksOneNotFound_SkipsAndReturnsValidTasks)
{
    auto handler = MakeHandler();

    Task validTask1 = MakeTask("valid-task-1", "ctx-1");
    Task validTask2 = MakeTask("valid-task-2", "ctx-1");

    EXPECT_CALL(*taskStore_, Get("valid-task-1", _)).WillOnce(Return(std::make_shared<Task>(validTask1)));
    EXPECT_CALL(*taskStore_, Get("not-found-task", _)).WillOnce(Return(nullptr));
    EXPECT_CALL(*taskStore_, Get("valid-task-2", _)).WillOnce(Return(std::make_shared<Task>(validTask2)));

    Message msg = MakeMessage(std::nullopt, "ctx-1");
    msg.referenceTaskIds = std::vector<std::string>{"valid-task-1", "not-found-task", "valid-task-2"};
    MessageSendParams params = MakeSendParams(msg);

    auto result = handler->GetRelatedTasksFromReferenceTaskIds(params, ctx_);

    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].id, "valid-task-1");
    EXPECT_EQ(result[1].id, "valid-task-2");
}

TEST_F(DefaultRequestHandlerTest, GetRelatedTasksFromReferenceTaskIds_DifferentContexts_StillReturnsTasks)
{
    auto handler = MakeHandler();

    Task taskFromDifferentContext = MakeTask("ref-task-diff", "different-context");

    EXPECT_CALL(*taskStore_, Get("ref-task-diff", _))
        .WillOnce(Return(std::make_shared<Task>(taskFromDifferentContext)));

    Message msg = MakeMessage(std::nullopt, "current-context");
    msg.referenceTaskIds = std::vector<std::string>{"ref-task-diff"};
    MessageSendParams params = MakeSendParams(msg);

    auto result = handler->GetRelatedTasksFromReferenceTaskIds(params, ctx_);

    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].id, "ref-task-diff");
    EXPECT_EQ(result[0].contextId, "different-context");
}

TEST_F(DefaultRequestHandlerTest, GetRelatedTasksFromReferenceTaskIds_WithCompletedTask_ReturnsCompletedTask)
{
    auto handler = MakeHandler();

    Task completedTask = MakeTask("completed-task", "ctx-1", TaskState::COMPLETED);

    EXPECT_CALL(*taskStore_, Get("completed-task", _))
        .WillOnce(Return(std::make_shared<Task>(completedTask)));

    Message msg = MakeMessage(std::nullopt, "ctx-1");
    msg.referenceTaskIds = std::vector<std::string>{"completed-task"};
    MessageSendParams params = MakeSendParams(msg);

    auto result = handler->GetRelatedTasksFromReferenceTaskIds(params, ctx_);

    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].id, "completed-task");
    EXPECT_EQ(result[0].status.state, TaskState::COMPLETED);
}

TEST_F(DefaultRequestHandlerTest, GetRelatedTasksFromReferenceTaskIds_WithFailedTask_ReturnsFailedTask)
{
    auto handler = MakeHandler();

    Task failedTask = MakeTask("failed-task", "ctx-1", TaskState::FAILED);

    EXPECT_CALL(*taskStore_, Get("failed-task", _))
        .WillOnce(Return(std::make_shared<Task>(failedTask)));

    Message msg = MakeMessage(std::nullopt, "ctx-1");
    msg.referenceTaskIds = std::vector<std::string>{"failed-task"};
    MessageSendParams params = MakeSendParams(msg);

    auto result = handler->GetRelatedTasksFromReferenceTaskIds(params, ctx_);

    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].id, "failed-task");
    EXPECT_EQ(result[0].status.state, TaskState::FAILED);
}

TEST_F(DefaultRequestHandlerTest, GetRelatedTasksFromReferenceTaskIds_PreservesOrder)
{
    auto handler = MakeHandler();

    Task task1 = MakeTask("task-1", "ctx-1");
    Task task2 = MakeTask("task-2", "ctx-1");
    Task task3 = MakeTask("task-3", "ctx-1");

    // 按顺序设置期望
    EXPECT_CALL(*taskStore_, Get("task-1", _))
        .WillOnce(Return(std::make_shared<Task>(task1)));
    EXPECT_CALL(*taskStore_, Get("task-3", _))
        .WillOnce(Return(std::make_shared<Task>(task3)));
    EXPECT_CALL(*taskStore_, Get("task-2", _))
        .WillOnce(Return(std::make_shared<Task>(task2)));

    Message msg = MakeMessage(std::nullopt, "ctx-1");
    msg.referenceTaskIds = std::vector<std::string>{"task-1", "task-3", "task-2"};
    MessageSendParams params = MakeSendParams(msg);

    auto result = handler->GetRelatedTasksFromReferenceTaskIds(params, ctx_);

    ASSERT_EQ(result.size(), 3u);
    // 验证顺序与输入一致
    EXPECT_EQ(result[0].id, "task-1");
    EXPECT_EQ(result[1].id, "task-3");
    EXPECT_EQ(result[2].id, "task-2");
}

TEST_F(DefaultRequestHandlerTest, GetRelatedTasksFromReferenceTaskIds_WithTaskStoreException_PropagatesException)
{
    auto handler = MakeHandler();

    EXPECT_CALL(*taskStore_, Get("task-that-throws", _))
        .WillOnce(Throw(std::runtime_error("Task store error")));

    Message msg = MakeMessage(std::nullopt, "ctx-1");
    msg.referenceTaskIds = std::vector<std::string>{"task-that-throws"};
    MessageSendParams params = MakeSendParams(msg);

    EXPECT_THROW(handler->GetRelatedTasksFromReferenceTaskIds(params, ctx_), std::runtime_error);
}

} // namespace