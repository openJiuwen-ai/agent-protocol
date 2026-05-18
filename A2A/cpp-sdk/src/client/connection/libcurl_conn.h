/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_LIBCURL_CONN
#define A2A_LIBCURL_CONN

#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "event_system.h"
#include "http_common.h"
#include "message_queue/mpsc_notify_queue.h"
#include "client_conn.h"

namespace A2A::Http {

// Service state enumeration
enum class ConnState {
    RUNNING,  // Service is running and accepting requests
    STOPPING, // Service is stopping (no new requests, but finishing active ones)
    STOPPED   // Service has stopped completely
};

// HTTP Client constants (same as original)
constexpr int DEFAULT_CONNECTION_TIMEOUT_MS = 30000; // 30 seconds
constexpr int DEFAULT_REQUEST_TIMEOUT_MS = 60000; // 60 seconds
constexpr int DEFAULT_EVENT_LOOP_TIMEOUT_MS = 100; // 100 milliseconds

// Forward declaration
class LibcurlConn;

/**
 * @brief Configuration for HTTP client service
 */
struct LibcurlConnConfig {
    int connectionTimeoutMs = DEFAULT_CONNECTION_TIMEOUT_MS; // Connection timeout in milliseconds
    int requestTimeoutMs = DEFAULT_REQUEST_TIMEOUT_MS; // Request timeout in milliseconds
    int eventLoopTimeout = DEFAULT_EVENT_LOOP_TIMEOUT_MS; // EventSystem loop timeout

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
 * @brief HTTP request context containing all necessary request information
 */
struct RequestContext {
    UserData userData;
    HttpRequest request;
    HttpResponse response;
    HttpCallback responseHeaderCallback;
    HttpCallback responseBodyCallback;
    CURL* easyHandle = nullptr;
    std::string responseData;
    std::string headerData;
    int timeoutMs;
    std::chrono::steady_clock::time_point startTime;
    struct curl_slist* headers = nullptr; // Store headers for cleanup

    RequestContext(const HttpRequest& req, HttpCallback responseHeaderCallback, HttpCallback responseBodyCallback,
        int timeout, UserData* userData)
        : userData(*userData),
          request(req),
          responseHeaderCallback(responseHeaderCallback),
          responseBodyCallback(responseBodyCallback),
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
    LibcurlConn* service; // Service pointer for callback
    std::atomic<bool> valid{true}; // Validity flag for safe access

    CurlSocketContext(curl_socket_t fd, LibcurlConn* svc) : eventId(-1), sockfd(fd), service(svc), valid(true)
    {
    }

    // Safe access check
    bool IsValid() const
    {
        return valid.load();
    }

    void Invalidate()
    {
        valid.store(false);
    }
};

/**
 * @brief HTTP Client Service using libcurl multi_socket API
 */
class LibcurlConn : public ClientConn {
public:
    explicit LibcurlConn(
        std::string url, std::unordered_map<std::string, std::string> headers = {},
        int timeout = DEFAULT_CONNECTION_TIMEOUT_MS, int sseReadTimeout = DEFAULT_REQUEST_TIMEOUT_MS);

    ~LibcurlConn() override;

    // Send message to server
    void SendMessage(const std::string& message, const std::map<std::string, std::string>& headers,
        UserData* userData) override;

    void Connect() override;

    void Terminate() override;

    void SetCallback(std::shared_ptr<ConnCallback> callback) override;

private:
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
        return state_.load(std::memory_order_acquire) == ConnState::RUNNING;
    }

    /**
     * @brief Asynchronously send HTTP request
     * @param request HTTP request structure with URL, method, headers, and body
     * @param userData User data pointer
     * @param timeoutMs Request timeout in milliseconds
     * @param responseHeaderCallback Response header callback function called on completion
     * @param responseBodyCallback Response body callback function called on completion
     */
    void Send(const HttpRequest& request, UserData* userData, int timeoutMs, HttpCallback responseHeaderCallback,
        HttpCallback responseBodyCallback);

private:
    std::string url_;

    int sseReadTimeout_;
    std::string sessionId_ = "";
    std::string protocolVersion_ = "";
    uint64_t sseConnectionId_ = 0;
    std::unique_ptr<Http::LibcurlConn> libcurlConn_ = nullptr;
    std::shared_ptr<ConnCallback> callback_ = nullptr;
    std::unordered_map<std::string, std::string> requestHeaders_;

    // Core components
    LibcurlConnConfig config_ = {0}; // Service configuration parameters
    CURLM* multiHandle_{nullptr}; // libcurl multi handle for concurrent transfers
    std::atomic<ConnState> state_{ConnState::STOPPED}; // Service state
    std::unique_ptr<EventSystem> eventSystem_; // Event system for async I/O

    // Request queue for async submission from user threads to I/O thread
    MPSCNotifyQueue<std::shared_ptr<RequestContext>> requestQueue_;

    // activeRequests_ is only accessed in I/O thread, no lock needed
    std::unordered_map<std::string, std::shared_ptr<RequestContext>> activeRequests_;
    std::unordered_map<curl_socket_t, std::shared_ptr<CurlSocketContext>> socketContexts_;

    int timerEventId_{-1}; // Timer event ID
    int stopNotifyEventId_{-1}; // Event ID for stopping the service
    std::thread ioThread_; // I/O thread for event processing
    size_t ioThreadIndex_{0};  // Index for naming IO threads

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
     * @brief Handle finished HTTP response
     * @param request Shared pointer to request context
     */
    void HandleFinishedResponse(const std::shared_ptr<RequestContext>& request);

    /**
     * @brief Handle request in I/O thread (called from queue callback)
     * @param request Shared pointer to request context containing all request information
     */
    void HandleRequestInIOThread(const std::shared_ptr<RequestContext>& request);

    /**
     * @brief Handle stop request in I/O thread
     * Processes remaining queue items and cancels active requests before stopping
     */
    void HandleStopRequestInIOThread();

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
     * @brief libcurl socket callback
     * @param easy CURL easy handle associated with this socket
     * @param sockfd Socket file descriptor
     * @param action Action type (CURL_POLL_IN, CURL_POLL_OUT, CURL_POLL_REMOVE)
     * @param userp User data pointer (LibcurlConn instance)
     * @param socketp Socket-specific data pointer (CurlSocketContext)
     * @return 0 on success, -1 on error
     */
    static int SocketCallback(CURL* easy, curl_socket_t sockfd, int action, void* userp, void* socketp);

    /**
     * @brief libcurl timer callback
     * @param multi CURL multi handle that triggered this timer callback
     * @param timeoutMs Timeout in milliseconds (-1 to clear timer, 0 to trigger immediately)
     * @param userp User data pointer (LibcurlConn instance)
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
    static std::unordered_map<std::string, std::string> ParseHeaderData(const std::string& headerData);

    /**
     * @brief init curl related resources
     */
    bool CurlInit();

    void HandleJsonResponse(const HttpResponse& response);
    void HandleSseResponse(const HttpResponse& response);
    void SendSessionTerminatedError(const HttpResponse& response);
    void HandleUnexpectedContentType(const HttpResponse& response);

    // Handle response from server (called internally when response received, triggers session callback)
    void HandleResponse(const HttpResponse& response);
};

} // namespace A2A::Http

#endif // A2A_LIBCURL_CONN
