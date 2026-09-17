/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <utility>

#include "types.h"
#include "error.h"
#include "jsonrpc.h"
#include "common_types.h"
#include "utils_helpers.h"
#include "jsonrpc_handler.h"

namespace A2A::Server {

using nlohmann::json;

json JSONRPCHandler::OnMessageSend(const json& req, StreamEmitter emit, const std::string& method)
{
    auto id = req.value(JSON_FIELD_ID, json{});
    try {
        const MessageSendParams p = req.at(JSON_FIELD_PARAMS);
        handler_->OnSendMessage(p, nullptr, std::move(emit), method);
        // 正常处理场景，通过emit发送
        return {};
    } catch (const A2AServerError& e) {
        return MakeError(id, e.statusCode, e.what());
    } catch (const std::exception& e) {
        return MakeError(id, static_cast<int>(A2AErrorCode::JSONRPC_INTERNAL_ERROR), e.what());
    }
}

json JSONRPCHandler::OnGetTask(const json& req)
{
    auto id = req.value(JSON_FIELD_ID, json{});
    try {
        const TaskQueryParams p = req.at(JSON_FIELD_PARAMS);
        auto t = handler_->OnGetTask(p, nullptr);
        return json{{JSON_FIELD_JSONRPC, JSON_VERSION}, {JSON_FIELD_ID, id}, {JSON_FIELD_RESULT, t}};
    } catch (const A2AServerError& e) {
        return MakeError(id, e.statusCode, e.what());
    } catch (const std::exception& e) {
        return MakeError(id, static_cast<int>(A2AErrorCode::JSONRPC_INTERNAL_ERROR), e.what());
    }
}

json JSONRPCHandler::OnCancelTask(const json& req)
{
    auto id = req.value(JSON_FIELD_ID, json{});
    try {
        const TaskIdParams p = req.at(JSON_FIELD_PARAMS);
        auto t = handler_->OnCancelTask(p, nullptr);
        return json{{JSON_FIELD_JSONRPC, JSON_VERSION}, {JSON_FIELD_ID, id}, {JSON_FIELD_RESULT, t}};
    } catch (const A2AServerError& e) {
        return MakeError(id, e.statusCode, e.what());
    } catch (const std::exception& e) {
        return MakeError(id, static_cast<int>(A2AErrorCode::JSONRPC_INTERNAL_ERROR), e.what());
    }
}

json JSONRPCHandler::OnSetPushNotificationConfig(const json& req)
{
    auto id = req.value(JSON_FIELD_ID, json{});
    try {
        const auto cfg = req.at(JSON_FIELD_PARAMS).get<TaskPushNotificationConfig>();
        handler_->OnSetTaskPushNotificationConfig(cfg, nullptr);
        return json{{JSON_FIELD_JSONRPC, JSON_VERSION}, {JSON_FIELD_ID, id}, {JSON_FIELD_RESULT, cfg}};
    } catch (const A2AServerError& e) {
        return MakeError(id, e.statusCode, e.what());
    } catch (const std::exception& e) {
        return MakeError(id, static_cast<int>(A2AErrorCode::JSONRPC_INTERNAL_ERROR), e.what());
    }
}

json JSONRPCHandler::OnGetPushNotificationConfig(const json& req)
{
    auto id = req.value(JSON_FIELD_ID, json{});
    try {
        const auto p = req.at(JSON_FIELD_PARAMS).get<GetTaskPushNotificationConfigParams>();
        auto r = handler_->OnGetTaskPushNotificationConfig(p, nullptr);
        return json{{JSON_FIELD_JSONRPC, JSON_VERSION}, {JSON_FIELD_ID, id}, {JSON_FIELD_RESULT, r}};
    } catch (const A2AServerError& e) {
        return MakeError(id, e.statusCode, e.what());
    } catch (const std::exception& e) {
        return MakeError(id, static_cast<int>(A2AErrorCode::JSONRPC_INTERNAL_ERROR), e.what());
    }
}

json JSONRPCHandler::OnListPushNotificationConfig(const json& req)
{
    auto id = req.value(JSON_FIELD_ID, json{});
    try {
        const auto p = req.at(JSON_FIELD_PARAMS).get<ListTaskPushNotificationConfigParams>();
        auto r = handler_->OnListTaskPushNotificationConfigs(p, nullptr);
        return json{{JSON_FIELD_JSONRPC, JSON_VERSION}, {JSON_FIELD_ID, id}, {JSON_FIELD_RESULT, r}};
    } catch (const A2AServerError& e) {
        return MakeError(id, e.statusCode, e.what());
    } catch (const std::exception& e) {
        return MakeError(id, static_cast<int>(A2AErrorCode::JSONRPC_INTERNAL_ERROR), e.what());
    }
}

json JSONRPCHandler::OnDeletePushNotificationConfig(const json& req)
{
    auto id = req.value(JSON_FIELD_ID, json{});
    try {
        const auto p = req.at(JSON_FIELD_PARAMS).get<DeleteTaskPushNotificationConfigParams>();
        handler_->OnDeleteTaskPushNotificationConfig(p, nullptr);
        return json{{JSON_FIELD_JSONRPC, JSON_VERSION}, {JSON_FIELD_ID, id}, {JSON_FIELD_RESULT, nullptr}};
    } catch (const A2AServerError& e) {
        return MakeError(id, e.statusCode, e.what());
    } catch (const std::exception& e) {
        return MakeError(id, static_cast<int>(A2AErrorCode::JSONRPC_INTERNAL_ERROR), e.what());
    }
}

json JSONRPCHandler::OnGetAgentCard(const json& req)
{
    auto id = req.value(JSON_FIELD_ID, json{});
    try {
        auto c = handler_->OnGetCard(nullptr);
        return json{{JSON_FIELD_JSONRPC, JSON_VERSION}, {JSON_FIELD_ID, id}, {JSON_FIELD_RESULT, c}};
    } catch (const A2AServerError& e) {
        return MakeError(id, e.statusCode, e.what());
    } catch (const std::exception& e) {
        return MakeError(id, static_cast<int>(A2AErrorCode::JSONRPC_INTERNAL_ERROR), e.what());
    }
}

void JSONRPCHandler::OnMessageSendStreaming(const json& req, StreamEmitter emit)
{
    const MessageSendParams p = req.at(JSON_FIELD_PARAMS);
    handler_->OnSendMessageStreaming(p, std::move(emit), nullptr);
}

void JSONRPCHandler::OnResubscribeToTask(const json& req, StreamEmitter emit)
{
    const TaskIdParams p = req.at(JSON_FIELD_PARAMS);
    handler_->OnResubscribeToTask(p, std::move(emit), nullptr);
}

} // namespace A2A::Server