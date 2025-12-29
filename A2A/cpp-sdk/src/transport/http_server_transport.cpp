/*
* Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <httplib.h>

#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>

#include "http_server_transport.h"
#include "utils/errors.h"

namespace a2a::transport {
constexpr auto SERVER_STARTUP_DELAY = 50;
// ========================
// HttpServerTransportEmiter (concrete implementation for httplib)
// ========================
class HttplibEmitter : public AbstractServerTransportEmiter {
public:
    explicit HttplibEmitter(httplib::DataSink* sink) : sink_(sink)
    {
    }

    void WriteData(const std::string& data) override
    {
        if (sink_) {
            std::string chunk = "data: " + data + "\n\n";
            sink_->write(chunk.data(), chunk.size());
        }
    }

    void WriteDone() override
    {
        if (sink_) {
            std::string done = "event: done\ndata: {\"done\":true}\n\n";
            sink_->write(done.data(), done.size());
        }
    }

private:
    httplib::DataSink* sink_;
};

// ========================
// HttpServerTransport
// ========================
HttpServerTransport::HttpServerTransport(std::shared_ptr<AgentCard> agentCard) : agentCard_(agentCard)
{
}

HttpServerTransport& HttpServerTransport::SetHeader(std::string key, std::string value)
{
    headers_[std::move(key)] = std::move(value);
    return *this;
}

HttpServerTransport& HttpServerTransport::SetBearerToken(std::string token)
{
    bearerToken_ = std::move(token);
    return *this;
}

HttpServerTransport& HttpServerTransport::SetTimeoutMs(long connectMs, long readMs)
{
    connectTimeoutMs_ = connectMs;
    readTimeoutMs_ = readMs;
    return *this;
}

void HttpServerTransport::SetEventHandler(ServerTransportEventHandler handler)
{
    handler_ = std::move(handler);
}

void HttpServerTransport::SetStreamEventHandler(ServerTransportStreamEventHandler handler)
{
    streamHandler_ = std::move(handler);
}

int HttpServerTransport::Start(const std::string& ipAddr, uint16_t port)
{
    ipAddr_ = ipAddr;
    port_ = port;

    // Set up JSON-RPC endpoint
    server_.Post("/jsonrpc", [this](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Content-Type", "application/json");

        // 添加全局 headers
        for (const auto& [key, value] : headers_) {
            res.set_header(key.c_str(), value.c_str());
        }

        std::string resp;
        try {
            if (handler_) {
                handler_(req.body, resp); // handler_ 处理 req.body → resp
            } else {
                resp = R"({"jsonrpc":"2.0","id":null,"error":{"code":-32601,"message":"Handler not set"}})";
            }
        } catch (const std::exception& e) {
            resp =
                R"({"jsonrpc":"2.0","id":null,"error":{"code":-32603,"message":")" + std::string(e.what()) + R"("}})";
        }
        printf("-resp: %s\n", resp.c_str());
        res.set_content(resp, "application/json");
    });

    // Set up Streaming endpoint
    server_.Post("/stream", [this](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Content-Type", "text/event-stream");
        res.set_header("Cache-Control", "no-cache");
        res.set_header("Connection", "keep-alive");
        res.set_header("X-Accel-Buffering", "no");

        // 添加全局 headers
        for (const auto& [key, value] : headers_) {
            res.set_header(key.c_str(), value.c_str());
        }

        // 使用 set_chunked_content_provider
        res.set_chunked_content_provider("text/event-stream",
            [this, body = req.body](size_t, httplib::DataSink& sink) -> bool {
                HttplibEmitter emitter(&sink);
                try {
                    if (streamHandler_) {
                        streamHandler_(body, emitter);
                    }
                    sink.done(); // 显式标记完成
                } catch (const std::exception& e) {
                    std::cout << e.what();
                    sink.done();
                }
                return true; // 返回 true 表示完成
            });
    });

    server_.Get("/.well-known/agent-card.json", [this](const httplib::Request& req, httplib::Response& res) {
        // 添加全局 headers
        for (const auto& [key, value] : headers_) {
            res.set_header(key.c_str(), value.c_str());
        }
        nlohmann::json ret;
        to_json(ret, *agentCard_);
        res.set_content(ret.dump(), "application/json");
    });

    server_.Get("/tasks/:id", [this](const httplib::Request& req, httplib::Response& res) {
        // 添加全局 headers
        for (const auto& [key, value] : headers_) {
            res.set_header(key.c_str(), value.c_str());
        }

        try {
            std::string taskId = req.path_params.at("id");
            std::cout << "Get task by ID: " << taskId << std::endl;

            nlohmann::json jsonRpcReq = {
                {"jsonrpc", "2.0"}, {"id", 1}, {"method", "task/get"}, {"params", {{"id", taskId}}}};

            std::string requestBody = jsonRpcReq.dump();
            std::string resp;

            if (handler_) {
                handler_(requestBody, resp);
                res.set_content(resp, "application/json");
            } else {
                res.status = static_cast<int>(HttpStatusCode::InternalServerError);
                res.set_content(R"({"jsonrpc":"2.0","id":null,"error":{"code":-32601,"message":"Handler not set"}})",
                                "application/json");
            }
        } catch (const std::exception& e) {
            res.status = static_cast<int>(HttpStatusCode::BadRequest);
            std::string errorResp =
                R"({"jsonrpc":"2.0","id":null,"error":{"code":-32602,"message":")" + std::string(e.what()) + R"("}})";
            res.set_content(errorResp, "application/json");
        }
    });

    server_.Post("/tasks/:id/cancel", [this](const httplib::Request& req, httplib::Response& res) {
        // 添加全局 headers
        for (const auto& [key, value] : headers_) {
            res.set_header(key.c_str(), value.c_str());
        }

        try {
            std::string taskId = req.path_params.at("id");
            std::cout << "Get task by ID: " << taskId << std::endl;

            nlohmann::json jsonRpcReq = {
                {"jsonrpc", "2.0"}, {"id", 1}, {"method", "task/cancel"}, {"params", {{"id", taskId}}}};

            std::string requestBody = jsonRpcReq.dump();
            std::string resp;

            if (handler_) {
                handler_(requestBody, resp);
                res.set_content(resp, "application/json");
            } else {
                res.status = static_cast<int>(HttpStatusCode::InternalServerError);
                res.set_content(R"({"jsonrpc":"2.0","id":null,"error":{"code":-32601,"message":"Handler not set"}})",
                                "application/json");
            }
        } catch (const std::exception& e) {
            res.status = static_cast<int>(HttpStatusCode::BadRequest);
            std::string errorResp =
                R"({"jsonrpc":"2.0","id":null,"error":{"code":-32602,"message":")" + std::string(e.what()) + R"("}})";
            res.set_content(errorResp, "application/json");
        }
    });

    // Start server in background thread
    listen_thread_ = std::thread([this]() { server_.listen(ipAddr_.c_str(), port_); });

    // Ensure logs flush immediately (critical for short-lived threads)
    std::cout << std::flush;
    std::cerr << std::flush;

    // Give server time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(SERVER_STARTUP_DELAY));
    return 0;
}

void HttpServerTransport::Stop()
{
    server_.stop();
    if (listen_thread_.joinable()) {
        listen_thread_.join();
    }
}

int HttpServerTransport::SendData(const std::string& url, const std::string& data) const
{
    // Server does not send data to external URLs
    return -1;
}

HttpServerTransport::~HttpServerTransport()
{
    if (listen_thread_.joinable()) {
        server_.stop();
        listen_thread_.join();
    }
}

} // namespace a2a::transport