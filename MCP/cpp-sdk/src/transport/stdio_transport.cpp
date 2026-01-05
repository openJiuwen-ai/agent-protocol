/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "transport/stdio_transport.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

#include "mcp_log.h"
#include "mcp_type.h"
#include "shared/common_type.h"
#include "shared/jsonrpc.h"

namespace Mcp {
constexpr int DEFAULT_RECV_BUFF_SIZE = 4096;

// StdioConnection Implementation
StdioConnection::StdioConnection() : eventSystem_(std::make_unique<EventSystem>())
{
    // Initialize event system
    if (!eventSystem_->Init()) {
        NotifyError("Failed to initialize event system");
        return;
    }

    MCP_LOG(MCP_LOG_LEVEL_INFO, "StdioConnection created");
}

StdioConnection::~StdioConnection()
{
    Disconnect();
}

bool StdioConnection::Listen()
{
    if (connected_) {
        MCP_LOG(MCP_LOG_LEVEL_WARN, "StdioConnection already listening");
        return true;
    }

    // Set stdin/stdout to non-blocking
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
    fcntl(STDOUT_FILENO, F_SETFL, O_NONBLOCK);

    // Register stdin read event for server mode
    readEventId_ = eventSystem_->AddEvent(
        stdinFd_, EventType::READ | EventType::PERSIST,
        [this](int fd, short events, void* arg) { HandleStdinReadable(fd, events, arg); }, this);
    if (readEventId_ < 0) {
        NotifyError("Failed to register stdin read event");
        return false;
    }

    connected_ = true;

    // Start event system in background thread
    eventSystem_->Start(true);

    // Notify connection established
    if (onConnected_) {
        onConnected_();
    }

    MCP_LOG(MCP_LOG_LEVEL_INFO, "StdioConnection started listening on stdin/stdout");
    return true;
}

bool StdioConnection::Connect()
{
    if (connected_) {
        MCP_LOG(MCP_LOG_LEVEL_WARN, "StdioConnection already connected");
        return true;
    }

    // Start subprocess if configured
    if (!subprocessCommand_.empty()) {
        if (!StartSubprocess()) {
            NotifyError("Failed to start subprocess");
            return false;
        }
        MCP_LOG(MCP_LOG_LEVEL_INFO, "Subprocess started successfully");
    } else {
        // Set stdin/stdout to non-blocking for direct communication
        fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
        fcntl(STDOUT_FILENO, F_SETFL, O_NONBLOCK);
        MCP_LOG(MCP_LOG_LEVEL_INFO, "Using direct stdio communication");
    }

    // Register stdin read event for client mode
    readEventId_ = eventSystem_->AddEvent(
        stdinFd_, EventType::READ | EventType::PERSIST,
        [this](int fd, short events, void* arg) { HandleStdinReadable(fd, events, arg); }, this);
    if (readEventId_ < 0) {
        NotifyError("Failed to register stdin read event");
        return false;
    }

    connected_ = true;

    // Start event system in background thread
    eventSystem_->Start(true);

    // Notify connection established
    if (onConnected_) {
        onConnected_();
    }

    MCP_LOG(MCP_LOG_LEVEL_INFO, "StdioConnection established successfully");
    return true;
}

void StdioConnection::Disconnect()
{
    if (!connected_) {
        return;
    }

    connected_ = false;

    // Remove event handlers
    if (readEventId_ >= 0) {
        eventSystem_->RemoveEvent(readEventId_);
        readEventId_ = -1;
    }

    if (writeEventId_ >= 0) {
        eventSystem_->RemoveEvent(writeEventId_);
        writeEventId_ = -1;
    }

    // Stop event system
    eventSystem_->Stop();

    // Stop subprocess if running
    if (subprocessPid_ > 0) {
        StopSubprocess();
    }

    // Notify disconnected
    if (onDisconnected_) {
        onDisconnected_("");
    }

    MCP_LOG(MCP_LOG_LEVEL_INFO, "StdioConnection disconnected");
}

bool StdioConnection::IsConnected() const
{
    return connected_;
}

void StdioConnection::SendMessage(const std::string& data)
{
    if (!connected_) {
        NotifyError("Cannot send message: not connected");
        return;
    }

    std::lock_guard<std::mutex> lock(writeQueueMutex_);
    writeQueue_.push(data);

    // Enable write event if not already enabled
    if (writeEventId_ < 0) {
        EnableWriteEvent();
    }
}

void StdioConnection::SetConnectedCallback(std::function<void()> callback)
{
    onConnected_ = std::move(callback);
}

void StdioConnection::SetDisconnectedCallback(std::function<void(const std::string&)> callback)
{
    onDisconnected_ = std::move(callback);
}

void StdioConnection::SetMessageReceivedCallback(std::function<void(const std::string&)> callback)
{
    onMessageReceived_ = std::move(callback);
}

void StdioConnection::SetErrorCallback(std::function<void(const std::string&)> callback)
{
    onError_ = std::move(callback);
}

uint64_t StdioConnection::GetBytesSent() const
{
    return bytesSent_.load();
}

uint64_t StdioConnection::GetBytesReceived() const
{
    return bytesReceived_.load();
}

void StdioConnection::SetSubprocessConfig(const std::string& command, const std::vector<std::string>& args,
                                          const std::map<std::string, std::string>& env)
{
    subprocessCommand_ = command;
    subprocessArgs_ = args;
    subprocessEnv_ = env;
}

void StdioConnection::HandleStdinReadable(int fd, short events, void* arg)
{
    if (events & static_cast<short>(EventType::READ)) {
        char buffer[DEFAULT_RECV_BUFF_SIZE];
        ssize_t bytesRead = read(fd, buffer, sizeof(buffer));
        if (bytesRead > 0) {
            bytesReceived_ += bytesRead;
            std::string data(buffer, bytesRead);
            readBuffer_ += data;

            // Process received data for complete messages
            ProcessReceivedData();
        } else if (bytesRead == 0) {
            // EOF - connection closed
            NotifyError("Connection closed by peer");
            Disconnect();
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            NotifyError("Read error: " + std::string(strerror(errno)));
            Disconnect();
        }
    }
}

void StdioConnection::HandleStdoutWritable(int fd, short events, void* arg)
{
    if (events & static_cast<short>(EventType::WRITE)) {
        SendQueuedMessages();
    }
}

void StdioConnection::ProcessReceivedData()
{
    // Process received data to extract complete messages
    auto messages = ProcessReceivedMessages(readBuffer_);
    for (const auto& message : messages) {
        MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Received complete message: %s", message.c_str());
        if (onMessageReceived_) {
            onMessageReceived_(message);
        }
    }

    // Keep any remaining partial data in messageBuffer_
    readBuffer_ = messageBuffer_;
}

ssize_t StdioConnection::Write(std::string message)
{
    const char* ptr = static_cast<const char*>(message.c_str());
    size_t total_written = 0;
    size_t count = message.length();

    while (total_written < count) {
        ssize_t written = write(stdoutFd_, ptr + total_written, count - total_written);
        if (written == -1) {
            if (errno == EINTR) {
                continue;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else {
                MCP_LOG(MCP_LOG_LEVEL_ERROR, "write failed: %s", strerror(errno));
                return -1;
            }
        }

        total_written += static_cast<size_t>(written);
    }

    return total_written;
}

void StdioConnection::SendQueuedMessages()
{
    std::lock_guard<std::mutex> lock(writeQueueMutex_);

    while (!writeQueue_.empty()) {
        const std::string& message = writeQueue_.front();
        MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Sending queued message: %s", message.c_str());
        // Apply message framing
        std::string framedMessage = FrameMessage(message);
        ssize_t bytesWritten = Write(framedMessage);
        if (bytesWritten >= 0) {
            bytesSent_ += bytesWritten;
            if (static_cast<size_t>(bytesWritten) < framedMessage.length()) {
                // Partial write, adjust the message in the queue
                std::string remaining = framedMessage.substr(bytesWritten);
                writeQueue_.front() = remaining;
                MCP_LOG(MCP_LOG_LEVEL_INFO, "Partial write of message %s, %zu bytes remaining", message.c_str(),
                        remaining.length());
                break; // Exit to try again later
            } else {
                writeQueue_.pop();
            }
        } else {
            NotifyError("Failed to write message: " + std::string(strerror(errno)));
            break;
        }
    }

    // If write queue is empty, disable write event
    if (writeQueue_.empty()) {
        DisableWriteEvent();
    }
}

void StdioConnection::EnableWriteEvent()
{
    if (writeEventId_ >= 0) {
        return; // Already enabled
    }

    writeEventId_ = eventSystem_->AddEvent(
        stdoutFd_, EventType::WRITE | EventType::PERSIST,
        [this](int fd, short events, void* arg) { HandleStdoutWritable(fd, events, arg); }, this);
    if (writeEventId_ < 0) {
        NotifyError("Failed to register stdout write event");
    }
}

void StdioConnection::DisableWriteEvent()
{
    if (writeEventId_ >= 0) {
        eventSystem_->RemoveEvent(writeEventId_);
        writeEventId_ = -1;
    }
}

bool StdioConnection::ContainsInvalidChars(const std::string& str)
{
    return std::any_of(str.begin(), str.end(), [](char c) { return c == '=' || c == '\n' || c == '\r'; });
}

bool StdioConnection::StartSubprocess()
{
    // Create pipes for communication
    if (pipe(subprocessInputPipe_) != 0 || pipe(subprocessOutputPipe_) != 0) {
        NotifyError("Failed to create pipes");
        return false;
    }

    subprocessPid_ = fork();
    if (subprocessPid_ < 0) {
        NotifyError("Failed to fork process");
        return false;
    }

    if (subprocessPid_ == 0) {
        // Child process
        close(subprocessInputPipe_[1]); // Close write end of input pipe
        close(subprocessOutputPipe_[0]); // Close read end of output pipe

        // Redirect stdin from input pipe
        dup2(subprocessInputPipe_[0], STDIN_FILENO);
        close(subprocessInputPipe_[0]);

        // Redirect stdout to output pipe
        dup2(subprocessOutputPipe_[1], STDOUT_FILENO);
        close(subprocessOutputPipe_[1]);

        // Set up environment
        for (const auto& env : subprocessEnv_) {
            if (ContainsInvalidChars(env.first) || ContainsInvalidChars(env.second)) {
                continue;
            }
            setenv(env.first.c_str(), env.second.c_str(), 1);
        }

        // Build command arguments
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(subprocessCommand_.c_str()));
        for (const auto& arg : subprocessArgs_) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        // Execute command
        execvp(subprocessCommand_.c_str(), argv.data());
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Failed to exec subprocess: %s", strerror(errno));

        // If we get here, exec failed
        exit(1);
    } else {
        // Parent process
        close(subprocessInputPipe_[0]); // Close read end of input pipe
        close(subprocessOutputPipe_[1]); // Close write end of output pipe

        // Set pipe ends to non-blocking
        fcntl(subprocessInputPipe_[1], F_SETFL, O_NONBLOCK);
        fcntl(subprocessOutputPipe_[0], F_SETFL, O_NONBLOCK);

        // Update file descriptors for communication
        stdinFd_ = subprocessOutputPipe_[0];
        stdoutFd_ = subprocessInputPipe_[1];

        MCP_LOG(MCP_LOG_LEVEL_INFO, "Subprocess started with PID: %d", subprocessPid_);
        return true;
    }
}

void StdioConnection::StopSubprocess()
{
    if (subprocessPid_ > 0) {
        // Send SIGTERM to subprocess
        int ret = kill(subprocessPid_, SIGTERM);
        if (ret == -1) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "Failed to send SIGTERM to subprocess: %s", strerror(errno));
        }

