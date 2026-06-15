/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <algorithm>
#include <cstring>
#include <queue>
#include <sstream>
#include <mutex>
#include <nlohmann/json.hpp>

#include "event_system.h"
#include "thread_utils.h"
#include "a2a_log.h"
#include "libcurl_conn.h"

namespace A2A::Http {

constexpr int MAX_PROCESSED_REQUEST_WHEN_STOP = 1000;
constexpr int GRACEFUL_STOP_TIMEOUT = 3000;
constexpr int GRACEFUL_STOP_SLEEP_TIME = 10;
constexpr int DEFAULT_QUEUE_CAPACITY = 1024;
constexpr int DEFAULT_QUEUE_MAX_BATCH_SIZE = 16;

// SSE field prefix lengths
constexpr int MAX_TIMEOUT_MS = 30 * 60 * 1000; // 30 minutes in milliseconds

class CurlGlobal {
public:
    bool Init()
    {
        if (ok_) {
            return true;
        }

        ok_ = (curl_global_init(CURL_GLOBAL_DEFAULT) == 0);
        return ok_;
    }

    ~CurlGlobal()
    {
        if (ok_) {
            curl_global_cleanup();
        }
    }

private:
    bool ok_ = false;
};

static bool EnsureCurlGlobal()
{
    static CurlGlobal instance;
    return instance.Init();
}

LibcurlConn::LibcurlConn(std::string url, std::unordered_map<std::string, std::string> headers,
    int timeout, int sseReadTimeout)
    : url_(std::move(url)), sseReadTimeout_(sseReadTimeout),
    requestQueue_(DEFAULT_QUEUE_CAPACITY, DEFAULT_QUEUE_MAX_BATCH_SIZE)
{
    if (timeout <= 0 || timeout > MAX_TIMEOUT_MS) {
        throw std::invalid_argument("Invalid timeout value");
    }

    if (sseReadTimeout_ <= 0 || sseReadTimeout_ > MAX_TIMEOUT_MS) {
        throw std::invalid_argument("Invalid SSE read timeout value");
    }

    // Create LibcurlConnConfig with timeout settings
    config_.connectionTimeoutMs = timeout;
    config_.requestTimeoutMs = sseReadTimeout;

    // Initialize request headers with Accept and Content-Type, then merge user headers
    requestHeaders_[Http::CONTENT_TYPE_HEADER] = Http::CONTENT_TYPE_JSON;
    for (const auto& [key, value] : headers) {
        requestHeaders_[key] = value;
    }

    // Initialize HTTP client service
    if (!this->Start()) {
        throw std::runtime_error("Failed to start LibcurlConn");
    }
}

void LibcurlConn::SendSessionTerminatedError(const HttpResponse& response)
{
    if (callback_ != nullptr) {
        ConnEventData event;
        event.errCode = Http::HTTP_STATUS_NOT_FOUND;
        event.data = "Status not found";
        callback_->OnMessageReceived(event, response.userData);
    }
}

void LibcurlConn::SendError(const std::string& errorMsg, const int errorCode, const UserData& userData) const
{
    nlohmann::json data = {
        {JSON_FIELD_JSONRPC, JSON_VERSION},
        {JSON_FIELD_ID, userData.requestId},
        {JSON_FIELD_ERROR, {
            {"code", errorCode},
            {"message", errorMsg}
        }}
    };

    ConnEventData event;
    event.errCode = errorCode;
    event.data = data.dump();
    A2A_LOG(A2A_LOG_LEVEL_ERROR, "Error msg: " + errorMsg + ", error code: " + std::to_string(errorCode));
    try {
        callback_->OnMessageReceived(event, userData);
    } catch (const std::exception& e) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, e.what());
    }
}

LibcurlConn::~LibcurlConn()
{
    DoTerminate();

    // Clear session state
    sessionId_.clear();
    protocolVersion_.clear();
    sseConnectionId_ = 0;
    callback_ = nullptr;
}

