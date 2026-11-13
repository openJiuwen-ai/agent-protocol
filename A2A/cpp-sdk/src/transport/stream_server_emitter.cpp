/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <nlohmann/json.hpp>

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
    if (streamDone_.load()) {
        A2A_LOG(A2A_LOG_LEVEL::WARN, "ignoring stream data written after the stream was terminated");
        return;
    }
    BuildAndSend("data: " + data);
}

void StreamServerEmitter::WriteErrorEvent(const std::string& message)
{
    if (streamDone_.load()) {
        A2A_LOG(A2A_LOG_LEVEL::WARN, "ignoring error event written after the stream was terminated");
        return;
    }
    // Serialized rather than concatenated: exception messages routinely carry quotes, braces
    // and newlines, any of which would break the payload or split the SSE event. 'replace'
    // keeps dump() from throwing on invalid UTF-8: this runs on an error path, typically from
    // a catch block on a worker thread, where an escaping exception would end the process.
    nlohmann::json payload;
    payload["error"] = message;
    BuildAndSend("event: error\ndata: " +
        payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
}

void StreamServerEmitter::WriteNonStreamingData(const std::string& data)
{
    if (streamDone_.load()) {
        A2A_LOG(A2A_LOG_LEVEL::WARN, "ignoring response written after the stream was terminated");
        return;
    }

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
    A2A_LOG(A2A_LOG_LEVEL::WARN, "Non-streaming data already written");
}

void StreamServerEmitter::WriteDone()
{
    // Idempotent: the first call writes the terminating chunk, later ones are no-ops, so a
    // handler and an error path can both terminate the stream without writing it twice.
    if (streamDone_.exchange(true)) {
        return;
    }
    BuildAndSend("");
}
} // namespace A2A::Transport