        // Wait for process to terminate
        int status;
        waitpid(subprocessPid_, &status, 0);

        // Close pipes
        if (subprocessInputPipe_[0] >= 0) {
            close(subprocessInputPipe_[0]);
            subprocessInputPipe_[0] = -1;
        }
        if (subprocessInputPipe_[1] >= 0) {
            close(subprocessInputPipe_[1]);
            subprocessInputPipe_[1] = -1;
        }
        if (subprocessOutputPipe_[0] >= 0) {
            close(subprocessOutputPipe_[0]);
            subprocessOutputPipe_[0] = -1;
        }
        if (subprocessOutputPipe_[1] >= 0) {
            close(subprocessOutputPipe_[1]);
            subprocessOutputPipe_[1] = -1;
        }

        subprocessPid_ = -1;
        MCP_LOG(MCP_LOG_LEVEL_INFO, "Subprocess stopped");
    }
}

void StdioConnection::NotifyError(const std::string& error)
{
    MCP_LOG(MCP_LOG_LEVEL_ERROR, "StdioConnection error: %s", error.c_str());
    if (onError_) {
        onError_(error);
    }
}

// StdioConnection message framing implementations
std::vector<std::string> StdioConnection::ProcessReceivedMessages(const std::string& data)
{
    messageBuffer_.append(data);
    return ExtractLinesFromBuffer();
}

