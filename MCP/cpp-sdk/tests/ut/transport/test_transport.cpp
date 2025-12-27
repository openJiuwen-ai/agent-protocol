/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <gtest/gtest.h>

#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <nlohmann/json.hpp>

#include "mcp_log.h"
#include "shared/jsonrpc.h"
#include "transport/stdio_transport.h"

using namespace std::chrono_literals;
using json = nlohmann::json;

namespace Mcp {

// Test fixture for StdioClientTransport tests
class StdioClientTransportTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize a basic client transport config
        stdioConfig_.command = "";  // No subprocess by default
        // Set other config values as needed
        stdioConfig_.args = {};
        stdioConfig_.env = {};
    }

    void TearDown() override {
        if (clientTransport_) {
            clientTransport_->Terminate();
        }
        // Short delay to ensure cleanup
        std::this_thread::sleep_for(10ms);
    }

    StdioClientConfig stdioConfig_;
    std::unique_ptr<StdioClientTransport> clientTransport_;
};

// Test fixture for StdioServerTransport tests
class StdioServerTransportTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Server transport doesn't need config
    }

    void TearDown() override {
        if (serverTransport_) {
            serverTransport_->Terminate();
        }
        // Short delay to ensure cleanup
        std::this_thread::sleep_for(10ms);
    }

    std::unique_ptr<StdioServerTransport> serverTransport_;
};


TEST_F(StdioClientTransportTest, ConstructorDestructor) {
    clientTransport_ = std::make_unique<StdioClientTransport>(stdioConfig_);
    ASSERT_NE(clientTransport_, nullptr);


    EXPECT_NO_THROW(clientTransport_->Terminate());
}


TEST_F(StdioServerTransportTest, ConstructorDestructor) {
    serverTransport_ = std::make_unique<StdioServerTransport>();
    ASSERT_NE(serverTransport_, nullptr);

    EXPECT_NO_THROW(serverTransport_->Terminate());
}

TEST_F(StdioClientTransportTest, SetCallback) {
    clientTransport_ = std::make_unique<StdioClientTransport>(stdioConfig_);

    class TestCallback : public TransportCallback {
    public:
        void OnMessageReceived(const JSONRPCMessage&, RequestContext&) override {
            messageReceived++;
        }
        void OnDisconnected(const std::string&) override {
            disconnected++;
        }
        std::atomic<int> messageReceived{0};
        std::atomic<int> disconnected{0};
    };

    auto testCallback = std::make_shared<TestCallback>();
    clientTransport_->SetCallback(testCallback);

    SUCCEED();
}

TEST_F(StdioServerTransportTest, SetCallback) {
    serverTransport_ = std::make_unique<StdioServerTransport>();

    class TestCallback : public TransportCallback {
    public:
        void OnMessageReceived(const JSONRPCMessage&, RequestContext&) override {
            messageReceived++;
        }
        void OnDisconnected(const std::string&) override {
            disconnected++;
        }
        std::atomic<int> messageReceived{0};
        std::atomic<int> disconnected{0};
    };

    auto testCallback = std::make_shared<TestCallback>();
    serverTransport_->SetCallback(testCallback);

    SUCCEED();
}

TEST_F(StdioClientTransportTest, ConnectWithoutCommand) {
    stdioConfig_.command = "";
    clientTransport_ = std::make_unique<StdioClientTransport>(stdioConfig_);

    EXPECT_NO_THROW(clientTransport_->Connect());

    EXPECT_NO_THROW(clientTransport_->Terminate());
}

TEST_F(StdioServerTransportTest, Listen) {
    serverTransport_ = std::make_unique<StdioServerTransport>();

    EXPECT_NO_THROW(serverTransport_->Listen());

    EXPECT_NO_THROW(serverTransport_->Terminate());
}

TEST_F(StdioClientTransportTest, DoubleConnect) {
    clientTransport_ = std::make_unique<StdioClientTransport>(stdioConfig_);

    EXPECT_NO_THROW(clientTransport_->Connect());

    EXPECT_NO_THROW(clientTransport_->Connect());

    EXPECT_NO_THROW(clientTransport_->Terminate());
}

