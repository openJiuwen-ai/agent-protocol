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
    explicit StreamServerEmitter(const Http::HttpRequestContext& ctx,
        const std::map<std::string, std::string>& headers = {})
        : ctx_(ctx), headers_(headers), responseSent_(false)
    {
    }
    ~StreamServerEmitter() override = default;
    void WriteStreamingData(const std::string& data) override;
    void WriteNonStreamingData(const std::string& data) override;
    void WriteDone() override;

private:
    Http::HttpRequestContext ctx_;
    const std::map<std::string, std::string>& headers_;
    std::atomic<bool> responseSent_;

    void BuildAndSend(const std::string &body) const;
};

} // namespace A2A::Transport

#endif