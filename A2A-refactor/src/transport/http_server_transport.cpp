/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>
#include <mutex>
#include <algorithm>

#include "transport_emitter.h"
#include "stream_server_emitter.h"
#include "a2a_log.h"
#include "common_types.h"
#include "http_server_manager.h"
#include "http_common.h"
#include "types.h"
#include "http_server_transport.h"

namespace A2A::Transport {

struct EmptyEmitter : TransportEmitter {
    void WriteData(const std::string& data) override {
        // Do nothing for non-streaming requests
    }
    void WriteDone() override {
        // Do nothing for non-streaming requests
    }
} g_emptyEmitter;

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
    config.port = config_.port;
    config.ioThreadNum = config_.ioThreadNum;

    // Disable TLS for simple HTTP service
    config.tlsConfig_.enabled = false;

    config.routeMap = routeMap;

    // Create and start the server manager
    httpServerMgr_ = std::make_unique<Server::HttpServerManager>(config);

    // Start server in background thread
    listenThread_ = std::thread([this]() {
        httpServerMgr_->Start();
    });

    return 0;
}

void HttpServerTransport::SetupJsonRpcEndpoint(Server::RouteMap& routeMap)
{
    routeMap[JSONRPC_ENDPOINT] = [this](const Http::HttpRequest& req, const Http::HttpRequestContext& ctx) {
        HandleJsonRpcRequest(req, ctx);
    };
}