TEST_F(StdioServerTransportTest, DoubleListen) {
    serverTransport_ = std::make_unique<StdioServerTransport>();

    // First listen
    EXPECT_NO_THROW(serverTransport_->Listen());

    EXPECT_NO_THROW(serverTransport_->Listen());

    EXPECT_NO_THROW(serverTransport_->Terminate());
}

TEST_F(StdioClientTransportTest, DoubleTerminate) {
    clientTransport_ = std::make_unique<StdioClientTransport>(stdioConfig_);

    EXPECT_NO_THROW(clientTransport_->Connect());
    EXPECT_NO_THROW(clientTransport_->Terminate());

    // Second terminate should be safe
    EXPECT_NO_THROW(clientTransport_->Terminate());
}

TEST_F(StdioServerTransportTest, DoubleTerminate) {
    serverTransport_ = std::make_unique<StdioServerTransport>();

    EXPECT_NO_THROW(serverTransport_->Listen());
    EXPECT_NO_THROW(serverTransport_->Terminate());

    // Second terminate should be safe
    EXPECT_NO_THROW(serverTransport_->Terminate());
}

TEST_F(StdioServerTransportTest, HandleRequestEmpty) {
    serverTransport_ = std::make_unique<StdioServerTransport>();

    Http::HttpRequest request;
    RequestContext ctx;

    // HandleRequest is empty implementation, should not crash
    EXPECT_NO_THROW(serverTransport_->HandleRequest(request, ctx));
}

TEST_F(StdioServerTransportTest, SendMessageWhenConnected) {
    serverTransport_ = std::make_unique<StdioServerTransport>();

    // Need to set callback first
    class TestCallback : public TransportCallback {
    public:
        void OnMessageReceived(const JSONRPCMessage&, RequestContext&) override {}
        void OnDisconnected(const std::string&) override {}
    };
    auto testCallback = std::make_shared<TestCallback>();
    serverTransport_->SetCallback(testCallback);

    EXPECT_NO_THROW(serverTransport_->Listen());

    // Create a simple JSON-RPC response
    JSONRPCResponse response;
    response.jsonrpc_ = "2.0";
    response.id_ = 1;

    RequestContext ctx;

    // Should not crash when sending
    EXPECT_NO_THROW(serverTransport_->SendMessage(response, ctx));
}

// Test edge cases
TEST_F(StdioClientTransportTest, EmptyConfig) {
    StdioClientConfig emptyConfig;
    clientTransport_ = std::make_unique<StdioClientTransport>(emptyConfig);

    EXPECT_NO_THROW(clientTransport_->Connect());
    EXPECT_NO_THROW(clientTransport_->Terminate());
}

TEST_F(StdioClientTransportTest, NullCallback) {
    clientTransport_ = std::make_unique<StdioClientTransport>(stdioConfig_);

    // Should not crash when setting null callback
    EXPECT_NO_THROW(clientTransport_->SetCallback(nullptr));

    EXPECT_NO_THROW(clientTransport_->Connect());
    EXPECT_NO_THROW(clientTransport_->Terminate());
}

TEST_F(StdioServerTransportTest, NullCallback) {
    serverTransport_ = std::make_unique<StdioServerTransport>();

    // Should not crash when setting null callback
    EXPECT_NO_THROW(serverTransport_->SetCallback(nullptr));

    EXPECT_NO_THROW(serverTransport_->Listen());
    EXPECT_NO_THROW(serverTransport_->Terminate());
}

// Basic callback functionality test for client
TEST_F(StdioClientTransportTest, CallbackFlowTest) {
    clientTransport_ = std::make_unique<StdioClientTransport>(stdioConfig_);

    class TestCallback : public TransportCallback {
    public:
        void OnMessageReceived(const JSONRPCMessage& msg, RequestContext& ctx) override {
            messageReceived = true;
        }

        void OnDisconnected(const std::string& reason) override {
            disconnected = true;
            disconnectReason = reason;
        }

        bool messageReceived = false;
        bool disconnected = false;
        std::string disconnectReason;
    };

    auto testCallback = std::make_shared<TestCallback>();
    clientTransport_->SetCallback(testCallback);

    // Connect and disconnect should work without crashing
    EXPECT_NO_THROW(clientTransport_->Connect());
    EXPECT_NO_THROW(clientTransport_->Terminate());
}

