/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "server/server_manager.h"

#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <random>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include "event/event_system.h"
#include "mcp_auth.h"
#include "mcp_log.h"
#include "shared/thread_utils.h"
#include "server/transport/streamable_http_server_transport.h"
#include "transport/stdio_transport.h"

#define URL_REGEX_GROUP_HOST 2
#define URL_REGEX_GROUP_PORT 3
#define URL_REGEX_GROUP_URI 4
#define PORT_MIN 1

namespace Mcp {

constexpr size_t MAX_BATCH_SIZE = 32;
constexpr int MAX_PORT_NUMBER = 65535;
constexpr int QUEUE_CAPACITY = 4096;

ServerManager::ServerManager(const ServerConfig& config, const StreamableHttpServerConfig& transportConfig)
    : config_(config), streamableConfig_(transportConfig), isStdio_{false},
      authenticator_(transportConfig.authenticator), authorizer_(transportConfig.authorizer) {}

ServerManager::ServerManager(const ServerConfig& config) : config_(config) {}

void ServerManager::ThreadMain(int id)
{
    // Set thread name
    SetCurrentThreadName("MCP-Worker-" + std::to_string(id));
    // Start EventSystem event loop
    MCP_LOG(MCP_LOG_LEVEL_INFO, "Protocol thread " + std::to_string(id) + " started");
    eventSystems_[id]->Start(false); // Block current thread
    MCP_LOG(MCP_LOG_LEVEL_INFO, "Protocol thread " + std::to_string(id) + " event loop ended");
}

ServerManager::~ServerManager()
{
}

void ServerManager::HttpServerManagerStart()
{
    Http::HttpServerManagerConfig httpConfig;

    // Parse endpoint from transport config
    std::string endpoint = streamableConfig_.endpoint;

    // Default values
    std::string host{};
    uint16_t port{0};
    std::string uri{}; // Default URI when not specified

    if (endpoint.empty()) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "No endpoint specified for transport");
        throw std::runtime_error("No endpoint specified for transport");
    }

    // Parse endpoint format: http://{host}:{port} or http://{host}:{port}/{uri}
    // Host and port are required fields
    std::regex url_regex(R"(^(https?:\/\/)?([^:\/\s]+):(\d+)(\/.*)?$)");
    std::smatch matches;

    if (std::regex_match(endpoint, matches, url_regex)) {
        // matches[0]: full match
        // matches[1]: protocol (http:// or https://) - optional
        // matches[2]: host - required
        // matches[3]: port - required
        // matches[4]: uri path (including the leading '/') - optional

        // Parse host (required)
        host = matches[URL_REGEX_GROUP_HOST].str();
        if (host.empty()) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "Host is required in endpoint: " + endpoint);
            throw std::runtime_error("Host is required in endpoint: " + endpoint);
        }

        // Parse port (required)
        try {
            int portInt = std::stoi(matches[URL_REGEX_GROUP_PORT].str());
            if (portInt < PORT_MIN || portInt > MAX_PORT_NUMBER) {
                MCP_LOG(MCP_LOG_LEVEL_ERROR, "Port must be between " + std::to_string(PORT_MIN) +
                        " and " + std::to_string(MAX_PORT_NUMBER) +
                        " in endpoint: " + endpoint);
                throw std::runtime_error("Port must be between " + std::to_string(PORT_MIN) +
                                        " and " + std::to_string(MAX_PORT_NUMBER) +
                                        " in endpoint: " + endpoint);
            }
            port = static_cast<uint16_t>(portInt);
        } catch (const std::exception& e) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "Failed to parse port from endpoint '" + endpoint + "': " + e.what());
            throw std::runtime_error("Failed to parse port from endpoint '" + endpoint + "': " + e.what());
        }

        // Parse URI - if not specified, use default "/"
        if (matches[URL_REGEX_GROUP_URI].matched) {
            uri = matches[URL_REGEX_GROUP_URI].str();
            // Ensure URI starts with '/'
            if (!uri.empty() && uri[0] != '/') {
                uri = "/" + uri;
            }
        } else {
            uri = "/"; // Default URI when not specified
        }
        MCP_LOG(MCP_LOG_LEVEL_INFO, "Parsed endpoint - Host: " + host + ", Port: " +
            std::to_string(port) + ", URI: " + uri);
    }

    // Configure HTTP server manager
    httpConfig.host = host;
    httpConfig.port = port;
    httpConfig.tlsConfig_ = streamableConfig_.tlsConfig;
    httpConfig.ioThreadNum = streamableConfig_.ioThreads;
    // Setup default route for MCP communication
    httpConfig.routeMap[uri] = [this](const Http::HttpRequest& request, RequestContext& ctx) {
        // Dispatch request to worker thread
        DispatchRequest(request, ctx);
    };

    httpServerManager_ = std::make_unique<Http::HttpServerManager>(httpConfig);
    // Start HTTP server manager
    try {
        httpServerManager_->Start();
    } catch (const std::exception& e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Failed to start HTTP server manager: " + std::string(e.what()));
        throw;
    }
    MCP_LOG(MCP_LOG_LEVEL_INFO, "HTTP server manager started successfully");
}

