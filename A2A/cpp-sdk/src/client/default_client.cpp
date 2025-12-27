/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "client_task_manager.h"
#include "default_client.h"
#include "utils/errors.h"

namespace a2a::client {

DefaultClient::DefaultClient(const a2a::AgentCard& card, ClientConfig config,
                             std::shared_ptr<ClientTransport> transport, std::vector<Consumer> consumers,
                             std::vector<ClientCallInterceptor*> middleware)
    : card_(card),
      config_(config),
      transport_(std::move(transport)),
      consumers_(std::move(consumers)),
      middleware_(std::move(middleware))
{
}

DefaultClient::~DefaultClient() = default;

void DefaultClient::SendMessage(const Message& msg, const ClientCallContext* context, const Consumer& consumer)
{
    SendMessage(msg, context, std::vector<Consumer>{consumer});
}

void DefaultClient::SendMessage(const Message& msg, const ClientCallContext* context,
                                const std::vector<Consumer>& consumers)
{
    MessageSendParams params;
    params.message = msg; // populate configuration
    nlohmann::json cfg;
    if (!config_.acceptedOutputModes.empty()) {
        cfg["acceptedOutputModes"] = config_.acceptedOutputModes;
    }

    cfg["blocking"] = !config_.polling;
    if (!config_.pushNotificationConfigs.empty()) {
        cfg["pushNotificationConfig"] = config_.pushNotificationConfigs.front();
    }
    params.configuration = cfg;

    const bool canStream = config_.streaming && card_.capabilities.streaming.value_or(false);
    if (!canStream) {
        auto result = transport_->SendMessage(params, context);
        if (std::holds_alternative<Message>(result)) {
            ClientEvent ev = std::get<Message>(result);
            Consume(ev);
            for (auto& c : consumers) {
                c(ev, this->card_);
            }
            return;
        }
        Task t = std::get<Task>(result);
        ClientEvent ev = std::make_pair(std::move(t), std::monostate{});
        Consume(ev);
        for (auto& c : consumers) {
            c(ev, this->card_);
        }
        return;
    }

    // streaming path: the first event determines if it's a Message or Task/Updates; we parse per SSE event
    bool seenFirst = false;
    ClientTaskManager mgr; // if async send, lifetime of ClientTaskManager should be reconsidered
    transport_->SendMessageStreaming(
        params,
        [&](const std::string& data) {
            nlohmann::json j;
            try {
                nlohmann::json res = nlohmann::json::parse(data);
                if (!res.contains("result")) {
                    throw a2a::A2AClientJSONError("result not found");
                }
                j = res.at("result");
            } catch (const std::exception& e) {
                throw a2a::A2AClientJSONError(e.what());
            }

            if (!seenFirst) {
                seenFirst = true;
                // first event could be Message or Task
                if (j.contains("kind") && j.at("kind") == "message") {
                    auto m = j.get<Message>();
                    ClientEvent ev = m;
                    Consume(ev);
                    for (auto& c : consumers) {
                        c(ev, this->card_);
                    }
                    return; // done
                }
            }
            // From here events are Task or Update
            if (j.contains("kind") && j.at("kind") == "task") {
                auto t = j.get<Task>();
                mgr.SaveTaskEvent(t);
                ClientEvent ev = std::make_pair(t, std::monostate{});
                Consume(ev);
                for (auto& c : consumers) {
                    c(ev, this->card_);
                }
                return;
            }
            // status-update or artifact-update
            if (j.contains("kind") && j.at("kind") == "status-update") {
                auto u = j.get<TaskStatusUpdateEvent>();
                mgr.SaveTaskEvent(u);
                Task tsk = mgr.GetTaskOrRaise();
                ClientEvent ev = std::make_pair(tsk, u);
                Consume(ev);
                for (auto& c : consumers) {
                    c(ev, this->card_);
                }
                return;
            }
            if (j.contains("kind") && j.at("kind") == "artifact-update") {
                auto u = j.get<TaskArtifactUpdateEvent>();
                mgr.SaveTaskEvent(u);
                Task tsk = mgr.GetTaskOrRaise();
                ClientEvent ev = std::make_pair(tsk, u);
                Consume(ev);
                for (auto& c : consumers) {
                    c(ev, this->card_);
                }
                return;
            }
        },
        context);
}

Task DefaultClient::GetTask(const TaskQueryParams& params, const ClientCallContext* context)
{
    return transport_->GetTask(params, context);
}

Task DefaultClient::CancelTask(const TaskIdParams& params, const ClientCallContext* context)
{
    return transport_->CancelTask(params, context);
}

TaskPushNotificationConfig DefaultClient::SetTaskPushNotificationConfig(const TaskPushNotificationConfig& config,
                                                                        const ClientCallContext* context)
{
    return transport_->SetTaskPushNotificationConfig(config, context);
}

TaskPushNotificationConfig DefaultClient::GetTaskPushNotificationConfig(
    const GetTaskPushNotificationConfigParams& params, const ClientCallContext* context)
{
    return transport_->GetTaskPushNotificationConfig(params, context);
}

std::vector<TaskPushNotificationConfig> DefaultClient::ListTaskPushNotificationConfigs(
    const ListTaskPushNotificationConfigParams& params, const ClientCallContext* context)
{
    return transport_->ListTaskPushNotificationConfigs(params, context);
}

void DefaultClient::DeleteTaskPushNotificationConfig(const DeleteTaskPushNotificationConfigParams& params,
                                                     const ClientCallContext* context)
{
    return transport_->DeleteTaskPushNotificationConfig(params, context);
}

void DefaultClient::Resubscribe(const TaskIdParams& params, const ClientCallContext* context, const Consumer& consumer)
{
    const bool canStream = config_.streaming && card_.capabilities.streaming.value_or(false);
    if (!canStream) {
        throw std::runtime_error("client and/or server do not support resubscription.");
    }

    ClientTaskManager mgr;
    transport_->Resubscribe(
        params,
        [&](const std::string& data) {
            nlohmann::json j;
            try {
                nlohmann::json res = nlohmann::json::parse(data);
                if (!res.contains("result")) {
                    throw a2a::A2AClientJSONError("result not found");
                }
                j = res.at("result");
            } catch (const std::exception& e) {
                throw a2a::A2AClientJSONError(e.what());
            }

            // Only task/update events
            if (j.contains("kind") && j.at("kind") == "task") {
                auto t = j.get<Task>();
                mgr.SaveTaskEvent(t);
                ClientEvent ev = std::make_pair(t, std::monostate{});
                Consume(ev);
                consumer(ev, card_);
                return;
            }
            if (j.contains("kind") && j.at("kind") == "status-update") {
                auto u = j.get<TaskStatusUpdateEvent>();
                mgr.SaveTaskEvent(u);
                Task tsk = mgr.GetTaskOrRaise();
                ClientEvent ev = std::make_pair(tsk, u);
                Consume(ev);
                consumer(ev, card_);
                return;
            }
            if (j.contains("kind") && j.at("kind") == "artifact-update") {
                auto u = j.get<TaskArtifactUpdateEvent>();
                mgr.SaveTaskEvent(u);
                Task tsk = mgr.GetTaskOrRaise();
                ClientEvent ev = std::make_pair(tsk, u);
                Consume(ev);
                consumer(ev, card_);
                return;
            }
        },
        context);
}

a2a::AgentCard DefaultClient::GetCard(const ClientCallContext* context)
{
    return transport_->GetCard(context);
}

void DefaultClient::AddEventConsumer(Consumer c)
{
    consumers_.push_back(std::move(c));
}

void DefaultClient::AddRequestMiddleware(ClientCallInterceptor* middleware)
{
    middleware_.push_back(std::move(middleware));
}

void DefaultClient::Close()
{
    transport_->Close();
}

void DefaultClient::Consume(const ClientEvent& ev)
{
    for (auto& c : consumers_) {
        c(ev, card_);
    }
}

} // namespace a2a::client
