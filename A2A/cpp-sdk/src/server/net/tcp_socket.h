/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_TCP_SOCKET_INCLUDE_H_
#define A2A_TCP_SOCKET_INCLUDE_H_

#include <string>
#include <string_view>
#include <vector>

#include "net/socket.h"

namespace A2A::Server {

// TCP-specific options
struct TcpSocketOptions : public SocketOptions {
    bool tcpNoDelay{true};
    bool keepAlive{false};
    // How long the connection must be completely idle before sending
    // TCP keepalive probes (seconds).
    int keepAliveIdleSec{0};
    // Interval between individual keepalive probes (seconds).
    int keepAliveIntvlSec{0};
    // Maximum number of unanswered keepalive probes before considering the
    // connection dead.
    int keepAliveCnt{0};
};

// Simple dynamic buffer used for sending and receiving data
class Buffer {
public:
    explicit Buffer(size_t initialCap = 4096);
    ~Buffer() = default;

    size_t ReadableBytes() const;
    size_t WritableBytes() const;
    const char* Peek() const;

    std::string RetrieveAllAsString();

    // Read data from the given fd. Returns number of bytes read,
    // 0 = EOF, -1 = error (errno).
    ssize_t ReadFd(int fd);

private:
    void EnsureWritable(size_t len);
    void MakeSpace(size_t len);

    std::vector<char> buf_;
    size_t r_;
    size_t w_;
};

// Represents a TCP connection socket
class TcpSocket;
using TcpSocketPtr = std::shared_ptr<TcpSocket>;

class TcpSocket : public Socket {
public:
    TcpSocket(EventSystem& es, int fd, const TcpSocketOptions& opts);
    // Adopt an existing file descriptor (typically from accept).
    static TcpSocketPtr Adopt(EventSystem& es, int fd, const TcpSocketOptions& opts = {});

    // Actively connect to a remote host (client-side).
    static TcpSocketPtr Connect(EventSystem& es, const std::string& host, uint16_t port,
        int connectTimeoutMs = 0, const TcpSocketOptions& opts = {});

    ~TcpSocket() override;

    bool Connected() const;

    // Send data. Returns true on success or if data was queued.
    bool Send(const char* data, size_t len) override;

    // Half-close / close the connection
    void ShutdownWrite() const;
    void Close() override;

    bool Send(std::string_view sv);
    // Receive buffer access
    Buffer& InputBuffer();
    Buffer inBuf_;

protected:
    void HandleReadable() override;
    void HandleWritable() override;

private:
    enum class State { CONNECTING, CONNECTED, CLOSING, CLOSED };

    void SetTcpOptions(const TcpSocketOptions& opts) const;
    void HandleConnectWritable();
    void HandleConnectTimeout();
    void TransitionTo(State s);

    static int CreateNonblockingTcpSocket(int family);

    State state_{State::CONNECTING};

    int evConnTimeoutId_{0};

    std::vector<char> outBuf_;
    size_t outBufSent_{0};
    size_t outBufSize_{0};

    TcpSocketOptions tcpOpts_;
};

} // namespace A2A::Server

#endif // A2A_TCP_SOCKET_INCLUDE_H_