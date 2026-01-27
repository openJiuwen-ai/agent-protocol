/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "http_client_service.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <map>
#include <random>
#include <thread>
#include <vector>

#include "common_type.h"
#include "mcp_log.h"
#include "mock_server.cpp"

using namespace Mcp;
using namespace Mcp::Http;

// ============================================================
// Test Configuration Constants
// ============================================================

// Server configuration
constexpr int MOCK_SERVER_PORT = 9999;
constexpr const char* LOCALHOST_IP = "127.0.0.1";
constexpr const char* MOCK_SERVER_BASE_URL = "http://127.0.0.1:9999";

// HTTP status codes - Note: HTTP_STATUS_OK is defined in http_common.h

// Internal status codes for error tracking
constexpr int STATUS_CODE_ERROR = -1;
constexpr int STATUS_CODE_TIMEOUT = -2;

// Request ID base values for different tests
constexpr uint64_t REQUEST_ID_BASE_BASIC = 1000;
constexpr uint64_t REQUEST_ID_OFFSET_BASIC_FIRST = 1;
constexpr uint64_t REQUEST_ID_BASE_CONCURRENT = 2000;
constexpr uint64_t REQUEST_ID_BASE_METHODS = 3000;
constexpr uint64_t REQUEST_ID_BASE_HEADERS = 4000;
constexpr uint64_t REQUEST_ID_BASE_ERROR = 5000;
constexpr uint64_t REQUEST_ID_BASE_RAPID = 6000;
constexpr uint64_t REQUEST_ID_BASE_GRACEFUL = 7000;
constexpr uint64_t REQUEST_ID_BASE_QUEUE = 8000;

// Time intervals (milliseconds)
constexpr int DELAY_SERVER_STARTUP_MS = 500;
constexpr int DELAY_SERVICE_STARTUP_MS = 1000;
constexpr int DELAY_TEST_ITERATION_MS = 1000;
constexpr int DELAY_POLL_MS = 100;
constexpr int DELAY_RAPID_CYCLE_MS = 200;
constexpr int DELAY_RAPID_SERVICE_MS = 500;
constexpr int DELAY_WAIT_REQUESTS_SENT_MS = 500;
constexpr int DELAY_GRACEFUL_WAIT_MS = 1000;

// Request timeouts (milliseconds)
constexpr int TIMEOUT_WAIT_COMPLETION_DEFAULT_MS = 30000;
constexpr int TIMEOUT_REQUEST_DEFAULT_MS = 10000;
constexpr int TIMEOUT_REQUEST_EXTENDED_MS = 15000;
constexpr int TIMEOUT_REQUEST_SHORT_MS = 5000;
constexpr int TIMEOUT_REQUEST_RAPID_MS = 3000;
constexpr int TIMEOUT_WAIT_CONCURRENT_MS = 45000;
constexpr int TIMEOUT_WAIT_ERROR_MS = 10000;
constexpr int TIMEOUT_WAIT_RAPID_MS = 5000;
constexpr int TIMEOUT_SHUTDOWN_MAX_MS = 8000;

// Request counts for different tests
constexpr int NUM_CONCURRENT_REQUESTS = 10;
constexpr int NUM_GRACEFUL_REQUESTS = 5;
constexpr int NUM_RAPID_ITERATIONS = 3;
constexpr int NUM_QUEUE_FLOOD_REQUESTS = 2000;

// Display formatting
constexpr int BODY_PREVIEW_MAX_LENGTH = 50;
constexpr int SEPARATOR_LINE_LENGTH = 50;
constexpr int TEST_NAME_MAX_WIDTH = 50;

// Helper function to build test URLs
inline std::string buildTestUrl(const std::string& path = "/test")
{
    return std::string(MOCK_SERVER_BASE_URL) + path;
}

// ============================================================
// Test Helper Functions
// ============================================================

struct TestEnvironment {
    MockHttpServer mockServer;
    std::unique_ptr<HttpClientService> service;

    explicit TestEnvironment(int port) : mockServer(port) {}