void HttpServerTransport::SetupCardEndpoint(Server::RouteMap& routeMap)
{
    routeMap[AGENT_CARD_ENDPOINT] = [this](const Http::HttpRequest& req, const Http::HttpRequestContext& ctx) {
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
    // Parse the request to check if it's a streaming method
    std::string contentType = req.headers.count(Http::CONTENT_TYPE_HEADER) ?
                              req.headers.at(Http::CONTENT_TYPE_HEADER) : "";

    if (!handler_) {
        Http::HttpResponse response;
        response.success = false;
        response.statusCode = Http::HTTP_STATUS_INTERNAL_SERVER_ERROR;
        A2A::MethodNotFoundError err;
        response.body = "{\n"
            "    \"jsonrpc\":\"2.0\",\n"
            "    \"id\":null,\n"
            "    \"error\": {\n"
            "        \"code\":" + std::to_string(err.code) + ",\n"
            "        \"message\":\n"
            "        \"" + err.message.value() + "\"\n"
            "    }\n"
            "}\n";
        response.headers[Http::CONTENT_TYPE_HEADER] = Http::CONTENT_TYPE_JSON;
        ctx.httpSendFunc(response, ctx);
        return;
    }

    // Determine if this is a streaming request by checking the method
    if (IsStreamingMethod(req.body)) {
        HandleStreamingRequest(req.body, ctx, headers_);
    } else {
        HandleNonStreamingRequest(req.body, ctx, headers_);
    }
}

bool HttpServerTransport::IsStreamingMethod(const std::string& reqBody)
{
    try {
        auto jsonReq = nlohmann::json::parse(reqBody);
        if (jsonReq.contains("method")) {
            std::string method = jsonReq["method"];
            return method == METHOD_MESSAGE_STREAM;
        }
    } catch (...) {
        // If parsing fails, treat as non-streaming
    }
    return false;
}

void HttpServerTransport::HandleStreamingRequest(const std::string& reqBody, const Http::HttpRequestContext& ctx,
    const std::map<std::string, std::string>& headers_copy)
{
    // Create a background thread to handle streaming request
    std::thread workerThread([this, reqBody, ctx, headers_copy]() mutable {
        // Prepare initial response for streaming
        Http::HttpResponse response;
        SetCommonHeaders(response);

        // Add global headers
        for (const auto& [key, value] : headers_copy) {
            response.headers[key] = value;
        }
        response.type = Http::HttpSendType::HTTPRESPONSESTART;

        try {
            ctx.httpSendFunc(response, ctx);

            StreamServerEmitter emitter(ctx);
            std::string unusedResp; // For streaming, response body is not used
            handler_(reqBody, unusedResp, emitter);

            response.type = Http::HttpSendType::HTTPRESPONSEBODY;
            ctx.httpSendFunc(response, ctx);
        } catch (const std::exception& e) {
            A2A_LOG(A2A_LOG_LEVEL_ERROR, "SetNonBlocking failed: " + std::string(e.what()));

            // Send error as SSE event
            Http::HttpResponse error_response;
            SetCommonHeaders(error_response);
            error_response.body = "event: error\ndata: {\"error\":\"" + std::string(e.what()) + "\"}\n\n";

            ctx.httpSendFunc(error_response, ctx);
        }

        // Clean up the thread from the vector when done
        {
            std::lock_guard<std::mutex> lock(workerThreadsMutex_);
            workerThreads_.erase(
                std::remove_if(workerThreads_.begin(), workerThreads_.end(),
                    [](const std::thread& t) { return !t.joinable(); }),
                workerThreads_.end());
        }
    });

    // Store the thread in our vector and detach it
    {
        std::lock_guard<std::mutex> lock(workerThreadsMutex_);
        workerThreads_.emplace_back(std::move(workerThread));
    }
}

void HttpServerTransport::HandleNonStreamingRequest(const std::string& reqBody, const Http::HttpRequestContext& ctx,
    const std::map<std::string, std::string>& headers_copy)
{
    // Handle non-streaming request with a dedicated background thread
    // Create a background thread to handle the request
    std::thread workerThread([this, reqBody, ctx, headers_copy]() mutable {
        std::string responseLocal;
        try {
            handler_(reqBody, responseLocal, g_emptyEmitter);
        } catch (const std::exception& e) {
            responseLocal = "{\n"
                "    \"jsonrpc\":\"2.0\",\n"
                "    \"id\":null,\n"
                "    \"error\": {\n"
                "        \"code\":-32603,\n"
                "        \"message\":\n"
                "        \"" + std::string(e.what()) + "\"\n"
                "    }\n"
                "}\n";
        }

        // Prepare response
        Http::HttpResponse response;
        response.headers[Http::CONTENT_TYPE_HEADER] = Http::CONTENT_TYPE_JSON;

        // Add global headers
        for (const auto& [key, value] : headers_copy) {
            response.headers[key] = value;
        }

        response.body = responseLocal;
        ctx.httpSendFunc(response, ctx);

        // Clean up the thread from the vector when done
        {
            std::lock_guard<std::mutex> lock(workerThreadsMutex_);
            workerThreads_.erase(
                std::remove_if(workerThreads_.begin(), workerThreads_.end(),
                    [](const std::thread& t) { return !t.joinable(); }),
                workerThreads_.end());
        }
    });

    // Store the thread in our vector and detach it
    {
        std::lock_guard<std::mutex> lock(workerThreadsMutex_);
        workerThreads_.emplace_back(std::move(workerThread));
    }
}

void HttpServerTransport::SetCommonHeaders(Http::HttpResponse& response) {
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

int HttpServerTransport::SendData(const std::string& url, const std::string& data) const
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
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "Exception in stopping http server: %s", e.what());
    } catch (...) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "Unknown exception in stopping http server");
    }

    try {
        if (listenThread_.joinable()) {
            listenThread_.join();
        }
    } catch (const std::exception& e) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "Exception in joining listen thread: %s", e.what());
    } catch (...) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "Unknown exception in joining listen thread");
    }

    // Join all worker threads before destruction
    try {
        std::lock_guard<std::mutex> lock(workerThreadsMutex_);
        for (auto& worker_thread : workerThreads_) {
            if (worker_thread.joinable()) {
                worker_thread.join();
            }
        }
        workerThreads_.clear();
    } catch (const std::exception& e) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "Exception in joining worker threads: %s", e.what());
    } catch (...) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "Unknown exception in joining worker threads");
    }
}

} // namespace A2A::Transport