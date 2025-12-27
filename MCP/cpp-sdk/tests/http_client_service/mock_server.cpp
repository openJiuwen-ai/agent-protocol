/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>

class MockHttpServer {
private:
    std::atomic<bool> running_{false};
    std::thread serverThread_;
    int serverSocket_{-1};
    int port_;
    std::map<std::string, std::string> responses_;

    std::string generateResponse(int statusCode, const std::string& body,
                                 const std::map<std::string, std::string>& headers = {})
    {
        std::ostringstream response;

        // Status line
        switch (statusCode) {
            case 200:
                response << "HTTP/1.1 200 OK\r\n";
                break;
            case 201:
                response << "HTTP/1.1 201 Created\r\n";
                break;
            case 404:
                response << "HTTP/1.1 404 Not Found\r\n";
                break;
            case 500:
                response << "HTTP/1.1 500 Internal Server Error\r\n";
                break;
            default:
                response << "HTTP/1.1 " << statusCode << " Status\r\n";
                break;
        }

        // Default headers
        response << "Content-Type: application/json\r\n";
        response << "Content-Length: " << body.length() << "\r\n";
        response << "Connection: close\r\n";
        response << "Server: MockHttpServer/1.0\r\n";

        // Custom headers
        for (const auto& header : headers) {
            response << header.first << ": " << header.second << "\r\n";
        }

        response << "\r\n";
        response << body;

        return response.str();
    }

    std::string handleRequest(const std::string& method, const std::string& path, const std::string& body,
                              const std::string& fullRequest = "")
    {
        // Route handling
        if (path == "/test" && method == "GET") {
            // Check for slow response indicator
            bool slowResponse = false;
            if (fullRequest.find("X-Slow-Response: true") != std::string::npos ||
                fullRequest.find("X-Graceful-Test: true") != std::string::npos) {
                slowResponse = true;
            }

            // Add artificial delay for graceful shutdown testing
            if (slowResponse) {
                std::cout << "MockServer: Slow response requested, waiting 3 seconds..." << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(3));
                std::cout << "MockServer: Slow response wait completed, sending response" << std::endl;
            }

            std::string responseBody = "{\"status\": \"ok\", \"message\": \"GET test successful\", \"timestamp\": " +
                                       std::to_string(time(nullptr)) +
                                       ", \"slow\": " + (slowResponse ? "true" : "false") + "}";
            return generateResponse(200, responseBody);
        }

        if (path == "/test" && method == "POST") {
            std::string responseBody =
                "{\"status\": \"ok\", \"message\": \"POST test successful\", \"received_bytes\": " +
                std::to_string(body.length()) + "}";
            return generateResponse(201, responseBody);
        }

        if (path == "/echo" && method == "POST") {
            std::string responseBody =
                "{\"echo\": \"" + body + "\", \"original_length\": " + std::to_string(body.length()) + "}";
            return generateResponse(200, responseBody);
        }

        if (path == "/slow" && method == "GET") {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // 1 second delay
            std::string responseBody = "{\"status\": \"ok\", \"message\": \"Slow response after 1 second\"}";
            return generateResponse(200, responseBody);
        }

        if (path == "/status/404" && method == "GET") {
            std::string responseBody = "{\"error\": \"not found\", \"path\": \"" + path + "\"}";
            return generateResponse(404, responseBody);
        }

        if (path == "/status/500" && method == "GET") {
            std::string responseBody =
                "{\"error\": \"internal server error\", \"timestamp\": " + std::to_string(time(nullptr)) + "}";
            return generateResponse(500, responseBody);
        }

        if (path == "/headers" && method == "GET") {
            std::string responseBody = "{\"message\": \"Headers received\", \"server\": \"MockHttpServer\"}";
            return generateResponse(200, responseBody);
        }

        // Default 404
        std::string responseBody = "{\"error\": \"not found\", \"path\": \"" + path + "\"}";
        return generateResponse(404, responseBody);
    }

    std::string parseHttpRequest(const std::string& request)
    {
        std::istringstream iss(request);
        std::string line;
        std::string method, path, version;

        // Parse first line (request line)
        if (std::getline(iss, line)) {
            std::istringstream requestLine(line);
            requestLine >> method >> path >> version;
        }

        // Read body if exists
        std::string body;
        char ch;
        while (iss.get(ch)) {
            body += ch;
        }

        // Pass the full request to handleRequest for header checking
        return handleRequest(method, path, body, request);
    }

    void serverLoop()
    {
        while (running_) {
            struct sockaddr_in clientAddr;
            socklen_t clientAddrLen = sizeof(clientAddr);

            int clientSocket = accept(serverSocket_, (struct sockaddr*)&clientAddr, &clientAddrLen);
            if (clientSocket < 0) {
                continue;
            }

            // Read request
            char buffer[4096];
            std::string request;
            ssize_t bytesRead;

            while ((bytesRead = read(clientSocket, buffer, sizeof(buffer) - 1)) > 0) {
                buffer[bytesRead] = '\0';
                request += buffer;

                // Check for end of HTTP request
                if (request.find("\r\n\r\n") != std::string::npos) {
                    break;
                }
            }

            // Process request and send response
            if (!request.empty()) {
                std::string response = parseHttpRequest(request);
                write(clientSocket, response.c_str(), response.length());
            }

            close(clientSocket);
        }
    }

public:
    MockHttpServer(int port = 8080) : port_(port)
    {
    }

    ~MockHttpServer()
    {
        stop();
    }

    bool start()
    {
        // Create socket
        serverSocket_ = socket(AF_INET, SOCK_STREAM, 0);
        if (serverSocket_ < 0) {
            std::cerr << "❌ Failed to create socket" << std::endl;
            return false;
        }

        // Set socket options
        int opt = 1;
        if (setsockopt(serverSocket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            std::cerr << "❌ Failed to set socket options" << std::endl;
            close(serverSocket_);
            return false;
        }

        // Bind to address
        struct sockaddr_in serverAddr;
        std::memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(port_);

        if (bind(serverSocket_, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            std::cerr << "❌ Failed to bind to port " << port_ << std::endl;
            close(serverSocket_);
            return false;
        }

        // Start listening
        if (listen(serverSocket_, 10) < 0) {
            std::cerr << "❌ Failed to start listening" << std::endl;
            close(serverSocket_);
            return false;
        }

        running_ = true;
        serverThread_ = std::thread(&MockHttpServer::serverLoop, this);

        // Give server time to start
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        return true;
    }

    void stop()
    {
        if (running_) {
            running_ = false;

            // Close server socket to unblock accept()
            if (serverSocket_ >= 0) {
                close(serverSocket_);
                serverSocket_ = -1;
            }

            // Create a temporary connection to unblock accept() if it's still stuck
            int tempSocket = socket(AF_INET, SOCK_STREAM, 0);
            if (tempSocket >= 0) {
                struct sockaddr_in addr;
                std::memset(&addr, 0, sizeof(addr));
                addr.sin_family = AF_INET;
                addr.sin_addr.s_addr = inet_addr("127.0.0.1");
                addr.sin_port = htons(port_);

                // This connect() should unblock the accept() call in the server thread
                connect(tempSocket, (struct sockaddr*)&addr, sizeof(addr));
                close(tempSocket);
            }

            if (serverThread_.joinable()) {
                // Check if thread is still alive after a reasonable time
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                if (serverThread_.joinable()) {
                    serverThread_.detach(); // Force detach rather than hang
                } else {
                    serverThread_.join();
                }
            }
        }
    }

    int getPort() const
    {
        return port_;
    }
    bool isRunning() const
    {
        return running_;
    }
};