    bool Setup()
    {
        if (!mockServer.start()) {
            std::cout << "FAIL: Could not start mock server" << std::endl;
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(DELAY_SERVER_STARTUP_MS));

        HttpClientServiceConfig config;
        service = HttpClientServiceFactory::Create(config);
        if (!service || !service->Start()) {
            std::cout << "FAIL: Could not create/start HttpClientService" << std::endl;
            mockServer.stop();
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(DELAY_SERVICE_STARTUP_MS));
        return true;
    }

    void Teardown()
    {
        if (service) {
            service->Stop();
        }
        mockServer.stop();
    }
};

// ============================================================
// Test Results Class
// ============================================================

class TestResults {
public:
    std::atomic<int> successCount_{0};
    std::atomic<int> errorCount_{0};
    std::atomic<int> timeoutCount_{0};
    std::map<int, std::string> responses_;
    std::map<int, long> statusCodes_;
    std::mutex resultsMutex_;

    void RecordSuccess(uint64_t requestId, const HttpResponse& response)
    {
        successCount_++;
        std::lock_guard<std::mutex> lock(resultsMutex_);
        statusCodes_[static_cast<int>(requestId)] = response.statusCode;
        responses_[static_cast<int>(requestId)] = response.body;
    }

    void RecordError(uint64_t requestId)
    {
        errorCount_++;
        std::lock_guard<std::mutex> lock(resultsMutex_);
        statusCodes_[static_cast<int>(requestId)] = STATUS_CODE_ERROR;
    }

    void RecordTimeout(uint64_t requestId)
    {
        timeoutCount_++;
        std::lock_guard<std::mutex> lock(resultsMutex_);
        statusCodes_[static_cast<int>(requestId)] = STATUS_CODE_TIMEOUT;
    }

    void PrintSummary()
    {
        std::cout << "\n=== Test Results Summary ===" << std::endl;
        std::cout << "Success: " << successCount_.load() << std::endl;
        std::cout << "Errors: " << errorCount_.load() << std::endl;
        std::cout << "Timeouts: " << timeoutCount_.load() << std::endl;
        std::cout << "Total: " << (successCount_.load() + errorCount_.load() + timeoutCount_.load()) << std::endl;

        std::lock_guard<std::mutex> lock(resultsMutex_);
        std::cout << "\nStatus Codes:" << std::endl;
        for (const auto& pair : statusCodes_) {
            std::cout << "  Request " << pair.first << ": " << pair.second << std::endl;
        }
    }

    bool WaitForCompletion(int expectedCount, int timeoutMs = TIMEOUT_WAIT_COMPLETION_DEFAULT_MS)
    {
        auto start = std::chrono::steady_clock::now();
        int total = successCount_ + errorCount_ + timeoutCount_;

        while (total < expectedCount) {
            auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            if (elapsed > timeoutMs) {
                std::cout << "Timeout waiting for completion. Got " << total << "/" << expectedCount <<
                    " responses." << std::endl;
                return false;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(DELAY_POLL_MS));
            total = successCount_ + errorCount_ + timeoutCount_;
        }

        return true;
    }
};

// Helper function to create standard callback
HttpCallback CreateStandardCallback(std::shared_ptr<TestResults> results)
{
    return [results](const HttpResponse& response) {
        if (response.success) {
            results->RecordSuccess(response.userData.requestId, response);
        } else {
            results->RecordError(response.userData.requestId);
        }
    };
}

// ============================================================
// Test Functions
// ============================================================

