/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <future>
#include <stdexcept>
#include <thread>

#include "events/event_consumer.h"
#include "request_context_impl.h"
#include "result_aggregator.h"
#include "server/agent_executor.h"
#include "queue_manager.h"
#include "tasks/push_notification_config_store.h"
#include "tasks/push_notification_sender.h"
#include "tasks/task_manager.h"
#include "server/task_store.h"
#include "types.h"
#include "error.h"
#include "id_generator.h"
#include "server/task_updater.h"
#include "default_request_handler.h"

namespace A2A::Server {

std::variant<Task, Message> DefaultRequestHandler::OnSendMessage(const MessageSendParams& params,
    const std::shared_ptr<ServerCallContext> ctx)
{
    // Determine task ID and check if it's an existing task
    std::optional<Task> existing_task = std::nullopt;
    std::string taskId = DetermineTaskId(params, ctx, existing_task);

    // Create task manager
    auto taskManager = CreateTaskManager(taskId, params.message.contextId.value_or(""), params.message, ctx);

    // Update existing task or create new task
    UpdateOrCreateTask(params, taskId, taskManager, existing_task, ctx);

    if (queueManager_ && executor_) {
        // Create RequestContext using helper method
        RequestContext request_context = CreateRequestContext(
            params, taskId, params.message.contextId, existing_task, ctx);

        // Execute agent and get result
        auto result = ExecuteAgentAndGetResult(params, taskId, taskManager, request_context, ctx);

        // Send push notification if needed
        SendPushNotificationIfNeeded(taskId, ctx);

        // Return the result or fallback
        if (result.has_value()) {
            return result.value();
        }

        // Fallback - return a minimal task
        Task task;
        task.id = taskId;
        task.contextId = params.message.contextId.value_or("default-context");

        TaskStatus status;
        status.state = TaskState::WORKING;
        task.status = status;

        return task;
    }

    // If no queue manager or executor, fall back to simpler behavior
    if (auto taskOpt = taskStore_->Get(taskId, ctx); taskOpt.has_value()) {
        return taskOpt.value();
    }

    // Fallback - create a minimal task
    Task task;
    task.id = taskId;
    task.contextId = params.message.contextId.value_or("default-context");

    TaskStatus status;
    status.state = TaskState::WORKING;
    task.status = status;

    return task;
}

std::string DefaultRequestHandler::DetermineTaskId(const MessageSendParams& params,
    const std::shared_ptr<ServerCallContext> ctx,
    std::optional<Task>& existing_task) const
{
    std::string taskId;
    if (params.message.taskId.has_value()) {
        // This is an existing task
        taskId = params.message.taskId.value();
        if (auto taskOpt = taskStore_->Get(taskId, ctx); taskOpt.has_value()) {
            existing_task = taskOpt.value();
        }
    } else {
        // This is a new task
        taskId = GenerateTaskId();
    }
    return taskId;
}

std::shared_ptr<TaskManager> DefaultRequestHandler::CreateTaskManager(const std::string& taskId,
    const std::string& contextId,
    const Message& message,
    const std::shared_ptr<ServerCallContext> ctx) const
{
    return std::make_shared<TaskManager>(taskId, contextId, taskStore_, message, ctx);
}

void DefaultRequestHandler::UpdateOrCreateTask(const MessageSendParams& params,
    const std::string& taskId,
    const std::shared_ptr<TaskManager>& taskManager,
    const std::optional<Task>& existing_task,
    const std::shared_ptr<ServerCallContext> ctx) const
{
    if (existing_task.has_value()) {
        const Task updated_task = taskManager->UpdateWithMessage(params.message, existing_task.value());
        taskStore_->Save(updated_task, ctx);
    } else {
        // Create a new task
        Task new_task;
        new_task.id = taskId;
        new_task.contextId = params.message.contextId.value_or("");

        TaskStatus status;
        status.state = TaskState::SUBMITTED;
        new_task.status = status;

        taskStore_->Save(new_task, ctx);

        UpdatePushNotificationConfig(params, taskId);
    }
}

std::optional<std::variant<Task, Message>> DefaultRequestHandler::ExecuteAgentAndGetResult(
    const MessageSendParams& params,
    const std::string& taskId,
    const std::shared_ptr<TaskManager>& taskManager,
    const RequestContext& request_context,
    const std::shared_ptr<ServerCallContext> ctx) const
{
    // 1. Get or create an EventQueue
    auto eventQueue = queueManager_->CreateOrTap(taskId);

    // 2. Create ResultAggregator
    ResultAggregator resultAggregator(taskManager);

    // 3. Create TaskUpdater
    auto artifactIdGenerator = std::make_shared<UUIDGenerator>();
    auto messageIdGenerator = std::make_shared<UUIDGenerator>();
    auto taskUpdater = std::make_shared<A2A::Server::TaskUpdater>(eventQueue, taskId,
        params.message.contextId.value_or(""));

    // 4. Create EventConsumer and consume events
    std::shared_ptr<EventConsumer> consumer = std::make_shared<EventConsumer>(eventQueue);

    // Handle blocking/non-blocking behavior
    bool blocking = true; // Default to blocking behavior
    if (params.configuration.has_value() && params.configuration.value().blocking) {
        blocking = params.configuration.value().blocking.value_or(false);
    }

    // Prepare result storage
    std::optional<std::variant<Task, Message>> result = std::nullopt;

    // Execute the agent directly (without creating a thread)
    executor_->Execute(request_context, taskUpdater);

    // Use the new ConsumeAndBreakOnInterrupt method
    try {
        auto [interrupted, background_future] = resultAggregator.ConsumeAndBreakOnInterrupt(consumer, blocking);

        // Get the result based on interrupt status and blocking mode
        if (interrupted && !blocking) {
            // For non-blocking interrupted calls, we should get the current result
            result = resultAggregator.CurrentResult();
        } else {
            // For blocking or normal cases, get the final result
            result = resultAggregator.ConsumeAll(consumer);
        }

        // If interrupted and we have a background future, we should handle it
        if (interrupted && background_future.valid()) {
            // In a real implementation, we might want to track this task
            // For now, we let it run in the background
        }
    } catch (const std::exception& e) {
        // Handle exception appropriately
    }

    return result;
}

void DefaultRequestHandler::SendPushNotificationIfNeeded(const std::string& taskId,
    const std::shared_ptr<ServerCallContext> ctx) const
{
    if (pushSender_ && !taskId.empty() && taskStore_->Get(taskId, ctx).has_value()) {
        // Note: This is a simplified implementation compared to Python
        // In Python, it gets the latest task through resultAggregator
        // Here, we'll just use the most recent saved task
        auto taskOpt = taskStore_->Get(taskId, ctx);
        try {
            pushSender_->SendNotification(taskOpt.value());
        } catch (const std::exception& e) {
            // Log error but don't fail the request
        }
    }
}

Task DefaultRequestHandler::OnGetTask(const TaskQueryParams& params, const std::shared_ptr<ServerCallContext> ctx)
{
    const auto taskOpt = taskStore_->Get(params.id, ctx);
    if (!taskOpt.has_value()) {
        throw A2AServerError("Task id not found");
    }

    Task task = taskOpt.value();
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

Task DefaultRequestHandler::OnCancelTask(const TaskIdParams& params, const std::shared_ptr<ServerCallContext> context)
{
    const auto taskOpt = taskStore_->Get(params.id, context);
    if (!taskOpt.has_value()) {
        throw A2AServerError("Task id not found");
    }

    Task task = taskOpt.value();
    // Check if task is in a non-cancelable state
    if (IsFinalState(task.status.state)) {
        throw A2AServerError("Cancel task failed");
    }

    if (queueManager_ && executor_) {
        // 1. Create a TaskManager
        TaskManager taskManager(task.id, task.contextId, taskStore_, Message{}); // Empty message for cancel

        // 2. Get or create an EventQueue
        auto eventQueue = queueManager_->CreateOrTap(task.id);

        // 3. Create RequestContext using new helper method
        RequestContext requestContext = CreateRequestContext(std::nullopt, task.id, task.contextId, task, context);

        // 4. Create TaskUpdater
        auto artifactIdGenerator = std::make_shared<UUIDGenerator>();
        auto messageIdGenerator = std::make_shared<UUIDGenerator>();
        auto taskUpdater = std::make_shared<A2A::Server::TaskUpdater>(eventQueue, task.id, task.contextId);

        // 5. Execute the cancel agent
        executor_->Cancel(requestContext, taskUpdater);

        // 5. Consume events and close queue
        eventQueue->Close();

        // 6. Update task status
        task.status.state = TaskState::CANCELED;
        taskStore_->Save(task, context);
    } else {
        // For this simplified version, we'll just mark the task as canceled
        task.status.state = TaskState::CANCELED;
        taskStore_->Save(task, context);
    }

    return task;
}

TaskPushNotificationConfig DefaultRequestHandler::OnSetTaskPushNotificationConfig(
    const TaskPushNotificationConfig& cfg, const std::shared_ptr<ServerCallContext> ctx)
{
    if (!pushConfigStore_) {
        throw A2AServerError("Push notification config is not set");
    }

    if (const auto taskOpt = taskStore_->Get(cfg.taskId, ctx); !taskOpt.has_value()) {
        throw A2AServerError("Task id not found");
    }

    if (pushConfigStore_) {
        pushConfigStore_->SetInfo(cfg.taskId, cfg.pushNotificationConfig);
    }
    return cfg;
}

TaskPushNotificationConfig DefaultRequestHandler::OnGetTaskPushNotificationConfig(
    const GetTaskPushNotificationConfigParams& params, const std::shared_ptr<ServerCallContext> ctx)
{
    if (!pushConfigStore_) {
        throw A2AServerError("Push notification config is not set");
    }

    if (const auto taskOpt = taskStore_->Get(params.id, ctx); !taskOpt.has_value()) {
        throw A2AServerError("Task id not found");
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

    PushNotificationConfig push_config;
    push_config.url = "http://example.com/callback";
    config.pushNotificationConfig = push_config;

    return config;
}

std::vector<TaskPushNotificationConfig> DefaultRequestHandler::OnListTaskPushNotificationConfigs(
    const ListTaskPushNotificationConfigParams& params, const std::shared_ptr<ServerCallContext> ctx)
{
    if (!pushConfigStore_) {
        throw A2AServerError("Push notification config is not set");
    }

    if (const auto taskOpt = taskStore_->Get(params.id, ctx); !taskOpt.has_value()) {
        throw A2AServerError("Task id not found");
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
        throw A2AServerError("Push notification config is not set");
    }

    if (const auto taskOpt = taskStore_->Get(params.id, ctx); !taskOpt.has_value()) {
        throw A2AServerError("Task id not found");
    }
    if (pushConfigStore_) {
        pushConfigStore_->DeleteInfo(params.id, params.pushNotificationConfigId);
    }
}

void DefaultRequestHandler::OnSendMessageStreaming(const MessageSendParams& params,
    const StreamEmitter& emit,
    const std::shared_ptr<ServerCallContext> context)
{
    if (!queueManager_ || !executor_) {
        // For this simplified version, we'll throw an exception as in the base class
        throw std::runtime_error("Streaming not supported");
    }

    // Initialize the streaming task
    std::optional<Task> existingTask = std::nullopt;
    std::string taskId;
    std::shared_ptr<TaskManager> taskManager;

    InitializeStreamingTask(params, context, existingTask, taskId, taskManager);

    RequestContext requestContext = CreateRequestContext(
        params,
        taskId,
        params.message.contextId,
        existingTask.has_value() ? std::make_optional(existingTask.value()) : std::nullopt,
        context);

    // Set up and execute the streaming agent
    SetupAndExecuteStreamingAgent(params, taskId, requestContext, taskManager, emit);
}

void DefaultRequestHandler::InitializeStreamingTask(const MessageSendParams& params,
    const std::shared_ptr<ServerCallContext> context,
    std::optional<Task>& existingTask,
    std::string& taskId,
    std::shared_ptr<TaskManager>& taskManager) const
{
    // Determine task ID and check if it's an existing task
    taskId = DetermineTaskId(params, context, existingTask);

    // Set up task manager
    taskManager = CreateTaskManager(taskId, params.message.contextId.value_or(""), params.message, context);

    // Update existing task or create new task
    UpdateOrCreateTask(params, taskId, taskManager, existingTask, context);
}

void DefaultRequestHandler::SetupAndExecuteStreamingAgent(const MessageSendParams& params,
    const std::string& taskId,
    const RequestContext& requestContext,
    const std::shared_ptr<TaskManager>& taskManager,
    const StreamEmitter& emit) const
{
    // 3. Set up event queues and consumers
    auto eventQueue = queueManager_->CreateOrTap(taskId);

    // 4. Create ResultAggregator and EventConsumer
    ResultAggregator resultAggregator(taskManager);
    auto consumer = std::make_shared<EventConsumer>(eventQueue);

    // 5. Create TaskUpdater
    auto artifactIdGenerator = std::make_shared<UUIDGenerator>();
    auto messageIdGenerator = std::make_shared<UUIDGenerator>();
    auto taskUpdater = std::make_shared<TaskUpdater>(eventQueue, taskId, params.message.contextId.value_or(""));

    auto agentFuture = std::async(std::launch::async, [this, &requestContext, taskUpdater]() {
        executor_->Execute(requestContext, taskUpdater);
    });

    // 6. Stream events back to the client via the emit callback
    auto consumerFuture = std::async(std::launch::async,
        [this, consumer, &resultAggregator, &emit, &taskId, &eventQueue]() {
        while (!eventQueue->IsClosed()) {
            try {
                auto event = eventQueue->Dequeue();
                // Validate task ID match if needed
                // Then send event to client
                resultAggregator.ConsumeAndEmit(consumer, emit);
                if (!pushSender_ || taskId.empty()) {
                    continue;
                }
                auto currentResult = resultAggregator.CurrentResult();
                if (!currentResult.has_value() || !std::holds_alternative<Task>(currentResult.value())) {
                    continue;
                }
                try {
                    pushSender_->SendNotification(std::get<Task>(currentResult.value()));
                } catch (const std::exception& e) {
                    // Log error but don't fail the stream
                }
            } catch (const std::runtime_error&) {
                // Queue is empty and closed, break the loop
                break;
            }
        }
    });

    agentFuture.wait();
    consumerFuture.wait();
}

void DefaultRequestHandler::OnResubscribeToTask(const TaskIdParams& params,
    const StreamEmitter& emit,
    const std::shared_ptr<ServerCallContext> context)
{
    if (!queueManager_) {
        // For this simplified version, we'll throw an exception as in the base class
        throw std::runtime_error("Streaming not supported");
    }

    // 1. Retrieve the existing task
    const auto taskOpt = taskStore_->Get(params.id, context);
    if (!taskOpt.has_value()) {
        throw A2AServerError("Task id not found");
    }

    // 2. Tap into the existing event queue
    auto eventQueue = queueManager_->Tap(params.id);
    if (!eventQueue) {
        throw A2AServerError("Event queue not found for task");
    }

    // 3. Resume streaming events to the client via the emit callback
    while (!eventQueue->IsClosed()) {
        try {
            auto event = eventQueue->Dequeue();
            emit(event);
        } catch (const std::runtime_error&) {
            // Queue is empty and closed, break the loop
            break;
        }
    }
}

bool DefaultRequestHandler::IsFinalState(const TaskState state)
{
    return state == TaskState::COMPLETED || state == TaskState::CANCELED || state == TaskState::FAILED ||
           state == TaskState::REJECTED;
}

std::string DefaultRequestHandler::GenerateTaskId()
{
    static std::atomic<int> counter{0};
    return "task-" + std::to_string(++counter);
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
        }
    }
}

RequestContext DefaultRequestHandler::CreateRequestContext(
    std::optional<MessageSendParams> params,
    std::string taskId,
    std::optional<std::string> contextId,
    std::optional<Task> existingTask,
    const std::shared_ptr<ServerCallContext>& ctx)
{
    auto taskIdOptional = std::make_optional(taskId);
    std::vector<Task> relatedTasks = {};
    auto buildParam = RequestContextParam{
        params, taskIdOptional, contextId, existingTask, relatedTasks, ctx
    };
    return RequestContext(buildParam);
}

AgentCard DefaultRequestHandler::OnGetCard(const std::shared_ptr<ServerCallContext> ctx)
{
    return *agentCard_;
}

} // namespace A2A::Server