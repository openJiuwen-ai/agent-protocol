/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_TRANSPORT_EMITTER_H
#define A2A_TRANSPORT_EMITTER_H

#include <string>

namespace A2A::Transport {

class TransportEmitter {
public:
    virtual ~TransportEmitter() = default;
    virtual void WriteStreamingData(const std::string& data) = 0;
    virtual void WriteNonStreamingData(const std::string& data) = 0;
    virtual void WriteDone() = 0;
};
} // namespace A2A::Transport

#endif