bool TestBasicFunctionality()
{
    std::cout << "\n=== Test 1: Basic Functionality ===" << std::endl;

    auto results = std::make_shared<TestResults>();
    TestEnvironment env(MOCK_SERVER_PORT);

    if (!env.Setup()) {
        return false;
    }

    HttpRequest request;
    request.method = "GET";
    request.url = buildTestUrl();
    request.headers["User-Agent"] = "ComprehensiveTest/1.0";

    UserData userData;
    userData.requestId = REQUEST_ID_BASE_BASIC + REQUEST_ID_OFFSET_BASIC_FIRST;
    userData.method = "CALL_TOOL";

    env.service->Send(request, &userData, TIMEOUT_REQUEST_DEFAULT_MS,
        [results](const HttpResponse& response) { results->RecordSuccess(response.userData.requestId, response); });

    std::cout << "Request sent successfully, requestId=" << userData.requestId << std::endl;

    if (!results->WaitForCompletion(1)) {
        std::cout << "FAIL: Timeout waiting for GET response" << std::endl;
        env.Teardown();
        return false;
    }

    if (results->successCount_.load() != 1 ||
        results->statusCodes_[REQUEST_ID_BASE_BASIC + REQUEST_ID_OFFSET_BASIC_FIRST] != HTTP_STATUS_OK) {
        std::cout << "FAIL: GET request failed" << std::endl;
        env.Teardown();
        return false;
    }

    env.Teardown();
    std::cout << "PASS: Basic functionality test" << std::endl;
    return true;
}

bool TestConcurrentRequests()
{
    std::cout << "\n=== Test 2: Concurrent Requests ===" << std::endl;

    auto results = std::make_shared<TestResults>();
    TestEnvironment env(MOCK_SERVER_PORT);

    if (!env.Setup()) {
        return false;
    }

    std::vector<UserData> userDataArray(NUM_CONCURRENT_REQUESTS);

    for (int i = 0; i < NUM_CONCURRENT_REQUESTS; i++) {
        HttpRequest request;
        request.method = "GET";
        request.url = buildTestUrl();
        request.headers["X-Request-ID"] = std::to_string(i);

        userDataArray[i].requestId = REQUEST_ID_BASE_CONCURRENT + i;
        userDataArray[i].method = "CALL_TOOL";

        env.service->Send(request, &userDataArray[i], TIMEOUT_REQUEST_EXTENDED_MS, CreateStandardCallback(results));
    }

    if (!results->WaitForCompletion(NUM_CONCURRENT_REQUESTS, TIMEOUT_WAIT_CONCURRENT_MS)) {
        std::cout << "FAIL: Timeout waiting for concurrent responses" << std::endl;
        env.Teardown();
        return false;
    }

    if (results->successCount_.load() != NUM_CONCURRENT_REQUESTS) {
        std::cout << "FAIL: Expected " << NUM_CONCURRENT_REQUESTS << " successes, got " <<
            results->successCount_.load() << std::endl;
        env.Teardown();
        return false;
    }

    env.Teardown();
    std::cout << "PASS: Concurrent requests test (" << NUM_CONCURRENT_REQUESTS << " requests)" << std::endl;
    return true;
}

bool TestDifferentMethods()
{
    std::cout << "\n=== Test 3: Different HTTP Methods ===" << std::endl;

    auto results = std::make_shared<TestResults>();
    TestEnvironment env(MOCK_SERVER_PORT);

    if (!env.Setup()) {
        return false;
    }

    std::vector<std::string> methods = {"GET", "POST", "PUT", "DELETE", "PATCH"};
    std::vector<UserData> userDataArray(methods.size());

    for (size_t i = 0; i < methods.size(); i++) {
        HttpRequest request;
        request.method = methods[i];
        request.url = buildTestUrl();

        if (methods[i] == "POST" || methods[i] == "PUT" || methods[i] == "PATCH") {
            request.body = "Test request body for " + methods[i];
            request.headers[Mcp::Http::CONTENT_TYPE_HEADER] = "application/json";
        }

        request.headers["X-Method-Test"] = methods[i];
        userDataArray[i].requestId = REQUEST_ID_BASE_METHODS + static_cast<int>(i);
        userDataArray[i].method = methods[i].c_str();

        env.service->Send(request, &userDataArray[i], TIMEOUT_REQUEST_DEFAULT_MS, CreateStandardCallback(results));
    }

    if (!results->WaitForCompletion(methods.size())) {
        std::cout << "FAIL: Timeout waiting for method test responses" << std::endl;
        env.Teardown();
        return false;
    }

    if (results->successCount_.load() != static_cast<int>(methods.size())) {
        std::cout << "FAIL: Expected " << methods.size() << " successes, got " << results->successCount_.load()
                  << std::endl;
        env.Teardown();
        return false;
    }

    env.Teardown();
    std::cout << "PASS: Different HTTP methods test" << std::endl;
    return true;
}

