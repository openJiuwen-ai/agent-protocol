/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "http_client_service.h"

#include <sys/syscall.h>

#include <algorithm>
#include <cstring>
#include <queue>
#include <sstream>

#include "event_system.h"
#include "mcp_log.h"

namespace Mcp {
namespace Http {

constexpr int MAX_PROCESSED_REQUEST_WHEN_STOP = 1000;
constexpr int GRACEFUL_STOP_TIMEOUT = 3000;
constexpr int GRACEFUL_STOP_SLEEP_TIME = 10;
constexpr int DEFAULT_QUEUE_CAPACITY = 1024;
constexpr int DEFAULT_QUEUE_MAX_BATCH_SIZE = 16;

// Constructor and destructor
HttpClientService::HttpClientService(const HttpClientServiceConfig& config)
    : config_(config), requestQueue_(DEFAULT_QUEUE_CAPACITY, DEFAULT_QUEUE_MAX_BATCH_SIZE) {}

HttpClientService::~HttpClientService()
{
    try {
        Stop();
    } catch (const std::exception& e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("Exception caught in destructor: ") + e.what());
    }
}

// Lifecycle management
bool HttpClientService::Start()
{
    if (state_.load(std::memory_order_acquire) != ServiceState::STOPPED) {
        MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Service already running");
        return true;
    }

    MCP_LOG(MCP_LOG_LEVEL_INFO, "Starting HTTP client service...");

    // Initialize libcurl
    CURLcode code = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (code != CURLE_OK) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR,
                std::string("Failed to initialize CURL: ") + std::string(curl_easy_strerror(code)));
        return false;
    }

    // Initialize CURL multi handle
    multiHandle_ = curl_multi_init();
    if (multiHandle_ == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Failed to initialize CURL multi handle");
        curl_global_cleanup();
        return false;
    }

    // Initialize EventSystem
    try {
        eventSystem_ = std::make_unique<EventSystem>(true);
        if (!eventSystem_->Init()) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "Failed to initialize EventSystem");
            curl_multi_cleanup(multiHandle_);
            multiHandle_ = nullptr;
            curl_global_cleanup();
            return false;
        }
    } catch (const std::exception& e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, std::string("Failed to create EventSystem: ") + e.what());
        curl_multi_cleanup(multiHandle_);
        multiHandle_ = nullptr;
        curl_global_cleanup();
        return false;
    }

    // Set libcurl callbacks
    curl_multi_setopt(multiHandle_, CURLMOPT_SOCKETFUNCTION, SocketCallback);
    curl_multi_setopt(multiHandle_, CURLMOPT_SOCKETDATA, this);
    curl_multi_setopt(multiHandle_, CURLMOPT_TIMERFUNCTION, TimerCallback);
    curl_multi_setopt(multiHandle_, CURLMOPT_TIMERDATA, this);

    // Initialize request queue
    if (!requestQueue_.Initialize(eventSystem_.get(),
        [this](const std::shared_ptr<RequestContext>& req) {
            // Check if service is stopping
            auto currentState = state_.load(std::memory_order_acquire);
            if (currentState != ServiceState::RUNNING) {
                HandleErrorResponse(req, "Service is stopping");
                return;
            }
            HandleRequestInIOThread(req);
        })) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Failed to initialize request queue");
        eventSystem_.reset();
        curl_multi_cleanup(multiHandle_);
        multiHandle_ = nullptr;
        curl_global_cleanup();
        return false;
    }

    // Create stop notify event
    stopNotifyEventId_ = eventSystem_->CreateNotifyEventId(
        [this](int fd, short events, void* arg) {
            (void)events;
            (void)arg;
            // Clear the eventfd
            uint64_t value;
            while (eventSystem_->ReadEventFd(fd, value)) {
            }
            HandleStopRequestInIOThread();
        }, nullptr, true);
    if (stopNotifyEventId_ == -1) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Failed to create stop notify event");
        requestQueue_.Cleanup();
        eventSystem_.reset();
        curl_multi_cleanup(multiHandle_);
        multiHandle_ = nullptr;
        curl_global_cleanup();
        return false;
    }

    state_.store(ServiceState::RUNNING, std::memory_order_release);

    ioThread_ = std::thread(&HttpClientService::IoThreadMain, this);
    MCP_LOG(MCP_LOG_LEVEL_INFO, "HTTP client service started successfully");
    return true;
}

