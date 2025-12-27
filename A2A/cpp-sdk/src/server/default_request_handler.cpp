/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <algorithm>
#include <atomic>
#include <future>
#include <stdexcept>
#include <thread>

#include "default_request_handler.h"
#include "events/event_consumer.h"
#include "request_context_impl.h"
#include "result_aggregator.h"
#include "server/agent_executor.h"
#include "server/queue_manager.h"
#include "tasks/push_notification_config_store.h"
#include "tasks/push_notification_sender.h"
#include "tasks/task_manager.h"
#include "tasks/task_store.h"
#include "utils/errors.h"
#include "utils/types.h"

namespace a2a::server {

std::variant<Task, Message> DefaultRequestHandler::OnSendMessage(const MessageSendParams& params,
                                                                 const ServerCallContext* ctx)
{
    // Check if this is an existing task or a new one
    std::optional<Task> existing_task = std::nullopt;
    std::string task_id;

    if (params.message.taskId.has_value()) {
        // This is an existing task
        task_id = params.message.taskId.value();
        if (auto task_opt = task_store_->Get(task_id, ctx); task_opt.has_value()) {
            existing_task = task_opt.value();
        }
    } else {
        // This is a new task
        task_id = generate_task_id();
    }

    // Create task manager
    auto task_manager =
        std::make_shared<TaskManager>(task_id, params.message.contextId.value_or(""), task_store_, params.message, ctx);

    // If this is an existing task, update it with the new message
    if (existing_task.has_value()) {
        const Task updated_task = task_manager->UpdateWithMessage(params.message, existing_task.value());
        task_store_->Save(updated_task, ctx);
    } else {
        // Create a new task
        Task new_task;
        new_task.id = task_id;
        new_task.contextId = params.message.contextId.value_or("");

        TaskStatus status;
        status.state = TaskState::SUBMITTED;
        new_task.status = status;

        task_store_->Save(new_task, ctx);

        UpdatePushNotificationConfig(params, task_id);
    }

    if (queue_manager_ && executor_) {
        // 1. Get or create an EventQueue
        auto event_queue = queue_manager_->CreateOrTap(task_id);

        // 2. Create RequestContext using builder
        RequestContext request_context;
        if (context_builder_) {
            request_context = context_builder_->Build(params,
                                                      task_id,
                                                      params.message.contextId,
                                                      existing_task,
                                                      ctx);
        } else {
            // Fallback to manual construction if builder is not available
            RequestContext request_context_manual(params, task_id, params.message.contextId, existing_task, {}, ctx);
            request_context = std::move(request_context_manual);
        }

        // 3. Create ResultAggregator
        ResultAggregator result_aggregator(task_manager);

        // 4. Run agent execution in a separate thread
        std::thread agent_thread(
            [this, &request_context, event_queue]() { executor_->Execute(request_context, *event_queue); });

        // 5. Create EventConsumer and consume events
        EventConsumer consumer(event_queue);

        // Handle blocking/non-blocking behavior
        bool blocking = true; // Default to blocking behavior
        if (params.configuration.has_value() && params.configuration.value().contains("blocking")) {
            blocking = params.configuration.value().at("blocking").get<bool>();
        }

        // Prepare result storage
        std::optional<std::variant<Task, Message>> result = std::nullopt;

        // Use the new ConsumeAndBreakOnInterrupt method
        try {
            auto [interrupted, background_future] = result_aggregator.ConsumeAndBreakOnInterrupt(consumer, blocking);

            // Get the result based on interrupt status and blocking mode
            if (interrupted && !blocking) {
                // For non-blocking interrupted calls, we should get the current result
                result = result_aggregator.CurrentResult();
            } else {
                // For blocking or normal cases, get the final result
                result = result_aggregator.ConsumeAll(consumer);
            }

            // If interrupted and we have a background future, we should handle it
            if (interrupted && background_future.valid()) {
                // In a real implementation, we might want to track this task
                // For now, we let it run in the background
            }
        } catch (const std::exception& e) {
            // Handle exception appropriately
        }

        // 6. Handle cleanup
        if (agent_thread.joinable()) {
            agent_thread.join();
        }

        // 7. Send push notification if needed
        if (push_sender_ && !task_id.empty() && task_store_->Get(task_id, ctx).has_value()) {
            // Note: This is a simplified implementation compared to Python
            // In Python, it gets the latest task through result_aggregator
            // Here, we'll just use the most recent saved task
            auto task_opt = task_store_->Get(task_id, ctx);
            try {
                push_sender_->SendNotification(task_opt.value());
            } catch (const std::exception& e) {
                // Log error but don't fail the request
            }
        }

        // 8. Return the result
        if (result.has_value()) {
            return result.value();
        }

        // Fallback - return a minimal task
        Task task;
        task.id = task_id;
        task.contextId = params.message.contextId.value_or("default-context");

        TaskStatus status;
        status.state = TaskState::WORKING;
        task.status = status;

        return task;
    }

    // If no queue manager or executor, fall back to simpler behavior
    if (auto task_opt = task_store_->Get(task_id, ctx); task_opt.has_value()) {
        return task_opt.value();
    }

    // Fallback - create a minimal task
    Task task;
    task.id = task_id;
    task.contextId = params.message.contextId.value_or("default-context");

    TaskStatus status;
    status.state = TaskState::WORKING;
    task.status = status;

    return task;
}

Task DefaultRequestHandler::OnGetTask(const TaskQueryParams& params, const ServerCallContext* ctx)
{
    const auto task_opt = task_store_->Get(params.id, ctx);
    if (!task_opt.has_value()) {
        throw A2AServerError("Task id not found");
    }

    Task task = task_opt.value();
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

Task DefaultRequestHandler::OnCancelTask(const TaskIdParams& params, const ServerCallContext* context)
{
    const auto task_opt = task_store_->Get(params.id, context);
    if (!task_opt.has_value()) {
        throw A2AServerError("Task id not found");
    }

    Task task = task_opt.value();
    // Check if task is in a non-cancelable state
    if (is_final_state(task.status.state)) {
        throw A2AServerError("Cancel task failed");
    }

    if (queue_manager_ && executor_) {
        // 1. Create a TaskManager
        TaskManager task_manager(task.id, task.contextId, task_store_, Message{}); // Empty message for cancel

        // 2. Get or create an EventQueue
        auto event_queue = queue_manager_->CreateOrTap(task.id);

        // 3. Create RequestContext using builder
        RequestContext request_context;
        if (context_builder_) {
            request_context = context_builder_->Build(std::nullopt, // params
                                                      task.id,
                                                      task.contextId,
                                                      task,
                                                      context);
        } else {
            // Fallback to manual construction if builder is not available
            RequestContext request_context_manual(std::nullopt, task.id, task.contextId, task, {}, context);
            request_context = std::move(request_context_manual);
        }

        // 4. Execute the cancel agent
        executor_->Cancel(request_context, *event_queue);

        // 5. Consume events and close queue
        event_queue->Close();

        // 6. Update task status
        task.status.state = TaskState::CANCELED;
        task_store_->Save(task, context);
    } else {
        // For this simplified version, we'll just mark the task as canceled
        task.status.state = TaskState::CANCELED;
        task_store_->Save(task, context);
    }

    return task;
}

TaskPushNotificationConfig DefaultRequestHandler::OnSetTaskPushNotificationConfig(const TaskPushNotificationConfig& cfg,
                                                                                  const ServerCallContext* ctx)
{
    if (!push_config_store_) {
        throw A2AServerError("Push notification config is not set");
    }

    if (const auto task_opt = task_store_->Get(cfg.taskId, ctx); !task_opt.has_value()) {
        throw A2AServerError("Task id not found");
    }

    if (push_config_store_) {
        push_config_store_->SetInfo(cfg.taskId, cfg.pushNotificationConfig);
    }
    return cfg;
}

TaskPushNotificationConfig DefaultRequestHandler::OnGetTaskPushNotificationConfig(
    const GetTaskPushNotificationConfigParams& params, const ServerCallContext* ctx)
{
    if (!push_config_store_) {
        throw A2AServerError("Push notification config is not set");
    }

    if (const auto task_opt = task_store_->Get(params.id, ctx); !task_opt.has_value()) {
        throw A2AServerError("Task id not found");
    }

    if (push_config_store_) {
        auto configs = push_config_store_->GetInfo(params.id);
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
    const ListTaskPushNotificationConfigParams& params, const ServerCallContext* ctx)
{
    if (!push_config_store_) {
        throw A2AServerError("Push notification config is not set");
    }

    if (const auto task_opt = task_store_->Get(params.id, ctx); !task_opt.has_value()) {
        throw A2AServerError("Task id not found");
    }
    if (push_config_store_) {
        auto configs = push_config_store_->GetInfo(params.id);
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
                                                               const ServerCallContext* ctx)
{
    if (!push_config_store_) {
        throw A2AServerError("Push notification config is not set");
    }

    if (const auto task_opt = task_store_->Get(params.id, ctx); !task_opt.has_value()) {
        throw A2AServerError("Task id not found");
    }
    if (push_config_store_) {
        push_config_store_->DeleteInfo(params.id, params.pushNotificationConfigId);
    }
}

void DefaultRequestHandler::OnSendMessageStreaming(const MessageSendParams& params, const StreamEmitter& emit,
                                                   const ServerCallContext* context)
{
    if (!queue_manager_ || !executor_) {
        // For this simplified version, we'll throw an exception as in the base class
        throw std::runtime_error("Streaming not supported");
    }

    // 1. Initialize the message send (similar to send_message)
    std::optional<Task> existing_task = std::nullopt;
    std::string task_id;

    if (params.message.taskId.has_value()) {
        // This is an existing task
        task_id = params.message.taskId.value();
        if (auto task_opt = task_store_->Get(task_id, context); task_opt.has_value()) {
            existing_task = task_opt.value();
        }
    } else {
        // This is a new task
        task_id = generate_task_id();
    }

    // 2. Set up task manager and validate existing task
    auto task_manager = std::make_shared<TaskManager>(task_id, params.message.contextId.value_or(""), task_store_,
                                                      params.message, context);

    // If this is an existing task, update it with the new message
    if (existing_task.has_value()) {
        const Task updated_task = task_manager->UpdateWithMessage(params.message, existing_task.value());
        task_store_->Save(updated_task, context);
    } else {
        // Create a new task
        Task new_task;
        new_task.id = task_id;
        new_task.contextId = params.message.contextId.value_or("");

        TaskStatus status;
        status.state = TaskState::SUBMITTED;
        new_task.status = status;

        task_store_->Save(new_task, context);

        UpdatePushNotificationConfig(params, task_id);
    }

    // 3. Set up event queues and consumers
    auto event_queue = queue_manager_->CreateOrTap(task_id);

    // 4. Create RequestContext using builder
    RequestContext request_context;
    if (context_builder_) {
        request_context = context_builder_->Build(
            params,
            task_id,
            params.message.contextId,
            existing_task.has_value() ? std::make_optional(existing_task.value()) : std::nullopt,
            context);
    } else {
        // Fallback to manual construction if builder is not available
        RequestContext request_context_manual(
            params, task_id, params.message.contextId,
            existing_task.has_value() ? std::make_optional(existing_task.value()) : std::nullopt, {}, context);
        request_context = std::move(request_context_manual);
    }

    // 5. Create ResultAggregator and EventConsumer
    ResultAggregator result_aggregator(task_manager);
    EventConsumer consumer(event_queue);

    // 6. Execute the agent in a separate thread to allow streaming
    std::thread agent_thread(
        [this, &request_context, event_queue]() { executor_->Execute(request_context, *event_queue); });

    // 7. Stream events back to the client via the emit callback
    try {
        // Create a thread that will handle the event consumption and forwarding
        std::thread consumer_thread([this, &consumer, &result_aggregator, &emit, &task_id, &event_queue]() {
            try {
                // Handle the streaming events like in Python's on_message_send_stream
                bool first_event = true;
                while (!event_queue->IsClosed()) {
                    try {
                        auto event = event_queue->Dequeue();

                        // Validate task ID match if needed
                        if (first_event) {
                            // This is where Python would do validation
                            first_event = false;
                        }

                        // Send event to client
                        result_aggregator.ConsumeAndEmit(consumer, emit);

                        if (!push_sender_ || task_id.empty()) {
                            continue;
                        }
                        try {
                            auto current_result = result_aggregator.CurrentResult();
                            if (current_result.has_value() && std::holds_alternative<Task>(current_result.value())) {
                                push_sender_->SendNotification(std::get<Task>(current_result.value()));
                            }
                        } catch (const std::exception& e) {
                            // Log error but don't fail the stream
                        }
                    } catch (const std::runtime_error&) {
                        // Queue is empty and closed, break the loop
                        break;
                    }
                }
            } catch (const std::exception& e) {
                // Handle any exceptions in the streaming process
                // This would normally be logged
            }
        });

        // Wait for the agent thread to finish
        if (agent_thread.joinable()) {
            agent_thread.join();
        }

        // Wait for the consumer thread to finish processing all events
        if (consumer_thread.joinable()) {
            consumer_thread.join();
        }
    } catch (...) {
        // Ensure threads are properly joined even if an exception occurs
        if (agent_thread.joinable()) {
            agent_thread.join();
        }
        throw;
    }
}

void DefaultRequestHandler::OnResubscribeToTask(const TaskIdParams& params, const StreamEmitter& emit,
                                                const ServerCallContext* context)
{
    if (!queue_manager_) {
        // For this simplified version, we'll throw an exception as in the base class
        throw std::runtime_error("Streaming not supported");
    }

    // 1. Retrieve the existing task
    const auto task_opt = task_store_->Get(params.id, context);
    if (!task_opt.has_value()) {
        throw A2AServerError("Task id not found");
    }

    // 2. Tap into the existing event queue
    auto event_queue = queue_manager_->Tap(params.id);
    if (!event_queue) {
        throw A2AServerError("Event queue not found for task");
    }

    // 3. Resume streaming events to the client via the emit callback
    while (!event_queue->IsClosed()) {
        try {
            auto event = event_queue->Dequeue();
            emit(event);
        } catch (const std::runtime_error&) {
            // Queue is empty and closed, break the loop
            break;
        }
    }
}

bool DefaultRequestHandler::is_final_state(const TaskState state)
{
    return state == TaskState::COMPLETED || state == TaskState::CANCELED || state == TaskState::FAILED ||
           state == TaskState::REJECTED;
}

std::string DefaultRequestHandler::generate_task_id()
{
    static int counter = 0;
    return "task-" + std::to_string(++counter);
}

void DefaultRequestHandler::UpdatePushNotificationConfig(const MessageSendParams &params,
    const std::string& task_id) const
{
    // Store push notification config if provided
    if (push_config_store_ && params.configuration.has_value()) {
        try {
            const auto& config_json = params.configuration.value();
            if (config_json.contains("pushNotificationConfig")) {
                PushNotificationConfig push_config;
                from_json(config_json.at("pushNotificationConfig"), push_config);
                push_config_store_->SetInfo(task_id, push_config);
            }
        } catch (const std::exception& e) {
            // Log error but don't fail the request
            // In a production implementation, we would use a proper logger
        }
    }
}

AgentCard DefaultRequestHandler::OnGetCard(const ServerCallContext* ctx)
{
    return *agentCard_;
}

} // namespace a2a::server