bool TestHeadersAndBody()
{
    std::cout << "\n=== Test 4: Headers and Body Handling ===" << std::endl;

    auto results = std::make_shared<TestResults>();
    TestEnvironment env(MOCK_SERVER_PORT);

    if (!env.Setup()) {
        return false;
    }

    HttpRequest request;
    request.method = "POST";
    request.url = buildTestUrl();
    request.body = R"({"name": "test", "value": 123})";
    request.headers[Mcp::Http::CONTENT_TYPE_HEADER] = "application/json";
    request.headers["Authorization"] = "Bearer test-token";
    request.headers["X-Custom-Header"] = "custom-value";
    request.headers["User-Agent"] = "HeadersTest/1.0";

    UserData userData;
    userData.requestId = REQUEST_ID_BASE_HEADERS;
    userData.method = "CALL_TOOL";

    auto verboseCallback = [results](const HttpResponse& response) {
        if (response.success) {
            results->RecordSuccess(response.userData.requestId, response);
            std::cout << "Response Headers:" << std::endl;
            for (const auto& header : response.headers) {
                std::cout << "  " << header.first << ": " << header.second << std::endl;
            }
            std::cout << "Response Body: " << response.body << std::endl;
        } else {
            results->RecordError(response.userData.requestId);
        }
    };

    env.service->Send(request, &userData, TIMEOUT_REQUEST_DEFAULT_MS, verboseCallback);

    if (!results->WaitForCompletion(1)) {
        std::cout << "FAIL: Timeout waiting for headers/body response" << std::endl;
        env.Teardown();
        return false;
    }

    if (results->successCount_.load() != 1) {
        std::cout << "FAIL: Headers/body test failed" << std::endl;
        env.Teardown();
        return false;
    }

    env.Teardown();
    std::cout << "PASS: Headers and body handling test" << std::endl;
    return true;
}

bool TestErrorHandling()
{
    std::cout << "\n=== Test 5: Error Handling ===" << std::endl;

    auto results = std::make_shared<TestResults>();
    TestEnvironment env(MOCK_SERVER_PORT);

    if (!env.Setup()) {
        return false;
    }

    HttpRequest request;
    request.method = "GET";
    request.url = "http://" + std::string(LOCALHOST_IP) + ":19999/nonexistent";

    UserData userData;
    userData.requestId = REQUEST_ID_BASE_ERROR;
    userData.method = "ERROR_TEST";

    auto errorCallback = [results](const HttpResponse& response) {
        if (response.success) {
            results->RecordSuccess(response.userData.requestId, response);
        } else {
            results->RecordError(response.userData.requestId);
            std::cout << "Error response: " << response.errorMessage << std::endl;
        }
    };

    env.service->Send(request, &userData, TIMEOUT_REQUEST_SHORT_MS, errorCallback);

    if (!results->WaitForCompletion(1, TIMEOUT_WAIT_ERROR_MS)) {
        std::cout << "FAIL: Timeout waiting for error response" << std::endl;
        env.Teardown();
        return false;
    }

    env.Teardown();
    std::cout << "PASS: Error handling test" << std::endl;
    return true;
}

// Graceful shutdown test helper structures
struct GracefulShutdownContext {
    std::shared_ptr<TestResults> results;
    std::atomic<bool>& shutdownStarted;
    std::atomic<int>& requestsInProgress;
    std::atomic<int>& requestsSent;
    HttpClientService* service;
    std::vector<UserData>& userDataArray;