void HttpClientService::Stop()
{
    auto currentState = state_.load(std::memory_order_acquire);
    if (currentState != ServiceState::RUNNING) {
        return;
    }

    MCP_LOG(MCP_LOG_LEVEL_INFO, "Stopping HTTP client service");

    // Transition to stopping state
    state_.store(ServiceState::STOPPING, std::memory_order_release);

    // Notify I/O thread to handle stop
    if (eventSystem_ != nullptr && stopNotifyEventId_ != -1) {
        eventSystem_->NotifyEventId(stopNotifyEventId_);
    }

    // Wait for I/O thread to finish
    if (ioThread_.joinable()) {
        MCP_LOG(MCP_LOG_LEVEL_INFO, "Waiting for I/O thread to finish...");
        ioThread_.join();
    }

    // Cleanup timer event
    if (timerEventId_ != -1 && eventSystem_ != nullptr) {
        eventSystem_->RemoveEvent(timerEventId_);
        timerEventId_ = -1;
    }

    // Cleanup stop notify event
    if (stopNotifyEventId_ != -1 && eventSystem_ != nullptr) {
        eventSystem_->CloseNotifyEventId(stopNotifyEventId_);
        stopNotifyEventId_ = -1;
    }

    // Cleanup request queue
    requestQueue_.Cleanup();

    if (multiHandle_ != nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Cleaning up libcurl multi handle");
        curl_multi_cleanup(multiHandle_);
        multiHandle_ = nullptr;
    }
    MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Cleaning up libcurl global resources");
    curl_global_cleanup();

    if (eventSystem_ != nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Cleaning up EventSystem");
        eventSystem_.reset();
    }

    MCP_LOG(MCP_LOG_LEVEL_INFO, "HTTP client service stopped");
}

// Core sending interface
void HttpClientService::Send(const HttpRequest& request, void* userData, int timeoutMs, HttpCallback callback)
{
    if (state_.load(std::memory_order_acquire) != ServiceState::RUNNING) {
        throw std::runtime_error("Failed to send HTTP request: Service is not running");
    }

    uint64_t requestId = ((UserData*)userData)->requestId;
    MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Send request " + std::to_string(requestId) + ": " +
            request.method + " " + request.url);

    auto requestContext = std::make_shared<RequestContext>(request, callback, timeoutMs, userData);

    // Submit to queue and return immediately
    if (!requestQueue_.Send(requestContext)) {
        throw std::runtime_error("Request queue is full");
    }

    MCP_LOG(MCP_LOG_LEVEL_INFO, "Send request into queue successfully");
}

void HttpClientService::IoThreadMain()
{
    MCP_LOG(MCP_LOG_LEVEL_INFO, "Starting EventSystem event loop...");
    eventSystem_->Start(false); // Block current thread
    MCP_LOG(MCP_LOG_LEVEL_INFO, "I/O thread event loop ended");
}

void HttpClientService::HandleErrorResponse(const std::shared_ptr<RequestContext>& request,
                                            const std::string& errorMessage)
{
    MCP_LOG(MCP_LOG_LEVEL_ERROR, "Request " + std::to_string(request->userData.requestId) + " error: " + errorMessage);
    HttpResponse errorResponse;
    errorResponse.success = false;
    errorResponse.userData = request->userData;
    errorResponse.statusCode = 0;
    errorResponse.errorMessage = errorMessage;
    ExecuteCallback(request, errorResponse);
}

