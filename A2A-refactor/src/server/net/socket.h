/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_SOCKET_INCLUDE_H_
#define A2A_SOCKET_INCLUDE_H_

#include <functional>
#include <memory>
#include <string>

#include "event_system.h"

namespace A2A::Server {

class Socket;
using SocketPtr = std::shared_ptr<Socket>;
constexpr int LISTENER_PORT_STRING_SIZE = 16;

// Callback signatures
using ReadCallback = std::function<void(const SocketPtr& sock)>;
using WritableCallback = std::function<void(const SocketPtr& sock)>;
using CloseCallback = std::function<void(const SocketPtr& sock)>;
using ErrorCallback = std::function<void(const SocketPtr& sock, int err, const std::string& msg)>;

// General socket options
struct SocketOptions {
    bool nonBlocking{true};

    int recvBufSize{0}; // 0 = do not set when 0
    int sendBufSize{0}; // 0 = do not set when 0
    bool reuseAddr{false};
    bool reusePort{false};
};

// Generic Socket abstraction that manages the fd and the read/write events
// registered with the EventSystem
class Socket : public std::enable_shared_from_this<Socket> {
public:
    Socket(EventSystem& es, int fd, const SocketOptions& opts = {});
    virtual ~Socket() = default;

    int Fd() const;
    bool Valid() const;

    // Callback registration
    void OnRead(ReadCallback cb);
    void OnWritable(WritableCallback cb);
    void OnClose(CloseCallback cb);
    void OnError(ErrorCallback cb);

    // Event control
    void EnableRead();
    void DisableRead();
    void EnableWrite();
    void DisableWrite();

    // Explicit close
    virtual void Close();

    // Send data (pure virtual; implemented by subclasses)
    virtual bool Send(const char* data, size_t len) = 0;

    // Internal fd settings
    static bool SetNonBlocking(int fd);

protected:
    virtual void HandleReadable()
    {
    }
    virtual void HandleWritable()
    {
    }

    void NotifyError(int err, const char* where);
    void NotifyCloseOnce();

    // Apply common socket options
    void ApplyOptions(const SocketOptions& opts);

    EventSystem& es_;
    int fd_{-1};

    int evReadId_{0};
    int evWriteId_{0};

    SocketOptions options_;

    ReadCallback onRead_{nullptr};
    WritableCallback onWritable_{nullptr};
    CloseCallback onClose_{nullptr};
    ErrorCallback onError_{nullptr};

    // Self-reference to keep the object alive until close
    SocketPtr selfHold_;
};

} // namespace A2A::Server

#endif // A2A_SOCKET_INCLUDE_H_