    void ProcessResponse(const HttpResponse& response)
    {
        requestsInProgress--;
        std::string shutdownStatus = shutdownStarted.load() ? "AFTER" : "BEFORE";
        std::cout << "Request " << response.userData.requestId << " completed " << shutdownStatus << " shutdown - ";

        if (response.success) {
            results->RecordSuccess(response.userData.requestId, response);
            std::cout << "SUCCESS (body: " << response.body.substr(0, BODY_PREVIEW_MAX_LENGTH) << "...)" << std::endl;
        } else {
            results->RecordError(response.userData.requestId);
            std::cout << "ERROR: " << response.errorMessage << std::endl;
        }
    }

    bool SendSlowRequest(int index)
    {
        HttpRequest request;
        request.method = "GET";
        request.url = buildTestUrl();
        request.headers["X-Request-ID"] = std::to_string(index);
        request.headers["X-Graceful-Test"] = "true";
        request.headers["X-Slow-Response"] = "true";

        userDataArray[index].requestId = REQUEST_ID_BASE_GRACEFUL + index;
        userDataArray[index].method = "GRACEFUL_SHUTDOWN_TEST";

        std::cout << "Sending slow request " << (REQUEST_ID_BASE_GRACEFUL + index) <<
            " (will take ~3s to complete)..." << std::endl;

        auto callback = [this](const HttpResponse& response) { ProcessResponse(response); };

        service->Send(request, &userDataArray[index], TIMEOUT_REQUEST_DEFAULT_MS, callback);

        requestsInProgress++;
        requestsSent++;
        std::cout << "Request " << (REQUEST_ID_BASE_GRACEFUL + index) << " sent successfully" << std::endl;
        return true;
    }
};

bool SendGracefulShutdownRequests(TestEnvironment& env, std::vector<UserData>& userDataArray,
    std::atomic<bool>& shutdownStarted, std::atomic<int>& requestsInProgress, std::atomic<int>& requestsSent,
    std::shared_ptr<TestResults> results)
{
    std::vector<std::future<void>> requestFutures;

    for (int i = 0; i < NUM_GRACEFUL_REQUESTS; i++) {
        requestFutures.emplace_back(std::async(std::launch::async, [&, i]() {
            GracefulShutdownContext ctx{results, shutdownStarted, requestsInProgress, requestsSent, env.service.get(),
                userDataArray};
            ctx.SendSlowRequest(i);
        }));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(DELAY_WAIT_REQUESTS_SENT_MS));

    std::cout << "\nStep 2: Requests sent: " << requestsSent.load() <<
        ", Requests in progress: " << requestsInProgress.load() << std::endl;
    std::cout << "Step 3: Waiting 1 second for requests to start processing..." << std::endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(DELAY_GRACEFUL_WAIT_MS));

    return true;
}

bool VerifyGracefulShutdown(TestEnvironment& env, std::shared_ptr<TestResults> results, long long shutdownDuration)
{
    (void) env;
    std::cout << "\n=== Graceful Shutdown Results ===" << std::endl;
    std::cout << "Shutdown completed in " << shutdownDuration << "ms" << std::endl;
    std::cout << "Results during shutdown: " << results->successCount_.load() << " success, " <<
        results->errorCount_.load() << " errors" << std::endl;

    int totalProcessed = results->successCount_.load() + results->errorCount_.load();
    std::cout << "Total requests processed: " << totalProcessed << "/" << NUM_GRACEFUL_REQUESTS << std::endl;

    if (shutdownDuration > TIMEOUT_SHUTDOWN_MAX_MS) {
        std::cout << "FAIL: Shutdown took too long: " << shutdownDuration << "ms (> " <<
            TIMEOUT_SHUTDOWN_MAX_MS << "ms)" << std::endl;
        return false;
    }

    if (totalProcessed == 0) {
        std::cout << "FAIL: No requests were processed during graceful shutdown" << std::endl;
        return false;
    }

    if (results->errorCount_.load() > 0) {
        std::cout << "✓ PASS: Some requests were properly cancelled during shutdown (" <<
            results->errorCount_.load() << ")" << std::endl;
    } else {
        std::cout << "INFO: All requests completed successfully before shutdown finished" << std::endl;
    }

    if (results->successCount_.load() > 0) {
        std::cout << "✓ PASS: Some requests completed successfully during shutdown (" <<
            results->successCount_.load() << ")" << std::endl;
    }

    std::cout << "Shutdown behavior: " << results->successCount_.load() << " completed, " <<
        results->errorCount_.load() << " cancelled" << std::endl;

    std::cout << "PASS: Graceful shutdown test - service stopped without hanging" << std::endl;
    return true;
}