void HttpClientService::HandleSuccessResponse(const std::shared_ptr<RequestContext>& request, long statusCode,
                                              const std::unordered_map<std::string, std::string>& headers)
{
    HttpResponse response;
    response.success = true;
    response.userData = request->userData;
    response.statusCode = static_cast<int>(statusCode);
    response.body = request->responseData;
    response.headers = headers;

    MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Request " + std::to_string(request->userData.requestId) +
            " completed with status " + std::to_string(statusCode) +
            ", headers_count=" + std::to_string(response.headers.size()));
    ExecuteCallback(request, response);
}

void HttpClientService::HandleRequestInIOThread(const std::shared_ptr<RequestContext>& request)
{
    if (request == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "failed: received null request");
        return;
    }

    uint64_t requestId = request->userData.requestId;
    MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Processing request " + std::to_string(requestId) +
            " in I/O thread: " + request->request.method + " " +
            request->request.url + " (timeout=" + std::to_string(request->timeoutMs) + "ms)");

    // Create CURL handle
    request->easyHandle = curl_easy_init();
    if (request->easyHandle == nullptr) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Failed to create CURL handle for request " + std::to_string(requestId));
        HandleErrorResponse(request, "Failed to create CURL handle");
        return;
    }

    // Setup CURL handle options
    SetupCurlHandle(request->easyHandle, request);

    // Add to multi handle
    CURLMcode code = curl_multi_add_handle(multiHandle_, request->easyHandle);
    if (code != CURLM_OK) {
        // Cleanup resources
        if (request->easyHandle) {
            curl_easy_cleanup(request->easyHandle);
            request->easyHandle = nullptr;
        }

        HandleErrorResponse(request,
                            "Failed to add CURL handle to multi handle: " + std::string(curl_multi_strerror(code)));
        return;
    }

    activeRequests_[requestId] = request;

    MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Request " + std::to_string(requestId) + " added to libcurl multi handle");
}

void HttpClientService::HandleStopRequestInIOThread()
{
    MCP_LOG(MCP_LOG_LEVEL_INFO, "Processing stop request in I/O thread");
    // 1. Process remaining requests in queue
    size_t remaining = requestQueue_.GetQueueSize();
    if (remaining > 0) {
        MCP_LOG(MCP_LOG_LEVEL_INFO, "Processing " + std::to_string(remaining) +
                " remaining requests in queue before stop");
        std::shared_ptr<RequestContext> request;
        int processed = 0;
        // Process up to 1000 requests to prevent infinite loop
        while (processed < MAX_PROCESSED_REQUEST_WHEN_STOP) {
            if (!requestQueue_.TryPop(request)) {
                break; // Queue is empty
            }
            // Reject the request
            HandleErrorResponse(request, "Service is stopping");
            processed++;
        }
        MCP_LOG(MCP_LOG_LEVEL_INFO, "Processed " + std::to_string(processed) +
                " requests from queue during stop");
    }
    // 3. Wait for active requests to complete (max 3 seconds)
    if (!activeRequests_.empty()) {
        MCP_LOG(MCP_LOG_LEVEL_INFO, "Waiting for " + std::to_string(activeRequests_.size()) +
                " active requests to complete");
        auto start = std::chrono::steady_clock::now();
        while (!activeRequests_.empty()) {
            // Trigger socket action to allow requests to complete
            int running = 0;
            curl_multi_socket_action(multiHandle_, CURL_SOCKET_TIMEOUT, 0, &running);
            CheckMultiInfo();
            // Check timeout
            auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            if (elapsed > GRACEFUL_STOP_TIMEOUT) {
                break;
            }
            // Short sleep to avoid busy waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(GRACEFUL_STOP_SLEEP_TIME));
        }
    }
    // 4. Force cancel remaining active requests
    if (!activeRequests_.empty()) {
        MCP_LOG(MCP_LOG_LEVEL_INFO, "Cancelling " + std::to_string(activeRequests_.size()) +
                " remaining active requests");
        CancelAllActiveRequests();
    }
    // 5. Cleanup socket contexts before stopping event loop
    for (auto& [sockfd, context] : socketContexts_) {
        if (context != nullptr) {
            MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Cleaning up socket context: sockfd=" + std::to_string(sockfd));

            // Remove event if still active
            if (context->eventId != -1 && eventSystem_ != nullptr) {
                eventSystem_->RemoveEvent(context->eventId);
                context->eventId = -1; // Clear to prevent double removal
            }
        }
    }
    socketContexts_.clear();
    // 6. Set state to stopped before stopping event loop
    state_.store(ServiceState::STOPPED, std::memory_order_release);
    // 7. Stop event loop
    MCP_LOG(MCP_LOG_LEVEL_INFO, "Stopping event loop");
    eventSystem_->Stop();
}

