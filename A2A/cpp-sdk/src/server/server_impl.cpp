/*
* Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <iostream>
#include <nlohmann/json.hpp>

#include "http_server_transport.h"
#include "jsonrpc_handler.h"
#include "server/request_handler.h"
#include "server_impl.h"
#include "utils/errors.h"


using json = nlohmann::json;
using StreamEvent = a2a::server::RequestHandler::StreamEvent;
using Task = a2a::Task;
using Message = a2a::Message;
using TaskArtifactUpdateEvent = a2a::TaskArtifactUpdateEvent;
using TaskStatusUpdateEvent = a2a::TaskStatusUpdateEvent;

namespace a2a::server {
static bool IsSupportedStreamingMethod(const std::string& method)
{
    return method == "message/stream" || method == "task/resubscribe";
}

ServerImpl::ServerImpl(const std::string& transportType, std::shared_ptr<RequestHandler> handler,
                       std::shared_ptr<AgentCard> agentCard)
    : transportType_(transportType),
      handler_(std::move(handler)),
      agentCard_(agentCard),
      jsonRpcHandler_(std::make_unique<JSONRPCHandler>(handler_)),
      transport_(std::make_unique<a2a::transport::HttpServerTransport>(agentCard))
{
}

ServerImpl::~ServerImpl() = default;

int ServerImpl::Start(const std::string& ip, int port)
{
    // 非流式处理器
    transport_->SetEventHandler([this](const std::string& req_body, std::string& resp_body) {
        try {
            // 特殊处理 /card
            if (req_body == "{}") {
                json req = {{"jsonrpc", "2.0"}, {"method", "agent.card"}, {"id", 1}};
                auto resp = jsonRpcHandler_->OnGetAgentCard(req);
                resp_body = resp.dump();
                return;
            }

            // 标准 JSON-RPC
            auto req = json::parse(req_body);
            std::string method = req.value("method", "");
            if (method == "agent.card") {
                resp_body = jsonRpcHandler_->OnGetAgentCard(req).dump();
            } else if (method == "message/send") {
                resp_body = jsonRpcHandler_->OnMessageSend(req).dump();
            } else if (method == "task/get") {
                resp_body = jsonRpcHandler_->OnGetTask(req).dump();
            } else if (method == "task/cancel") {
                resp_body = jsonRpcHandler_->OnCancelTask(req).dump();
            } else if (method == "push_notification/set") {
                resp_body = jsonRpcHandler_->OnSetPushNotificationConfig(req).dump();
            } else if (method == "push_notification/get") {
                resp_body = jsonRpcHandler_->OnGetPushNotificationConfig(req).dump();
            } else if (method == "push_notification/list") {
                resp_body = jsonRpcHandler_->OnListPushNotificationConfig(req).dump();
            } else if (method == "push_notification/delete") {
                resp_body = jsonRpcHandler_->OnDeletePushNotificationConfig(req).dump();
            } else {
                auto err = json{{"jsonrpc", "2.0"},
                                {"id", req.value("id", json{})},
                                {"error", {{"code", static_cast<int>(JSONRPCErrorCode::MethodNotFound)},
                                    {"message", "Method not found: " + method}}}};
                resp_body = err.dump();
            }
        } catch (const std::exception& e) {
            auto err = json{{"jsonrpc", "2.0"},
                            {"id", nullptr},
                            {"error", {{"code", static_cast<int>(JSONRPCErrorCode::InternalError)},
                                {"message", std::string("Internal error: ") + e.what()}}}};
            resp_body = err.dump();
        }
    });

    transport_->SetStreamEventHandler([this](const std::string& req_body, auto& emitter) {
        try {
            auto req = json::parse(req_body);
            std::string method = req.value("method", "");
            if (!IsSupportedStreamingMethod(method)) {
                throw std::runtime_error("Unsupported streaming method: " + method);
            }

            std::function<void(const StreamEvent&)> stream_emit = [&](const StreamEvent& event) {
                json j;
                if (auto* task = std::get_if<Task>(&event)) {
                    SendMessageSuccessResponse response;
                    response.result = *task;
                    to_json(j, response);
                } else if (auto* msg = std::get_if<Message>(&event)) {
                    SendMessageSuccessResponse response;
                    response.result = *msg;
                    to_json(j, response);
                } else if (auto* artifact = std::get_if<TaskArtifactUpdateEvent>(&event)) {
                    j = {{"jsonrpc", "2.0"}, {"id", req.value("id", json{})}, {"result", *artifact}};
                } else if (auto* status = std::get_if<TaskStatusUpdateEvent>(&event)) {
                    j = {{"jsonrpc", "2.0"}, {"id", req.value("id", json{})}, {"result", *status}};
                }
                emitter.WriteData(j.dump());
            };

            // 直接调用JSONRPCHandler的流式方法
            if (method == "message/stream") {
                jsonRpcHandler_->OnMessageSendStreaming(req, stream_emit);
            } else if (method == "task/resubscribe") {
                jsonRpcHandler_->OnResubscribeToTask(req, stream_emit);
            } else {
                throw std::runtime_error("Unsupported streaming method: " + method);
            }
        } catch (const std::exception& e) {
            json err = {{"kind", "error"}, {"message", std::string("Streaming error: ") + e.what()}};
            emitter.WriteData(err.dump());
            emitter.WriteDone();
        }
    });

    std::cout << "Starting A2A server on " << ip << ":" << port << std::endl;
    return transport_->Start(ip, port);
}

void ServerImpl::Stop()
{
    transport_->Stop();
    transport_.reset();
}

AgentCard ServerImpl::OnGetAuthenticatedExtendedCard(const ServerCallContext* context)
{
    if (!agentCard_) {
        throw std::runtime_error("AgentCard not initialized");
    }
    return *agentCard_;
}

AgentCard ServerImpl::OnGetCard(const ServerCallContext* context)
{
    if (!agentCard_) {
        throw std::runtime_error("AgentCard not initialized");
    }
    return *agentCard_;
}

} // namespace a2a::server