bool TestGracefulShutdown()
{
    std::cout << "\n=== Test 6: Graceful Shutdown ===" << std::endl;

    auto results = std::make_shared<TestResults>();
    std::atomic<bool> shutdownStarted{false};
    std::atomic<int> requestsInProgress{0};
    std::atomic<int> requestsSent{0};

    TestEnvironment env(MOCK_SERVER_PORT);
    if (!env.Setup()) {
        return false;
    }

    std::vector<UserData> userDataArray(NUM_GRACEFUL_REQUESTS);
    std::vector<std::future<void>> requestFutures;

    std::cout << "=== CORRECTED GRACEFUL SHUTDOWN TEST ===" << std::endl;
    std::cout << "Step 1: Sending " << NUM_GRACEFUL_REQUESTS << " slow requests (3s server delay)..." << std::endl;

    // Send slow requests
    for (int i = 0; i < NUM_GRACEFUL_REQUESTS; i++) {
        requestFutures.emplace_back(std::async(std::launch::async, [&, i]() {
            GracefulShutdownContext ctx{results, shutdownStarted, requestsInProgress, requestsSent, env.service.get(),
                userDataArray};
            ctx.SendSlowRequest(i);
        }));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(DELAY_WAIT_REQUESTS_SENT_MS));

    std::cout << "\nStep 2: Requests sent: " << requestsSent.load() <<
        ", Requests in progress: " << requestsInProgress.load() << std::endl;
    std::cout << "Step 3: Waiting 1 second for requests to start processing..." << std::endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(DELAY_GRACEFUL_WAIT_MS));

    std::cout << "Step 4: Initiating graceful shutdown while requests are actively processing..." << std::endl;

    shutdownStarted = true;

    auto shutdownStart = std::chrono::steady_clock::now();
    env.service->Stop();
    auto shutdownEnd = std::chrono::steady_clock::now();
    auto shutdownDuration = std::chrono::duration_cast<std::chrono::milliseconds>(shutdownEnd - shutdownStart).count();

    for (auto& future : requestFutures) {
        if (future.valid()) {
            future.wait();
        }
    }

    env.mockServer.stop();

    return VerifyGracefulShutdown(env, results, shutdownDuration);
}

bool TestRapidStartStop()
{
    std::cout << "\n=== Test 7: Rapid Start/Stop ===" << std::endl;

    for (int i = 0; i < NUM_RAPID_ITERATIONS; i++) {
        std::cout << "Iteration " << (i + 1) << "/" << NUM_RAPID_ITERATIONS << "..." << std::endl;

        TestEnvironment env(MOCK_SERVER_PORT);
        if (!env.Setup()) {
            return false;
        }

        HttpRequest request;
        request.method = "GET";
        request.url = buildTestUrl();

        auto results = std::make_shared<TestResults>();
        UserData userData;
        userData.requestId = REQUEST_ID_BASE_RAPID + i;
        userData.method = "RAPID_TEST";

        env.service->Send(request, &userData, TIMEOUT_REQUEST_RAPID_MS, CreateStandardCallback(results));

        results->WaitForCompletion(1, TIMEOUT_WAIT_RAPID_MS);

        env.Teardown();
        std::this_thread::sleep_for(std::chrono::milliseconds(DELAY_RAPID_CYCLE_MS));
    }

    std::cout << "PASS: Rapid start/stop test" << std::endl;
    return true;
}