void HttpClientService::CheckMultiInfo()
{
    CURLMsg* msg = nullptr;
    int pending;

    while ((msg = curl_multi_info_read(multiHandle_, &pending))) {
        if (msg != nullptr && msg->msg == CURLMSG_DONE) {
            CURL* easyHandle = msg->easy_handle;
            std::shared_ptr<RequestContext> request;

            // Find request context using CURLOPT_PRIVATE
            RequestContext* requestPtr = nullptr;
            curl_easy_getinfo(easyHandle, CURLINFO_PRIVATE, &requestPtr);
            if (requestPtr != nullptr) {
                // Find shared_ptr from active requests
                auto it = std::find_if(activeRequests_.begin(), activeRequests_.end(),
                                       [requestPtr](const auto& pair) { return pair.second.get() == requestPtr; });
                if (it != activeRequests_.end()) {
                    request = it->second;
                    activeRequests_.erase(it);
                }
            }

            if (request == nullptr) {
                MCP_LOG(MCP_LOG_LEVEL_WARN, "Received completion for unknown request");
                curl_multi_remove_handle(multiHandle_, easyHandle);
                curl_easy_cleanup(easyHandle);
                continue;
            }

            // Process response based on result
            if (msg->data.result == CURLE_OK) {
                long statusCode;
                curl_easy_getinfo(easyHandle, CURLINFO_RESPONSE_CODE, &statusCode);
                std::unordered_map<std::string, std::string> parsedHeaders = ParseHeaderData(request->headerData);
                HandleSuccessResponse(request, statusCode, parsedHeaders);
            } else {
                HandleErrorResponse(request, curl_easy_strerror(msg->data.result));
            }

            // Cleanup resources
            curl_multi_remove_handle(multiHandle_, easyHandle);
            curl_easy_cleanup(easyHandle);
        }
    }
}

int HttpClientService::SocketCallback(CURL* easy, curl_socket_t sockfd, int action, void* userp, void* socketp)
{
    (void)easy;
    auto* service = static_cast<HttpClientService*>(userp);
    // Allow socket operations in RUNNING and STOPPING states
    // (STOPPING allows active requests to finish)
    auto currentState = service->state_.load(std::memory_order_acquire);
    if (currentState == ServiceState::STOPPED) {
        return -1;
    }

    int events = 0;
    CurlSocketContext* context = static_cast<CurlSocketContext*>(socketp);
    switch (action) {
        case CURL_POLL_IN:
        case CURL_POLL_OUT:
        case CURL_POLL_INOUT:
            // Create or reuse context
            if (context == nullptr) {
                auto contextSharedPtr = service->CreateSocketContext(sockfd);
                context = contextSharedPtr.get();
                curl_multi_assign(service->multiHandle_, sockfd, context);
            }

            // Determine events based on action
            if (action != CURL_POLL_IN) {
                events |= EV_WRITE;
            }
            if (action != CURL_POLL_OUT) {
                events |= EV_READ;
            }
            events |= EV_PERSIST;

            // Update event
            if (service->eventSystem_ != nullptr && context->isValid()) {
                if (context->eventId != -1) {
                    service->eventSystem_->RemoveEvent(context->eventId);
                }
                context->eventId = service->eventSystem_->AddEvent(
                    sockfd, static_cast<EventType>(events),
                    [service](int fd, short eventFlags, void* arg) { service->OnSocketEvent(fd, eventFlags, arg); },
                    context);
            }

            break;

        case CURL_POLL_REMOVE:
            if (context != nullptr && context->isValid()) {
                // Find the shared_ptr from the socketContexts_ map
                auto it = service->socketContexts_.find(sockfd);
                if (it != service->socketContexts_.end()) {
                    service->DestroySocketContext(it->second);
                }
                curl_multi_assign(service->multiHandle_, sockfd, nullptr);
            }
            break;

        default:
            return -1;
    }

    return 0;
}