void ServerManager::StdioServerManagerStart()
{
    std::shared_ptr<ServerTransport> stdioTransport_ = std::make_shared<StdioServerTransport>();
    stdioSession_ = std::make_shared<ServerSession>(stdioTransport_, config_, GenerateSessionId());
    // Set incoming request callback if provided
    if (requestCallback_) {
        stdioSession_->SetIncomingRequestCallback(requestCallback_);
    }

    stdioTransport_->Listen();
}

void ServerManager::Start()
{
    if (isStdio_) {
        StdioServerManagerStart();
        return;
    }

    // Get the number of worker threads from config
    uint32_t numThreads = config_.workerThreads;
    MCP_LOG(MCP_LOG_LEVEL_INFO, "Starting ServerManager with " + std::to_string(numThreads) + " worker threads");

    // Reserve space for EventSystems, threads, and their session maps
    eventSystems_.reserve(numThreads);
    workerThreads_.reserve(numThreads);
    threadSessions_.resize(numThreads);
    threadQueues_.reserve(numThreads);
    threadQueueEventIds_.resize(numThreads, -1);
    notifyArgs_.reserve(numThreads);

    // Create EventSystems and queues for each thread BEFORE starting HTTP server
    for (uint32_t i = 0; i < numThreads; ++i) {
        // Create EventSystem with index
        auto eventSystem = std::make_unique<EventSystem>(true, i);
        if (!eventSystem->Init()) {
            // Handle initialization error
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "Failed to initialize EventSystem for thread " + std::to_string(i));
            throw std::runtime_error("Failed to initialize EventSystem for thread " + std::to_string(i));
        }

        // Create MPSCQueue for this thread
        auto queue = std::make_unique<MPSCQueue<DispatchRequestMsg>>(QUEUE_CAPACITY);

        // Create and store notifyArg to keep it alive
        auto notifyArg = std::make_shared<NotifyEventArg>();
        notifyArg->threadId = static_cast<int>(i);
        notifyArg->eventSystem = eventSystem.get();
        notifyArgs_.push_back(notifyArg);

        // Set up eventfd notification for queue messages
        int eventId = eventSystem->CreateNotifyEventId(
            [this](int fd, short events, void* arg) { HandleQueueNotification(fd, events, arg); },
            notifyArg.get(), // arg - pointer stays valid since notifyArg is stored in notifyArgs_
            true);
        if (eventId == -1) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "Failed to create event notification for thread " + std::to_string(i));
            throw std::runtime_error("Failed to create event notification for thread " + std::to_string(i));
        }

        threadQueueEventIds_[i] = eventId;
        eventSystems_.push_back(std::move(eventSystem));
        threadQueues_.push_back(std::move(queue));
    }

    // Start HTTP server AFTER queues and EventSystems are initialized
    // This ensures DispatchRequest can safely access threadQueues_
    HttpServerManagerStart();

    // Start protocol threads
    for (uint32_t i = 0; i < numThreads; ++i) {
        workerThreads_.emplace_back(&ServerManager::ThreadMain, this, i);
    }
    MCP_LOG(MCP_LOG_LEVEL_INFO, "ServerManager started successfully with " +
            std::to_string(numThreads) + " worker threads");
}

void ServerManager::StdioServerManagerStop()
{
    MCP_LOG(MCP_LOG_LEVEL_INFO, "Stopping Stdio ServerManager...");
    if (stdioSession_) {
        stdioSession_.reset();
    }

    if (stdioTransport_) {
        stdioTransport_->Terminate();
        stdioTransport_.reset();
    }

    MCP_LOG(MCP_LOG_LEVEL_INFO, "Stdio ServerManager stopped successfully.");
}

