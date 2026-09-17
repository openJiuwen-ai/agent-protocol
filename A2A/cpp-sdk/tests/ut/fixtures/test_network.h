/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_TEST_FIXTURES_TEST_NETWORK_H
#define A2A_TEST_FIXTURES_TEST_NETWORK_H

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>

namespace A2A::Test {

// Returns an ephemeral TCP port on 127.0.0.1, or 0 on failure.
inline uint16_t GetFreeTcpPort()
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return 0;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return 0;
    }

    socklen_t len = sizeof(addr);
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) < 0) {
        close(fd);
        return 0;
    }

    close(fd);
    return ntohs(addr.sin_port);
}

} // namespace A2A::Test

#endif
