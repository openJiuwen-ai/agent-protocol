/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "base_session.h"
#include "mcp_error.h"
#include "mcp_log.h"
#include "mcp_type.h"

namespace Mcp {

void BaseSession::SendRequest(std::unique_ptr<Request> request, std::function<void(std::shared_ptr<Result>)> completion,
                              std::optional<std::chrono::seconds> requestTimeout,
                              std::optional<ProgressCallback> progressCallback)
{
    if (request == nullptr) {
        throw std::invalid_argument("Request cannot be null");
    }

    RequestId requestId = RequestId(requestId_.fetch_add(1));

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (progressCallback) {
            progressCallbacks_[requestId] = std::move(*progressCallback);
        }
        if (completion)
            completionCallbacks_[requestId] = std::move(completion);
    }

    JSONRPCRequest jsonrpcRequest;
    jsonrpcRequest.jsonrpc_ = JSONRPC_VERSION;
    jsonrpcRequest.id_ = requestId;
    jsonrpcRequest.method_ = request->method_;
    jsonrpcRequest.request_ = std::move(request);

    JSONRPCMessage msg = std::move(jsonrpcRequest);
    
    try {
        clientTransport_->SendMessage(msg);
    } catch (const std::exception& e) {
        if (completion) {
            auto err = std::make_shared<ErrorResult>();
            err->code = static_cast<int>(JsonRpcErrorCode::INTERNAL_ERROR);
            err->message = std::string("Transport error: ") + e.what();
            completion(err);
        }
        std::lock_guard<std::mutex> lock(mutex_);
        completionCallbacks_.erase(requestId);
        progressCallbacks_.erase(requestId);
    }
}

void BaseSession::SendResponse(const RequestId& requestId, std::unique_ptr<Result> result, RequestContext& ctx)
{
    JSONRPCResponse response;
    response.jsonrpc_ = JSONRPC_VERSION;
    response.id_ = requestId;
    response.result_ = std::move(result);
    JSONRPCMessage msg = std::move(response);
    serverTransport_->SendMessage(msg, ctx);
}

void BaseSession::SendResponse(const RequestId& requestId, JSONRPCError error, RequestContext& ctx)
{
    JSONRPCMessage msg = std::move(error);
    serverTransport_->SendMessage(msg, ctx);
}

void BaseSession::HandleResponse(const JSONRPCResponse& response)
{
    const RequestId& responseId = response.id_;
    std::function<void(std::shared_ptr<Result>)> completion;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        progressCallbacks_.erase(responseId);

        auto cit = completionCallbacks_.find(responseId);
        if (cit != completionCallbacks_.end()) {
            completion = std::move(cit->second);
            completionCallbacks_.erase(cit);
        }
    }

    if (!completion) {
        return;
    }

    completion(response.result_); // ✅ shared_ptr directly, no raw pointer access
}

void BaseSession::HandleResponse(const JSONRPCError& error)
{
    const RequestId& responseId = error.id_;
    std::function<void(std::shared_ptr<Result>)> completion;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        progressCallbacks_.erase(responseId);

        auto cit = completionCallbacks_.find(responseId);
        if (cit != completionCallbacks_.end()) {
            completion = std::move(cit->second);
            completionCallbacks_.erase(cit);
        }
    }

    if (completion == nullptr) {
        return;
    }

    auto err = std::make_shared<ErrorResult>();
    err->code = error.code_;
    err->message = error.message_;
    if (error.data_.has_value()) {
        err->data = error.data_.value().dump();
    }
    completion(err);
}

void BaseSession::ProcessIncomingRequest(const JSONRPCRequest& rpcRequest, RequestContext& ctx)
{
    if (!rpcRequest.request_) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "rpcRequest.request_ is null");
    }
    const Request& typedRequest = *static_cast<const Request*>(rpcRequest.request_.get());
    ctx.method = typedRequest.method_;
    ReceivedRequest(rpcRequest.id_, typedRequest, ctx);
}

void BaseSession::ProcessIncomingNotification(const JSONRPCNotification& rpcNotification)
{
    if (rpcNotification.notification_) {
        const Notification& notif = *static_cast<const Notification*>(rpcNotification.notification_.get());
        ReceivedNotification(notif);
    } else {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "rpcNotification.notification_ is null");
    }
}

void BaseSession::OnTransportMessage(const JSONRPCMessage& message, RequestContext& ctx)
{
    if (std::holds_alternative<JSONRPCRequest>(message)) {
        ProcessIncomingRequest(std::get<JSONRPCRequest>(message), ctx);
        return;
    }

    if (std::holds_alternative<JSONRPCNotification>(message)) {
        ProcessIncomingNotification(std::get<JSONRPCNotification>(message));
        return;
    }

    if (std::holds_alternative<JSONRPCResponse>(message)) {
        HandleResponse(std::get<JSONRPCResponse>(message));
        return;
    }

    if (std::holds_alternative<JSONRPCError>(message)) {
        HandleResponse(std::get<JSONRPCError>(message));
        return;
    }
}

void BaseSession::SendProgressNotification([[maybe_unused]] int64_t progressToken,
 	                                         [[maybe_unused]] double progress,
 	                                         [[maybe_unused]] std::optional<double> total,
 	                                         [[maybe_unused]] const std::optional<std::string>& message)
{
    // Empty implementation - subclasses can override if needed
}

} // namespace Mcp