void LibcurlConn::SetCallback(std::shared_ptr<ConnCallback> callback)
{
    callback_ = std::move(callback);
}

void LibcurlConn::FinishRequest([[maybe_unused]] int timerId)
{
}

void LibcurlConn::RefreshRequest([[maybe_unused]] int timerId, [[maybe_unused]] int timeout)
{
}

int LibcurlConn::SendMessage(const std::string& message, const std::map<std::string, std::string>& headers,
    std::shared_ptr<UserData> userData, [[maybe_unused]] int timeout)
{
    // Prepare HTTP request for LibcurlConn
    HttpRequest httpRequest{};
    if (userData->method == METHOD_AGENT_CARD_GET) {
        httpRequest.method = "GET";
        httpRequest.headers[A2A::Http::ACCEPT_ENCODING_HEADER] = Http::ACCEPT_ENCODING_VALUE;
        httpRequest.headers[A2A::Http::ACCEPT_HEADER] = "*/*";
        httpRequest.headers[A2A::Http::CONNECTION_HEADER] = Http::CONNECTION_KEEP_ALIVE;
    } else {
        httpRequest.method = "POST";
        httpRequest.headers = requestHeaders_;
        if (userData->isStream) {
            httpRequest.headers[Http::ACCEPT_HEADER] = Http::CONTENT_TYPE_SSE;
            httpRequest.headers[Http::CONNECTION_HEADER] = Http::CONNECTION_KEEP_ALIVE;
            httpRequest.headers[Http::CACHE_CONTROL_HEADER] = Http::CACHE_CONTROL_NO_CACHE_NO_TRANSFORM;
        } else {
            httpRequest.headers[Http::ACCEPT_HEADER] = std::string(Http::CONTENT_TYPE_JSON);
        }
    }
    httpRequest.url = url_;
    httpRequest.body = message;
    httpRequest.headers.insert(headers.begin(), headers.end());

    if (!this->IsRunning()) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "LibcurlConn is not running");
        return -1;
    }

    // Bind response callback to HandleResponse
    HttpCallback callback = [this](const HttpResponse& response) {
        try {
            HandleResponse(response);
        } catch (const std::exception& e) {
            SendError(std::string("Handle response failed: ") + e.what(), HTTP_PARSE_ERROR, response.userData);
        }
    };

    return this->Send(httpRequest, *userData, sseReadTimeout_, nullptr, callback);
}

void LibcurlConn::HandleResponse(const HttpResponse& response)
{
    // Check low-level HTTP client success flag first
    if (!response.success) {
        std::string reason = "HTTP request failed";
        if (!response.errorMessage.empty()) {
            reason += ": " + response.errorMessage;
        }
        SendError(reason, HTTP_PARSE_ERROR, response.userData);
        if (callback_ != nullptr) {
            callback_->OnDisconnected(reason);
        }
        return;
    }

    if (response.statusCode == Http::HTTP_STATUS_NOT_FOUND) {
        // 404 Not Found - Session not found or expired
        SendError("Session not found or expired", HTTP_STATUS_NOT_FOUND, response.userData);
        return;
    }

    if (response.statusCode != Http::HTTP_STATUS_OK) {
        // Handle other error responses
        SendError("Http status not ok", response.statusCode, response.userData);
        return;
    }

    // Determine content type
    auto contentTypeIter = FindHeaderCaseInsensitive(response.headers, Http::CONTENT_TYPE_HEADER);
    if (contentTypeIter == response.headers.end()) {
        HandleUnexpectedContentType(response);
        SendError("Content-Type not found in header", HTTP_PARSE_ERROR, response.userData);
        return;
    }

    // Handle based on content type
    const std::string& contentType = contentTypeIter->second;
    if (contentType.find(Http::CONTENT_TYPE_JSON) != std::string::npos) {
        HandleJsonResponse(response);
    } else if (contentType.find(Http::CONTENT_TYPE_SSE) != std::string::npos) {
        HandleSseResponse(response);
    } else {
        HandleUnexpectedContentType(response);
        SendError("application/json or text/event-stream not found in header", HTTP_PARSE_ERROR, response.userData);
    }
}

