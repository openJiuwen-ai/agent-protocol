/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "event/event_system.h"

namespace Mcp::Net {
struct TcpSocketOptions {
    bool tcpNoDelay = true;
};

class Buffer {
public:
    std::size_t ReadableBytes() const { return data_.size(); }
    std::string RetrieveAllAsString() { return std::exchange(data_, {}); }
    void Append(std::string_view data) { data_.append(data); }

private:
    std::string data_;
};

class TcpSocket;
using SocketPtr = std::shared_ptr<TcpSocket>;

class TcpSocket : public std::enable_shared_from_this<TcpSocket> {
public:
    inline static SocketPtr connection;
    static SocketPtr Connect(Mcp::EventSystem&, const std::string&, uint16_t, int, const TcpSocketOptions&)
    {
        connection = std::make_shared<TcpSocket>();
        return connection;
    }
    void OnError(std::function<void(const SocketPtr&, int, const std::string&)> callback)
    {
        onError_ = std::move(callback);
    }
    void OnRead(std::function<void(const SocketPtr&)> callback) { onRead_ = std::move(callback); }
    void OnClose(std::function<void(const SocketPtr&)> callback) { onClose_ = std::move(callback); }
    bool Send(std::string_view) { return true; }
    int Fd() const { return -1; }
    Buffer& InputBuffer() { return buffer_; }
    void Close()
    {
        if (!closed_) {
            closed_ = true;
            onClose_(shared_from_this());
        }
    }
    void Deliver(std::string_view data, bool eofBeforeRead = false)
    {
        buffer_.Append(data);
        // Real TcpSocket::HandleReadable can invoke OnClose synchronously
        // before Socket's read event dispatches OnRead.
        if (eofBeforeRead) {
            Close();
        }
        onRead_(shared_from_this());
    }

private:
    Buffer buffer_;
    bool closed_ = false;
    std::function<void(const SocketPtr&)> onRead_;
    std::function<void(const SocketPtr&)> onClose_;
    std::function<void(const SocketPtr&, int, const std::string&)> onError_;
};
}