void ServerManager::Stop()
{
    if (isStdio_) {
        StdioServerManagerStop();
        return;
    }

    MCP_LOG(MCP_LOG_LEVEL_INFO, "Stopping ServerManager...");
    httpServerManager_->Stop();

    // Stop EventSystems first
    for (auto& eventSystem : eventSystems_) {
        if (eventSystem) {
            eventSystem->Stop();
        }
    }

    // Close queue event notifications
    for (size_t i = 0; i < threadQueueEventIds_.size(); ++i) {
        if (threadQueueEventIds_[i] != -1 && eventSystems_[i]) {
            eventSystems_[i]->CloseNotifyEventId(threadQueueEventIds_[i]);
        }
    }

    // Wait for all threads to finish
    for (auto& thread : workerThreads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    MCP_LOG(MCP_LOG_LEVEL_INFO, "ServerManager stopped successfully.");
}

std::shared_ptr<ServerSession> ServerManager::GetSession(const std::string& sessionId)
{
    if (isStdio_) {
        return stdioSession_;
    }

    int threadId = GetThreadIdForSession(sessionId);
    auto& sessions = threadSessions_[threadId];
    auto it = sessions.find(sessionId);
    if (it != sessions.end()) {
        return it->second;
    }
    return nullptr;
}

void ServerManager::SetIncomingRequestCallback(IncomingRequestCallback callback)
{
    requestCallback_ = callback;
}

std::shared_ptr<ServerSession> ServerManager::NewSession(const std::string& sessionId)
{
    // Create transport
    std::shared_ptr<ServerTransport> transport = std::make_shared<StreamableHttpServerTransport>(
        sessionId, streamableConfig_.isJsonResponseEnabled);
    if (transport == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Failed to create transport layer.");
        throw std::runtime_error("Failed to create transport layer.");
    }
    transport->Listen();

    auto session = std::make_shared<ServerSession>(transport, config_, sessionId);
    if (session == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Failed to create session layer.");
        throw std::runtime_error("Failed to create session layer.");
    }
    // Set incoming request callback if provided
    if (requestCallback_) {
        session->SetIncomingRequestCallback(requestCallback_);
    }

    return session;
}

int ServerManager::GetThreadIdForSession(const std::string& sessionId) const
{
    std::hash<std::string> hasher;
    return static_cast<int>(hasher(sessionId) % config_.workerThreads);
}

void ServerManager::HandleRequest(const HttpRequest& request, RequestContext& context)
{
    // Perform authentication if authenticator is configured
    if (authenticator_ != nullptr) {
        AuthenticationResult authResult = authenticator_->Authenticate(request.headers);
        if (!authResult.authenticated) {
            MCP_LOG(MCP_LOG_LEVEL_WARN,
                    std::string("Authentication failed: ") +
                        authResult.errorDescription.value_or("Unknown error"));
            HttpResponse response;
            response.statusCode = Http::HTTP_STATUS_UNAUTHORIZED;
            response.statusText = "Unauthorized";
            response.headers[Http::CONTENT_TYPE_HEADER] = Http::CONTENT_TYPE_JSON;
            nlohmann::json errorJson;
            errorJson["error"] = authResult.errorDescription.value_or("Authentication failed");
            response.body = errorJson.dump();
            if (context.httpSendFunc) {
                context.httpSendFunc(response, context);
            }
            return;
        }

        // Perform authorization if authorizer is configured
        if (authorizer_ != nullptr && !authorizer_->Authorize(authResult)) {
            MCP_LOG(MCP_LOG_LEVEL_WARN, "Authorization failed: insufficient scopes");
            HttpResponse response;
            response.statusCode = Http::HTTP_STATUS_FORBIDDEN;
            response.statusText = "Forbidden";
            response.headers[Http::CONTENT_TYPE_HEADER] = Http::CONTENT_TYPE_JSON;
            nlohmann::json errorJson;
            errorJson["error"] = "Insufficient permissions";
            response.body = errorJson.dump();
            if (context.httpSendFunc) {
                context.httpSendFunc(response, context);
            }
            return;
        }

        MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Authentication and authorization successful");
    }

    // Dispatch the request to the appropriate thread
    int threadId = GetThreadIdForSession(context.sessionId);
    MCP_LOG(MCP_LOG_LEVEL_INFO, "Handling request for session " + context.sessionId +
            " in thread " + std::to_string(threadId));

    // Find session in the specific thread's session map
    std::shared_ptr<ServerSession> session = nullptr;
    auto& sessions =
        const_cast<std::unordered_map<std::string, std::shared_ptr<ServerSession>>&>(threadSessions_[threadId]);
    auto it = sessions.find(context.sessionId);
    if (it != sessions.end()) {
        // Session exists, forward request to transport
        session = std::dynamic_pointer_cast<ServerSession>(it->second);
    } else {
        // Session does not exist, create new transport and session
        session = NewSession(context.sessionId);
        sessions[context.sessionId] = session;
    }
    session->HandleRequest(request, context);
}

std::string ServerManager::GenerateSessionId() const
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    std::uniform_int_distribution<> dis2(8, 11);

    std::stringstream ss;
    
    for (int i = 0; i < 32; i++) {
        if (i == 8 || i == 12 || i == 16 || i == 20) {
            ss << "-";
        }
        int v;
        if (i == 12) {
            v = 4; // Version 4
        } else if (i == 16) {
            v = dis2(gen); // Variant (8, 9, A, B)
        } else {
            v = dis(gen);
        }
        ss << std::hex << v;
    }

    return ss.str();
}