void LibcurlConn::Terminate()
{
    DoTerminate();
}

void LibcurlConn::DoTerminate()
{
    // Stop HTTP client service if running
    try {
        this->Stop();
    } catch (const std::exception& e) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, std::string("Exception caught in terminate: ") + e.what());
    }
}

void LibcurlConn::HandleJsonResponse(const HttpResponse& response)
{
    // Invoke connection callback
    if (callback_ != nullptr) {
        ConnEventData event;
        if (response.statusCode == Http::HTTP_STATUS_OK) {
            event.errCode = 0;
        } else {
            event.errCode = response.statusCode;
        }
        event.isStreamFin = false;
        event.data = response.body;
        callback_->OnMessageReceived(event, response.userData);
    }
}

void LibcurlConn::HandleSseResponse(const HttpResponse& response)
{
    if (callback_ != nullptr) {
        ConnEventData event;
        if (response.statusCode == Http::HTTP_STATUS_OK) {
            event.errCode = 0;
        } else {
            event.errCode = response.statusCode;
        }
        event.isStreamFin = response.sseEvent.data.empty();
        event.data = response.sseEvent.data;
        callback_->OnMessageReceived(event, response.userData);
    }
}

void LibcurlConn::HandleUnexpectedContentType(const HttpResponse& response) const
{
    std::string contentType = GetHeaderValue(response, Http::CONTENT_TYPE_HEADER);
    if (contentType.empty()) {
        contentType = "<missing>";
    }
    A2A_LOG(A2A_LOG_LEVEL_ERROR, "receive unexpected content type: " + contentType);
}

// Lifecycle management
bool LibcurlConn::Start()
{
    if (state_.load(std::memory_order_acquire) != ConnState::STOPPED) {
        A2A_LOG(A2A_LOG_LEVEL_INFO, "Service already running");
        return true;
    }

    A2A_LOG(A2A_LOG_LEVEL_INFO, "Starting HTTP client service...");
    if (!CurlInit()) {
        return false;
    }

    // Initialize request queue
    auto f = [this](const std::shared_ptr<RequestContext>& req) {
        // Check if service is stopping
        auto currentState = state_.load(std::memory_order_acquire);
        if (currentState != ConnState::RUNNING) {
            HandleErrorResponse(req, "Service is stopping");
            return;
        }
        HandleRequestInIOThread(req);
    };
    if (!requestQueue_.Initialize(eventSystem_.get(), f)) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "Failed to initialize request queue");
        eventSystem_.reset();
        curl_multi_cleanup(multiHandle_);
        multiHandle_ = nullptr;
        return false;
    }

    // Create stop notify event
    stopNotifyEventId_ = eventSystem_->CreateNotifyEventId(
        [this](int fd, [[maybe_unused]] short events, [[maybe_unused]] void* arg) {
            // Clear the eventfd
            uint64_t value;
            while (eventSystem_->ReadEventFd(fd, value)) {
            }
            HandleStopRequestInIOThread();
        }, nullptr, true);
    if (stopNotifyEventId_ == -1) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "Failed to create stop notify event");
        requestQueue_.Cleanup();
        eventSystem_.reset();
        curl_multi_cleanup(multiHandle_);
        multiHandle_ = nullptr;
        return false;
    }

    state_.store(ConnState::RUNNING, std::memory_order_release);

    ioThread_ = std::thread(&LibcurlConn::IoThreadMain, this);
    A2A_LOG(A2A_LOG_LEVEL_INFO, "HTTP client service started successfully");
    return true;
}