int HttpClientService::TimerCallback(CURLM* multi, long timeoutMs, void* userp)
{
    (void)multi;
    auto* service = static_cast<HttpClientService*>(userp);
    // Allow timer operations in RUNNING and STOPPING states
    auto currentState = service->state_.load(std::memory_order_acquire);
    if (currentState == ServiceState::STOPPED) {
        return 0;
    }

    // Remove existing timer
    if (service->timerEventId_ != -1) {
        MCP_LOG(MCP_LOG_LEVEL_DEBUG, "remove event id: " + std::to_string(service->timerEventId_));
        if (service->eventSystem_ != nullptr) {
            service->eventSystem_->RemoveEvent(service->timerEventId_);
        }
        service->timerEventId_ = -1;
    }

    if (timeoutMs < 0) {
        // No timer needed
        return 0;
    }

    // Handle special timeout values
    if (timeoutMs == 0) {
        timeoutMs = 1; // 0 means call socket_action asap
    }

    // Set new timer (with safety check)
    if (service->eventSystem_ != nullptr) {
        service->timerEventId_ = service->eventSystem_->AddTimer(
            timeoutMs, [service](int fd, short events, void* arg) { service->OnTimeout(fd, events, arg); }, nullptr,
            false);

        MCP_LOG(MCP_LOG_LEVEL_DEBUG, "add Timer id: " + std::to_string(service->timerEventId_) +
                ", timeout: " + std::to_string(timeoutMs) + " ms");
    }

    return 0;
}

void HttpClientService::OnTimeout(int fd, short events, void* arg)
{
    (void)fd;
    (void)events;
    (void)arg;
    auto currentState = state_.load(std::memory_order_acquire);
    if (currentState == ServiceState::STOPPED) {
        return;
    }

    // Clear timer ID since this timer is now executing
    timerEventId_ = -1;
    // Notify libcurl that timeout occurred
    int running_before = 0;
    CURLMcode code = curl_multi_socket_action(multiHandle_, CURL_SOCKET_TIMEOUT, 0, &running_before);
    if (code != CURLM_OK) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "OnTimeout() - curl_multi_socket_action failed: " +
                std::string(curl_multi_strerror(code)));
    }

    // Check for completed requests
    CheckMultiInfo();
}

void HttpClientService::OnSocketEvent(int fd, short events, void* arg)
{
    auto currentState = state_.load(std::memory_order_acquire);
    if (currentState == ServiceState::STOPPED) {
        return;
    }

    CurlSocketContext* context = static_cast<CurlSocketContext*>(arg);
    if (context == nullptr || !context->isValid()) {
        MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Socket context is null or invalid for fd=" + std::to_string(fd));
        return;
    }

    // Convert libevent flags to libcurl flags
    int curlAction = 0;
    if ((static_cast<uint32_t>(events) & EV_READ) != 0) {
        curlAction |= CURL_CSELECT_IN;
    }
    if ((static_cast<uint32_t>(events) & EV_WRITE) != 0) {
        curlAction |= CURL_CSELECT_OUT;
    }

    MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Socket activity: fd=" + std::to_string(fd) +
            ", sockfd=" + std::to_string(context->sockfd) +
            ", events=0x" + std::to_string(events) + ", curlAction=" + std::to_string(curlAction));

    // Notify libcurl of socket activity
    int running_before = 0;
    CURLMcode code = curl_multi_socket_action(multiHandle_, context->sockfd, curlAction, &running_before);
    if (code != CURLM_OK) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "curl_multi_socket_action failed: " + std::string(curl_multi_strerror(code)));
    }

    // Check for completed requests
    CheckMultiInfo();
}

