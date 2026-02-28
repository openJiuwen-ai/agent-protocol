/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */
#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>

#include "client/client_session.h"
#include "shared/jsonrpc.h"
#include "transport/transport.h"

using namespace Mcp;

namespace {

constexpr double TEST_PROGRESS_HALF = 0.5;      // Half progress value (0.0-1.0) for progress tests
constexpr double TEST_PROGRESS_COMPLETE = 1.0;  // Full progress / total value for progress tests
constexpr int64_t TEST_PROGRESS_TOKEN_ID = 42;  // Arbitrary int64_t progressToken for notification test

class ProgressTestTransport : public ClientTransport {
public:
    ~ProgressTestTransport() override = default;

    void SetCallback(std::shared_ptr<TransportCallback> cb) override { callback_ = std::move(cb); }
    void Connect() override {}
    void Terminate() override {}

    void SendMessage(const JSONRPCMessage& message, std::optional<std::string> method = std::nullopt) override
    {
        lastSerialized = SerializeJSONRPCMessage(message, method);
        if (!lastSerialized.empty()) {
            const auto j = nlohmann::json::parse(lastSerialized);
            lastMethod = j.value("method", "");
        }
        sendCount++;
    }

    void EmitNotification(const JSONRPCMessage& message)
    {
        RequestContext ctx;
        if (callback_) {
            callback_->OnMessageReceived(message, ctx);
        }
    }

    std::shared_ptr<TransportCallback> callback_;
    std::string lastSerialized;
    std::string lastMethod;
    int sendCount{0};
};

class ClientSessionProgressTest : public ::testing::Test {
protected:
    ~ClientSessionProgressTest() override = default;

    void SetUp() override
    {
        transport_ = std::make_shared<ProgressTestTransport>();
        session_ = std::make_unique<ClientSession>(transport_, ClientConfig{}, "");
    }

    std::shared_ptr<ProgressTestTransport> transport_;
    std::unique_ptr<ClientSession> session_;
};

} // namespace

// CallTool with progressCallback: serialized request must contain params._meta.progressToken
TEST_F(ClientSessionProgressTest, CallToolWithProgressCallback_RequestContainsMetaProgressToken)
{
    ProgressCallback pcb = [](double, std::optional<double>, const std::optional<std::string>&) {};
    auto future = session_->CallTool("test_tool", std::nullopt, 0, pcb);
    (void)future;

    ASSERT_EQ(transport_->sendCount, 1);
    const auto j = nlohmann::json::parse(transport_->lastSerialized);
    EXPECT_EQ(j.value("method", ""), "tools/call");
    ASSERT_TRUE(j.contains("params"));
    ASSERT_TRUE(j.at("params").contains("_meta"));
    ASSERT_TRUE(j.at("params").at("_meta").contains("progressToken"));
    // progressToken is requestId (int64_t) or string; we use int64_t
    const auto& token = j.at("params").at("_meta").at("progressToken");
    EXPECT_TRUE(token.is_number_integer() || token.is_string());
}

// Incoming notifications/progress invokes the progress callback for the matching request
TEST_F(ClientSessionProgressTest, IncomingProgressNotification_InvokesCallback)
{
    std::atomic<int> callbackCount{0};
    double lastProgress{0};
    std::optional<double> lastTotal;
    std::optional<std::string> lastMessage;

    ProgressCallback pcb = [&](double progress, std::optional<double> total,
                               const std::optional<std::string>& message) {
        callbackCount++;
        lastProgress = progress;
        lastTotal = total;
        lastMessage = message;
    };

    auto future = session_->CallTool("tool", std::nullopt, 0, pcb);
    (void)future;
    ASSERT_EQ(transport_->sendCount, 1);

    // First request gets requestId 1; progressToken in params is 1 (int64_t)
    const auto jReq = nlohmann::json::parse(transport_->lastSerialized);
    constexpr int64_t firstRequestId = 1;  // BaseSession assigns requestId starting from 1
    int64_t tokenValue = firstRequestId;
    if (jReq.contains("params") && jReq.at("params").contains("_meta") &&
        jReq.at("params").at("_meta").contains("progressToken")) {
        const auto& t = jReq.at("params").at("_meta").at("progressToken");
        if (t.is_number_integer())
            tokenValue = t.get<int64_t>();
        else if (t.is_string())
            tokenValue = std::stoll(t.get<std::string>());
    }

    // Build and emit notifications/progress
    nlohmann::json params;
    params["progressToken"] = tokenValue;
    params["progress"] = TEST_PROGRESS_HALF;
    params["total"] = TEST_PROGRESS_COMPLETE;
    params["message"] = "half done";
    nlohmann::json notifJson;
    notifJson["jsonrpc"] = JSONRPC_VERSION;
    notifJson["method"] = "notifications/progress";
    notifJson["params"] = params;
    JSONRPCMessage notifMsg = DeserializeJSONRPCMessage(notifJson.dump(), "notifications/progress");
    ASSERT_TRUE(std::holds_alternative<JSONRPCNotification>(notifMsg));
    transport_->EmitNotification(notifMsg);

    EXPECT_EQ(callbackCount.load(), 1);
    EXPECT_DOUBLE_EQ(lastProgress, TEST_PROGRESS_HALF);
    ASSERT_TRUE(lastTotal.has_value());
    EXPECT_DOUBLE_EQ(*lastTotal, TEST_PROGRESS_COMPLETE);
    EXPECT_EQ(*lastMessage, "half done");
}

// SendProgressNotification with string token: sends notification with correct params
TEST_F(ClientSessionProgressTest, SendProgressNotification_WithStringToken_SendsNotification)
{
    session_->SendProgressNotification(std::string("my-token"), TEST_PROGRESS_HALF, TEST_PROGRESS_COMPLETE,
                                       std::optional<std::string>("50%"));

    ASSERT_EQ(transport_->sendCount, 1);
    const auto j = nlohmann::json::parse(transport_->lastSerialized);
    EXPECT_EQ(j.value("method", ""), "notifications/progress");
    ASSERT_TRUE(j.contains("params"));
    EXPECT_EQ(j.at("params").value("progressToken", std::string()), "my-token");
    EXPECT_DOUBLE_EQ(j.at("params").value("progress", 0.0), TEST_PROGRESS_HALF);
    EXPECT_DOUBLE_EQ(j.at("params").value("total", 0.0), TEST_PROGRESS_COMPLETE);
    EXPECT_EQ(j.at("params").value("message", std::string()), "50%");
}

// SendProgressNotification with int64_t token: sends notification with number progressToken
TEST_F(ClientSessionProgressTest, SendProgressNotification_WithInt64Token_SendsNotification)
{
    session_->SendProgressNotification(ProgressToken(static_cast<int64_t>(TEST_PROGRESS_TOKEN_ID)),
                                       TEST_PROGRESS_COMPLETE, std::nullopt, std::nullopt);

    ASSERT_EQ(transport_->sendCount, 1);
    const auto j = nlohmann::json::parse(transport_->lastSerialized);
    EXPECT_EQ(j.value("method", ""), "notifications/progress");
    ASSERT_TRUE(j.contains("params"));
    EXPECT_TRUE(j.at("params").at("progressToken").is_number_integer());
    EXPECT_EQ(j.at("params").at("progressToken").get<int64_t>(), TEST_PROGRESS_TOKEN_ID);
    EXPECT_DOUBLE_EQ(j.at("params").value("progress", 0.0), TEST_PROGRESS_COMPLETE);
    EXPECT_FALSE(j.at("params").contains("total"));
    EXPECT_FALSE(j.at("params").contains("message"));
}