void LibcurlConn::Stop()
{
    auto currentState = state_.load(std::memory_order_acquire);
    if (currentState != ConnState::RUNNING) {
        return;
    }

    A2A_LOG(A2A_LOG_LEVEL_INFO, "Stopping HTTP client service");

    // Transition to stopping state
    state_.store(ConnState::STOPPING, std::memory_order_release);

    // Notify I/O thread to handle stop
    if (eventSystem_ != nullptr && stopNotifyEventId_ != -1) {
        eventSystem_->NotifyEventId(stopNotifyEventId_);
    }

    // Wait for I/O thread to finish
    if (ioThread_.joinable()) {
        A2A_LOG(A2A_LOG_LEVEL_INFO, "Waiting for I/O thread to finish...");
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
        A2A_LOG(A2A_LOG_LEVEL_DEBUG, "Cleaning up libcurl multi handle");
        curl_multi_cleanup(multiHandle_);
        multiHandle_ = nullptr;
    }
    A2A_LOG(A2A_LOG_LEVEL_DEBUG, "Cleaning up libcurl global resources");

    if (eventSystem_ != nullptr) {
        A2A_LOG(A2A_LOG_LEVEL_DEBUG, "Cleaning up EventSystem");
        eventSystem_.reset();
    }

    A2A_LOG(A2A_LOG_LEVEL_INFO, "HTTP client service stopped");
}

// Core sending interface
int LibcurlConn::Send(const HttpRequest& request, UserData& userData, int timeoutMs,
    HttpCallback responseHeaderCallback, HttpCallback responseBodyCallback)
{
    if (state_.load(std::memory_order_acquire) != ConnState::RUNNING) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "Failed to send HTTP request: Service is not running");
        return -1;
    }

    if (responseBodyCallback == nullptr) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "Response body callback is required");
        return -1;
    }

    std::string requestId = userData.requestId;
    A2A_LOG(A2A_LOG_LEVEL_DEBUG, "Send request " + requestId + ": " + request.method + " " + request.url);

    auto requestContext = std::make_shared<RequestContext>(request, responseHeaderCallback, responseBodyCallback,
        timeoutMs, userData);

    // Submit to queue and return immediately
    if (!requestQueue_.Send(requestContext)) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "Request queue is full");
        return -1;
    }

    A2A_LOG(A2A_LOG_LEVEL_INFO, "Send request into queue successfully");
    return 0;
}

void LibcurlConn::IoThreadMain()
{
    SetCurrentThreadName("A2A-IO-" + std::to_string(ioThreadIndex_));
    A2A_LOG(A2A_LOG_LEVEL_INFO, "Starting EventSystem event loop...");
    eventSystem_->Start(false); // Block current thread
    A2A_LOG(A2A_LOG_LEVEL_INFO, "I/O thread event loop ended");
}

void LibcurlConn::HandleErrorResponse(const std::shared_ptr<RequestContext>& request,
    const std::string& errorMessage) const
{
    HttpResponse errorResponse;
    errorResponse.success = false;
    errorResponse.userData = request->userData;
    errorResponse.statusCode = 0;
    errorResponse.errorMessage = errorMessage;
    if (request->responseHeaderCallback != nullptr) {
        request->responseHeaderCallback(errorResponse);
    }
    request->responseBodyCallback(errorResponse);
}

void LibcurlConn::HandleFinishedResponse(const std::shared_ptr<RequestContext>& request) const
{
    HttpResponse &response = request->response;
    if (request->responseData.empty()) {
        if (!request->userData.isStream) {
            SendError("Empty non-streaming response", HTTP_PARSE_ERROR, request->userData);
        }
        return;
    }
    response.body = request->responseData;
    if (response.userData.requestId != request->userData.requestId) {
        SendError("RequestId in response data does not match with requestId in request",
            HTTP_PARSE_ERROR, request->userData);
        return;
    }

    A2A_LOG(A2A_LOG_LEVEL_INFO, "Request " + request->userData.requestId + " completed with status: " +
        std::to_string(response.statusCode) + ", headers_count=" + std::to_string(response.headers.size()));
    request->responseBodyCallback(response);
}

