/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_TEST_FIXTURES_TEST_AGENT_CARD_H
#define A2A_TEST_FIXTURES_TEST_AGENT_CARD_H

#include <cstdint>
#include <string>

#include "server/http_server_builder.h"
#include "types.h"

namespace A2A::Test {

inline AgentCard MakeAgentCard(const std::string& url,
    const std::string& protocolBinding = "JSONRPC",
    const std::string& protocolVersion = "1.0")
{
    AgentCard card;
    AgentInterface iface;
    iface.url = url;
    iface.protocolBinding = protocolBinding;
    iface.protocolVersion = protocolVersion;
    card.supportedInterfaces.push_back(iface);
    return card;
}

inline Server::HttpConfig MakeHttpConfig(const std::string& ip = "127.0.0.1",
    uint16_t port = 0,
    size_t ioThreadNum = 1)
{
    Server::HttpConfig cfg;
    cfg.ip = ip;
    cfg.port = port;
    cfg.ioThreadNum = ioThreadNum;
    return cfg;
}

} // namespace A2A::Test

#endif
