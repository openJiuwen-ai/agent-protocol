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

class TestResults {
public:
    std::atomic<int> successCount{0};
    std::atomic<int> errorCount{0};
    std::atomic<int> timeoutCount{0};
    std::map<int, std::string> responses;
    std::map<int, long> statusCodes;
    std::mutex resultsMutex;

    void recordSuccess(uint64_t requestId, const HttpResponse& response)
    {
        successCount++;
        std::lock_guard<std::mutex> lock(resultsMutex);
        statusCodes[static_cast<int>(requestId)] = response.statusCode;
        responses[static_cast<int>(requestId)] = response.body;
    }

    void recordError(uint64_t requestId)
    {
        errorCount++;
        std::lock_guard<std::mutex> lock(resultsMutex);
        statusCodes[static_cast<int>(requestId)] = -1;
    }

    void recordTimeout(uint64_t requestId)
    {
        timeoutCount++;
        std::lock_guard<std::mutex> lock(resultsMutex);
        statusCodes[static_cast<int>(requestId)] = -2;
    }

    void printSummary()
    {
        std::cout << "\n=== Test Results Summary ===" << std::endl;
        std::cout << "Success: " << successCount.load() << std::endl;
        std::cout << "Errors: " << errorCount.load() << std::endl;
        std::cout << "Timeouts: " << timeoutCount.load() << std::endl;
        std::cout << "Total: " << (successCount.load() + errorCount.load() + timeoutCount.load()) << std::endl;

        std::lock_guard<std::mutex> lock(resultsMutex);
        std::cout << "\nStatus Codes:" << std::endl;
        for (const auto& pair : statusCodes) {
            std::cout << "  Request " << pair.first << ": " << pair.second << std::endl;
        }
    }