void LibcurlConn::HandleRequestInIOThread(const std::shared_ptr<RequestContext>& request)
{
    if (request == nullptr) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "failed: received null request");
        return;
    }

    std::string requestId = request->userData.requestId;
    A2A_LOG(A2A_LOG_LEVEL_INFO, "Processing request " + requestId + " in I/O thread: " +
        request->request.method + " " + request->request.url + " (timeout=" +
        std::to_string(request->timeoutMs) + "ms)");

    // Create CURL handle
    request->easyHandle = curl_easy_init();
    if (request->easyHandle == nullptr) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "Failed to create CURL handle for request " + requestId);
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

    A2A_LOG(A2A_LOG_LEVEL_INFO, "Request " + requestId + " added to libcurl multi handle");
}

void LibcurlConn::HandleStopRequestInIOThread()
{
    A2A_LOG(A2A_LOG_LEVEL_INFO, "Processing stop request in I/O thread");
    // 1. Process remaining requests in queue
    size_t remaining = requestQueue_.GetQueueSize();
    if (remaining > 0) {
        A2A_LOG(A2A_LOG_LEVEL_INFO, "Processing " + std::to_string(remaining) +
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
        A2A_LOG(A2A_LOG_LEVEL_INFO, "Processed " + std::to_string(processed) + " requests from queue during stop");
    }
    // 3. Wait for active requests to complete (max 3 seconds)
    if (!activeRequests_.empty()) {
        A2A_LOG(A2A_LOG_LEVEL_INFO, "Waiting for " + std::to_string(activeRequests_.size()) +
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
        A2A_LOG(A2A_LOG_LEVEL_INFO, "Cancelling " + std::to_string(activeRequests_.size()) +
        " remaining active requests");
        CancelAllActiveRequests();
    }
    // 5. Cleanup socket contexts before stopping event loop
    for (auto& [sockfd, context] : socketContexts_) {
        if (context != nullptr) {
            A2A_LOG(A2A_LOG_LEVEL_INFO, "Cleaning up socket context: sockfd=" + std::to_string(sockfd));

            // Remove event if still active
            if (context->eventId != -1 && eventSystem_ != nullptr) {
                eventSystem_->RemoveEvent(context->eventId);
                context->eventId = -1; // Clear to prevent double removal
            }
        }
    }
    socketContexts_.clear();
    // 6. Set state to stopped before stopping event loop
    state_.store(ConnState::STOPPED, std::memory_order_release);
    // 7. Stop event loop
    A2A_LOG(A2A_LOG_LEVEL_INFO, "Stopping event loop");
    eventSystem_->Stop();
}

void LibcurlConn::CheckMultiInfo()
{
    CURLMsg* msg = nullptr;
    int pending;

    while ((msg = curl_multi_info_read(multiHandle_, &pending)) != nullptr) {
        if (msg->msg != CURLMSG_DONE) {
            continue;
        }

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
            A2A_LOG(A2A_LOG_LEVEL_ERROR, "Received completion for unknown request");
            curl_multi_remove_handle(multiHandle_, easyHandle);
            curl_easy_cleanup(easyHandle);
            continue;
        }

        // Process response based on result
        if (msg->data.result == CURLE_OK) {
            HandleFinishedResponse(request);
        } else {
            HandleErrorResponse(request, curl_easy_strerror(msg->data.result));
        }

        // Cleanup resources
        curl_multi_remove_handle(multiHandle_, easyHandle);
        curl_easy_cleanup(easyHandle);
    }
}

int LibcurlConn::SocketCallback([[maybe_unused]] CURL* easy, curl_socket_t sockfd, int action, void* userp,
    void* socketp)
{
    auto* service = static_cast<LibcurlConn*>(userp);
    // Allow socket operations in RUNNING and STOPPING states
    // (STOPPING allows active requests to finish)
    auto currentState = service->state_.load(std::memory_order_acquire);
    if (currentState == ConnState::STOPPED) {
        return -1;
    }

    uint32_t events = 0;
    CurlSocketContext* context = static_cast<CurlSocketContext*>(socketp);
    switch (action) {
        case CURL_POLL_IN:
        case CURL_POLL_OUT:
        case CURL_POLL_INOUT:
            // Create or reuse context
            if (context == nullptr) {
                auto contextSharedPtr = service->CreateSocketContext(sockfd);
                if (contextSharedPtr == nullptr) {
                    return -1;
                }
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
            if (service->eventSystem_ != nullptr && context->IsValid()) {
                if (context->eventId != -1) {
                    service->eventSystem_->RemoveEvent(context->eventId);
                }
                try {
                    context->eventId = service->eventSystem_->AddEvent(
                        sockfd, static_cast<EventType>(events),
                        [service](int fd, short eventFlags, void* arg) { service->OnSocketEvent(fd, eventFlags, arg); },
                        context);
                }  catch (const std::bad_alloc& e) {
                    A2A_LOG(A2A_LOG_LEVEL_ERROR, std::string("exception occured: ") + e.what());
                    return -1;
                }
            }

            break;

        case CURL_POLL_REMOVE:
            if (context != nullptr && context->IsValid()) {
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

int LibcurlConn::TimerCallback([[maybe_unused]] CURLM* multi, long timeoutMs, void* userp)
{
    auto* service = static_cast<LibcurlConn*>(userp);
    // Allow timer operations in RUNNING and STOPPING states
    auto currentState = service->state_.load(std::memory_order_acquire);
    if (currentState == ConnState::STOPPED) {
        return 0;
    }

    // Remove existing timer
    if (service->timerEventId_ != -1) {
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
    }

    return 0;
}

void LibcurlConn::OnTimeout([[maybe_unused]] int fd, [[maybe_unused]] short events, [[maybe_unused]] void* arg)
{
    auto currentState = state_.load(std::memory_order_acquire);
    if (currentState == ConnState::STOPPED) {
        return;
    }

    // Clear timer ID since this timer is now executing
    timerEventId_ = -1;
    // Notify libcurl that timeout occurred
    int runningBefore = 0;
    CURLMcode code = curl_multi_socket_action(multiHandle_, CURL_SOCKET_TIMEOUT, 0, &runningBefore);
    if (code != CURLM_OK) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "OnTimeout() - curl_multi_socket_action failed: " +
                std::string(curl_multi_strerror(code)));
    }

    // Check for completed requests
    CheckMultiInfo();
}

void LibcurlConn::OnSocketEvent(int fd, short events, void* arg)
{
    auto currentState = state_.load(std::memory_order_acquire);
    if (currentState == ConnState::STOPPED) {
        return;
    }

    CurlSocketContext* context = static_cast<CurlSocketContext*>(arg);
    if (context == nullptr || !context->IsValid()) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "Socket context is null or invalid for fd=" + std::to_string(fd));
        return;
    }

    // Convert libevent flags to libcurl flags
    uint32_t curlAction = 0;
    if ((static_cast<uint32_t>(events) & EV_READ) != 0) {
        curlAction |= CURL_CSELECT_IN;
    }
    if ((static_cast<uint32_t>(events) & EV_WRITE) != 0) {
        curlAction |= CURL_CSELECT_OUT;
    }

    A2A_LOG(A2A_LOG_LEVEL_DEBUG, "Socket activity: fd=" + std::to_string(fd) +
            ", sockfd=" + std::to_string(context->sockfd) +
            ", events=0x" + std::to_string(events) + ", curlAction=" + std::to_string(curlAction));

    // Notify libcurl of socket activity
    int runningBefore = 0;
    CURLMcode code = curl_multi_socket_action(multiHandle_, context->sockfd,
        static_cast<int>(curlAction), &runningBefore);
    if (code != CURLM_OK) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "curl_multi_socket_action failed: " + std::string(curl_multi_strerror(code)));
    }

    // Check for completed requests
    CheckMultiInfo();
}

