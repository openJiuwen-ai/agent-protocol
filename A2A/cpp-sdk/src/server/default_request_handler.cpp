/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <future>
#include <stdexcept>
#include <thread>
#include <utility>

#include "common_types.h"
#include "server/request_context.h"
#include "server/agent_executor.h"
#include "tasks/push_notification_config_store.h"
#include "tasks/push_notification_sender.h"
#include "tasks/task_manager.h"
#include "server/task_store.h"
#include "utils_helpers.h"
#include "types.h"
#include "error.h"
#include "a2a_log.h"
#include "uuid.h"
#include "tasks/task_updater_impl.h"
#include "default_request_handler.h"

namespace A2A::Server {

std::vector<Task> DefaultRequestHandler::GetRelatedTasksFromReferenceTaskIds(
    const MessageSendParams& params, const std::shared_ptr<ServerCallContext>& ctx)
{
    std::vector<Task> relatedTasks = {};
    if (!params.message.referenceTaskIds.has_value()) {
        return relatedTasks;
    }

    const auto& referenceTaskIds = params.message.referenceTaskIds.value();
    for (const auto& refTaskId : referenceTaskIds) {
        if (refTaskId.empty()) {
            continue;
        }
        auto referencedTask = taskStore_->Get(refTaskId, ctx);
        if (referencedTask == nullptr) {
            A2A_LOG(A2A_LOG_LEVEL_WARN, "Reference task Id does not exist: " + refTaskId);
            continue;
        }
        relatedTasks.push_back(*referencedTask);
    }

    return relatedTasks;
}

void DefaultRequestHandler::OnSendMessage(const MessageSendParams& params,
    const std::shared_ptr<ServerCallContext> ctx, StreamEmitter emit, const std::string& method)
{
    // Determine task ID and check if it's an existing task
    std::shared_ptr<Task> existingTask = nullptr;
    std::string taskId = DetermineTaskId(params, ctx, existingTask);
    std::string contextId = existingTask
        ? existingTask->contextId
        : params.message.contextId.value_or(GenerateUuid());

    // Get related tasks from referenceTaskIds
    std::vector<Task> relatedTasks = GetRelatedTasksFromReferenceTaskIds(params, ctx);

    const auto executeInfo = std::make_shared<TaskExecuteInfo>();
    executeInfo->callContext = ctx;
    // Update existing task or create new task
    if (existingTask) {
        if (IsFinal(existingTask->status.state)) {
            throw A2AServerError("Cannot execute task in final state",
                static_cast<int>(A2AErrorCode::UNSUPPORTED_OPERATION));
        }
        taskManager_->RegisterTask(taskId, executeInfo);
        taskManager_->UpdateWithMessage(params.message, *existingTask);
    } else {
        CreateNewTask(params, taskId, contextId, ctx);
        taskManager_->RegisterTask(taskId, executeInfo);
    }
    taskManager_->ExchangeMessageSent(taskId, false);

    auto taskIdOptional = std::make_optional(taskId);
    auto buildParam = RequestContextParam{params, taskIdOptional, contextId,
        taskStore_, relatedTasks, ctx};
    auto requestContext = std::make_shared<RequestContext>(buildParam);

    // Execute agent and get result
    ExecuteAgentAndGetResult(params, taskId, requestContext, emit, method);

    // Send push notification if needed
    SendPushNotificationIfNeeded(taskId, ctx);
}

std::string DefaultRequestHandler::DetermineTaskId(const MessageSendParams& params,
    const std::shared_ptr<ServerCallContext> ctx,
    std::shared_ptr<Task>& existingTask) const
{
    if (!params.message.taskId.has_value() || params.message.taskId.value().empty()) {
        return GenerateTaskId();
    }
    // This is an existing task
    auto taskId = params.message.taskId.value();
    if (auto taskOpt = taskStore_->Get(taskId, ctx); taskOpt != nullptr) {
        if (params.message.contextId.has_value() &&
            !params.message.contextId.value().empty() &&
            taskOpt->contextId != params.message.contextId) {
            throw A2AServerError("Existing task contextId does not match requested contextId",
                static_cast<int>(A2AErrorCode::JSONRPC_INVALID_REQUEST));
        }
        existingTask = taskOpt;
        return taskId;
    }
    throw A2AServerError("Task id not found", static_cast<int>(A2AErrorCode::TASK_NOT_FOUND));
}

void DefaultRequestHandler::CreateNewTask(const MessageSendParams& params,
    const std::string& taskId,
    const std::string& contextId,
    const std::shared_ptr<ServerCallContext> ctx) const
{
    // Create a new task
    Task newTask;
    newTask.id = taskId;
    newTask.contextId = contextId;
    TaskStatus status;
    status.state = TaskState::SUBMITTED;
    newTask.status = status;
    newTask.history = {params.message};
    taskStore_->Save(newTask, ctx);
    UpdatePushNotificationConfig(params, taskId);
}

void DefaultRequestHandler::ExecuteAgentAndGetResult(
    const MessageSendParams& params,
    const std::string& taskId,
    std::shared_ptr<RequestContext> requestContext,
    StreamEmitter emit,
    const std::string& method) const
{
    // 3. Create TaskUpdater
    auto taskUpdater = std::make_shared<TaskUpdaterImpl>(taskId, taskManager_->GetContextId(taskId), taskManager_);

    // Handle blocking/non-blocking behavior
    bool blocking = true; // Default to blocking behavior
    if (params.configuration.has_value()) {
        blocking = !params.configuration.value().returnImmediately.value_or(false);
    }

    taskManager_->AddEventCallback(taskId, [this, emit, blocking, taskId] (const StreamEvent& event) {
        try {
            auto task = taskManager_->GetTask(taskId);
            if (task == nullptr) {
                A2A_LOG(A2A_LOG_LEVEL_ERROR, "Error processing event: task id is invalid, task id: " + taskId);
                return;
            }
            if (std::holds_alternative<Message>(event)) {
                // 收到Message，视为任务结束，尝试发送
                task->status.state = TaskState::COMPLETED;
                if (!taskManager_->ExchangeMessageSent(taskId, true)) {
                    emit(event);
                }
                return;
            }
            if (IsFinalEvent(event) || !blocking) {
                // 结束或者中断状态发送响应，如果是非阻塞模式，直接发送
                if (!taskManager_->ExchangeMessageSent(taskId, true)) {
                    emit(*task);
                }
            }
        } catch (const std::exception& e) {
            A2A_LOG(A2A_LOG_LEVEL_ERROR, "Error processing event: " + std::string(e.what()));
            if (taskManager_) {
                auto task = taskManager_->GetTask(taskId);
                if (task != nullptr) {
                    task->status.state = TaskState::FAILED;
                }
            }
        }
    });
    if (method == METHOD_MESSAGE_SEND) {
        executor_->Execute(std::move(requestContext), taskUpdater);
    } else {
        executor_->Execute(std::move(requestContext), taskUpdater, method);
    }
}

void DefaultRequestHandler::SendPushNotificationIfNeeded(const std::string& taskId,
    const std::shared_ptr<ServerCallContext> ctx) const
{
    if (pushSender_ && !taskId.empty()) {
        // Note: This is a simplified implementation compared to Python
        // In Python, it gets the latest task through resultAggregator
        // Here, we'll just use the most recent saved task
        auto taskOpt = taskStore_->Get(taskId, ctx);
        try {
            if (taskOpt != nullptr) {
                pushSender_->SendNotification(taskOpt);
            }
        } catch (const std::exception& e) {
            // Log error but don't fail the request
            A2A_LOG(A2A_LOG_LEVEL_ERROR, "Failed to send push notification: " + std::string(e.what()));
        }
    }
}

Task DefaultRequestHandler::OnGetTask(const TaskQueryParams& params, const std::shared_ptr<ServerCallContext> ctx)
{
    const auto taskOpt = taskStore_->Get(params.id, ctx);
    if (taskOpt == nullptr) {
        throw A2AServerError("Task id not found", static_cast<int>(A2AErrorCode::TASK_NOT_FOUND));
    }

    Task task = *taskOpt;
    // Apply history length limit if specified
    if (params.historyLength.has_value() && params.historyLength.value() > 0 && task.history.has_value() &&
        task.history.value().size() > static_cast<size_t>(params.historyLength.value())) {
        // Keep only the most recent history_length messages
        auto& history = task.history.value();
        if (history.size() > static_cast<size_t>(params.historyLength.value())) {
            history.erase(history.begin(), history.end() - params.historyLength.value());
        }
    }

    return task;
}

Task DefaultRequestHandler::OnCancelTask(const TaskIdParams& params, const std::shared_ptr<ServerCallContext> ctx)
{
    auto taskPtr = taskStore_->Get(params.id, ctx);
    if (taskPtr == nullptr) {
        throw A2AServerError("Task id not found", static_cast<int>(A2AErrorCode::TASK_NOT_FOUND));
    }
    // Check if task is in a non-cancelable state
    if (IsFinal(taskPtr->status.state)) {
        throw A2AServerError("Cancel task failed", static_cast<int>(A2AErrorCode::TASK_NOT_CANCELABLE));
    }

    if (executor_) {
        // Create RequestContext using new helper method
        auto taskIdOptional = std::make_optional(taskPtr->id);
        std::vector<Task> relatedTasks = {};
        auto buildParam = RequestContextParam{std::nullopt, taskIdOptional, taskPtr->contextId,
            taskStore_, relatedTasks, ctx};
        auto requestContext = std::make_shared<RequestContext>(buildParam);

        // Create TaskUpdater
        auto taskUpdater = std::make_shared<TaskUpdaterImpl>(taskPtr->id, taskPtr->contextId, taskManager_);

        // Execute the cancel agent
        executor_->Cancel(std::move(requestContext), taskUpdater);

        // Update task status
        taskManager_->CancelTask(taskPtr);
        A2A_LOG(A2A_LOG_LEVEL_DEBUG, "Task canceled, task id: " + taskPtr->id);
    } else {
        taskPtr->status.state = TaskState::CANCELED;
        taskStore_->Save(*taskPtr, ctx);
        A2A_LOG(A2A_LOG_LEVEL_WARN,
            "Agent executor not available, task state set to canceled, task id: " + taskPtr->id);
    }

    return *taskPtr;
}

void DefaultRequestHandler::OnSetTaskPushNotificationConfig(
    const TaskPushNotificationConfig& cfg, const std::shared_ptr<ServerCallContext> ctx)
{
    if (!pushConfigStore_) {
        throw A2AServerError("Push notification config is not set",
            static_cast<int>(A2AErrorCode::PUSH_NOTIFICATION_NOT_SUPPORTED));
    }

    if (const auto taskOpt = taskStore_->Get(cfg.taskId, ctx); taskOpt == nullptr) {
        throw A2AServerError("Task id not found", static_cast<int>(A2AErrorCode::TASK_NOT_FOUND));
    }

    if (pushConfigStore_) {
        pushConfigStore_->SetInfo(cfg.taskId, cfg.pushNotificationConfig);
    }
}

TaskPushNotificationConfig DefaultRequestHandler::OnGetTaskPushNotificationConfig(
    const GetTaskPushNotificationConfigParams& params, const std::shared_ptr<ServerCallContext> ctx)
{
    if (!pushConfigStore_) {
        throw A2AServerError("Push notification config is not set",
            static_cast<int>(A2AErrorCode::PUSH_NOTIFICATION_NOT_SUPPORTED));
    }

    if (const auto taskOpt = taskStore_->Get(params.id, ctx); taskOpt == nullptr) {
        throw A2AServerError("Task id not found", static_cast<int>(A2AErrorCode::TASK_NOT_FOUND));
    }

    if (pushConfigStore_) {
        auto configs = pushConfigStore_->GetInfo(params.id);
        if (!configs.empty()) {
            TaskPushNotificationConfig config;
            config.taskId = params.id;
            config.pushNotificationConfig = configs.front(); // Return the first config
            return config;
        }
    }

    // Fallback - create a minimal config
    TaskPushNotificationConfig config;
    config.taskId = params.id;

    return config;
}

std::vector<TaskPushNotificationConfig> DefaultRequestHandler::OnListTaskPushNotificationConfigs(
    const ListTaskPushNotificationConfigParams& params, const std::shared_ptr<ServerCallContext> ctx)
{
    if (!pushConfigStore_) {
        throw A2AServerError("Push notification config is not set",
            static_cast<int>(A2AErrorCode::PUSH_NOTIFICATION_NOT_SUPPORTED));
    }

    if (const auto taskOpt = taskStore_->Get(params.id, ctx); taskOpt == nullptr) {
        throw A2AServerError("Task id not found", static_cast<int>(A2AErrorCode::TASK_NOT_FOUND));
    }
    if (pushConfigStore_) {
        auto configs = pushConfigStore_->GetInfo(params.id);
        std::vector<TaskPushNotificationConfig> result;
        for (const auto& config : configs) {
            TaskPushNotificationConfig task_config;
            task_config.taskId = params.id;
            task_config.pushNotificationConfig = config;
            result.push_back(task_config);
        }
        return result;
    }
    return {};
}

void DefaultRequestHandler::OnDeleteTaskPushNotificationConfig(const DeleteTaskPushNotificationConfigParams& params,
    const std::shared_ptr<ServerCallContext> ctx)
{
    if (!pushConfigStore_) {
        throw A2AServerError("Push notification config is not set",
            static_cast<int>(A2AErrorCode::PUSH_NOTIFICATION_NOT_SUPPORTED));
    }

    if (const auto taskOpt = taskStore_->Get(params.id, ctx); taskOpt == nullptr) {
        throw A2AServerError("Task id not found", static_cast<int>(A2AErrorCode::TASK_NOT_FOUND));
    }
    if (pushConfigStore_) {
        pushConfigStore_->DeleteInfo(params.id, params.pushNotificationConfigId);
    }
}

void DefaultRequestHandler::OnSendMessageStreaming(const MessageSendParams& params,
    StreamEmitter emit,
    const std::shared_ptr<ServerCallContext> ctx)
{
    // Initialize the streaming task
    std::shared_ptr<Task> existingTask = nullptr;
    std::string taskId;

    InitializeStreamingTask(params, ctx, existingTask, taskId);

    if (existingTask) {
        emit(*existingTask);
    } else {
        throw A2AServerError("find or create task failed");
    }

    // Process referenceTaskIds and build relatedTasks
    std::vector<Task> relatedTasks = GetRelatedTasksFromReferenceTaskIds(params, ctx);

    auto taskIdOptional = std::make_optional(taskId);
    auto buildParam = RequestContextParam{params, taskIdOptional,
        taskManager_->GetContextId(taskId), taskStore_, relatedTasks, ctx};
    auto requestContext = std::make_shared<RequestContext>(buildParam);

    // Set up and execute the streaming agent
    SetupAndExecuteStreamingAgent(taskId, requestContext, emit, false);
}

void DefaultRequestHandler::InitializeStreamingTask(const MessageSendParams& params,
    const std::shared_ptr<ServerCallContext> ctx,
    std::shared_ptr<Task>& existingTask,
    std::string& taskId) const
{
    // Determine task ID and check if it's an existing task
    taskId = DetermineTaskId(params, ctx, existingTask);
    const std::string contextId = existingTask
        ? existingTask->contextId
        : params.message.contextId.value_or(GenerateUuid());

    // Set up task manager
    const auto executeInfo = std::make_shared<TaskExecuteInfo>();
    executeInfo->callContext = ctx;

    if (existingTask) {
        if (IsFinal(existingTask->status.state)) {
            throw A2AServerError("Cannot execute task in final state",
                static_cast<int>(A2AErrorCode::UNSUPPORTED_OPERATION));
        }
        taskManager_->RegisterTask(taskId, executeInfo);
        taskManager_->UpdateWithMessage(params.message, *existingTask);
        return;
    }

    // Update existing task or create new task
    CreateNewTask(params, taskId, contextId, ctx);
    existingTask = taskStore_->Get(taskId, ctx);

    taskManager_->RegisterTask(taskId, executeInfo);
}

void DefaultRequestHandler::SetupAndExecuteStreamingAgent(
    const std::string& taskId,
    std::shared_ptr<RequestContext> requestContext,
    const StreamEmitter& emit,
    const bool resubscribe)
{
    taskManager_->AddEventCallback(taskId, [this, emit, taskId, resubscribe] (const StreamEvent& ev) {
        try {
            emit(ev);
        } catch (const std::exception& e) {
            A2A_LOG(A2A_LOG_LEVEL_ERROR, "An error occurred while processing streaming request: " +
                std::string(e.what()));
            if (taskManager_) {
                auto task = taskManager_->GetTask(taskId);
                if (task != nullptr) {
                    task->status.state = TaskState::FAILED;
                }
            }
        }

        if (!pushSender_ || resubscribe) {
            return;
        }
        try {
            if (const auto task = taskManager_->GetTask(taskId)) {
                pushSender_->SendNotification(task);
            }
        } catch (const std::exception& e) {
            A2A_LOG(A2A_LOG_LEVEL_ERROR, "Failed to send push notification: " + std::string(e.what()));
        }
    });
    if (resubscribe) {
        // 续订阅场景，不需要触发AgentExecutor
        return;
    }

    // 5. Create TaskUpdater
    auto taskUpdater = std::make_shared<TaskUpdaterImpl>(taskId, taskManager_->GetContextId(taskId), taskManager_);
    executor_->Execute(std::move(requestContext), taskUpdater);
}

void DefaultRequestHandler::OnResubscribeToTask(const TaskIdParams& params,
    StreamEmitter emit,
    const std::shared_ptr<ServerCallContext> ctx)
{
    const auto taskOpt = taskStore_->Get(params.id, ctx);
    if (taskOpt == nullptr) {
        throw A2AServerError("Task id not found", static_cast<int>(A2AErrorCode::TASK_NOT_FOUND));
    }
    emit(*taskOpt);
    if (IsFinalOrInterrupted(taskOpt->status.state)) {
        // 本轮对话已完成，Agent不会继续产生内容，停止消费
        return;
    }
    // 使用新的emit覆盖原有taskUpdater的回调函数
    SetupAndExecuteStreamingAgent(taskOpt->id, nullptr, emit, true);
}

std::string DefaultRequestHandler::GenerateTaskId()
{
    return "task-" + GenerateUuid();
}

void DefaultRequestHandler::UpdatePushNotificationConfig(const MessageSendParams &params,
    const std::string& taskId) const
{
    // Store push notification config if provided
    if (pushConfigStore_ && params.configuration.has_value()) {
        try {
            const auto& configJson = params.configuration.value();
            if (configJson.pushNotificationConfig.has_value()) {
                PushNotificationConfig pushConfig = configJson.pushNotificationConfig.value();
                pushConfigStore_->SetInfo(taskId, pushConfig);
            }
        } catch (const std::exception& e) {
            // Log error but don't fail the request
            A2A_LOG(A2A_LOG_LEVEL_ERROR, "Failed to update push notification config: " + std::string(e.what()));
        }
    }
}

AgentCard DefaultRequestHandler::OnGetCard([[maybe_unused]] const std::shared_ptr<ServerCallContext> ctx)
{
    return *agentCard_;
}

} // namespace A2A::Server