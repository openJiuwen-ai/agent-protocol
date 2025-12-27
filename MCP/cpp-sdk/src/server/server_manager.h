/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef MCP_SERVER_MANAGER_INCLUDE_H_
#define MCP_SERVER_MANAGER_INCLUDE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "event/event_system.h"
#include "mcp_type.h"
#include "shared/message_queue/lock_free_queue.h"
#include "server/http_server_manager.h"
#include "server_session.h"
#include "shared/http_common.h"

namespace Mcp {
struct DispatchRequestMsg {
    HttpRequest request; // Store by value, not pointer
    RequestContext context; // Store by value, not pointer
};

struct NotifyEventArg {
    int threadId;
    EventSystem* eventSystem;
};

class ServerManager {
public:
    ServerManager(const ServerConfig& config, const StreamableHttpServerConfig& transportConfig);
    ServerManager(const ServerConfig& config);
    ~ServerManager();

    void Start();
    void Stop();
    std::shared_ptr<ServerSession> GetSession(const std::string& sessionId);
    void SetIncomingRequestCallback(IncomingRequestCallback callback);

private:
    void StdioServerManagerStart();
    void StdioServerManagerStop();
    void HttpServerManagerStart();
    void ThreadMain(int id);
    void HandleRequest(const HttpRequest& request, RequestContext& context);
    void DispatchRequest(const HttpRequest& request, RequestContext& context);
    std::shared_ptr<ServerSession> NewSession(const std::string& sessionId);
    int GetThreadIdForSession(const std::string& sessionId) const;
    std::string GenerateSessionId() const;
    void HandleQueueNotification(int fd, short events, void* arg);

    ServerConfig config_;
    StreamableHttpServerConfig streamableConfig_;
    bool isStdio_{true};
    std::unique_ptr<Http::HttpServerManager> httpServerManager_;
    std::vector<std::unique_ptr<EventSystem>> eventSystems_;
    std::vector<std::thread> workerThreads_;
    IncomingRequestCallback requestCallback_{nullptr};

    // Each thread has its own session map to avoid contention
    // Index: thread_id -> (session_id -> session)
    std::vector<std::unordered_map<std::string, std::shared_ptr<ServerSession>>> threadSessions_;
    std::vector<std::unique_ptr<MPSCQueue<DispatchRequestMsg>>> threadQueues_;
    std::vector<int> threadQueueEventIds_;
    std::vector<std::shared_ptr<NotifyEventArg>> notifyArgs_;
    std::shared_ptr<ServerSession> stdioSession_{nullptr};
    std::shared_ptr<ServerTransport> stdioTransport_{nullptr};
};
} // namespace Mcp

#endif // MCP_SERVER_MANAGER_INCLUDE_H_
