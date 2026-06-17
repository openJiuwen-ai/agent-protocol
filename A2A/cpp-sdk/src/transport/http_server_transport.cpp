/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <nlohmann/json.hpp>
#include <thread>
#include <mutex>
#include <algorithm>

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

    // Disable TLS for simple HTTP service
    config.tlsConfig.enabled = false;

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
    // Create a background thread to handle streaming request
    std::thread workerThread([this, reqBody, ctx, headersCopy]() mutable {
        // Prepare initial response for streaming
        Http::HttpResponse response;
        SetCommonHeaders(response);

        // Add global headers
        for (const auto& [key, value] : headersCopy) {
            response.headers[key] = value;
        }
        response.type = Http::HttpSendType::HTTPRESPONSESTART;
        ctx.httpSendFunc(response, ctx);

        try {
            auto emitter = std::make_shared<StreamServerEmitter>(ctx);
            std::string unusedResp; // For streaming, response body is not used
            handler_(reqBody, unusedResp, emitter);
        } catch (const std::exception& e) {
            std::string error_msg = e.what() ? e.what() : "Unknown exception";
            A2A_LOG(A2A_LOG_LEVEL::ERROR, "SetNonBlocking failed: " + error_msg);

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
    const std::map<std::string, std::string>& headersCopy)
{
    // Handle non-streaming request with a dedicated background thread
    // Create a background thread to handle the request
    std::thread workerThread([this, reqBody, ctx, headersCopy]() mutable {
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

    // Join all worker threads before destruction
    std::vector<std::thread> threadsToJoin;
    {
        std::lock_guard<std::mutex> lock(workerThreadsMutex_);
        for (auto& worker_thread : workerThreads_) {
            if (worker_thread.joinable()) {
                threadsToJoin.emplace_back(std::move(worker_thread));
            }
        }
        workerThreads_.clear();
    }
    for (auto& t : threadsToJoin) {
        if (t.joinable()) {
            t.join();
        }
    }
}

} // namespace A2A::Transport