bool TestQueueFullScenario()
{
    std::cout << "\n=== Test 8: Queue Full Scenario ===" << std::endl;

    TestEnvironment env(MOCK_SERVER_PORT);
    if (!env.Setup()) {
        return false;
    }

    std::cout << "Attempting to flood the request queue..." << std::endl;

    std::atomic<int> requestsSent{0};
    std::atomic<int> queueFullCount{0};
    std::vector<UserData> userDataArray(NUM_QUEUE_FLOOD_REQUESTS);

    // Try to send a large number of requests to fill the queue
    for (int i = 0; i < NUM_QUEUE_FLOOD_REQUESTS; i++) {
        try {
            HttpRequest request;
            request.method = "GET";
            request.url = buildTestUrl();

            userDataArray[i].requestId = REQUEST_ID_BASE_QUEUE + i;
            userDataArray[i].method = "QUEUE_TEST";

            auto emptyCallback = [](const HttpResponse& response) { (void)response; };

            env.service->Send(request, &userDataArray[i], TIMEOUT_REQUEST_DEFAULT_MS, emptyCallback);
            requestsSent++;
        } catch (const std::runtime_error& e) {
            // Expected: queue will eventually be full
            if (std::string(e.what()) == "Request queue is full") {
                queueFullCount++;
                std::cout << "Queue full at request " << i << " (expected behavior)" << std::endl;
                break; // Stop trying once queue is full
            } else {
                std::cout << "Unexpected error: " << e.what() << std::endl;
                env.Teardown();
                return false;
            }
        }
    }

    std::cout << "Queue flood test: sent " << requestsSent.load() << " requests before queue was full" << std::endl;
    std::cout << "Queue full occurred " << queueFullCount.load() << " time(s)" << std::endl;

    env.Teardown();

    // The test passes if we successfully filled the queue
    if (requestsSent.load() > 0 && queueFullCount.load() > 0) {
        std::cout << "PASS: Queue correctly handles overflow scenario" << std::endl;
        return true;
    } else {
        std::cout << "FAIL: Queue did not behave as expected" << std::endl;
        return false;
    }
}

// ============================================================
// Main Test Runner
// ============================================================

int main()
{
    std::cout << "HttpClientService Test Suite" << std::endl;
    std::cout << "=========================================" << std::endl;

    SetLogLevel(MCP_LOG_LEVEL_INFO);

    std::vector<std::pair<std::string, std::function<bool()>>> tests = {
        {"Basic Functionality", TestBasicFunctionality},
        {"Concurrent Requests", TestConcurrentRequests},
        {"Different HTTP Methods", TestDifferentMethods},
        {"Headers and Body", TestHeadersAndBody},
        {"Error Handling", TestErrorHandling},
        {"Graceful Shutdown", TestGracefulShutdown},
        {"Rapid Start/Stop", TestRapidStartStop},
        {"Queue Full Scenario", TestQueueFullScenario}};

    int passed = 0;
    int total = tests.size();

    for (const auto& test : tests) {
        std::cout << "\n" << std::string(SEPARATOR_LINE_LENGTH, '=') << std::endl;
        std::cout << "Running: " << test.first << std::endl;

        try {
            if (test.second()) {
                passed++;
                std::cout << "✓ " << test.first << " PASSED" << std::endl;
            } else {
                std::cout << "✗ " << test.first << " FAILED" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cout << "✗ " << test.first << " EXCEPTION: " << e.what() << std::endl;
        } catch (...) {
            std::cout << "✗ " << test.first << " UNKNOWN EXCEPTION" << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(DELAY_TEST_ITERATION_MS));
    }

    std::cout << "\n" << std::string(SEPARATOR_LINE_LENGTH, '=') << std::endl;
    std::cout << "Test Suite Results:" << std::endl;
    std::cout << "Passed: " << passed << "/" << total << " tests" << std::endl;

    if (passed == total) {
        std::cout << "\n🎉 ALL TESTS PASSED! 🎉" << std::endl;
        return 0;
    } else {
        std::cout << "\n❌ " << (total - passed) << " test(s) failed" << std::endl;
        return 1;
    }
}
