/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_STREAM_SERVER_EMITTER_H
#define A2A_STREAM_SERVER_EMITTER_H

#include "http_server_manager.h"
#include "transport_emitter.h"

namespace A2A::Transport {
class StreamServerEmitter : public TransportEmitter {
public:
    // streamStarted: true when the transport already put the chunked SSE response head on the
    // wire before handing this emitter to the handler (streaming request path). It must be
    // passed by whoever constructs the emitter, right after sending the response start.
    explicit StreamServerEmitter(const Http::HttpRequestContext& ctx,
        const std::map<std::string, std::string>& headers = {}, const bool streamStarted = false)
        : ctx_(ctx), headers_(headers), responseSent_(false), streamStarted_(streamStarted)
    {
    }
    ~StreamServerEmitter() override = default;
    void WriteStreamingData(const std::string& data) override;
    void WriteNonStreamingData(const std::string& data) override;
    void WriteDone() override;

    // Emits an SSE error event inside the open stream. Not part of TransportEmitter: callers
    // that hold the concrete emitter use this instead of hand-building a response, so the
    // "nothing after the stream ended" rule below applies to error paths too.
    void WriteErrorEvent(const std::string& message);

private:
    Http::HttpRequestContext ctx_;
    // Held by value: the constructor's default argument is a temporary, and the emitter
    // outlives the expression that created it.
    std::map<std::string, std::string> headers_;
    std::atomic<bool> responseSent_;
    // True from construction when the response head is already on the wire: a one-shot
    // response can no longer be written (it would add a second status line and
    // Content-Length inside the open chunked stream), so WriteNonStreamingData degrades to
    // a data: event instead.
    std::atomic<bool> streamStarted_{false};
    // Set by WriteDone. The terminating chunk ends the response, so anything written after it
    // would land outside the body and corrupt the stream; later writes are dropped instead.
    std::atomic<bool> streamDone_{false};

    void BuildAndSend(const std::string &body) const;
};

} // namespace A2A::Transport

#endif