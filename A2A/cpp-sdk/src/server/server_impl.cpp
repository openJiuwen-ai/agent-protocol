/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <nlohmann/json.hpp>
#include <utility>

#include "http_server_transport.h"
#include "jsonrpc_handler.h"
#include "transport_emitter.h"
#include "default_request_handler.h"
#include "in_memory_queue_manager.h"
#include "a2a_errno.h"
#include "types.h"
#include "error.h"
#include "jsonrpc.h"
#include "server_impl.h"

namespace A2A::Server {
static bool IsSupportedStreamingMethod(const std::string& method)
{
    return method == "message/stream" || method == "task/resubscribe";
}

static std::unique_ptr<Transport::ServerTransport> BuildAgentCardTransport(
    const std::shared_ptr<AgentCard>& agentCard, const ServerConfig& config)
{
    if (!agentCard || agentCard->supportedInterfaces.empty()) {
        return nullptr;
    }

    if (agentCard->supportedInterfaces[0].protocolBinding == JSONRPC_TRANSPORT) {
        return std::make_unique<A2A::Transport::HttpServerTransport>(std::get<HttpConfig>(config));
    }
    // Placeholder for other transport types
    return nullptr;
}

ServerImpl::ServerImpl(std::shared_ptr<AgentCard> agentCard,
    std::shared_ptr<AgentCard> extendedAgentCard,
    const std::shared_ptr<AgentExecutor>& agent_executor,
    ServerConfig config,
    const std::shared_ptr<TaskStore>& taskStore)
    : agentCard_(std::move(agentCard)),
      extendedAgentCard_(std::move(extendedAgentCard)),
      config_(std::move(config)),
      handler_(std::make_shared<DefaultRequestHandler>(agent_executor, agentCard_,
          std::make_shared<InMemoryQueueManager>())),
      jsonRpcHandler_(std::make_unique<JSONRPCHandler>(handler_)),
      transport_(BuildAgentCardTransport(agentCard_, config_))
{
}

ServerImpl::~ServerImpl()
{
    if (transport_ != nullptr) {
        ServerImpl::Stop();
    }
}

int ServerImpl::Start()
{
    transport_->SetRpcHandler(
        [this](const std::string& req_body, std::string& resp_body, Transport::TransportEmitter& emitter) {
        try {
            auto req = json::parse(req_body);
            std::string method = req.value("method", "");
            if (IsSupportedStreamingMethod(method)) {
                HandleStreamingRequest(req, method, emitter);
            } else {
                HandleNonStreamingRequest(req, req_body, resp_body, method);
            }
        } catch (const std::exception& e) {
            // For non-streaming requests, set error in response body
            auto err = json{{"jsonrpc", "2.0"},
                            {"id", nullptr},
                            {"error", {{"code", static_cast<int>(JSONRPCErrorCode::INTERNAL_ERROR)},
                                {"message", std::string("Internal error: ") + e.what()}}}};
            resp_body = err.dump();
        }
    });

    transport_->SetCardHandler([this](const std::string& /* req_body */, std::string& resp_body) {
        nlohmann::json ret = OnGetCard(nullptr);
        resp_body = ret.dump();
    });
    return transport_->Start();
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

void ServerImpl::HandleStreamingRequest(const nlohmann::json& req, const std::string& method,
    Transport::TransportEmitter& emitter)
{
    try {
        std::function<void(const StreamEvent&)> streamEmit;
        CreateStreamEmitter(req, streamEmit, emitter);

        // Directly call JSONRPCHandler's streaming methods
        if (method == "message/stream") {
            jsonRpcHandler_->OnMessageSendStreaming(req, streamEmit);
        } else if (method == "task/resubscribe") {
            jsonRpcHandler_->OnResubscribeToTask(req, streamEmit);
        } else {
            throw std::runtime_error("Unsupported streaming method: " + method);
        }
    } catch (const std::exception& e) {
        json err = {{"kind", "error"}, {"message", std::string("Streaming error: ") + e.what()}};
        emitter.WriteData(err.dump());
        emitter.WriteDone();
    }
}

void ServerImpl::CreateStreamEmitter(const nlohmann::json& req, std::function<void(const StreamEvent&)>& streamEmit,
    Transport::TransportEmitter& emitter)
{
    streamEmit = [&](const StreamEvent& event) {
        json j;
        if (auto* task = std::get_if<Task>(&event)) {
            SendMessageSuccessResponse response;
            response.result = *task;
            j = response;
        } else if (auto* msg = std::get_if<Message>(&event)) {
            SendMessageSuccessResponse response;
            response.result = *msg;
            j = response;
        } else if (auto* artifact = std::get_if<TaskArtifactUpdateEvent>(&event)) {
            j = {{"jsonrpc", "2.0"}, {"id", req.value("id", json{})}, {"result", *artifact}};
        } else if (auto* status = std::get_if<TaskStatusUpdateEvent>(&event)) {
            j = {{"jsonrpc", "2.0"}, {"id", req.value("id", json{})}, {"result", *status}};
        }
        emitter.WriteData(j.dump());
    };
}

void ServerImpl::HandleNonStreamingRequest(const nlohmann::json& req, const std::string& reqBody,
    std::string& respBody, const std::string& method)
{
    // Special handling for /card (when req_body is "{}")
    if (reqBody == "{}") {
        json cardReq = {{"jsonrpc", "2.0"}, {"method", "agent.card"}, {"id", 1}};
        auto resp = jsonRpcHandler_->OnGetAgentCard(cardReq);
        respBody = resp.dump();
        return;
    }

    ProcessStandardJsonRpc(req, respBody, method);
}

void ServerImpl::ProcessStandardJsonRpc(const nlohmann::json& req, std::string& respBody, const std::string& method)
{
    if (method == "agent.card") {
        respBody = jsonRpcHandler_->OnGetAgentCard(req).dump();
    } else if (method == "message/send") {
        respBody = jsonRpcHandler_->OnMessageSend(req).dump();
    } else if (method == "task/get") {
        respBody = jsonRpcHandler_->OnGetTask(req).dump();
    } else if (method == "task/cancel") {
        respBody = jsonRpcHandler_->OnCancelTask(req).dump();
    } else if (method == "push_notification/set") {
        respBody = jsonRpcHandler_->OnSetPushNotificationConfig(req).dump();
    } else if (method == "push_notification/get") {
        respBody = jsonRpcHandler_->OnGetPushNotificationConfig(req).dump();
    } else if (method == "push_notification/list") {
        respBody = jsonRpcHandler_->OnListPushNotificationConfig(req).dump();
    } else if (method == "push_notification/delete") {
        respBody = jsonRpcHandler_->OnDeletePushNotificationConfig(req).dump();
    } else {
        auto err = json{{"jsonrpc", "2.0"},
                        {"id", req.value("id", json{})},
                        {"error", {{"code", static_cast<int>(JSONRPCErrorCode::METHOD_NOT_FOUND)},
                            {"message", "Method not found: " + method}}}};
        respBody = err.dump();
    }
}

} // namespace A2A::Server