std::vector<std::string> StdioConnection::ExtractLinesFromBuffer()
{
    std::vector<std::string> messages;
    size_t pos = 0;

    while (pos < messageBuffer_.length()) {
        // Find next newline
        size_t newlinePos = messageBuffer_.find('\n', pos);
        if (newlinePos == std::string::npos) {
            // No more complete lines, keep remaining data
            messageBuffer_.erase(0, pos);
            return messages;
        }

        // Extract the line (excluding the newline)
        std::string line = messageBuffer_.substr(pos, newlinePos - pos);
        // Remove trailing carriage return if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // Only add non-empty lines
        if (!line.empty()) {
            messages.push_back(line);
        }

        // Move past the newline
        pos = newlinePos + 1;
    }

    // All data processed, clear buffer
    messageBuffer_.clear();
    return messages;
}

std::string StdioConnection::FrameMessage(const std::string& message)
{
    return message + '\n';
}

// StdioClientTransport Implementation
StdioClientTransport::StdioClientTransport(const StdioClientConfig& config)
{
    config_ = config;
    ctx_.connectionId = 0;
    MCP_LOG(MCP_LOG_LEVEL_INFO, "StdioClientTransport created");
}

StdioClientTransport::~StdioClientTransport()
{
    Terminate();
}

void StdioClientTransport::SetCallback(std::shared_ptr<TransportCallback> callback)
{
    callback_ = callback;
    SetupConnectionCallbacks();
}