std::shared_ptr<CurlSocketContext> LibcurlConn::CreateSocketContext(curl_socket_t sockfd)
{
    try {
        auto context = std::make_shared<CurlSocketContext>(sockfd, this);
        socketContexts_[sockfd] = context;
        return context;
    } catch (const std::exception& e) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, std::string("exception occured: ") + e.what());
        return nullptr;
    }
}

void LibcurlConn::DestroySocketContext(const std::shared_ptr<CurlSocketContext>& context)
{
    if (context == nullptr || !context->IsValid()) {
        return;
    }

    A2A_LOG(A2A_LOG_LEVEL_DEBUG, "Socket destroy: fd=" + std::to_string(context->eventId) +
            ", sockfd=" + std::to_string(context->sockfd));

    // Mark as invalid first to prevent concurrent access
    context->Invalidate();

    // Remove event
    if (context->eventId != -1 && eventSystem_ != nullptr) {
        eventSystem_->RemoveEvent(context->eventId);
        context->eventId = -1; // Clear to prevent double removal
    }

    // Remove from map
    socketContexts_.erase(context->sockfd);
}

void LibcurlConn::SetupCurlHandle(CURL* easyHandle, const std::shared_ptr<RequestContext>& request)
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
    request->headers = nullptr;
    for (const auto& [name, value] : req.headers) {
        std::string header = name + ": " + value;
        request->headers = curl_slist_append(request->headers, header.c_str());
    }

    if (request->headers != nullptr) {
        curl_easy_setopt(easyHandle, CURLOPT_HTTPHEADER, request->headers);
    }

    if (request->userData.isStream) {
        curl_easy_setopt(easyHandle, CURLOPT_TCP_KEEPALIVE, HTTP_TCP_KEEP_ALIVE_S);
        curl_easy_setopt(easyHandle, CURLOPT_TCP_KEEPIDLE, HTTP_TCP_KEEP_IDLE_S);
        curl_easy_setopt(easyHandle, CURLOPT_TCP_KEEPINTVL, HTTP_TCP_KEEP_INTVL_S);
    }

    // Store request context for retrieval in callbacks
    curl_easy_setopt(easyHandle, CURLOPT_PRIVATE, request.get());
}

