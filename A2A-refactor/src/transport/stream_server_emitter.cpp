/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include "stream_server_emitter.h"
#include "http_server_transport.h"
#include "http_common.h"

namespace A2A::Transport {
void StreamServerEmitter::BuildAndSend(const std::string& body) const
{
    if (ctx_.httpSendFunc) {
        Server::HttpResponse response;
        HttpServerTransport::SetCommonHeaders(response);
        response.body = body + "\n\n";
        // Send the SSE event data
        ctx_.httpSendFunc(response, ctx_);
    }
}

void StreamServerEmitter::WriteData(const std::string& data)
{
    BuildAndSend("data: " + data);
}

void StreamServerEmitter::WriteDone()
{
    BuildAndSend("event: done\ndata: {\"done\":true}");
}
} // namespace A2A::Transport