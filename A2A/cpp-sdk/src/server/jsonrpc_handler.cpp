/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "jsonrpc_handler.h"
#include "utils/errors.h"

namespace a2a::server {

using nlohmann::json;

static json make_error(const json& id, int code, const std::string& msg)
{
    return json{{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", msg}}}};
}

json JSONRPCHandler::OnMessageSend(const json& req)
{
    auto id = req.value("id", json{});
    try {
        MessageSendParams p{};
        const auto& pr = req.at("params");
        p.message = pr.at("message").get<Message>();
        if (pr.contains("configuration")) {
            p.configuration = pr.at("configuration");
        }
        if (pr.contains("metadata")) {
            p.metadata = pr.at("metadata");
        }
        auto r = handler_->OnSendMessage(p, nullptr);
        json result = std::holds_alternative<Message>(r) ? json(std::get<Message>(r)) : json(std::get<Task>(r));
        return json{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
    } catch (const std::exception& e) {
        return make_error(id, static_cast<int>(JSONRPCErrorCode::InternalError), e.what());
    }
}

json JSONRPCHandler::OnGetTask(const json& req)
{
    auto id = req.value("id", json{});
    try {
        TaskQueryParams p{};
        const auto& pr = req.at("params");
        p.id = pr.at("id").get<std::string>();
        if (pr.contains("historyLength")) {
            p.historyLength = pr.at("historyLength").get<int>();
        }
        if (pr.contains("metadata")) {
            p.metadata = pr.at("metadata");
        }
        auto t = handler_->OnGetTask(p, nullptr);
        return json{{"jsonrpc", "2.0"}, {"id", id}, {"result", t}};
    } catch (const std::exception& e) {
        return make_error(id, static_cast<int>(JSONRPCErrorCode::InternalError), e.what());
    }
}

json JSONRPCHandler::OnCancelTask(const json& req)
{
    auto id = req.value("id", json{});
    try {
        TaskIdParams p{};
        const auto& pr = req.at("params");
        p.id = pr.at("id").get<std::string>();
        if (pr.contains("metadata")) {
            p.metadata = pr.at("metadata");
        }
        auto t = handler_->OnCancelTask(p, nullptr);
        return json{{"jsonrpc", "2.0"}, {"id", id}, {"result", t}};
    } catch (const std::exception& e) {
        return make_error(id, static_cast<int>(JSONRPCErrorCode::InternalError), e.what());
    }
}

json JSONRPCHandler::OnSetPushNotificationConfig(const json& req)
{
    auto id = req.value("id", json{});
    try {
        const auto cfg = req.at("params").get<TaskPushNotificationConfig>();
        auto r = handler_->OnSetTaskPushNotificationConfig(cfg, nullptr);
        return json{{"jsonrpc", "2.0"}, {"id", id}, {"result", r}};
    } catch (const std::exception& e) {
        return make_error(id, static_cast<int>(JSONRPCErrorCode::InternalError), e.what());
    }
}

json JSONRPCHandler::OnGetPushNotificationConfig(const json& req)
{
    auto id = req.value("id", json{});
    try {
        const auto p = req.at("params").get<GetTaskPushNotificationConfigParams>();
        auto r = handler_->OnGetTaskPushNotificationConfig(p, nullptr);
        return json{{"jsonrpc", "2.0"}, {"id", id}, {"result", r}};
    } catch (const std::exception& e) {
        return make_error(id, static_cast<int>(JSONRPCErrorCode::InternalError), e.what());
    }
}

json JSONRPCHandler::OnListPushNotificationConfig(const json& req)
{
    auto id = req.value("id", json{});
    try {
        const auto p = req.at("params").get<ListTaskPushNotificationConfigParams>();
        auto r = handler_->OnListTaskPushNotificationConfigs(p, nullptr);
        return json{{"jsonrpc", "2.0"}, {"id", id}, {"result", r}};
    } catch (const std::exception& e) {
        return make_error(id, static_cast<int>(JSONRPCErrorCode::InternalError), e.what());
    }
}

json JSONRPCHandler::OnDeletePushNotificationConfig(const json& req)
{
    auto id = req.value("id", json{});
    try {
        const auto p = req.at("params").get<DeleteTaskPushNotificationConfigParams>();
        handler_->OnDeleteTaskPushNotificationConfig(p, nullptr);
        return json{{"jsonrpc", "2.0"}, {"id", id}, {"result", nullptr}};
    } catch (const std::exception& e) {
        return make_error(id, static_cast<int>(JSONRPCErrorCode::InternalError), e.what());
    }
}

json JSONRPCHandler::OnGetAgentCard(const json& req)
{
    auto id = req.value("id", json{});
    try {
        auto c = handler_->OnGetCard(nullptr);
        return json{{"jsonrpc", "2.0"}, {"id", id}, {"result", c}};
    } catch (const std::exception& e) {
        return make_error(id, static_cast<int>(JSONRPCErrorCode::InternalError), e.what());
    }
}

void JSONRPCHandler::OnMessageSendStreaming(const json& req, const RequestHandler::StreamEmitter& emit)
{
    MessageSendParams p{};
    const auto& pr = req.at("params");
    p.message = pr.at("message").get<Message>();
    if (pr.contains("configuration")) {
        p.configuration = pr.at("configuration");
    }
    if (pr.contains("metadata")) {
        p.metadata = pr.at("metadata");
    }
    handler_->OnSendMessageStreaming(p, emit, nullptr);
}

void JSONRPCHandler::OnResubscribeToTask(const json& req, const RequestHandler::StreamEmitter& emit)
{
    TaskIdParams p{};
    const auto& pr = req.at("params");
    p.id = pr.at("id").get<std::string>();
    if (pr.contains("metadata")) {
        p.metadata = pr.at("metadata");
    }
    handler_->OnResubscribeToTask(p, emit, nullptr);
}

} // namespace a2a::server