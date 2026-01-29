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
    explicit StreamServerEmitter(const Server::HttpRequestContext& ctx) : ctx_(ctx) {}
    ~StreamServerEmitter() override = default;
    void WriteData(const std::string& data) override;
    void WriteDone() override;

private:
    Server::HttpRequestContext ctx_;

    void BuildAndSend(const std::string &body) const;
};

} // namespace A2A::Transport

#endif