std::shared_ptr<CurlSocketContext> HttpClientService::CreateSocketContext(curl_socket_t sockfd)
{
    auto context = std::make_shared<CurlSocketContext>(sockfd, this);
    socketContexts_[sockfd] = context;
    return context;
}

void HttpClientService::DestroySocketContext(const std::shared_ptr<CurlSocketContext>& context)
{
    if (context == nullptr || !context->isValid()) {
        return;
    }

    MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Socket destroy: fd=" + std::to_string(context->eventId) +
            ", sockfd=" + std::to_string(context->sockfd));

    // Mark as invalid first to prevent concurrent access
    context->invalidate();

    // Remove event
    if (context->eventId != -1 && eventSystem_ != nullptr) {
        eventSystem_->RemoveEvent(context->eventId);
        context->eventId = -1; // Clear to prevent double removal
    }

    // Remove from map
    socketContexts_.erase(context->sockfd);
}

void HttpClientService::SetupCurlHandle(CURL* easyHandle, const std::shared_ptr<RequestContext>& request)
{
    const HttpRequest& req = request->request;

    // Set basic request options
    curl_easy_setopt(easyHandle, CURLOPT_URL, req.url.c_str());
    curl_easy_setopt(easyHandle, CURLOPT_CUSTOMREQUEST, req.method.c_str());

    // Set request body if present
    if (!req.body.empty()) {
        curl_easy_setopt(easyHandle, CURLOPT_POSTFIELDS, req.body.c_str());
        curl_easy_setopt(easyHandle, CURLOPT_POSTFIELDSIZE, static_cast<long>(req.body.length()));
    }

    // Set response callbacks
    curl_easy_setopt(easyHandle, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(easyHandle, CURLOPT_WRITEDATA, request.get());
    curl_easy_setopt(easyHandle, CURLOPT_HEADERFUNCTION, HeaderCallback);
    curl_easy_setopt(easyHandle, CURLOPT_HEADERDATA, request.get());

    // Set timeouts
    curl_easy_setopt(easyHandle, CURLOPT_TIMEOUT_MS, request->timeoutMs);
    curl_easy_setopt(easyHandle, CURLOPT_CONNECTTIMEOUT_MS, config_.connectionTimeoutMs);

    // Set TLS options
    if (req.url.rfind("https://", 0) == 0) {
        curl_easy_setopt(easyHandle, CURLOPT_SSL_VERIFYPEER, config_.tlsVerifyPeer ? 1L : 0L);
        curl_easy_setopt(easyHandle, CURLOPT_SSL_VERIFYHOST, config_.tlsVerifyHost ? 2L : 0L);
        curl_easy_setopt(easyHandle, CURLOPT_SSLVERSION, config_.tlsMinVersion);

        if (!config_.tlsCipherList.empty()) {
            curl_easy_setopt(easyHandle, CURLOPT_SSL_CIPHER_LIST, config_.tlsCipherList.c_str());
        }

        if (!config_.tlsCaFile.empty()) {
            curl_easy_setopt(easyHandle, CURLOPT_CAINFO, config_.tlsCaFile.c_str());
        }

        if (!config_.tlsCaPath.empty()) {
            curl_easy_setopt(easyHandle, CURLOPT_CAPATH, config_.tlsCaPath.c_str());
        }

        if (!config_.tlsClientCertFile.empty()) {
            curl_easy_setopt(easyHandle, CURLOPT_SSLCERT, config_.tlsClientCertFile.c_str());
        }

        if (!config_.tlsClientKeyFile.empty()) {
            curl_easy_setopt(easyHandle, CURLOPT_SSLKEY, config_.tlsClientKeyFile.c_str());
        }

        if (!config_.tlsClientKeyPassword.empty()) {
            curl_easy_setopt(easyHandle, CURLOPT_KEYPASSWD, config_.tlsClientKeyPassword.c_str());
        }
    }

    // Set request headers
    struct curl_slist* headers = nullptr;
    for (const auto& [name, value] : req.headers) {
        std::string header = name + ": " + value;
        headers = curl_slist_append(headers, header.c_str());
    }

    if (headers != nullptr) {
        curl_easy_setopt(easyHandle, CURLOPT_HTTPHEADER, headers);
        request->headers = headers;
    }

    // Store request context for retrieval in callbacks
    curl_easy_setopt(easyHandle, CURLOPT_PRIVATE, request.get());
}

void HttpClientService::ExecuteCallback(const std::shared_ptr<RequestContext>& request, const HttpResponse& response)
{
    if (request->callback == nullptr) {
        return;
    }

    try {
        request->callback(response);
    } catch (const std::exception& e) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "Callback execution failed: " + std::string(e.what()));
    }
}

