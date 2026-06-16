/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <nlohmann/json.hpp>
#include <utility>

#include "http_server_transport.h"
#include "jsonrpc_handler.h"
#include "transport_emitter.h"
#include "default_request_handler.h"
#include "types.h"
#include "common_types.h"
#include "error.h"
#include "jsonrpc.h"
#include "a2a_log.h"
#include "utils_helpers.h"
#include "server_impl.h"

namespace A2A::Server {
static bool IsSupportedStreamingMethod(const std::string& method)
{
    return method == METHOD_MESSAGE_STREAM || method == METHOD_TASK_RESUBSCRIBE;
}

static std::unique_ptr<Transport::ServerTransport> BuildAgentCardTransport(
    const std::shared_ptr<AgentCard>& agentCard, const ServerConfig& config)
{
    if (!agentCard || agentCard->supportedInterfaces.empty()) {
        return nullptr;
    }

    if (agentCard->supportedInterfaces[0].protocolBinding == JSONRPC_TRANSPORT) {
        return std::make_unique<Transport::HttpServerTransport>(config);
    }
    // Placeholder for other transport types
    return nullptr;
}

ServerImpl::ServerImpl(std::shared_ptr<AgentCard> agentCard,
    std::shared_ptr<AgentCard> extendedAgentCard,
    const std::shared_ptr<AgentExecutor>& agentExecutor,
    ServerConfig config,
    std::shared_ptr<TaskStore> taskStore)
    : agentCard_(std::move(agentCard)),
    extendedAgentCard_(std::move(extendedAgentCard)),
    config_(std::move(config)),
    handler_(std::make_shared<DefaultRequestHandler>(agentExecutor, agentCard_, taskStore)),
    jsonRpcHandler_(std::make_unique<JSONRPCHandler>(handler_)),
    transport_(BuildAgentCardTransport(agentCard_, config_)),
    started_(false)
{
}

ServerImpl::ServerImpl(std::shared_ptr<AgentCard> agentCard,
    std::shared_ptr<AgentCard> extendedAgentCard,
    const std::shared_ptr<AgentExecutor>& agentExecutor,
    ServerConfig config,
    std::shared_ptr<Transport::ServerTransport> transport,
    std::shared_ptr<TaskStore> taskStore)
    : agentCard_(std::move(agentCard)),
    extendedAgentCard_(std::move(extendedAgentCard)),
    config_(std::move(config)),
    handler_(std::make_shared<DefaultRequestHandler>(agentExecutor, agentCard_, taskStore)),
    jsonRpcHandler_(std::make_unique<JSONRPCHandler>(handler_)),
    transport_(transport),
    started_(false)
{
}

ServerImpl::~ServerImpl()
{
    ServerImpl::Stop();
    transport_.reset();
}

int ServerImpl::Start()
{
    if (started_.load()) {
        A2A_LOG(A2A_LOG_LEVEL_WARN, "Server already started, ignoring duplicate Start() call");
        return 0;
    }
    if (transport_ == nullptr) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "Start server failed, server transport is null");
        return 1;
    }
    transport_->SetRpcHandler(
        [this](const std::string& reqBody, std::string& respBody,
            std::shared_ptr<Transport::TransportEmitter> emitter) {
        if (!started_.load()) {
            A2A_LOG(A2A_LOG_LEVEL_WARN, "Server stopped, will not process request");
            return;
        }
        try {
            const auto req = json::parse(reqBody);
            if (const std::string method = req.value(JSON_FIELD_METHOD, ""); IsSupportedStreamingMethod(method)) {
                HandleStreamingRequest(req, method, emitter);
            } else {
                HandleNonStreamingRequest(req, reqBody, respBody, method, emitter);
            }
        } catch (const std::exception& e) {
            // For non-streaming requests, set error in response body
            A2A_LOG(A2A_LOG_LEVEL_ERROR, std::string("Parse request failed: ") + e.what());
            if (!respBody.empty()) {
                return;
            }
            auto err = json{{JSON_FIELD_JSONRPC, JSON_VERSION},
                            {JSON_FIELD_ID, nullptr},
                            {JSON_FIELD_ERROR, {{"code", static_cast<int>(A2AErrorCode::JSONRPC_INTERNAL_ERROR)},
                                {"message", std::string("Internal error: ") + e.what()}}}};
            respBody = err.dump();
        }
    });

    transport_->SetCardHandler([this](const std::string& /* req_body */, std::string& resp_body) {
        nlohmann::json ret = OnGetCard(nullptr);
        resp_body = ret.dump();
    });
    const int result = transport_->Start();
    if (result == 0) {
        started_.store(true);
    }
    return result;
}

void ServerImpl::Stop()
{
    if (transport_ != nullptr) {
        transport_->Stop();
    }
    started_.store(false);
}

AgentCard ServerImpl::OnGetAuthenticatedExtendedCard([[maybe_unused]] const ServerCallContext* context)
{
    if (!agentCard_) {
        throw std::runtime_error("AgentCard not initialized");
    }
    return *agentCard_;
}

AgentCard ServerImpl::OnGetCard([[maybe_unused]] const ServerCallContext* context)
{
    if (!agentCard_) {
        throw std::runtime_error("AgentCard not initialized");
    }
    return *agentCard_;
}