void ServerManager::DispatchRequest(const HttpRequest& request, RequestContext& context)
{
    // Dispatch the request to the appropriate thread
    if (context.sessionId.empty()) {
        context.sessionId = GenerateSessionId();
    }

        MCP_LOG(MCP_LOG_LEVEL_INFO, "Get request from connection " +
                std::to_string(context.connectionId) + " with session ID: " + context.sessionId);
    int threadId = GetThreadIdForSession(context.sessionId);

    // Push message to the thread's queue
    auto& queue = threadQueues_[threadId];
    DispatchRequestMsg msg{request, context}; // Copy values instead of pointers

    if (queue->Push(msg)) {
        // Notify the worker thread via EventSystem
        if (eventSystems_[threadId] && threadQueueEventIds_[threadId] != -1) {
            if (eventSystems_[threadId]->NotifyEventId(threadQueueEventIds_[threadId])) {
                MCP_LOG(MCP_LOG_LEVEL_INFO, "Dispatched request to thread " +
                        std::to_string(threadId) + " for session " + context.sessionId);
            } else {
                MCP_LOG(MCP_LOG_LEVEL_ERROR, "Failed to notify thread " +
                        std::to_string(threadId) + " for session " + context.sessionId);
            }
        }
    } else {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Failed to dispatch request to thread " +
                std::to_string(threadId) + " for session " + context.sessionId);
    }
}

void ServerManager::HandleQueueNotification(int fd, short events, void* arg)
{
    NotifyEventArg* notifyEventArg = static_cast<NotifyEventArg*>(arg);
    if (notifyEventArg == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "NotifyEventArg is null in HandleQueueNotification");
        return;
    }

    // Clear the eventfd notification
    uint64_t eventValue;
    while (notifyEventArg->eventSystem->ReadEventFd(fd, eventValue)) {
        // Continue reading until eventfd is empty
    }

    // Process messages in batch
    auto& queue = threadQueues_[notifyEventArg->threadId];
    DispatchRequestMsg msg;
    size_t processedCount = 0;
    size_t errorCount = 0;

    // Process up to MAX_BATCH_SIZE messages
    while (processedCount < MAX_BATCH_SIZE && queue->TryPop(msg)) {
        try {
            // Handle the request
            HandleRequest(msg.request, msg.context);
        } catch (const std::exception& e) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "Error handling request for session " +
                    msg.context.sessionId + ": " + e.what());
            ++errorCount;
        }
        ++processedCount;
    }

    // Log statistics for monitoring
    if (processedCount > 0 || errorCount > 0) {
        MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Thread " + std::to_string(notifyEventArg->threadId) + " processed " +
            std::to_string(processedCount) + " messages (" +std::to_string(errorCount) + " errors) in batch");
    }

    // If we processed MAX_BATCH_SIZE messages and the queue is still not empty,
    // re-trigger the notification to continue processing in the next event loop iteration
    if (processedCount >= MAX_BATCH_SIZE && !queue->Empty()) {
        notifyEventArg->eventSystem->NotifyEventId(fd);
        MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Thread " + std::to_string(notifyEventArg->threadId) +
                " re-triggered notification for remaining messages");
    }
}

} // namespace Mcp