// Static libcurl callback functions
size_t HttpClientService::WriteCallback(char* contents, size_t size, size_t nmemb, void* userp)
{
    size_t totalSize = size * nmemb;
    auto* request = static_cast<RequestContext*>(userp);
    if (request != nullptr) {
        request->responseData.append(contents, totalSize);
    }
    return totalSize;
}

size_t HttpClientService::HeaderCallback(char* contents, size_t size, size_t nmemb, void* userp)
{
    size_t totalSize = size * nmemb;
    auto* request = static_cast<RequestContext*>(userp);
    if (request != nullptr) {
        request->headerData.append(contents, totalSize);
    }
    return totalSize;
}

std::unordered_map<std::string, std::string> HttpClientService::ParseHeaderData(const std::string& headerData)
{
    std::unordered_map<std::string, std::string> headers;
    std::istringstream stream(headerData);
    std::string line;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty() || line.find("HTTP/") == 0) {
            continue;
        }

        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos && colon_pos > 0) {
            std::string header_name = line.substr(0, colon_pos);
            std::string header_value = line.substr(colon_pos + 1);

            header_name.erase(0, header_name.find_first_not_of(" \t"));
            header_name.erase(header_name.find_last_not_of(" \t") + 1);
            header_value.erase(0, header_value.find_first_not_of(" \t"));
            header_value.erase(header_value.find_last_not_of(" \t") + 1);

            headers[header_name] = header_value;
            MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Parsed header [" + header_name + ": " + header_value + "]");
        }
    }

    return headers;
}

void HttpClientService::CancelRequest(const std::shared_ptr<RequestContext>& request)
{
    if (request == nullptr) {
        return;
    }

    if (request->easyHandle != nullptr) {
        // Remove from multi handle
        curl_multi_remove_handle(multiHandle_, request->easyHandle);

        HttpResponse response;
        response.success = false;
        response.statusCode = 0;
        response.errorMessage = "Request cancelled during shutdown";
        response.userData = request->userData;
        ExecuteCallback(request, response);

        // Cleanup CURL handle
        curl_easy_cleanup(request->easyHandle);
        request->easyHandle = nullptr;

        MCP_LOG(MCP_LOG_LEVEL_DEBUG, std::string("Request ") +
                std::to_string(request->userData.requestId) + " cancelled successfully");
    }
}

void HttpClientService::CancelAllActiveRequests()
{
    MCP_LOG(MCP_LOG_LEVEL_INFO,
            std::string("Cancelling ") + std::to_string(activeRequests_.size()) + " active requests");

    // Create a copy to avoid iterator invalidation
    std::vector<std::shared_ptr<RequestContext>> requestsToCancel;
    for (const auto& [id, request] : activeRequests_) {
        requestsToCancel.push_back(request);
    }

    // Cancel each request
    for (const auto& request : requestsToCancel) {
        CancelRequest(request);
    }

    // Clear active requests map
    activeRequests_.clear();

    MCP_LOG(MCP_LOG_LEVEL_INFO, "All active requests cancelled");
}

// Factory method implementation
std::unique_ptr<HttpClientService> HttpClientServiceFactory::Create(const HttpClientServiceConfig& config)
{
    return std::make_unique<HttpClientService>(config);
}

} // namespace Http
} // namespace Mcp