void ServerImpl::HandleStreamingRequest(const nlohmann::json& req, const std::string& method,
    const std::shared_ptr<Transport::TransportEmitter>& emitter)
{
    json id = {};
    try {
        id = req.value(JSON_FIELD_ID, json{});
        std::function<void(const StreamEvent&)> streamEmit;
        CreateStreamEmitter(req, streamEmit, emitter, true);

        if (!agentCard_->capabilities.streaming.value_or(false)) {
            throw A2AServerError("Streaming is not supported by the agent",
                static_cast<int>(A2AErrorCode::UNSUPPORTED_OPERATION));
        }

        // Directly call JSONRPCHandler's streaming methods
        if (method == METHOD_MESSAGE_STREAM) {
            jsonRpcHandler_->OnMessageSendStreaming(req, streamEmit);
        } else if (method == METHOD_TASK_RESUBSCRIBE) {
            jsonRpcHandler_->OnResubscribeToTask(req, streamEmit);
        } else {
            json error = MakeError(id, static_cast<int>(A2AErrorCode::JSONRPC_METHOD_NOT_FOUND),
                "Unsupported streaming method: " + method);
            emitter->WriteStreamingData(error.dump());
            emitter->WriteDone();
        }
    } catch (const A2AServerError& e) {
        json err = MakeError(id, e.statusCode, e.what());
        emitter->WriteStreamingData(err.dump());
        emitter->WriteDone();
    } catch (const std::exception& e) {
        json err = MakeError(id, static_cast<int>(A2AErrorCode::JSONRPC_INTERNAL_ERROR),
            std::string("Streaming error: ") + e.what());
        emitter->WriteStreamingData(err.dump());
        emitter->WriteDone();
    }
}

void ServerImpl::CreateStreamEmitter(const nlohmann::json& req, std::function<void(const StreamEvent&)>& streamEmit,
    const std::shared_ptr<Transport::TransportEmitter>& emitter, const bool streaming)
{
    std::optional<std::string> id = req.value(JSON_FIELD_ID, "");
    if (streaming) {
        streamEmit = [emitter, id](const StreamEvent& event) {
            SendStreamingMessageSuccessResponse response;
            response.id = id;
            response.result = event;
            json j = response;
            emitter->WriteStreamingData(j.dump());

            // Http SSE需要发送结束标志位
            if (IsFinalEvent(event)) {
                emitter->WriteDone();
            }
        };
    } else {
        streamEmit = [emitter, id](const StreamEvent& event) {
            SendMessageSuccessResponse response;
            response.id = id;
            if (std::holds_alternative<Task>(event)) {
                response.result = std::get<Task>(event);
            } else {
                response.result = std::get<Message>(event);
            }
            json j = response;
            emitter->WriteNonStreamingData(j.dump());
        };
    }
}

void ServerImpl::HandleNonStreamingRequest(const nlohmann::json& req, const std::string& reqBody,
    std::string& respBody, const std::string& method, const std::shared_ptr<Transport::TransportEmitter>& emitter)
{
    // Special handling for /card (when req_body is "{}")
    if (reqBody == "{}") {
        json cardReq = {{JSON_FIELD_JSONRPC, JSON_VERSION},
            {JSON_FIELD_METHOD, METHOD_AGENT_CARD_GET}, {JSON_FIELD_ID, 1}};
        auto resp = jsonRpcHandler_->OnGetAgentCard(cardReq);
        respBody = resp.dump();
        return;
    }

    ProcessStandardJsonRpc(req, respBody, method, emitter);
}

void ServerImpl::ProcessStandardJsonRpc(const nlohmann::json& req, std::string& respBody, const std::string& method,
    const std::shared_ptr<Transport::TransportEmitter>& emitter)
{
    if (method == METHOD_AGENT_CARD_GET) {
        respBody = jsonRpcHandler_->OnGetAgentCard(req).dump();
    } else if (method == METHOD_MESSAGE_SEND) {
        std::function<void(const StreamEvent&)> nonStreamEmit;
        CreateStreamEmitter(req, nonStreamEmit, emitter, false);
        const auto jsonResp = jsonRpcHandler_->OnMessageSend(req, std::move(nonStreamEmit), method);
        if (jsonResp.contains("error")) {
            respBody = jsonResp.dump();
        }
    } else if (method == METHOD_TASK_GET) {
        respBody = jsonRpcHandler_->OnGetTask(req).dump();
    } else if (method == METHOD_TASK_CANCEL) {
        respBody = jsonRpcHandler_->OnCancelTask(req).dump();
    } else if (method == METHOD_TASK_PUSH_NOTIFICATION_CONFIG_SET) {
        respBody = jsonRpcHandler_->OnSetPushNotificationConfig(req).dump();
    } else if (method == METHOD_TASK_PUSH_NOTIFICATION_CONFIG_GET) {
        respBody = jsonRpcHandler_->OnGetPushNotificationConfig(req).dump();
    } else if (method == METHOD_TASK_PUSH_NOTIFICATION_CONFIG_LIST) {
        respBody = jsonRpcHandler_->OnListPushNotificationConfig(req).dump();
    } else if (method == METHOD_TASK_PUSH_NOTIFICATION_CONFIG_DELETE) {
        respBody = jsonRpcHandler_->OnDeletePushNotificationConfig(req).dump();
    } else {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "Method not found");
        auto err = json{{JSON_FIELD_JSONRPC, JSON_VERSION},
                        {JSON_FIELD_ID, req.value(JSON_FIELD_ID, json{})},
                        {"error", {{"code", static_cast<int>(A2AErrorCode::JSONRPC_METHOD_NOT_FOUND)},
                            {"message", "Method not found: " + method}}}};
        respBody = err.dump();
    }
}

} // namespace A2A::Server