void StdioClientTransport::Connect()
{
    if (running_) {
        MCP_LOG(MCP_LOG_LEVEL_WARN, "StdioClientTransport already connected");
        return;
    }

    // Create connection
    connection_ = std::make_shared<StdioConnection>();
    if (!config_.command.empty()) {
        connection_->SetSubprocessConfig(config_.command, config_.args, config_.env);
    }
    // Attempt to connect
    if (connection_->Connect()) {
        MCP_LOG(MCP_LOG_LEVEL_INFO, "Client StdioClientTransport connected successfully");
        running_ = true;
    } else {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Client StdioClientTransport failed to connect");
        connection_ = nullptr;
    }

    // Set up callbacks
    SetupConnectionCallbacks();
}

void StdioClientTransport::Terminate()
{
    if (!running_) {
        return;
    }

    running_ = false;
    CleanupConnection();
    MCP_LOG(MCP_LOG_LEVEL_INFO, "ClientStdioTransport terminated");
}

void StdioClientTransport::GetMessageMethod(const JSONRPCMessage& message)
{
    std::string methodName = std::visit(
        [](auto&& arg) -> std::string {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, Mcp::JSONRPCRequest>) {
                return arg.method_;
            } else if constexpr (std::is_same_v<T, Mcp::JSONRPCNotification>) {
                return arg.method_;
            } else {
                return "";
            }
        },
        message);
    method_ = methodName;
}

void StdioClientTransport::SendMessage(const JSONRPCMessage& message)
{
    if (connection_) {
        GetMessageMethod(message);
        // Convert JSONRPCMessage to string for sending
        std::string data = SerializeJSONRPCMessage(message);
        MCP_LOG(MCP_LOG_LEVEL_DEBUG, "StdioClientTransport sending message: %s", data.c_str());
        connection_->SendMessage(data);
    } else {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Cannot send message: no connection available");
    }
}

void StdioClientTransport::SetupConnectionCallbacks()
{
    if (connection_ && callback_) {
        // Set message received callback
        connection_->SetMessageReceivedCallback([this](const std::string& message) {
            try {
                MCP_LOG(MCP_LOG_LEVEL_DEBUG, "StdioClientTransport received message: %s", message.c_str());
                JSONRPCMessage rpcMessage = DeserializeJSONRPCMessage(message, method_);
                callback_->OnMessageReceived(rpcMessage, ctx_);
            } catch (const nlohmann::json::parse_error& e) {
                MCP_LOG(MCP_LOG_LEVEL_ERROR, "Failed to parse JSON message: %s, Message: %s", e.what(),
                        message.c_str());
            } catch (const std::runtime_error& e) {
                MCP_LOG(MCP_LOG_LEVEL_ERROR, "Invalid JSON-RPC message: %s, Message: %s", e.what(), message.c_str());
            } catch (const std::exception& e) {
                MCP_LOG(MCP_LOG_LEVEL_ERROR, "Error processing message: %s, Message: %s", e.what(), message.c_str());
            }
        });

        connection_->SetDisconnectedCallback([this](const std::string& reason) { callback_->OnDisconnected(reason); });
        MCP_LOG(MCP_LOG_LEVEL_INFO, "StdioClientTransport connection callbacks set up successfully");
    } else {
        MCP_LOG(MCP_LOG_LEVEL_WARN, "Cannot set up connection callbacks: missing connection or callback");
    }
}

