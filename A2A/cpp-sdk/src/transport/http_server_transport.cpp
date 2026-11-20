/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <nlohmann/json.hpp>
#include <thread>
#include <future>

#include "stream_server_emitter.h"
#include "a2a_log.h"
#include "common_types.h"
#include "http_common.h"
#include "types.h"
#include "jsonrpc.h"
#include "http_server_transport.h"

namespace A2A::Transport {

void HttpServerTransport::SetHeader(const std::string& key, const std::string& value)
{
    headers_[key] = value;
}

void HttpServerTransport::SetBearerToken(const std::string& token)
{
    bearerToken_ = token;
}

void HttpServerTransport::SetTimeoutMs(long connectMs, long readMs)
{
    connectTimeoutMs_ = connectMs;
    readTimeoutMs_ = readMs;
}

void HttpServerTransport::SetRpcHandler(ServerTransportRpcHandler handler)
{
    handler_ = std::move(handler);
}

void HttpServerTransport::SetCardHandler(ServerTransportCardHandler handler)
{
    handlerCard_ = std::move(handler);
}

int HttpServerTransport::Start()
{
    // Create route map for the new server
    Server::RouteMap routeMap;

    // Set up endpoints using helper methods
    SetupJsonRpcEndpoint(routeMap);
    SetupCardEndpoint(routeMap);

    // Create server manager configuration
    Server::HttpServerManagerConfig config;
    config.host = config_.ip;
    config.port = static_cast<uint16_t>(config_.port);
    config.ioThreadNum = config_.ioThreadNum;
    config.connectionTimeoutSeconds = config_.connectionTimeoutSeconds;
    config.connectionSweepIntervalSeconds = config_.connectionSweepIntervalSeconds;
    config.requestTimeoutSeconds = config_.requestTimeoutSeconds;

    // Disable TLS for simple HTTP service
    config.tlsConfig.enabled = false;

    config.routeMap = routeMap;

    // Create and start the server manager
    httpServerMgr_ = std::make_unique<Server::HttpServerManager>(config);

    // Start server in background thread; wait until Start() finishes so Stop()
    // cannot race with servers_ construction (ASAN SEGV in unique_ptr clear).
    auto started = std::make_shared<std::promise<void>>();
    auto startedFuture = started->get_future();
    listenThread_ = std::thread([this, started]() {
        try {
            httpServerMgr_->Start();
            started->set_value();
        } catch (...) {
            started->set_exception(std::current_exception());
        }
    });
    startedFuture.get();

    return 0;
}

void HttpServerTransport::SetupJsonRpcEndpoint(Server::RouteMap& routeMap)
{
    routeMap[jsonrpc_endpoint_] = [this](const Http::HttpRequest& req, const Http::HttpRequestContext& ctx) {
        HandleJsonRpcRequest(req, ctx);
    };
    
    A2A_LOG(A2A_LOG_LEVEL::INFO, "JSON-RPC endpoint setup at: " + jsonrpc_endpoint_);
}

void HttpServerTransport::SetupCardEndpoint(Server::RouteMap& routeMap)
{
    routeMap[AGENT_CARD_ENDPOINT] = [this]([[maybe_unused]] const Http::HttpRequest& req,
                                            const Http::HttpRequestContext& ctx) {
        Http::HttpResponse response;

        // Add global headers
        for (const auto& [key, value] : headers_) {
            response.headers[key] = value;
        }

        std::string resp;
        handlerCard_("", resp);
        response.body = resp;
        response.headers[Http::CONTENT_TYPE_HEADER] = Http::CONTENT_TYPE_JSON;
        ctx.httpSendFunc(response, ctx);
    };
}

void HttpServerTransport::HandleJsonRpcRequest(const Http::HttpRequest& req, const Http::HttpRequestContext& ctx)
{
    // Reap workers that have finished serving earlier requests. They cannot reap themselves:
    // a thread stays joinable() after running to completion, so self-cleanup based on
    // joinable() removes nothing and finished handles would accumulate with every request
    // (see WorkerThreadRegistry). join() on a finished thread returns immediately.
    workerRegistry_.ReclaimFinished();

    // Parse the request to check if it's a streaming method
    std::string contentType = A2A::Http::GetHeaderValue(req.headers, Http::CONTENT_TYPE_HEADER);

    // Determine if this is a streaming request by checking the method
    if (IsStreamingMethod(req.body)) {
        HandleStreamingRequest(req.body, ctx, headers_);
    } else {
        HandleNonStreamingRequest(req.body, ctx, headers_);
    }
}

bool HttpServerTransport::IsStreamingMethod(const std::string& reqBody) const
{
    try {
        auto jsonReq = nlohmann::json::parse(reqBody);
        if (jsonReq.contains("method")) {
            std::string method = jsonReq["method"];
            return method == METHOD_MESSAGE_STREAM || method == METHOD_TASK_RESUBSCRIBE;
        }
    } catch (...) {
        // If parsing fails, treat as non-streaming
    }
    return false;
}

void HttpServerTransport::HandleStreamingRequest(const std::string& reqBody, const Http::HttpRequestContext& ctx,
    const std::map<std::string, std::string>& headersCopy)
{
    // The handler runs on its own worker thread so the IO thread is not held for the whole
    // (potentially minutes-long) streaming response. The registry tracks the worker; it is
    // joined once finished - never detached, the body below captures this and handler_.
    workerRegistry_.Spawn([this, reqBody, ctx, headersCopy]() mutable {
        // Prepare initial response for streaming
        Http::HttpResponse response;
        SetCommonHeaders(response);

        // Add global headers
        for (const auto& [key, value] : headersCopy) {
            response.headers[key] = value;
        }
        response.type = Http::HttpSendType::HTTPRESPONSESTART;
        ctx.httpSendFunc(response, ctx);

        // The response start above already put the chunked SSE head on the wire, so the emitter
        // must know the stream is open: a non-streaming write now degrades to a data: event
        // instead of injecting a second status line into the chunked stream.
        auto emitter = std::make_shared<StreamServerEmitter>(ctx, headersCopy, true);
        try {
            std::string unusedResp; // For streaming, response body is not used
            handler_(reqBody, unusedResp, emitter);
        } catch (const std::exception& e) {
            std::string error_msg = e.what() ? e.what() : "Unknown exception";
            A2A_LOG(A2A_LOG_LEVEL::ERROR, "streaming handler failed: " + error_msg);

            // The error goes out as an event inside the open stream (headers are already on the
            // wire), followed by the terminating chunk; both are dropped if the handler ended
            // the stream before throwing. Guarded because an exception escaping this lambda
            // would leave the worker thread and end the process.
            try {
                emitter->WriteErrorEvent(error_msg);
                emitter->WriteDone();
            } catch (const std::exception& reportError) {
                A2A_LOG(A2A_LOG_LEVEL::ERROR,
                    std::string("failed to report streaming error: ") + reportError.what());
            }
        }
    });
}

void HttpServerTransport::HandleNonStreamingRequest(const std::string& reqBody, const Http::HttpRequestContext& ctx,
    const std::map<std::string, std::string>& headersCopy)
{
    // Dedicated worker thread for the same reason as the streaming path: the IO thread must
    // stay free while the handler produces the response. Tracked (and joined) by the registry.
    workerRegistry_.Spawn([this, reqBody, ctx, headersCopy]() mutable {
        std::string responseLocal;
        auto emitter = std::make_shared<StreamServerEmitter>(ctx, headersCopy);
        try {
            handler_(reqBody, responseLocal, emitter);
        } catch (const std::exception& e) {
            nlohmann::json err;
            err[JSON_FIELD_JSONRPC] = JSON_VERSION;
            err[JSON_FIELD_ID] = "null";
            err[JSON_FIELD_ERROR] = InternalError{};
            responseLocal = err.dump();
        }

        if (!responseLocal.empty()) {
            emitter->WriteNonStreamingData(responseLocal);
        }
    });
}

void HttpServerTransport::SetCommonHeaders(Http::HttpResponse& response)
{
    response.success = true;
    response.statusCode = Http::HTTP_STATUS_OK;
    response.headers[Http::CACHE_CONTROL_HEADER] = Http::CACHE_CONTROL_NO_CACHE_NO_TRANSFORM;
    response.headers[Http::CONNECTION_HEADER] = Http::CONNECTION_KEEP_ALIVE;
    response.headers[Http::CONTENT_TYPE_HEADER] = Http::CONTENT_TYPE_SSE;
    response.headers[Http::TRANSFER_ENCODING_HEADER] = Http::TRANSFER_ENCODING_CHUNKED;
    response.headers[Http::X_ACCEL_BUFFERING_HEADER] = "no";
}

void HttpServerTransport::Stop()
{
    if (httpServerMgr_) {
        httpServerMgr_->Stop();
    }
    if (listenThread_.joinable()) {
        listenThread_.join();
    }
}

int HttpServerTransport::SendData([[maybe_unused]] const std::string& url,
    [[maybe_unused]] const std::string& data) const
{
    // Server does not send data to external URLs
    return -1;
}

HttpServerTransport::~HttpServerTransport()
{
    try {
        if (httpServerMgr_) {
            httpServerMgr_->Stop();
        }
    } catch (const std::exception& e) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, std::string("Exception in stopping http server: ") + e.what());
    } catch (...) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "Unknown exception in stopping http server");
    }

    try {
        if (listenThread_.joinable()) {
            listenThread_.join();
        }
    } catch (const std::exception& e) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, std::string("Exception in joining listen thread: ") + e.what());
    } catch (...) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "Unknown exception in joining listen thread");
    }

    // Join all worker threads before destruction: they capture this and handler_, so their
    // bodies must have returned before member destruction begins.
    workerRegistry_.JoinAll();
}

} // namespace A2A::Transport