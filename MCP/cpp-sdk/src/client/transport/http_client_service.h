/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef MCP_HTTP_CLIENT_SERVICE_INCLUDE_H_
#define MCP_HTTP_CLIENT_SERVICE_INCLUDE_H_

#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "event/event_system.h"
#include "shared/http_common.h"

namespace Mcp {
namespace Http {

// HTTP Client constants (same as original)
constexpr int DEFAULT_CONNECTION_TIMEOUT_MS = 30000; // 30 seconds
constexpr int DEFAULT_REQUEST_TIMEOUT_MS = 60000; // 60 seconds
constexpr int DEFAULT_EVENT_LOOP_TIMEOUT_MS = 100; // 100 milliseconds

// Forward declaration
class HttpClientService;

/**
 * @brief Configuration for HTTP client service
 */
struct HttpClientServiceConfig {
    int connectionTimeoutMs = DEFAULT_CONNECTION_TIMEOUT_MS; // Connection timeout in milliseconds
    int requestTimeoutMs = DEFAULT_REQUEST_TIMEOUT_MS; // Request timeout in milliseconds
    std::chrono::milliseconds eventLoopTimeout{DEFAULT_EVENT_LOOP_TIMEOUT_MS}; // EventSystem loop timeout

    // TLS configuration (effective when URL scheme is https)
    bool tlsVerifyPeer = true;
    bool tlsVerifyHost = true;
    long tlsMinVersion = CURL_SSLVERSION_TLSv1_2;
    std::string tlsCipherList = "HIGH:!aNULL:!MD5";
    std::string tlsCaFile;
    std::string tlsCaPath;
    std::string tlsClientCertFile;
    std::string tlsClientKeyFile;
    std::string tlsClientKeyPassword;
};

/**
 * @brief Result of sending request to queue
 */
struct SendResult {
    bool success = false; // Whether successfully added to queue
    uint64_t requestId = 0; // Request ID
    std::string errorMessage; // Reason for failure (e.g., queue full)
};

/**
 * @brief HTTP request context containing all necessary request information
 */
struct RequestContext {
    UserData userData;
    HttpRequest request;
    HttpCallback callback;
    CURL* easyHandle = nullptr;
    std::string responseData;
    std::string headerData;
    int timeoutMs;
    std::chrono::steady_clock::time_point startTime;
    struct curl_slist* headers = nullptr; // Store headers for cleanup

    RequestContext(const HttpRequest& req, HttpCallback cb, int timeout, void* userData)
        : userData(*(UserData*)userData),
          request(req),
          callback(cb),
          timeoutMs(timeout),
          startTime(std::chrono::steady_clock::now()),
          headers(nullptr)
    {
    }

    ~RequestContext()
    {
        // Cleanup headers to prevent memory leak
        if (headers) {
            curl_slist_free_all(headers);
            headers = nullptr;
        }
    }
};

/**
 * @brief Socket context structure
 */
struct CurlSocketContext {
    int eventId; // EventSystem event ID
    curl_socket_t sockfd; // Socket file descriptor
    HttpClientService* service; // Service pointer for callback
    std::atomic<bool> valid{true}; // Validity flag for safe access

    CurlSocketContext(curl_socket_t fd, HttpClientService* svc) : eventId(-1), sockfd(fd), service(svc), valid(true)
    {
    }

    // Safe access check
    bool isValid() const
    {
        return valid.load();
    }

    void invalidate()
    {
        valid.store(false);
    }
};

/**
 * @brief HTTP Client Service using libcurl multi_socket API
 */
class HttpClientService {
public:
    explicit HttpClientService(const HttpClientServiceConfig& config);
    ~HttpClientService();

    /**
     * @brief Start the HTTP client service and initialize all components
     * @return true if service started successfully, false otherwise
     */
    bool Start();

    /**
     * @brief Stop the HTTP client service with graceful shutdown
     */
    void Stop();

    /**
     * @brief Check if the HTTP client service is currently running
     * @return true if service is active and accepting requests
     */
    bool IsRunning() const
    {
        return running_;
    }

    /**
     * @brief Asynchronously send HTTP request
     * @param request HTTP request structure with URL, method, headers, and body
     * @param userData User data pointer
     * @param timeoutMs Request timeout in milliseconds
     * @param callback Response callback function called on completion
     * @return SendResult containing success status, requestId, and error message if applicable
     */
    SendResult Send(const HttpRequest& request, void* userData, int timeoutMs, HttpCallback callback);

private:
    // Core components
    HttpClientServiceConfig config_; // Service configuration parameters
    CURLM* multiHandle_; // libcurl multi handle for concurrent transfers
    std::atomic<bool> running_; // Service running state flag
    std::unique_ptr<EventSystem> eventSystem_; // Event system for async I/O
    std::unordered_map<uint64_t, std::shared_ptr<RequestContext>> activeRequests_; // Active requests tracking
    std::unordered_map<curl_socket_t, std::shared_ptr<CurlSocketContext>> socketContexts_; // Socket contexts

    int timerEventId_{-1}; // Timer event ID
    int stopEventId_{-1}; // Event ID for stopping the event loop
    std::thread ioThread_; // I/O thread for event processing

    // Graceful shutdown support
    static constexpr int GRACEFUL_STOP_TIMEOUT_MS = 3000; // 3 seconds timeout for graceful shutdown
    std::condition_variable stopCondition_; // Condition variable for graceful shutdown
    std::mutex stopMutex_; // Mutex for condition variable

    /**
     * @brief Main I/O thread function for event-driven HTTP processing
     */
    void IoThreadMain();