void StdioClientTransport::CleanupConnection()
{
    if (connection_) {
        connection_->Disconnect();
        connection_ = nullptr;
    }
}

// StdioServerTransport Implementation
StdioServerTransport::StdioServerTransport() : ctx_{ .connectionId = 0 }
{
}

StdioServerTransport::~StdioServerTransport()
{
}

void StdioServerTransport::SetCallback(std::shared_ptr<TransportCallback> callback)
{
    callback_ = callback;
    SetupConnectionCallbacks();
}

void StdioServerTransport::Listen()
{
    if (running_) {
        MCP_LOG(MCP_LOG_LEVEL_WARN, "StdioServerTransport already listening");
        return;
    }

    // Create connection
    connection_ = std::make_shared<StdioConnection>();
    // Start listening
    if (connection_->Listen()) {
        running_ = true;
        MCP_LOG(MCP_LOG_LEVEL_INFO, "Server StdioServerTransport started listening on");
    } else {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Server StdioServerTransport failed to listening on");
        connection_ = nullptr;
        throw std::runtime_error("Failed to start StdioServerTransport listening");
    }

    // Set up callbacks
    SetupConnectionCallbacks();
}

void StdioServerTransport::Terminate()
{
    if (!running_) {
        return;
    }

    running_ = false;
    CleanupConnection();
    MCP_LOG(MCP_LOG_LEVEL_INFO, "ClientStdioTransport terminated");
}

void StdioServerTransport::GetMessageMethod(const JSONRPCMessage& message)
{
    std::string methodName = std::visit(
        [](auto&& arg) -> std::string {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, Mcp::JSONRPCRequest>) {
                return arg.method_;
            } else if constexpr (std::is_same_v<T, Mcp::JSONRPCNotification>) {
                return arg.method_;
            } else {
                return "";
            }
        },
        message);
    method_ = methodName;
}

void StdioServerTransport::SetupConnectionCallbacks()
{
    if (connection_ && callback_) {
        // Set message received callback
        connection_->SetMessageReceivedCallback([this](const std::string& message) {
            try {
                MCP_LOG(MCP_LOG_LEVEL_DEBUG, "StdioServerTransport received message: %s", message.c_str());
                JSONRPCMessage rpcMessage = DeserializeJSONRPCMessage(message, method_);
                callback_->OnMessageReceived(rpcMessage, ctx_);
            } catch (const nlohmann::json::parse_error& e) {
                MCP_LOG(MCP_LOG_LEVEL_ERROR, "Failed to parse JSON message: %s, Message: %s", e.what(),
                        message.c_str());
            } catch (const std::runtime_error& e) {
                MCP_LOG(MCP_LOG_LEVEL_ERROR, "Invalid JSON-RPC message: %s, Message: %s", e.what(), message.c_str());
            } catch (const std::exception& e) {
                MCP_LOG(MCP_LOG_LEVEL_ERROR, "Error processing message: %s, Message: %s", e.what(), message.c_str());
            }
        });

        connection_->SetDisconnectedCallback([this](const std::string& reason) { callback_->OnDisconnected(reason); });
        MCP_LOG(MCP_LOG_LEVEL_INFO, "StdioServerTransport connection callbacks set up successfully");
    } else {
        MCP_LOG(MCP_LOG_LEVEL_WARN, "Cannot set up connection callbacks: missing connection or callback");
    }
}

void StdioServerTransport::CleanupConnection()
{
    if (connection_) {
        connection_->Disconnect();
        connection_ = nullptr;
    }
}

void StdioServerTransport::SendMessage(const JSONRPCMessage& message, const RequestContext& ctx)
{
    if (connection_) {
        GetMessageMethod(message);
        // Convert JSONRPCMessage to string for sending
        std::string data = SerializeJSONRPCMessage(message, ctx.method);
        MCP_LOG(MCP_LOG_LEVEL_DEBUG, "StdioServerTransport sending message: %s", data.c_str());
        connection_->SendMessage(data);
    } else {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Cannot send message: no connection available");
    }
}

void StdioServerTransport::HandleRequest(const Http::HttpRequest& request, RequestContext& ctx)
{
}

} // namespace Mcp
