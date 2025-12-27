/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "client/a2a_card_resolver.h"
#include "http_client_transport.h"
#include "jsonrpc_transport.h"
#include "utils/errors.h"

namespace a2a::client {

JsonRpcTransport::JsonRpcTransport(std::string url, const a2a::AgentCard* agentCard,
                                   std::vector<ClientCallInterceptor*> interceptors)
    : url_(std::move(url)), http_(url_), interceptors_(std::move(interceptors)), generator_()
{
    if (agentCard) {
        agentCard_ = *agentCard;
    }
}

std::variant<Task, Message> JsonRpcTransport::SendMessage(const MessageSendParams& params,
                                                          const ClientCallContext* context)
{
    nlohmann::json rpc{{"jsonrpc", "2.0"},
                       {"id", generator_.Generate(IDGeneratorContext())},
                       {"method", "message/send"},
                       {"params", params}};

    std::map<std::string, std::string> headers;
    auto payload = ApplyInterceptors("message/send", rpc, headers, context);
    // apply headers to http_ if needed
    // (per-request headers not supported by simple HttpClientTransport; merge into default)
    a2a::transport::HttpClientTransport http = http_;
    for (const auto& kv : headers) {
        http.SetHeader(kv.first, kv.second);
    }

    auto respStr = http.SendData(payload.dump());
    auto resp = nlohmann::json::parse(respStr);
    if (resp.contains("error")) {
        throw a2a::A2AClientJSONRPCError(resp["error"]);
    }

    const auto& r = resp.at("result");
    if (r.contains("kind") && r.at("kind") == "message") {
        return r.get<Message>();
    }

    return r.get<Task>();
}

void JsonRpcTransport::SendMessageStreaming(const MessageSendParams& params, const TransportEventCallback& onEvent,
                                            const ClientCallContext* context)
{
    nlohmann::json rpc{{"jsonrpc", "2.0"},
                       {"id", generator_.Generate(IDGeneratorContext())},
                       {"method", "message/stream"},
                       {"params", params}};

    std::map<std::string, std::string> hdrs;
    auto payload = ApplyInterceptors("message/stream", rpc, hdrs, context);
    auto http = http_;
    for (const auto& kv : hdrs) {
        http.SetHeader(kv.first, kv.second);
    }

    http.SendDataStreaming(payload.dump(), onEvent);
}

Task JsonRpcTransport::GetTask(const TaskQueryParams& params, const ClientCallContext* context)
{
    nlohmann::json rpc{{"jsonrpc", "2.0"},
                       {"id", generator_.Generate(IDGeneratorContext())},
                       {"method", "tasks/get"},
                       {"params", params}};

    std::map<std::string, std::string> headers;
    auto payload = ApplyInterceptors("tasks/get", rpc, headers, context);
    a2a::transport::HttpClientTransport http = http_;
    for (const auto& kv : headers) {
        http.SetHeader(kv.first, kv.second);
    }

    auto respStr = http.SendData(payload.dump());
    auto resp = nlohmann::json::parse(respStr);
    if (resp.contains("error")) {
        throw a2a::A2AClientJSONRPCError(resp["error"]);
    }

    return resp.at("result").get<Task>();
}

Task JsonRpcTransport::CancelTask(const TaskIdParams& params, const ClientCallContext* context)
{
    nlohmann::json rpc{{"jsonrpc", "2.0"},
                       {"id", generator_.Generate(IDGeneratorContext())},
                       {"method", "tasks/cancel"},
                       {"params", params}};

    std::map<std::string, std::string> headers;
    auto payload = ApplyInterceptors("tasks/cancel", rpc, headers, context);
    a2a::transport::HttpClientTransport http = http_;
    for (const auto& kv : headers) {
        http.SetHeader(kv.first, kv.second);
    }

    auto respStr = http.SendData(payload.dump());
    auto resp = nlohmann::json::parse(respStr);
    if (resp.contains("error")) {
        throw a2a::A2AClientJSONRPCError(resp["error"]);
    }

    return resp.at("result").get<Task>();
}

TaskPushNotificationConfig JsonRpcTransport::SetTaskPushNotificationConfig(const TaskPushNotificationConfig& config,
                                                                           const ClientCallContext* context)
{
    nlohmann::json rpc{{"jsonrpc", "2.0"},
                       {"id", generator_.Generate(IDGeneratorContext())},
                       {"method", "tasks/pushNotificationConfig/set"},
                       {"params", config}};

    std::map<std::string, std::string> headers;
    auto payload = ApplyInterceptors("tasks/pushNotificationConfig/set", rpc, headers, context);
    a2a::transport::HttpClientTransport http = http_;
    for (const auto& kv : headers) {
        http.SetHeader(kv.first, kv.second);
    }
    auto respStr = http.SendData(payload.dump());
    auto resp = nlohmann::json::parse(respStr);
    if (resp.contains("error")) {
        throw a2a::A2AClientJSONRPCError(resp["error"]);
    }

    return resp.at("result").get<TaskPushNotificationConfig>();
}

TaskPushNotificationConfig JsonRpcTransport::GetTaskPushNotificationConfig(
    const GetTaskPushNotificationConfigParams& params, const ClientCallContext* context)
{
    nlohmann::json rpc{{"jsonrpc", "2.0"},
                       {"id", generator_.Generate(IDGeneratorContext())},
                       {"method", "tasks/pushNotificationConfig/get"},
                       {"params", params}};

    std::map<std::string, std::string> headers;
    auto payload = ApplyInterceptors("tasks/pushNotificationConfig/get", rpc, headers, context);
    a2a::transport::HttpClientTransport http = http_;
    for (const auto& kv : headers) {
        http.SetHeader(kv.first, kv.second);
    }

    auto respStr = http.SendData(payload);
    auto resp = nlohmann::json::parse(respStr);
    if (resp.contains("error")) {
        throw a2a::A2AClientJSONRPCError(resp["error"]);
    }

    return resp.at("result").get<TaskPushNotificationConfig>();
}

std::vector<TaskPushNotificationConfig> JsonRpcTransport::ListTaskPushNotificationConfigs(
    const ListTaskPushNotificationConfigParams& params, const ClientCallContext* context)
{
    return {};
}

void JsonRpcTransport::DeleteTaskPushNotificationConfig(const DeleteTaskPushNotificationConfigParams& params,
                                                        const ClientCallContext* context)
{
}

void JsonRpcTransport::Resubscribe(const TaskIdParams& params, const TransportEventCallback& onEvent,
                                   const ClientCallContext* context)
{
    nlohmann::json rpc{{"jsonrpc", "2.0"}, {"method", "tasks/resubscribe"}, {"params", params}};

    std::map<std::string, std::string> hdrs;
    auto payload = ApplyInterceptors("tasks/resubscribe", rpc, hdrs, context);
    auto http = http_;
    for (const auto& kv : hdrs) {
        http.SetHeader(kv.first, kv.second);
    }

    http.SendDataStreaming(payload.dump(), onEvent);
}

a2a::AgentCard JsonRpcTransport::GetCard(const ClientCallContext* context)
{
    if (agentCard_) {
        return *agentCard_;
    }

    A2ACardResolver resolver(url_);
    auto card = resolver.GetAgentCard();
    agentCard_ = card;
    return card;
}

void JsonRpcTransport::Close()
{
}

nlohmann::json JsonRpcTransport::ApplyInterceptors(const std::string& method, nlohmann::json payload,
                                                   std::map<std::string, std::string>& headers,
                                                   const ClientCallContext* context) const
{
    const a2a::AgentCard* cardPtr = agentCard_ ? &*agentCard_ : nullptr;
    for (auto* i : interceptors_) {
        i->Intercept(method, payload, headers, cardPtr, context);
    }
    return payload;
}

} // namespace a2a::client
