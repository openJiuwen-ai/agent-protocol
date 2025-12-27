/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef MCP_STDIO_TRANSPORT_INCLUDE_H_
#define MCP_STDIO_TRANSPORT_INCLUDE_H_

#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "event/event_system.h"
#include "transport/transport.h"

namespace Mcp {

/**
 * @brief StdioConnection implements Connection interface for stdin/stdout communication
 *
 * This unified class handles MCP protocol communication over stdin/stdout for both client
 * and server use cases using the event system for non-blocking I/O and event-driven
 * message processing.
 */
class StdioConnection {
public:
    StdioConnection();

    ~StdioConnection();

    // Connection interface
    bool Listen();
    bool Connect();
    void Disconnect();
    bool IsConnected() const;
    void SendMessage(const std::string& data);

    // Callback setters
    void SetConnectedCallback(std::function<void()> callback);
    void SetDisconnectedCallback(std::function<void(const std::string&)> callback);
    void SetMessageReceivedCallback(std::function<void(const std::string&)> callback);
    void SetErrorCallback(std::function<void(const std::string&)> callback);

    // Statistics
    uint64_t GetBytesSent() const;
    uint64_t GetBytesReceived() const;

    // Process management (for client use)
    void SetSubprocessConfig(const std::string& command, const std::vector<std::string>& args = {},
                             const std::map<std::string, std::string>& env = {});

private:
    // Event handlers
    void HandleStdinReadable(int fd, short events, void* arg);
    void HandleStdoutWritable(int fd, short events, void* arg);

    // Message processing
    ssize_t Write(std::string message);
    void ProcessReceivedData();
    void SendQueuedMessages();
    void EnableWriteEvent();
    void DisableWriteEvent();

    // Error handling
    void NotifyError(const std::string& error);

    // Message processing
    std::vector<std::string> ProcessReceivedMessages(const std::string& data);
    std::vector<std::string> ExtractLinesFromBuffer();
    static std::string FrameMessage(const std::string& message);

    // Process management (for client use)
    bool ContainsInvalidChars(const std::string& str);
    bool StartSubprocess();
    void StopSubprocess();
    bool SetupPipes();
    void RestoreStdio();

    std::unique_ptr<Mcp::EventSystem> eventSystem_;
    int stdinFd_{STDIN_FILENO};
    int stdoutFd_{STDOUT_FILENO};
    int originalStdinFd_{-1};
    int originalStdoutFd_{-1};

    int readEventId_{-1};
    int writeEventId_{-1};

    std::atomic<bool> connected_{false};
    std::string readBuffer_;

    // Use queue for better thread safety (shared between client and server)
    std::mutex writeQueueMutex_;
    std::queue<std::string> writeQueue_;

    // Callbacks
    std::function<void()> onConnected_;
    std::function<void(const std::string&)> onDisconnected_;
    std::function<void(const std::string&)> onMessageReceived_;
    std::function<void(const std::string&)> onError_;

    // Statistics
    std::atomic<uint64_t> bytesSent_{0};
    std::atomic<uint64_t> bytesReceived_{0};

    // Message framing buffer
    std::string messageBuffer_;

    // Subprocess management (only for client use)
    std::string subprocessCommand_;
    std::vector<std::string> subprocessArgs_;
    std::map<std::string, std::string> subprocessEnv_;
    pid_t subprocessPid_{-1};
    int subprocessInputPipe_[2]{-1, -1}; // parent writes to [1], child reads from [0]
    int subprocessOutputPipe_[2]{-1, -1}; // child writes to [1], parent reads from [0]
};

/**
 * @brief StdioClientTransport implements ClientTransport for stdio communication
 */
class StdioClientTransport : public ClientTransport {
public:
    StdioClientTransport(const StdioClientConfig& config);
    ~StdioClientTransport() override;

    // Transport interface implementation
    void Connect() override;
    void Terminate() override;
    void SendMessage(const JSONRPCMessage& message) override;
    void SetCallback(std::shared_ptr<TransportCallback> callback) override;

private:
    std::shared_ptr<StdioConnection> connection_;
    std::atomic<bool> running_{false};
    std::shared_ptr<TransportCallback> callback_;
    StdioClientConfig config_;
    std::string method_;
    RequestContext ctx_;

    void SetupConnectionCallbacks();
    void CleanupConnection();
    void GetMessageMethod(const JSONRPCMessage& message);
};

/**
 * @brief StdioServerTransport implements ServerTransport for stdio communication
 */
class StdioServerTransport : public ServerTransport {
public:
    StdioServerTransport();
    ~StdioServerTransport() override;

    // Transport interface implementation
    void Listen() override;
    void Terminate() override;
    void SendMessage(const JSONRPCMessage& message, const RequestContext& ctx) override;
    void SetCallback(std::shared_ptr<TransportCallback> callback) override;
    void HandleRequest(const Http::HttpRequest& request, RequestContext& ctx) override;

private:
    std::shared_ptr<StdioConnection> connection_;
    std::atomic<bool> running_{false};
    std::shared_ptr<TransportCallback> callback_;
    std::string method_;
    RequestContext ctx_;

    void SetupConnectionCallbacks();
    void CleanupConnection();
    void GetMessageMethod(const JSONRPCMessage& message);
};

} // namespace Mcp

#endif // MCP_STDIO_TRANSPORT_INCLUDE_H_
