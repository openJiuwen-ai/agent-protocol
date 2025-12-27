/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef MCP_TCP_LISTENER_INCLUDE_H_
#define MCP_TCP_LISTENER_INCLUDE_H_

#include <functional>
#include <string>

#include "net/tcp_socket.h"

namespace Mcp::Net {

using NewConnectionCallback = std::function<void(const TcpSocketPtr& conn)>;

// TCP listener: responsible for bind/listen/accept
class TcpListener {
public:
    explicit TcpListener(Mcp::EventSystem& es);
    ~TcpListener();

    // Bind and listen. `host` may be empty or use "0.0.0.0"/"::"
    bool Listen(const std::string& host, uint16_t port, int backlog = 128, bool reusePort = false);

    bool Start();
    void Stop();

    void OnNewConnection(NewConnectionCallback cb);
    void OnError(ErrorCallback cb);

private:
    void HandleAccept();

    Mcp::EventSystem& es_;
    int listenFd_{-1};
    int evAcceptId_{0};

    NewConnectionCallback onNewConn_{nullptr};
    ErrorCallback onError_{nullptr};
};

} // namespace Mcp::Net

#endif // MCP_TCP_LISTENER_INCLUDE_H_
