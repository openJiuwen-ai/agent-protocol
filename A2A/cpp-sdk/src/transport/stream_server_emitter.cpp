/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include "stream_server_emitter.h"
#include "http_server_transport.h"
#include "http_common.h"

namespace A2A::Transport {
void StreamServerEmitter::BuildAndSend(const std::string& body) const
{
    if (!ctx_.httpSendFunc) {
        return;
    }
    Http::HttpResponse response;
    if (!body.empty()) {
        response.body = body + "\r\n\r\n";
    }
    response.type = Http::HttpSendType::HTTPRESPONSEBODY;
    // Send the SSE event data
    ctx_.httpSendFunc(response, ctx_);
}

void StreamServerEmitter::WriteStreamingData(const std::string& data)
{
    BuildAndSend("data: " + data);
}

void StreamServerEmitter::WriteNonStreamingData(const std::string& data)
{
    Http::HttpResponse response;
    response.headers[Http::CONTENT_TYPE_HEADER] = Http::CONTENT_TYPE_JSON;

    // Add global headers
    for (const auto& [key, value] : headers_) {
        response.headers[key] = value;
    }

    response.body = data;
    response.streaming = false;

    if (!responseSent_.exchange(true)) {
        ctx_.httpSendFunc(response, ctx_);
        return;
    }
    A2A_LOG(A2A_LOG_LEVEL_WARN, "Non-streaming data already written");
}

void StreamServerEmitter::WriteDone()
{
    BuildAndSend("");
}
} // namespace A2A::Transport