    /**
     * @brief Handle error response for HTTP requests
     * @param request Shared pointer to request context
     * @param errorMessage Error message to send in response
     */
    void HandleErrorResponse(const std::shared_ptr<RequestContext>& request, const std::string& errorMessage);

    /**
     * @brief Handle successful HTTP response
     * @param request Shared pointer to request context
     * @param statusCode HTTP status code
     * @param headers Response headers from curl_easy_getinfo
     */
    void HandleSuccessResponse(const std::shared_ptr<RequestContext>& request, long statusCode,
                               const std::unordered_map<std::string, std::string>& headers = {});

    /**
     * @brief Process single HTTP request
     * Creates CURL handle, configures with request data, and adds to multi interface
     * @param request Shared pointer to request context containing all request information
     */
    void HandleRequest(const std::shared_ptr<RequestContext>& request);

    /**
     * @brief Check for completed HTTP requests and process results
     */
    void CheckMultiInfo();

    /**
     * @brief Cancel a single request and notify user
     * @param request Shared pointer to request context to cancel
     */
    void CancelRequest(const std::shared_ptr<RequestContext>& request);

    /**
     * @brief Cancel all active requests during shutdown
     */
    void CancelAllActiveRequests();

    /**
     * @brief Process pending events to allow requests to complete
     */
    void ProcessPendingEvents();

    /**
     * @brief Notify when a request is completed (for graceful shutdown)
     * @param requestId ID of the completed request
     */
    void NotifyRequestCompleted(uint64_t requestId);

    /**
     * @brief libcurl socket callback
     * @param easy CURL easy handle associated with this socket
     * @param sockfd Socket file descriptor
     * @param action Action type (CURL_POLL_IN, CURL_POLL_OUT, CURL_POLL_REMOVE)
     * @param userp User data pointer (HttpClientService instance)
     * @param socketp Socket-specific data pointer (CurlSocketContext)
     * @return 0 on success, -1 on error
     */
    static int SocketCallback(CURL* easy, curl_socket_t sockfd, int action, void* userp, void* socketp);

    /**
     * @brief libcurl timer callback
     * @param multi CURL multi handle that triggered this timer callback
     * @param timeoutMs Timeout in milliseconds (-1 to clear timer, 0 to trigger immediately)
     * @param userp User data pointer (HttpClientService instance)
     * @return Always returns 0
     */
    static int TimerCallback(CURLM* multi, long timeoutMs, void* userp);

    /**
     * @brief Handle timeout event
     * @param fd Timer file descriptor (unused for libcurl timeouts)
     * @param events Event flags from EventSystem
     * @param arg User argument (unused)
     */
    void OnTimeout(int fd, short events, void* arg);

    /**
     * @brief Handle socket event
     * @param fd Socket file descriptor
     * @param events Event flags from EventSystem (EV_READ/EV_WRITE)
     * @param arg User argument (CurlSocketContext)
     */
    void OnSocketEvent(int fd, short events, void* arg);

    /**
     * @brief Create socket context
     * @param sockfd Socket file descriptor
     * @return Shared pointer to created socket context
     */
    std::shared_ptr<CurlSocketContext> CreateSocketContext(curl_socket_t sockfd);

    /**
     * @brief Destroy socket context
     * Uses reference counting and delayed cleanup for thread safety
     * @param context Shared pointer to socket context to destroy
     */
    void DestroySocketContext(const std::shared_ptr<CurlSocketContext>& context);

    /**
     * @brief Configure CURL easy handle with HTTP request parameters
     * @param easyHandle CURL easy handle to configure
     * @param request Shared pointer to request context containing request data
     */
    void SetupCurlHandle(CURL* easyHandle, const std::shared_ptr<RequestContext>& request);

    /**
     * @brief Check for completed HTTP requests and process results
     * Reads completion messages from libcurl multi interface and handles them
     */
    void CheckCompletedRequests();

    /**
     * @brief Execute user callback with complete HTTP response
     * @param request Shared pointer to request context
     * @param response Complete HTTP response object with status, body, and headers
     */
    void ExecuteCallback(const std::shared_ptr<RequestContext>& request, const HttpResponse& response);

    /**
     * @brief libcurl callback for receiving HTTP response body data
     * @param contents Pointer to received data
     * @param size Size of each data element
     * @param nmemb Number of elements received
     * @param userp User data pointer (RequestContext responseData string)
     * @return Number of bytes processed (size * nmemb)
     */
    static size_t WriteCallback(char* contents, size_t size, size_t nmemb, void* userp);

    /**
     * @brief libcurl callback for receiving HTTP header data
     * @param contents Pointer to received header data
     * @param size Size of each data element
     * @param nmemb Number of elements received
     * @param userp User data pointer (RequestContext headerData string)
     * @return Number of bytes processed (size * nmemb)
     */
    static size_t HeaderCallback(char* contents, size_t size, size_t nmemb, void* userp);

    /**
     * @brief Parse raw HTTP header data into key-value pairs
     * @param headerData Raw header data string from HTTP response
     * @return Unordered map of header name to header value
     */
    std::unordered_map<std::string, std::string> ParseHeaderData(const std::string& headerData);
};

/**
 * @brief Factory class for creating HttpClientService instances
 */
class HttpClientServiceFactory {
public:
    /**
     * @brief Create HTTP client service instance
     * @param config Service configuration
     * @return Unique pointer to created service
     */
    static std::unique_ptr<HttpClientService> Create(const HttpClientServiceConfig& config);
};

} // namespace Http
} // namespace Mcp

#endif // MCP_HTTP_CLIENT_SERVICE_INCLUDE_H_