void LibcurlConn::ExecuteCallback(const std::shared_ptr<RequestContext>& request, const HttpResponse& response) const
{
    if (request->responseHeaderCallback != nullptr) {
        request->responseHeaderCallback(response);
    }
    request->responseBodyCallback(response);
}

// Static libcurl callback functions
size_t LibcurlConn::WriteCallback(char* contents, size_t size, size_t nmemb, void* userp)
{
    size_t totalSize = size * nmemb;
    auto* request = static_cast<RequestContext*>(userp);
    if (request == nullptr) {
        return totalSize;
    }

    std::string contentType = GetHeaderValue(request->response, Http::CONTENT_TYPE_HEADER);
    const bool isSse = (contentType.find(CONTENT_TYPE_SSE) != std::string::npos);
    if (!isSse) {
        request->responseData.append(contents, totalSize);
        return totalSize;
    }

    for (size_t i = 0; i < totalSize; ++i) {
        // get a line
        if (contents[i] != '\n') {
            request->responseData.push_back(contents[i]);
            continue;
        }

        // normalize CRLF -> LF
        if (!request->responseData.empty() && request->responseData.back() == '\r') {
            request->responseData.pop_back();
        }

        // parse sse line, and check end of event
        bool isSseEnd = ParseSseLine(request->responseData, request->response.sseEvent);
        request->responseData.clear();

        // end of event, callback
        if (isSseEnd) {
            A2A_LOG(A2A_LOG_LEVEL_DEBUG, "SSE event end,event data:" + request->response.sseEvent.data);
            request->responseBodyCallback(request->response);
            request->response.sseEvent = ServerSentEvent();
            continue;
        }
    }

    return totalSize;
}