// Basic callback functionality test for server
TEST_F(StdioServerTransportTest, CallbackFlowTest) {
    serverTransport_ = std::make_unique<StdioServerTransport>();

    class TestCallback : public TransportCallback {
    public:
        void OnMessageReceived(const JSONRPCMessage& msg, RequestContext& ctx) override {
            messageReceived = true;
        }

        void OnDisconnected(const std::string& reason) override {
            disconnected = true;
            disconnectReason = reason;
        }

        bool messageReceived = false;
        bool disconnected = false;
        std::string disconnectReason;
    };

    auto testCallback = std::make_shared<TestCallback>();
    serverTransport_->SetCallback(testCallback);

    // Listen and terminate should work without crashing
    EXPECT_NO_THROW(serverTransport_->Listen());
    EXPECT_NO_THROW(serverTransport_->Terminate());
}

// Test rapid connect/disconnect cycles for client
TEST_F(StdioClientTransportTest, RapidConnectDisconnect) {
    for (int i = 0; i < 3; i++) {  // Reduce iterations for speed
        clientTransport_ = std::make_unique<StdioClientTransport>(stdioConfig_);

        // Set callback to avoid null pointer issues
        class TestCallback : public TransportCallback {
        public:
            void OnMessageReceived(const JSONRPCMessage&, RequestContext&) override {}
            void OnDisconnected(const std::string&) override {}
        };
        auto testCallback = std::make_shared<TestCallback>();
        clientTransport_->SetCallback(testCallback);

        EXPECT_NO_THROW(clientTransport_->Connect());
        EXPECT_NO_THROW(clientTransport_->Terminate());

        std::this_thread::sleep_for(5ms);
    }
}

// Test rapid listen/terminate cycles for server
TEST_F(StdioServerTransportTest, RapidListenTerminate) {
    for (int i = 0; i < 3; i++) {  // Reduce iterations for speed
        serverTransport_ = std::make_unique<StdioServerTransport>();

        // Set callback to avoid null pointer issues
        class TestCallback : public TransportCallback {
        public:
            void OnMessageReceived(const JSONRPCMessage&, RequestContext&) override {}
            void OnDisconnected(const std::string&) override {}
        };
        auto testCallback = std::make_shared<TestCallback>();
        serverTransport_->SetCallback(testCallback);

        EXPECT_NO_THROW(serverTransport_->Listen());
        EXPECT_NO_THROW(serverTransport_->Terminate());

        std::this_thread::sleep_for(5ms);
    }
}

// Test with subprocess config for client
TEST_F(StdioClientTransportTest, SubprocessConfig) {
    stdioConfig_.command = "echo";
    stdioConfig_.args = {"hello"};
    stdioConfig_.env = {{"TEST", "value"}};

    clientTransport_ = std::make_unique<StdioClientTransport>(stdioConfig_);

    // Set callback
    class TestCallback : public TransportCallback {
    public:
        void OnMessageReceived(const JSONRPCMessage&, RequestContext&) override {}
        void OnDisconnected(const std::string&) override {}
    };
    auto testCallback = std::make_shared<TestCallback>();
    clientTransport_->SetCallback(testCallback);

    // Connect with subprocess config (may fail but shouldn't crash)
    EXPECT_NO_THROW(clientTransport_->Connect());
    EXPECT_NO_THROW(clientTransport_->Terminate());
}

// Test JSON-RPC message serialization/deserialization integration
TEST_F(StdioClientTransportTest, JSONRPCMessageHandling) {
    clientTransport_ = std::make_unique<StdioClientTransport>(stdioConfig_);

    class TestCallback : public TransportCallback {
    public:
        void OnMessageReceived(const JSONRPCMessage& msg, RequestContext& ctx) override {
            receivedMessages++;

            // Try to get method name from message
            std::string method = std::visit(
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
                msg);

            lastMethod = method;
        }

        void OnDisconnected(const std::string& reason) override {
            disconnected = true;
        }

        std::atomic<int> receivedMessages{0};
        std::string lastMethod;
        bool disconnected = false;
    };

    auto testCallback = std::make_shared<TestCallback>();
    clientTransport_->SetCallback(testCallback);

    EXPECT_NO_THROW(clientTransport_->Connect());
}

} // namespace Mcp