    bool waitForCompletion(int expectedCount, int timeoutMs = 30000)
    {
        auto start = std::chrono::steady_clock::now();
        int total = successCount + errorCount + timeoutCount;

        while (total < expectedCount) {
            auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

            if (elapsed > timeoutMs) {
                std::cout << "Timeout waiting for completion. Got " << total << "/" << expectedCount << " responses."
                          << std::endl;
                return false;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            total = successCount + errorCount + timeoutCount;
        }

        return true;
    }
};

bool testBasicFunctionality()
{
    std::cout << "\n=== Test 1: Basic Functionality ===" << std::endl;

    TestResults results;

    // Start mock server
    MockHttpServer mockServer(9999);
    if (!mockServer.start()) {
        std::cout << "FAIL: Could not start mock server" << std::endl;
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Create and start HttpClientService
    HttpClientServiceConfig config;
    auto service = HttpClientServiceFactory::Create(config);
    if (!service || !service->Start()) {
        std::cout << "FAIL: Could not create/start HttpClientService" << std::endl;
        mockServer.stop();
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // Test GET request
    HttpRequest request;
    request.method = "GET";
    request.url = "http://127.0.0.1:9999/test";
    request.headers["User-Agent"] = "ComprehensiveTest/1.0";

    UserData userData;
    userData.requestId = 1001;
    userData.method = "CALL_TOOL";

    SendResult sendResult = service->Send(request, &userData, 10000, [&results](const HttpResponse& response) {
        results.recordSuccess(response.userData.requestId, response);
    });

    if (!sendResult.success) {
        std::cout << "FAIL: Could not send GET request" << std::endl;
        service->Stop();
        mockServer.stop();
        return false;
    }

    // Wait for response
    if (!results.waitForCompletion(1)) {
        std::cout << "FAIL: Timeout waiting for GET response" << std::endl;
        service->Stop();
        mockServer.stop();
        return false;
    }

    if (results.successCount.load() != 1 || results.statusCodes[1001] != 200) {
        std::cout << "FAIL: GET request failed" << std::endl;
        service->Stop();
        mockServer.stop();
        return false;
    }

    service->Stop();
    mockServer.stop();

    std::cout << "PASS: Basic functionality test" << std::endl;
    return true;
}

bool testConcurrentRequests()
{
    std::cout << "\n=== Test 2: Concurrent Requests ===" << std::endl;

    TestResults results;

    // Start mock server
    MockHttpServer mockServer(9999);
    if (!mockServer.start()) {
        std::cout << "FAIL: Could not start mock server" << std::endl;
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Create and start HttpClientService
    HttpClientServiceConfig config;
    auto service = HttpClientServiceFactory::Create(config);
    if (!service || !service->Start()) {
        std::cout << "FAIL: Could not create/start HttpClientService" << std::endl;
        mockServer.stop();
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    const int NUM_REQUESTS = 10;

    // Send multiple concurrent requests
    std::vector<UserData> userDataArray(NUM_REQUESTS);
    for (int i = 0; i < NUM_REQUESTS; i++) {
        HttpRequest request;
        request.method = "GET";
        request.url = "http://127.0.0.1:9999/test";
        request.headers["X-Request-ID"] = std::to_string(i);

        userDataArray[i].requestId = 2000 + i;
        userDataArray[i].method = "CALL_TOOL";

        SendResult sendResult =
            service->Send(request, &userDataArray[i], 15000, [&results](const HttpResponse& response) {
                if (response.success) {
                    results.recordSuccess(response.userData.requestId, response);
                } else {
                    results.recordError(response.userData.requestId);
                }
            });

        if (!sendResult.success) {
            std::cout << "FAIL: Could not send concurrent request " << i << std::endl;
            service->Stop();
            mockServer.stop();
            return false;
        }
    }

    // Wait for all responses
    if (!results.waitForCompletion(NUM_REQUESTS, 45000)) {
        std::cout << "FAIL: Timeout waiting for concurrent responses" << std::endl;
        service->Stop();
        mockServer.stop();
        return false;
    }

    if (results.successCount.load() != NUM_REQUESTS) {
        std::cout << "FAIL: Expected " << NUM_REQUESTS << " successes, got " << results.successCount.load()
                  << std::endl;
        service->Stop();
        mockServer.stop();
        return false;
    }

    service->Stop();
    mockServer.stop();

    std::cout << "PASS: Concurrent requests test (" << NUM_REQUESTS << " requests)" << std::endl;
    return true;
}

bool testDifferentMethods()
{
    std::cout << "\n=== Test 3: Different HTTP Methods ===" << std::endl;

    TestResults results;

    // Start mock server
    MockHttpServer mockServer(9999);
    if (!mockServer.start()) {
        std::cout << "FAIL: Could not start mock server" << std::endl;
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Create and start HttpClientService
    HttpClientServiceConfig config;
    auto service = HttpClientServiceFactory::Create(config);
    if (!service || !service->Start()) {
        std::cout << "FAIL: Could not create/start HttpClientService" << std::endl;
        mockServer.stop();
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // Test different HTTP methods
    std::vector<std::string> methods = {"GET", "POST", "PUT", "DELETE", "PATCH"};
    std::vector<UserData> userDataArray(methods.size());

    for (size_t i = 0; i < methods.size(); i++) {
        HttpRequest request;
        request.method = methods[i];
        request.url = "http://127.0.0.1:9999/test";

        if (methods[i] == "POST" || methods[i] == "PUT" || methods[i] == "PATCH") {
            request.body = "Test request body for " + methods[i];
            request.headers[Mcp::Http::CONTENT_TYPE_HEADER] = "application/json";
        }

        request.headers["X-Method-Test"] = methods[i];

        userDataArray[i].requestId = 3000 + static_cast<int>(i);
        userDataArray[i].method = methods[i].c_str();

        SendResult sendResult =
            service->Send(request, &userDataArray[i], 10000, [&results](const HttpResponse& response) {
                if (response.success) {
                    results.recordSuccess(response.userData.requestId, response);
                } else {
                    results.recordError(response.userData.requestId);
                }
            });

        if (!sendResult.success) {
            std::cout << "FAIL: Could not send " << methods[i] << " request" << std::endl;
            service->Stop();
            mockServer.stop();
            return false;
        }
    }

    // Wait for all responses
    if (!results.waitForCompletion(methods.size())) {
        std::cout << "FAIL: Timeout waiting for method test responses" << std::endl;
        service->Stop();
        mockServer.stop();
        return false;
    }

    if (results.successCount.load() != static_cast<int>(methods.size())) {
        std::cout << "FAIL: Expected " << methods.size() << " successes, got " << results.successCount.load()
                  << std::endl;
        service->Stop();
        mockServer.stop();
        return false;
    }

    service->Stop();
    mockServer.stop();

    std::cout << "PASS: Different HTTP methods test" << std::endl;
    return true;
}

bool testHeadersAndBody()
{
    std::cout << "\n=== Test 4: Headers and Body Handling ===" << std::endl;

    TestResults results;

    // Start mock server
    MockHttpServer mockServer(9999);
    if (!mockServer.start()) {
        std::cout << "FAIL: Could not start mock server" << std::endl;
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Create and start HttpClientService
    HttpClientServiceConfig config;
    auto service = HttpClientServiceFactory::Create(config);
    if (!service || !service->Start()) {
        std::cout << "FAIL: Could not create/start HttpClientService" << std::endl;
        mockServer.stop();
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // Test request with custom headers and body
    HttpRequest request;
    request.method = "POST";
    request.url = "http://127.0.0.1:9999/test";
    request.body = R"({"name": "test", "value": 123})";
    request.headers[Mcp::Http::CONTENT_TYPE_HEADER] = "application/json";
    request.headers["Authorization"] = "Bearer test-token";
    request.headers["X-Custom-Header"] = "custom-value";
    request.headers["User-Agent"] = "HeadersTest/1.0";

    UserData userData;
    userData.requestId = 4000;
    userData.method = "CALL_TOOL";

    SendResult sendResult = service->Send(request, &userData, 10000, [&results](const HttpResponse& response) {
        if (response.success) {
            results.recordSuccess(response.userData.requestId, response);

            // Verify response headers and body
            std::cout << "Response Headers:" << std::endl;
            for (const auto& header : response.headers) {
                std::cout << "  " << header.first << ": " << header.second << std::endl;
            }
            std::cout << "Response Body: " << response.body << std::endl;
        } else {
            results.recordError(response.userData.requestId);
        }
    });

    if (!sendResult.success) {
        std::cout << "FAIL: Could not send headers/body test request" << std::endl;
        service->Stop();
        mockServer.stop();
        return false;
    }

    // Wait for response
    if (!results.waitForCompletion(1)) {
        std::cout << "FAIL: Timeout waiting for headers/body response" << std::endl;
        service->Stop();
        mockServer.stop();
        return false;
    }

    if (results.successCount.load() != 1) {
        std::cout << "FAIL: Headers/body test failed" << std::endl;
        service->Stop();
        mockServer.stop();
        return false;
    }

    service->Stop();
    mockServer.stop();

    std::cout << "PASS: Headers and body handling test" << std::endl;
    return true;
}

bool testErrorHandling()
{
    std::cout << "\n=== Test 5: Error Handling ===" << std::endl;

    TestResults results;

    // Create and start HttpClientService (no mock server)
    HttpClientServiceConfig config;
    auto service = HttpClientServiceFactory::Create(config);
    if (!service || !service->Start()) {
        std::cout << "FAIL: Could not create/start HttpClientService" << std::endl;
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // Test request to non-existent server
    HttpRequest request;
    request.method = "GET";
    request.url = "http://127.0.0.1:19999/nonexistent"; // Port that shouldn't be listening

    UserData userData;
    userData.requestId = 5000;
    userData.method = "ERROR_TEST";

    SendResult sendResult = service->Send(request, &userData, 5000, [&results](const HttpResponse& response) {
        if (response.success) {
            results.recordSuccess(response.userData.requestId, response);
        } else {
            results.recordError(response.userData.requestId);
            std::cout << "Error response: " << response.errorMessage << std::endl;
        }
    });

    if (!sendResult.success) {
        std::cout << "FAIL: Could not send error test request" << std::endl;
        service->Stop();
        return false;
    }

    // Wait for response (should be an error)
    if (!results.waitForCompletion(1, 10000)) {
        std::cout << "FAIL: Timeout waiting for error response" << std::endl;
        service->Stop();
        return false;
    }

    service->Stop();

    std::cout << "PASS: Error handling test" << std::endl;
    return true;
}

bool testGracefulShutdown()
{
    std::cout << "\n=== Test 6: Graceful Shutdown ===" << std::endl;

    TestResults results;
    std::atomic<bool> shutdownStarted{false};
    std::atomic<int> requestsInProgress{0};
    std::atomic<int> requestsSent{0};

    // Start mock server with slow response capability
    MockHttpServer mockServer(9999);
    if (!mockServer.start()) {
        std::cout << "FAIL: Could not start mock server" << std::endl;
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Create and start HttpClientService
    HttpClientServiceConfig config;
    auto service = HttpClientServiceFactory::Create(config);
    if (!service || !service->Start()) {
        std::cout << "FAIL: Could not create/start HttpClientService" << std::endl;
        mockServer.stop();
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    const int NUM_REQUESTS = 5; // Reduced for clearer testing
    std::vector<UserData> userDataArray(NUM_REQUESTS);
    std::vector<std::future<void>> requestFutures;

    std::cout << "=== CORRECTED GRACEFUL SHUTDOWN TEST ===" << std::endl;
    std::cout << "Step 1: Sending " << NUM_REQUESTS << " slow requests (3s server delay)..." << std::endl;

    // Send slow requests synchronously to ensure they start processing
    for (int i = 0; i < NUM_REQUESTS; i++) {
        requestFutures.emplace_back(std::async(std::launch::async, [&, i]() {
            HttpRequest request;
            request.method = "GET";
            request.url = "http://127.0.0.1:9999/test";
            request.headers["X-Request-ID"] = std::to_string(i);
            request.headers["X-Graceful-Test"] = "true";
            request.headers["X-Slow-Response"] = "true"; // Ask server to respond slowly (3s)

            userDataArray[i].requestId = 7000 + i;
            userDataArray[i].method = "GRACEFUL_SHUTDOWN_TEST";

            std::cout << "Sending slow request " << (7000 + i) << " (will take ~3s to complete)..." << std::endl;

            SendResult sendResult = service->Send(
                request, &userDataArray[i], 10000, // 10s timeout
                [&](const HttpResponse& response) {
                    requestsInProgress--;

                    std::string shutdownStatus = shutdownStarted.load() ? "AFTER" : "BEFORE";
                    std::cout << "Request " << response.userData.requestId << " completed " << shutdownStatus
                              << " shutdown - ";

                    if (response.success) {
                        results.recordSuccess(response.userData.requestId, response);
                        std::cout << "SUCCESS (body: " << response.body.substr(0, 50) << "...)" << std::endl;
                    } else {
                        results.recordError(response.userData.requestId);
                        std::cout << "ERROR: " << response.errorMessage << std::endl;
                    }
                });

            if (sendResult.success) {
                requestsInProgress++;
                requestsSent++;
                std::cout << "Request " << (7000 + i) << " sent successfully" << std::endl;
            } else {
                std::cout << "FAIL: Could not send request " << (7000 + i) << std::endl;
            }
        }));
    }

    // Wait for all requests to be sent
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::cout << "\nStep 2: Requests sent: " << requestsSent.load()
              << ", Requests in progress: " << requestsInProgress.load() << std::endl;
    std::cout << "Step 3: Waiting 1 second for requests to start processing..." << std::endl;

    // Give requests time to reach the server but NOT complete
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    std::cout
        << "Step 4: Initiating graceful shutdown while requests are actively processing (server is sleeping for 3s)..."
        << std::endl;

    // Mark shutdown as started
    shutdownStarted = true;

    // Record the start time of shutdown
    auto shutdownStart = std::chrono::steady_clock::now();

    // Call Stop() while requests are actively being processed by the server
    service->Stop();

    // Record the end time of shutdown
    auto shutdownEnd = std::chrono::steady_clock::now();
    auto shutdownDuration = std::chrono::duration_cast<std::chrono::milliseconds>(shutdownEnd - shutdownStart).count();

    // Wait for all async tasks to complete
    for (auto& future : requestFutures) {
        if (future.valid()) {
            future.wait();
        }
    }

    std::cout << "\n=== Graceful Shutdown Results ===" << std::endl;
    std::cout << "Shutdown completed in " << shutdownDuration << "ms" << std::endl;
    std::cout << "Results during shutdown: " << results.successCount.load() << " success, " << results.errorCount.load()
              << " errors" << std::endl;
    std::cout << "Requests still in progress after Stop(): " << requestsInProgress.load() << std::endl;

    // Stop the mock server
    mockServer.stop();

    // Verify graceful shutdown behavior
    int totalProcessed = results.successCount.load() + results.errorCount.load();

    std::cout << "Total requests processed: " << totalProcessed << "/" << NUM_REQUESTS << std::endl;

    // Key validation: Shutdown should complete within graceful timeout (5 seconds for default config)
    if (shutdownDuration > 8000) { // Allow some buffer
        std::cout << "FAIL: Shutdown took too long: " << shutdownDuration << "ms (> 8s)" << std::endl;
        return false;
    }

    // Should have processed some requests (either completed or cancelled)
    if (totalProcessed == 0) {
        std::cout << "FAIL: No requests were processed during graceful shutdown" << std::endl;
        return false;
    }

    // The key test: Some requests might be cancelled due to shutdown, which is expected behavior
    if (results.errorCount.load() > 0) {
        std::cout << "✓ PASS: Some requests were properly cancelled during shutdown (" << results.errorCount.load()
                  << ")" << std::endl;
    } else {
        std::cout << "INFO: All requests completed successfully before shutdown finished" << std::endl;
    }

    if (results.successCount.load() > 0) {
        std::cout << "✓ PASS: Some requests completed successfully during shutdown (" << results.successCount.load()
                  << ")" << std::endl;
    }

    std::cout << "Shutdown behavior: " << results.successCount.load() << " completed, " << results.errorCount.load()
              << " cancelled" << std::endl;

    std::cout << "PASS: Graceful shutdown test - service stopped without hanging" << std::endl;
    return true;
}

bool testRapidStartStop()
{
    std::cout << "\n=== Test 7: Rapid Start/Stop ===" << std::endl;

    for (int i = 0; i < 3; i++) {
        std::cout << "Iteration " << (i + 1) << "/3..." << std::endl;

        // Start mock server
        MockHttpServer mockServer(9999);
        if (!mockServer.start()) {
            std::cout << "FAIL: Could not start mock server" << std::endl;
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Create and start HttpClientService
        HttpClientServiceConfig config;
        auto service = HttpClientServiceFactory::Create(config);
        if (!service || !service->Start()) {
            std::cout << "FAIL: Could not create/start HttpClientService" << std::endl;
            mockServer.stop();
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Quick request
        HttpRequest request;
        request.method = "GET";
        request.url = "http://127.0.0.1:9999/test";

        TestResults results;
        UserData userData;
        userData.requestId = 6000 + i;
        userData.method = "RAPID_TEST";

        SendResult sendResult = service->Send(request, &userData, 3000, [&results](const HttpResponse& response) {
            if (response.success) {
                results.recordSuccess(response.userData.requestId, response);
            } else {
                results.recordError(response.userData.requestId);
            }
        });

        if (sendResult.success) {
            results.waitForCompletion(1, 5000);
        }

        // Stop services
        service->Stop();
        mockServer.stop();

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "PASS: Rapid start/stop test" << std::endl;
    return true;
}

int main()
{
    std::cout << "HttpClientService Test Suite" << std::endl;
    std::cout << "=========================================" << std::endl;

    // Set log level to see important information
    SetLogLevel(MCP_LOG_LEVEL_INFO); // Reduce log verbosity for comprehensive test

    std::vector<std::pair<std::string, std::function<bool()>>> tests = {
        {"Basic Functionality", testBasicFunctionality},
        {"Concurrent Requests", testConcurrentRequests},
        {"Different HTTP Methods", testDifferentMethods},
        {"Headers and Body", testHeadersAndBody},
        {"Error Handling", testErrorHandling},
        {"Graceful Shutdown", testGracefulShutdown},
        {"Rapid Start/Stop", testRapidStartStop}};

    int passed = 0;
    int total = tests.size();

    for (const auto& test : tests) {
        std::cout << "\n" << std::string(50, '=') << std::endl;
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

        // Small delay between tests
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    std::cout << "\n" << std::string(50, '=') << std::endl;
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