size_t LibcurlConn::HeaderCallback(char* contents, size_t size, size_t nmemb, void* userp)
{
    size_t totalSize = size * nmemb;
    auto* request = static_cast<RequestContext*>(userp);
    if (request == nullptr) {
        return totalSize;
    }
    request->headerData.append(contents, totalSize);

    // check header finish
    bool isfinish = totalSize == 2 && contents[0] == '\r' && contents[1] == '\n';
    if (!isfinish) {
        isfinish = totalSize == 1 && contents[0] == '\n';
    }

    if (isfinish) {
        A2A_LOG(A2A_LOG_LEVEL_INFO, "header finished");
        request->response.headers = LibcurlConn::ParseHeaderData(request->headerData);
        request->response.success = true;
        request->response.userData = request->userData;
        long statusCode;
        curl_easy_getinfo(request->easyHandle, CURLINFO_RESPONSE_CODE, &statusCode);
        request->response.statusCode = static_cast<int>(statusCode);

        if (request->responseHeaderCallback != nullptr) {
            request->responseHeaderCallback(request->response);
        }
    }
    return totalSize;
}

std::unordered_map<std::string, std::string> LibcurlConn::ParseHeaderData(const std::string& headerData)
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

        size_t colonPos = line.find(':');
        if (colonPos != std::string::npos && colonPos > 0) {
            std::string headerName = line.substr(0, colonPos);
            std::string headerValue = line.substr(colonPos + 1);

            headerName.erase(0, headerName.find_first_not_of(" \t"));
            headerName.erase(headerName.find_last_not_of(" \t") + 1);
            headerValue.erase(0, headerValue.find_first_not_of(" \t"));
            headerValue.erase(headerValue.find_last_not_of(" \t") + 1);

            headers[headerName] = headerValue;
        }
    }

    return headers;
}

bool LibcurlConn::CurlInit()
{
    // Initialize libcurl
    if (!EnsureCurlGlobal()) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "Failed to initialize CURL");
        return false;
    }

    // Initialize CURL multi handle
    multiHandle_ = curl_multi_init();
    if (multiHandle_ == nullptr) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, "Failed to initialize CURL multi handle");
        return false;
    }

    // Set libcurl callbacks
    curl_multi_setopt(multiHandle_, CURLMOPT_SOCKETFUNCTION, SocketCallback);
    curl_multi_setopt(multiHandle_, CURLMOPT_SOCKETDATA, this);
    curl_multi_setopt(multiHandle_, CURLMOPT_TIMERFUNCTION, TimerCallback);
    curl_multi_setopt(multiHandle_, CURLMOPT_TIMERDATA, this);

    // Initialize EventSystem
    try {
        eventSystem_ = std::make_unique<EventSystem>(true, static_cast<int>(ioThreadIndex_));
        if (!eventSystem_->Init()) {
            A2A_LOG(A2A_LOG_LEVEL_ERROR, "Failed to initialize EventSystem");
            curl_multi_cleanup(multiHandle_);
            multiHandle_ = nullptr;
            return false;
        }
    } catch (const std::exception& e) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, std::string("Failed to create EventSystem: ") + e.what());
        curl_multi_cleanup(multiHandle_);
        multiHandle_ = nullptr;
        return false;
    }

    return true;
}

void LibcurlConn::CancelRequest(const std::shared_ptr<RequestContext>& request) const
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

        A2A_LOG(A2A_LOG_LEVEL_INFO, std::string("Request ") + request->userData.requestId +
                " cancelled successfully");
    }
}

void LibcurlConn::CancelAllActiveRequests()
{
    A2A_LOG(A2A_LOG_LEVEL_INFO,
            std::string("Cancelling ") + std::to_string(activeRequests_.size()) + " active requests");

    // Create a copy to avoid iterator invalidation
    std::vector<std::shared_ptr<RequestContext>> requestsToCancel;
    for (const auto& kv : activeRequests_) {
        requestsToCancel.push_back(kv.second);
    }

    // Cancel each request
    for (const auto& request : requestsToCancel) {
        CancelRequest(request);
    }

    // Clear active requests map
    activeRequests_.clear();

    A2A_LOG(A2A_LOG_LEVEL_INFO, "All active requests cancelled");
}

} // namespace A2A::Http