/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "shared/jsonrpc.h"

namespace A2A::Shared::Test {

using json = nlohmann::json;

class TypesSerializationTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // 初始化测试数据
    }

    A2A::Message CreateTestMessage(const std::string& id = "msg-001")
    {
        A2A::Message msg;
        msg.messageId = id;
        msg.role = A2A::Role::AGENT;
        msg.contextId = "ctx-001";
        msg.taskId = "task-001";

        A2A::Part part;
        part.text = "Hello World";
        part.mediaType = "text/plain";
        msg.parts.push_back(part);

        return msg;
    }

    A2A::PushNotificationConfig CreateTestPushConfig(const std::string& url = "https://example.com/notify")
    {
        A2A::PushNotificationConfig config;
        config.url = url;
        config.id = "config-001";
        config.token = "secret-token";

        A2A::PushNotificationAuthenticationInfo auth;
        auth.schemes = {"bearer"};
        auth.credentials = "auth-token";
        config.authentication = auth;

        return config;
    }

    A2A::AgentInterface CreateTestAgentInterface(
        const std::string& url = "https://api.example.com/a2a",
        const std::string& binding = "JSONRPC",
        const std::string& version = "2.0")
    {
        A2A::AgentInterface iface;
        iface.url = url;
        iface.protocolBinding = binding;
        iface.protocolVersion = version;
        return iface;
    }

    A2A::AgentCapabilities CreateTestAgentCapabilities()
    {
        A2A::AgentCapabilities caps;
        caps.streaming = true;
        caps.pushNotifications = false;
        caps.extendedAgentCard = true;
        return caps;
    }

    A2A::AgentSkill CreateTestAgentSkill(const std::string& id = "skill-001")
    {
        A2A::AgentSkill skill;
        skill.id = id;
        skill.name = "Test Skill";
        skill.description = "Test skill description";
        skill.tags = {"test", "example"};
        return skill;
    }

    A2A::SecurityScheme CreateTestAPIKeySecurityScheme()
    {
        A2A::APIKeySecurityScheme apiKey;
        apiKey.in_ = "header";
        apiKey.name = "X-API-Key";
        apiKey.description = "API Key authentication";

        A2A::SecurityScheme scheme;
        scheme.v = apiKey;
        return scheme;
    }
};

// ===========================================================================
// Role 序列化测试
// ===========================================================================

TEST_F(TypesSerializationTest, RoleSerialization)
{
    // 测试 AGENT 角色
    json j_agent = A2A::Role::AGENT;
    EXPECT_EQ(std::string(j_agent), "ROLE_AGENT");

    A2A::Role role_agent = j_agent.get<A2A::Role>();
    EXPECT_EQ(role_agent, A2A::Role::AGENT);

    // 测试 USER 角色
    json j_user = A2A::Role::USER;
    EXPECT_EQ(std::string(j_user), "ROLE_USER");

    A2A::Role role_user = j_user.get<A2A::Role>();
    EXPECT_EQ(role_user, A2A::Role::USER);
}

TEST_F(TypesSerializationTest, RoleInvalidValue)
{
    json j_invalid = "ROLE_INVALID";
    EXPECT_THROW(j_invalid.get<A2A::Role>(), std::runtime_error);
}

// ===========================================================================
// Part 序列化测试
// ===========================================================================

TEST_F(TypesSerializationTest, PartTextSerialization)
{
    nlohmann::json metadata = {{"data", "metadata"}};
    A2A::Part part;
    part.text = "Hello World";
    part.mediaType = "text/plain";
    part.metadata = metadata.dump();
    part.filename = "test.txt";

    json j = part;

    EXPECT_TRUE(j.contains("text"));
    EXPECT_EQ(std::string(j["text"]), "Hello World");
    EXPECT_EQ(std::string(j["mediaType"]), "text/plain");
    EXPECT_EQ(j["metadata"], metadata);
    EXPECT_EQ(std::string(j["filename"]), "test.txt");

    A2A::Part deserialized = j.get<A2A::Part>();
    EXPECT_TRUE(deserialized.text.has_value());
    EXPECT_EQ(deserialized.text.value(), "Hello World");
    EXPECT_TRUE(deserialized.mediaType.has_value());
    EXPECT_EQ(deserialized.mediaType.value(), "text/plain");
}

TEST_F(TypesSerializationTest, PartRawSerialization)
{
    A2A::Part part;
    part.raw = "raw data";
    part.mediaType = "application/octet-stream";

    json j = part;

    EXPECT_TRUE(j.contains("raw"));
    EXPECT_EQ(std::string(j["raw"]), "raw data");
    EXPECT_FALSE(j.contains("text"));
    EXPECT_FALSE(j.contains("url"));
    EXPECT_FALSE(j.contains("data"));

    A2A::Part deserialized = j.get<A2A::Part>();
    EXPECT_TRUE(deserialized.raw.has_value());
    EXPECT_EQ(deserialized.raw.value(), "raw data");
}

TEST_F(TypesSerializationTest, PartUrlSerialization)
{
    A2A::Part part;
    part.url = "https://example.com/file.txt";

    json j = part;

    EXPECT_TRUE(j.contains("url"));
    EXPECT_EQ(std::string(j["url"]), "https://example.com/file.txt");

    A2A::Part deserialized = j.get<A2A::Part>();
    EXPECT_TRUE(deserialized.url.has_value());
    EXPECT_EQ(deserialized.url.value(), "https://example.com/file.txt");
}

TEST_F(TypesSerializationTest, PartDataSerialization)
{
    nlohmann::json data = {{"key", "value"}};
    A2A::Part part;
    part.data = data.dump();

    json j = part;

    EXPECT_TRUE(j.contains("data"));
    EXPECT_EQ(j["data"], data);

    A2A::Part deserialized = j.get<A2A::Part>();
    EXPECT_TRUE(deserialized.data.has_value());
    EXPECT_EQ(std::get<std::string>(deserialized.data.value()), data.dump());
}

TEST_F(TypesSerializationTest, PartMissingMutualField)
{
    json j = {{"mediaType", "text/plain"}};
    EXPECT_THROW(j.get<A2A::Part>(), std::logic_error);
}

TEST_F(TypesSerializationTest, PartMultipleMutualFields)
{
    // 序列化时只取第一个出现的互斥字段
    A2A::Part part;
    part.text = "text";
    part.url = "url";  // 会被忽略

    json j = part;
    EXPECT_TRUE(j.contains("text"));
    EXPECT_FALSE(j.contains("url"));
}

// ===========================================================================
// Artifact 序列化测试
// ===========================================================================

TEST_F(TypesSerializationTest, ArtifactSerialization)
{
    nlohmann::json metadata = {{"data", "metadata"}};
    A2A::Artifact artifact;
    artifact.artifactId = "art-001";
    artifact.name = "output.txt";
    artifact.description = "Output file";
    artifact.metadata = metadata.dump();
    artifact.extensions = std::vector<std::string>{"ext1", "ext2"};

    A2A::Part part;
    part.text = "content";
    artifact.parts.push_back(part);

    json j = artifact;

    EXPECT_EQ(std::string(j["artifactId"]), "art-001");
    EXPECT_EQ(std::string(j["name"]), "output.txt");
    EXPECT_EQ(std::string(j["description"]), "Output file");
    EXPECT_EQ(j["metadata"], metadata);
    EXPECT_EQ(std::string(j["extensions"][0]), "ext1");
    EXPECT_EQ(j["parts"].size(), 1);

    A2A::Artifact deserialized = j.get<A2A::Artifact>();
    EXPECT_EQ(deserialized.artifactId, "art-001");
    EXPECT_TRUE(deserialized.name.has_value());
    EXPECT_EQ(deserialized.name.value(), "output.txt");
    EXPECT_EQ(deserialized.parts.size(), 1);
}

TEST_F(TypesSerializationTest, ArtifactMissingRequiredFields)
{
    json j_no_id = {{"parts", json::array()}};
    EXPECT_THROW(j_no_id.get<A2A::Artifact>(), std::runtime_error);

    json j_no_parts = {{"artifactId", "art-001"}};
    EXPECT_THROW(j_no_parts.get<A2A::Artifact>(), std::runtime_error);
}

TEST_F(TypesSerializationTest, ArtifactEmptyParts)
{
    A2A::Artifact artifact;
    artifact.artifactId = "art-001";
    artifact.parts = {};

    json j = artifact;
    EXPECT_EQ(std::string(j["artifactId"]), "art-001");
    EXPECT_TRUE(j["parts"].is_array());
    EXPECT_EQ(j["parts"].size(), 0);

    A2A::Artifact deserialized = j.get<A2A::Artifact>();
    EXPECT_EQ(deserialized.artifactId, "art-001");
    EXPECT_EQ(deserialized.parts.size(), 0);
}

// ===========================================================================
// Message 序列化测试
// ===========================================================================

TEST_F(TypesSerializationTest, MessageSerialization)
{
    nlohmann::json metadata = {{"data", "metadata"}};
    A2A::Message message;
    message.messageId = "msg-001";
    message.role = A2A::Role::AGENT;
    message.contextId = "ctx-001";
    message.taskId = "task-001";
    message.metadata = metadata.dump();
    message.referenceTaskIds = std::vector<std::string>{"ref-001"};

    A2A::Part part;
    part.text = "content";
    message.parts.push_back(part);

    json j = message;

    EXPECT_EQ(std::string(j["messageId"]), "msg-001");
    EXPECT_EQ(std::string(j["role"]), "ROLE_AGENT");
    EXPECT_EQ(std::string(j["contextId"]), "ctx-001");
    EXPECT_EQ(std::string(j["taskId"]), "task-001");
    EXPECT_EQ(j["metadata"], metadata);
    EXPECT_EQ(std::string(j["referenceTaskIds"][0]), "ref-001");
    EXPECT_EQ(j["parts"].size(), 1);

    A2A::Message deserialized = j.get<A2A::Message>();
    EXPECT_EQ(deserialized.messageId, "msg-001");
    EXPECT_EQ(deserialized.role, A2A::Role::AGENT);
    EXPECT_TRUE(deserialized.contextId.has_value());
    EXPECT_EQ(deserialized.contextId.value(), "ctx-001");
}

TEST_F(TypesSerializationTest, MessageMissingRequiredFields)
{
    json j_no_id = {{"parts", json::array()}};
    EXPECT_THROW(j_no_id.get<A2A::Message>(), std::runtime_error);

    json j_no_parts = {{"messageId", "msg-001"}};
    EXPECT_THROW(j_no_parts.get<A2A::Message>(), std::runtime_error);
}

TEST_F(TypesSerializationTest, MessageDefaultRole)
{
    json j = {
        {"messageId", "msg-001"},
        {"parts", json::array()}
    };

    A2A::Message message = j.get<A2A::Message>();
    EXPECT_EQ(message.role, A2A::Role::USER);
}

// ===========================================================================
// TaskStatus 序列化测试
// ===========================================================================

TEST_F(TypesSerializationTest, TaskStatusSerialization)
{
    A2A::TaskStatus status;
    status.state = A2A::TaskState::WORKING;
    status.timestamp = "2024-01-01T00:00:00Z";

    A2A::Message message;
    message.messageId = "msg-001";
    message.role = A2A::Role::AGENT;
    status.message = message;

    json j = status;

    EXPECT_EQ(std::string(j["state"]), "TASK_STATE_WORKING");
    EXPECT_EQ(std::string(j["timestamp"]), "2024-01-01T00:00:00Z");
    EXPECT_TRUE(j.contains("message"));

    A2A::TaskStatus deserialized = j.get<A2A::TaskStatus>();
    EXPECT_EQ(deserialized.state, A2A::TaskState::WORKING);
    EXPECT_TRUE(deserialized.timestamp.has_value());
    EXPECT_TRUE(deserialized.message.has_value());
}

TEST_F(TypesSerializationTest, TaskStatusAllStates)
{
    std::vector<std::pair<A2A::TaskState, std::string>> states = {
        {A2A::TaskState::SUBMITTED, "TASK_STATE_SUBMITTED"},
        {A2A::TaskState::WORKING, "TASK_STATE_WORKING"},
        {A2A::TaskState::INPUT_REQUIRED, "TASK_STATE_INPUT_REQUIRED"},
        {A2A::TaskState::COMPLETED, "TASK_STATE_COMPLETED"},
        {A2A::TaskState::CANCELED, "TASK_STATE_CANCELED"},
        {A2A::TaskState::FAILED, "TASK_STATE_FAILED"},
        {A2A::TaskState::REJECTED, "TASK_STATE_REJECTED"},
        {A2A::TaskState::AUTH_REQUIRED, "TASK_STATE_AUTH_REQUIRED"}
    };

    for (const auto& [state, str] : states) {
        A2A::TaskStatus status;
        status.state = state;

        json j = status;
        EXPECT_EQ(std::string(j["state"]), str);

        A2A::TaskStatus deserialized = j.get<A2A::TaskStatus>();
        EXPECT_EQ(deserialized.state, state);
    }
}

TEST_F(TypesSerializationTest, TaskStatusUnknownState)
{
    json j = {{"state", "TASK_STATE_UNSPECIFIED"}};
    A2A::TaskStatus status = j.get<A2A::TaskStatus>();
    EXPECT_EQ(status.state, A2A::TaskState::UNSPECIFIED);
}

// ===========================================================================
// Task 序列化测试
// ===========================================================================

TEST_F(TypesSerializationTest, TaskSerialization)
{
    nlohmann::json metadata = {{"data", "metadata"}};
    A2A::Task task;
    task.id = "task-001";
    task.contextId = "ctx-001";
    task.metadata = metadata.dump();

    A2A::TaskStatus status;
    status.state = A2A::TaskState::WORKING;
    task.status = status;

    A2A::Artifact artifact;
    artifact.artifactId = "art-001";
    task.artifacts = std::vector<A2A::Artifact>{artifact};

    A2A::Message message;
    message.messageId = "msg-001";
    message.role = A2A::Role::AGENT;
    task.history = std::vector<A2A::Message>{message};

    json j = task;

    EXPECT_EQ(std::string(j["id"]), "task-001");
    EXPECT_EQ(std::string(j["contextId"]), "ctx-001");
    EXPECT_EQ(j["metadata"], metadata);
    EXPECT_EQ(j["artifacts"].size(), 1);
    EXPECT_EQ(j["history"].size(), 1);
    EXPECT_EQ(std::string(j["status"]["state"]), "TASK_STATE_WORKING");

    A2A::Task deserialized = j.get<A2A::Task>();
    EXPECT_EQ(deserialized.id, "task-001");
    EXPECT_EQ(deserialized.contextId, "ctx-001");
    EXPECT_TRUE(deserialized.artifacts.has_value());
    EXPECT_EQ(deserialized.artifacts.value().size(), 1);
}

TEST_F(TypesSerializationTest, TaskMissingRequiredFields)
{
    json j_no_id = {{"contextId", "ctx-001"}, {"status", json::object()}};
    EXPECT_THROW(j_no_id.get<A2A::Task>(), std::runtime_error);

    json j_no_context = {{"id", "task-001"}, {"status", json::object()}};
    EXPECT_THROW(j_no_context.get<A2A::Task>(), std::runtime_error);

    json j_no_status = {{"id", "task-001"}, {"contextId", "ctx-001"}};
    EXPECT_THROW(j_no_status.get<A2A::Task>(), std::runtime_error);
}

// ===========================================================================
// PushNotification 相关序列化测试
// ===========================================================================

TEST_F(TypesSerializationTest, PushNotificationAuthenticationInfoSerialization)
{
    A2A::PushNotificationAuthenticationInfo info;
    info.schemes = {"bearer", "basic"};
    info.credentials = "token";

    json j = info;

    EXPECT_TRUE(j.contains("schemes"));
    EXPECT_EQ(j["schemes"].size(), 2);
    EXPECT_EQ(std::string(j["schemes"][0]), "bearer");
    EXPECT_EQ(std::string(j["schemes"][1]), "basic");
    EXPECT_EQ(std::string(j["credentials"]), "token");

    A2A::PushNotificationAuthenticationInfo deserialized = j.get<A2A::PushNotificationAuthenticationInfo>();
    EXPECT_EQ(deserialized.schemes.size(), 2);
    EXPECT_TRUE(deserialized.credentials.has_value());
    EXPECT_EQ(deserialized.credentials.value(), "token");
}

TEST_F(TypesSerializationTest, PushNotificationAuthenticationInfoMissingSchemes)
{
    json j = {{"credentials", "token"}};
    EXPECT_THROW(j.get<A2A::PushNotificationAuthenticationInfo>(), std::runtime_error);
}

TEST_F(TypesSerializationTest, PushNotificationConfigSerialization)
{
    A2A::PushNotificationConfig config;
    config.url = "https://example.com/notify";
    config.id = "config-001";
    config.token = "secret";

    A2A::PushNotificationAuthenticationInfo info;
    info.schemes = {"bearer"};
    config.authentication = info;

    json j = config;

    EXPECT_EQ(std::string(j["url"]), "https://example.com/notify");
    EXPECT_EQ(std::string(j["id"]), "config-001");
    EXPECT_EQ(std::string(j["token"]), "secret");
    EXPECT_TRUE(j.contains("authentication"));

    A2A::PushNotificationConfig deserialized = j.get<A2A::PushNotificationConfig>();
    EXPECT_EQ(deserialized.url, "https://example.com/notify");
    EXPECT_TRUE(deserialized.id.has_value());
    EXPECT_EQ(deserialized.id.value(), "config-001");
    EXPECT_TRUE(deserialized.authentication.has_value());
}

TEST_F(TypesSerializationTest, PushNotificationConfigMissingUrl)
{
    json j = {{"id", "config-001"}};
    EXPECT_THROW(j.get<A2A::PushNotificationConfig>(), std::runtime_error);
}

// ===========================================================================
// MessageSendConfiguration 序列化测试
// ===========================================================================

TEST_F(TypesSerializationTest, MessageSendConfigurationSerialization)
{
    A2A::MessageSendConfiguration config;
    config.acceptedOutputModes = {"text/plain", "application/json"};
    config.historyLength = 10;
    config.returnImmediately = false;

    A2A::PushNotificationConfig pushConfig;
    pushConfig.url = "https://example.com/notify";
    config.pushNotificationConfig = pushConfig;

    json j = config;

    EXPECT_EQ(j["acceptedOutputModes"].size(), 2);
    EXPECT_EQ(std::string(j["acceptedOutputModes"][0]), "text/plain");
    EXPECT_EQ(j["historyLength"], 10);
    EXPECT_EQ(j["returnImmediately"], false);
    EXPECT_TRUE(j.contains("pushNotificationConfig"));

    A2A::MessageSendConfiguration deserialized = j.get<A2A::MessageSendConfiguration>();
    ASSERT_TRUE(deserialized.acceptedOutputModes.has_value());
    EXPECT_EQ(deserialized.acceptedOutputModes->size(), 2);
    EXPECT_TRUE(deserialized.historyLength.has_value());
    EXPECT_EQ(deserialized.historyLength.value(), 10);
    EXPECT_TRUE(deserialized.returnImmediately.has_value());
    EXPECT_FALSE(deserialized.returnImmediately.value());
    EXPECT_TRUE(deserialized.pushNotificationConfig.has_value());
}

TEST_F(TypesSerializationTest, MessageSendConfigurationInvalidAcceptedOutputModes)
{
    json j = {
        {"acceptedOutputModes", "not an array"}
    };
    EXPECT_THROW(j.get<A2A::MessageSendConfiguration>(), std::runtime_error);
}

// ===========================================================================
// 复杂嵌套对象测试
// ===========================================================================

TEST_F(TypesSerializationTest, ComplexTaskSerialization)
{
    nlohmann::json metadata = {{"data", "metadata"}};
    A2A::Task task;
    task.id = "complex-task-001";
    task.contextId = "complex-ctx-001";
    task.metadata = metadata.dump();

    A2A::TaskStatus status;
    status.state = A2A::TaskState::WORKING;
    status.timestamp = "2024-01-01T12:00:00Z";

    A2A::Message statusMessage;
    statusMessage.messageId = "status-msg-001";
    statusMessage.role = A2A::Role::AGENT;

    A2A::Part statusPart;
    statusPart.text = "Working on task";
    statusMessage.parts.push_back(statusPart);

    status.message = statusMessage;
    task.status = status;

    std::vector<A2A::Artifact> artifacts;
    for (int i = 0; i < 2; i++) {
        A2A::Artifact artifact;
        artifact.artifactId = "art-" + std::to_string(i);
        artifact.name = "output" + std::to_string(i) + ".txt";

        A2A::Part part;
        part.text = "Content " + std::to_string(i);
        artifact.parts.push_back(part);

        artifacts.push_back(artifact);
    }
    task.artifacts = artifacts;

    std::vector<A2A::Message> history;
    for (int i = 0; i < 3; i++) {
        A2A::Message msg;
        msg.messageId = "history-" + std::to_string(i);
        msg.role = (i % 2 == 0) ? A2A::Role::USER : A2A::Role::AGENT;

        A2A::Part part;
        part.text = "Message " + std::to_string(i);
        msg.parts.push_back(part);

        history.push_back(msg);
    }
    task.history = history;

    json j = task;

    EXPECT_EQ(std::string(j["id"]), "complex-task-001");
    EXPECT_EQ(j["artifacts"].size(), 2);
    EXPECT_EQ(j["history"].size(), 3);
    EXPECT_EQ(std::string(j["status"]["state"]), "TASK_STATE_WORKING");

    A2A::Task deserialized = j.get<A2A::Task>();
    EXPECT_EQ(deserialized.id, "complex-task-001");
    EXPECT_EQ(deserialized.contextId, "complex-ctx-001");

    ASSERT_TRUE(deserialized.artifacts.has_value());
    EXPECT_EQ(deserialized.artifacts.value().size(), 2);

    ASSERT_TRUE(deserialized.history.has_value());
    EXPECT_EQ(deserialized.history.value().size(), 3);

    EXPECT_EQ(deserialized.status.state, A2A::TaskState::WORKING);
    ASSERT_TRUE(deserialized.status.message.has_value());
    EXPECT_EQ(deserialized.status.message.value().messageId, "status-msg-001");
}

// ===========================================================================
// MessageSendParams 序列化测试
// ===========================================================================

TEST_F(TypesSerializationTest, MessageSendParamsSerialization)
{
    nlohmann::json metadata = {{"data", "metadata"}};
    A2A::MessageSendParams params;
    params.message = CreateTestMessage();

    A2A::MessageSendConfiguration config;
    config.acceptedOutputModes = {"text/plain"};
    config.historyLength = 10;
    config.returnImmediately = false;
    params.configuration = config;

    params.metadata = metadata.dump();

    json j = params;

    EXPECT_TRUE(j.contains("message"));
    EXPECT_TRUE(j.contains("configuration"));
    EXPECT_TRUE(j.contains("metadata"));
    EXPECT_EQ(std::string(j["message"]["messageId"]), "msg-001");
    EXPECT_EQ(j["configuration"]["historyLength"], 10);
    EXPECT_EQ(j["metadata"], metadata);

    A2A::MessageSendParams deserialized = j.get<A2A::MessageSendParams>();
    EXPECT_EQ(deserialized.message.messageId, "msg-001");
    ASSERT_TRUE(deserialized.configuration.has_value());
    EXPECT_EQ(deserialized.configuration.value().historyLength.value(), 10);
    ASSERT_TRUE(deserialized.metadata.has_value());
    EXPECT_EQ(deserialized.metadata.value(), metadata.dump());
}

TEST_F(TypesSerializationTest, MessageSendParamsMinimalSerialization)
{
    A2A::MessageSendParams params;
    params.message = CreateTestMessage();

    json j = params;

    EXPECT_TRUE(j.contains("message"));
    EXPECT_FALSE(j.contains("configuration"));
    EXPECT_FALSE(j.contains("metadata"));

    A2A::MessageSendParams deserialized = j.get<A2A::MessageSendParams>();
    EXPECT_EQ(deserialized.message.messageId, "msg-001");
    EXPECT_FALSE(deserialized.configuration.has_value());
    EXPECT_FALSE(deserialized.metadata.has_value());
}

TEST_F(TypesSerializationTest, MessageSendParamsMissingMessage)
{
    json j = {{"metadata", "test"}};
    EXPECT_THROW(j.get<A2A::MessageSendParams>(), std::runtime_error);
}

// ===========================================================================
// TaskArtifactUpdateEvent 序列化测试
// ===========================================================================

TEST_F(TypesSerializationTest, TaskArtifactUpdateEventSerialization)
{
    nlohmann::json metadata = {{"data", "metadata"}};
    A2A::TaskArtifactUpdateEvent event;
    event.taskId = "task-001";
    event.contextId = "ctx-001";
    event.append = true;
    event.lastChunk = false;
    event.metadata = metadata.dump();

    A2A::Artifact artifact;
    artifact.artifactId = "art-001";
    artifact.name = "output.txt";

    A2A::Part part;
    part.text = "file content";
    artifact.parts.push_back(part);

    event.artifact = artifact;

    json j = event;

    EXPECT_EQ(std::string(j["taskId"]), "task-001");
    EXPECT_EQ(std::string(j["contextId"]), "ctx-001");
    EXPECT_EQ(j["append"], true);
    EXPECT_EQ(j["lastChunk"], false);
    EXPECT_EQ(j["metadata"], metadata);
    EXPECT_EQ(std::string(j["artifact"]["artifactId"]), "art-001");

    A2A::TaskArtifactUpdateEvent deserialized = j.get<A2A::TaskArtifactUpdateEvent>();
    EXPECT_EQ(deserialized.taskId, "task-001");
    EXPECT_EQ(deserialized.contextId, "ctx-001");
    ASSERT_TRUE(deserialized.append.has_value());
    EXPECT_TRUE(deserialized.append.value());
    ASSERT_TRUE(deserialized.metadata.has_value());
    EXPECT_EQ(deserialized.metadata.value(), metadata.dump());
    EXPECT_EQ(deserialized.artifact.artifactId, "art-001");
}

TEST_F(TypesSerializationTest, TaskArtifactUpdateEventMinimalSerialization)
{
    A2A::TaskArtifactUpdateEvent event;
    event.taskId = "task-001";
    event.contextId = "ctx-001";

    A2A::Artifact artifact;
    artifact.artifactId = "art-001";
    event.artifact = artifact;

    json j = event;

    EXPECT_EQ(std::string(j["taskId"]), "task-001");
    EXPECT_FALSE(j.contains("append"));
    EXPECT_FALSE(j.contains("lastChunk"));
    EXPECT_FALSE(j.contains("metadata"));

    A2A::TaskArtifactUpdateEvent deserialized = j.get<A2A::TaskArtifactUpdateEvent>();
    EXPECT_EQ(deserialized.taskId, "task-001");
    EXPECT_FALSE(deserialized.append.has_value());
    EXPECT_FALSE(deserialized.metadata.has_value());
}

TEST_F(TypesSerializationTest, TaskArtifactUpdateEventMissingRequiredFields)
{
    json j_no_task = {{"contextId", "ctx-001"}, {"artifact", json::object()}};
    EXPECT_THROW(j_no_task.get<A2A::TaskArtifactUpdateEvent>(), std::runtime_error);

    json j_empty_task = {{"taskId", ""}, {"contextId", "ctx-001"}, {"artifact", json::object()}};
    EXPECT_THROW(j_empty_task.get<A2A::TaskArtifactUpdateEvent>(), std::runtime_error);
}

// ===========================================================================
// TaskStatusUpdateEvent 序列化测试
// ===========================================================================

TEST_F(TypesSerializationTest, TaskStatusUpdateEventSerialization)
{
    nlohmann::json metadata = {{"data", "metadata"}};
    A2A::TaskStatusUpdateEvent event;
    event.taskId = "task-001";
    event.contextId = "ctx-001";
    event.metadata = metadata.dump();

    A2A::TaskStatus status;
    status.state = A2A::TaskState::COMPLETED;
    status.timestamp = "2024-01-01T00:00:00Z";

    A2A::Message message;
    message.messageId = "status-msg";
    message.role = A2A::Role::AGENT;
    status.message = message;

    event.status = status;

    json j = event;

    EXPECT_EQ(std::string(j["taskId"]), "task-001");
    EXPECT_EQ(std::string(j["contextId"]), "ctx-001");
    EXPECT_EQ(j["metadata"], metadata);
    EXPECT_EQ(std::string(j["status"]["state"]), "TASK_STATE_COMPLETED");

    A2A::TaskStatusUpdateEvent deserialized = j.get<A2A::TaskStatusUpdateEvent>();
    EXPECT_EQ(deserialized.taskId, "task-001");
    EXPECT_EQ(deserialized.status.state, A2A::TaskState::COMPLETED);
    EXPECT_TRUE(deserialized.status.message.has_value());
}

TEST_F(TypesSerializationTest, TaskStatusUpdateEventMissingRequiredFields)
{
    json j_no_task = {{"contextId", "ctx-001"}, {"status", json::object()}};
    EXPECT_THROW(j_no_task.get<A2A::TaskStatusUpdateEvent>(), std::runtime_error);

    json j_empty_task = {{"taskId", ""}, {"contextId", "ctx-001"}, {"status", json::object()}};
    EXPECT_THROW(j_empty_task.get<A2A::TaskStatusUpdateEvent>(), std::runtime_error);
}

// ===========================================================================
// TaskIdParams 序列化测试
// ===========================================================================

TEST_F(TypesSerializationTest, TaskIdParamsSerialization)
{
    nlohmann::json metadata = {{"data", "metadata"}};
    A2A::TaskIdParams params;
    params.id = "task-001";
    params.metadata = metadata.dump();

    json j = params;

    EXPECT_EQ(std::string(j["id"]), "task-001");
    EXPECT_EQ(j["metadata"], metadata);

    A2A::TaskIdParams deserialized = j.get<A2A::TaskIdParams>();
    EXPECT_EQ(deserialized.id, "task-001");
    ASSERT_TRUE(deserialized.metadata.has_value());
    EXPECT_EQ(deserialized.metadata.value(), metadata.dump());
}

// ===========================================================================
// TaskQueryParams 序列化测试
// ===========================================================================

TEST_F(TypesSerializationTest, TaskQueryParamsSerialization)
{
    nlohmann::json metadata = {{"data", "metadata"}};
    A2A::TaskQueryParams params;
    params.id = "task-001";
    params.historyLength = 5;
    params.metadata = metadata.dump();

    json j = params;

    EXPECT_EQ(std::string(j["id"]), "task-001");
    EXPECT_EQ(j["historyLength"], 5);
    EXPECT_EQ(j["metadata"], metadata);

    A2A::TaskQueryParams deserialized = j.get<A2A::TaskQueryParams>();
    EXPECT_EQ(deserialized.id, "task-001");
    ASSERT_TRUE(deserialized.historyLength.has_value());
    EXPECT_EQ(deserialized.historyLength.value(), 5);
}

// ===========================================================================
// TaskPushNotificationConfig 相关序列化测试
// ===========================================================================

TEST_F(TypesSerializationTest, TaskPushNotificationConfigSerialization)
{
    A2A::TaskPushNotificationConfig config;
    config.taskId = "task-001";
    config.pushNotificationConfig = CreateTestPushConfig();

    json j = config;

    EXPECT_EQ(std::string(j["taskId"]), "task-001");
    EXPECT_TRUE(j.contains("pushNotificationConfig"));
    EXPECT_EQ(std::string(j["pushNotificationConfig"]["url"]), "https://example.com/notify");

    A2A::TaskPushNotificationConfig deserialized = j.get<A2A::TaskPushNotificationConfig>();
    EXPECT_EQ(deserialized.taskId, "task-001");
    EXPECT_EQ(deserialized.pushNotificationConfig.url, "https://example.com/notify");
}

TEST_F(TypesSerializationTest, GetTaskPushNotificationConfigParamsSerialization)
{
    nlohmann::json metadata = {{"data", "metadata"}};
    A2A::GetTaskPushNotificationConfigParams params;
    params.id = "task-001";
    params.pushNotificationConfigId = "config-001";
    params.metadata = metadata.dump();

    json j = params;

    EXPECT_EQ(std::string(j["id"]), "task-001");
    EXPECT_EQ(std::string(j["pushNotificationConfigId"]), "config-001");
    EXPECT_EQ(j["metadata"], metadata);

    A2A::GetTaskPushNotificationConfigParams deserialized = j.get<A2A::GetTaskPushNotificationConfigParams>();
    EXPECT_EQ(deserialized.id, "task-001");
    ASSERT_TRUE(deserialized.pushNotificationConfigId.has_value());
    EXPECT_EQ(deserialized.pushNotificationConfigId.value(), "config-001");
}

TEST_F(TypesSerializationTest, ListTaskPushNotificationConfigParamsSerialization)
{
    nlohmann::json metadata = {{"data", "metadata"}};
    A2A::ListTaskPushNotificationConfigParams params;
    params.id = "task-001";
    params.metadata = metadata.dump();

    json j = params;

    EXPECT_EQ(std::string(j["id"]), "task-001");
    EXPECT_EQ(j["metadata"], metadata);

    A2A::ListTaskPushNotificationConfigParams deserialized = j.get<A2A::ListTaskPushNotificationConfigParams>();
    EXPECT_EQ(deserialized.id, "task-001");
    ASSERT_TRUE(deserialized.metadata.has_value());
    EXPECT_EQ(deserialized.metadata.value(), metadata.dump());
}

TEST_F(TypesSerializationTest, DeleteTaskPushNotificationConfigParamsSerialization)
{
    nlohmann::json metadata = {{"data", "metadata"}};
    A2A::DeleteTaskPushNotificationConfigParams params;
    params.id = "task-001";
    params.pushNotificationConfigId = "config-001";
    params.metadata = metadata.dump();

    json j = params;

    EXPECT_EQ(std::string(j["id"]), "task-001");
    EXPECT_EQ(std::string(j["pushNotificationConfigId"]), "config-001");
    EXPECT_EQ(j["metadata"], metadata);

    A2A::DeleteTaskPushNotificationConfigParams deserialized = j.get<A2A::DeleteTaskPushNotificationConfigParams>();
    EXPECT_EQ(deserialized.id, "task-001");
    EXPECT_EQ(deserialized.pushNotificationConfigId, "config-001");
}

// ===========================================================================
// A2AError 及其派生类序列化测试
// ===========================================================================

TEST_F(TypesSerializationTest, A2AErrorSerialization)
{
    A2A::A2AError error;
    error.code = -32001;
    error.message = "Task not found";
    error.data = "Additional info";

    json j = error;

    EXPECT_EQ(j["code"], -32001);
    EXPECT_EQ(std::string(j["message"]), "Task not found");
    EXPECT_EQ(std::string(j["data"]), "Additional info");

    A2A::A2AError deserialized = j.get<A2A::A2AError>();
    EXPECT_EQ(deserialized.code, -32001);
    ASSERT_TRUE(deserialized.message.has_value());
    EXPECT_EQ(deserialized.message.value(), "Task not found");
    ASSERT_TRUE(deserialized.data.has_value());
    EXPECT_EQ(deserialized.data.value(), "Additional info");
}

TEST_F(TypesSerializationTest, A2AErrorMinimalSerialization)
{
    A2A::A2AError error;
    error.code = -32600;

    json j = error;

    EXPECT_EQ(j["code"], -32600);
    EXPECT_EQ(std::string(j["message"]), "");
    EXPECT_FALSE(j.contains("data"));

    A2A::A2AError deserialized = j.get<A2A::A2AError>();
    EXPECT_EQ(deserialized.code, -32600);
    ASSERT_TRUE(deserialized.message.has_value());
    EXPECT_FALSE(deserialized.data.has_value());
}

TEST_F(TypesSerializationTest, InvalidRequestErrorSerialization)
{
    A2A::InvalidRequestError error;
    error.data = "Invalid JSON";

    json j = error;

    EXPECT_EQ(j["code"], -32600);
    EXPECT_EQ(std::string(j["message"]), "Request payload validation error");
    EXPECT_EQ(std::string(j["data"]), "Invalid JSON");

    A2A::InvalidRequestError deserialized = j.get<A2A::InvalidRequestError>();
    EXPECT_EQ(deserialized.code, -32600);
    ASSERT_TRUE(deserialized.message.has_value());
    EXPECT_EQ(deserialized.message.value(), "Request payload validation error");
    ASSERT_TRUE(deserialized.data.has_value());
    EXPECT_EQ(deserialized.data.value(), "Invalid JSON");
}

TEST_F(TypesSerializationTest, ServerErrorSerialization)
{
    A2A::ServerError error;

    json j = error;

    EXPECT_EQ(j["code"], -32603);
    EXPECT_EQ(std::string(j["message"]), "Server error");

    A2A::ServerError deserialized = j.get<A2A::ServerError>();
    EXPECT_EQ(deserialized.code, -32603);
    ASSERT_TRUE(deserialized.message.has_value());
    EXPECT_EQ(deserialized.message.value(), "Server error");
}

TEST_F(TypesSerializationTest, InvalidParamsErrorSerialization)
{
    A2A::InvalidParamsError error;

    json j = error;

    EXPECT_EQ(j["code"], -32602);
    EXPECT_EQ(std::string(j["message"]), "Invalid parameters");

    A2A::InvalidParamsError deserialized = j.get<A2A::InvalidParamsError>();
    EXPECT_EQ(deserialized.code, -32602);
    EXPECT_EQ(deserialized.message.value(), "Invalid parameters");
}

TEST_F(TypesSerializationTest, JSONParseErrorSerialization)
{
    A2A::JSONParseError error;

    json j = error;

    EXPECT_EQ(j["code"], -32700);
    EXPECT_EQ(std::string(j["message"]), "Invalid JSON payload");

    A2A::JSONParseError deserialized = j.get<A2A::JSONParseError>();
    EXPECT_EQ(deserialized.code, -32700);
    EXPECT_EQ(deserialized.message.value(), "Invalid JSON payload");
}

TEST_F(TypesSerializationTest, InternalErrorSerialization)
{
    A2A::InternalError error;

    json j = error;

    EXPECT_EQ(j["code"], -32603);
    EXPECT_EQ(std::string(j["message"]), "Internal error");

    A2A::InternalError deserialized = j.get<A2A::InternalError>();
    EXPECT_EQ(deserialized.code, -32603);
    EXPECT_EQ(deserialized.message.value(), "Internal error");
}

TEST_F(TypesSerializationTest, MethodNotFoundErrorSerialization)
{
    A2A::MethodNotFoundError error;

    json j = error;

    EXPECT_EQ(j["code"], -32601);
    EXPECT_EQ(std::string(j["message"]), "Method not found");

    A2A::MethodNotFoundError deserialized = j.get<A2A::MethodNotFoundError>();
    EXPECT_EQ(deserialized.code, -32601);
    EXPECT_EQ(deserialized.message.value(), "Method not found");
}

TEST_F(TypesSerializationTest, TaskNotFoundErrorSerialization)
{
    A2A::TaskNotFoundError error;

    json j = error;

    EXPECT_EQ(j["code"], -32001);
    EXPECT_EQ(std::string(j["message"]), "Task not found");

    A2A::TaskNotFoundError deserialized = j.get<A2A::TaskNotFoundError>();
    EXPECT_EQ(deserialized.code, -32001);
    EXPECT_EQ(deserialized.message.value(), "Task not found");
}

TEST_F(TypesSerializationTest, TaskNotCancelableErrorSerialization)
{
    A2A::TaskNotCancelableError error;

    json j = error;

    EXPECT_EQ(j["code"], -32002);
    EXPECT_EQ(std::string(j["message"]), "Task cannot be canceled");

    A2A::TaskNotCancelableError deserialized = j.get<A2A::TaskNotCancelableError>();
    EXPECT_EQ(deserialized.code, -32002);
    EXPECT_EQ(deserialized.message.value(), "Task cannot be canceled");
}

TEST_F(TypesSerializationTest, PushNotificationNotSupportedErrorSerialization)
{
    A2A::PushNotificationNotSupportedError error;

    json j = error;

    EXPECT_EQ(j["code"], -32003);
    EXPECT_EQ(std::string(j["message"]), "Push Notification is not supported");

    A2A::PushNotificationNotSupportedError deserialized = j.get<A2A::PushNotificationNotSupportedError>();
    EXPECT_EQ(deserialized.code, -32003);
    EXPECT_EQ(deserialized.message.value(), "Push Notification is not supported");
}

TEST_F(TypesSerializationTest, UnsupportedOperationErrorSerialization)
{
    A2A::UnsupportedOperationError error;

    json j = error;

    EXPECT_EQ(j["code"], -32004);
    EXPECT_EQ(std::string(j["message"]), "This operation is not supported");

    A2A::UnsupportedOperationError deserialized = j.get<A2A::UnsupportedOperationError>();
    EXPECT_EQ(deserialized.code, -32004);
    EXPECT_EQ(deserialized.message.value(), "This operation is not supported");
}

TEST_F(TypesSerializationTest, ContentTypeNotSupportedErrorSerialization)
{
    A2A::ContentTypeNotSupportedError error;

    json j = error;

    EXPECT_EQ(j["code"], -32005);
    EXPECT_EQ(std::string(j["message"]), "Incompatible content types");

    A2A::ContentTypeNotSupportedError deserialized = j.get<A2A::ContentTypeNotSupportedError>();
    EXPECT_EQ(deserialized.code, -32005);
    EXPECT_EQ(deserialized.message.value(), "Incompatible content types");
}

TEST_F(TypesSerializationTest, InvalidAgentResponseErrorSerialization)
{
    A2A::InvalidAgentResponseError error;

    json j = error;

    EXPECT_EQ(j["code"], -32006);
    EXPECT_EQ(std::string(j["message"]), "Invalid agent response");

    A2A::InvalidAgentResponseError deserialized = j.get<A2A::InvalidAgentResponseError>();
    EXPECT_EQ(deserialized.code, -32006);
    EXPECT_EQ(deserialized.message.value(), "Invalid agent response");
}

TEST_F(TypesSerializationTest, AuthenticatedExtendedCardNotConfiguredErrorSerialization)
{
    A2A::AuthenticatedExtendedCardNotConfiguredError error;

    json j = error;

    EXPECT_EQ(j["code"], -32007);
    EXPECT_EQ(std::string(j["message"]), "Authenticated Extended Card is not configured");

    A2A::AuthenticatedExtendedCardNotConfiguredError deserialized =
        j.get<A2A::AuthenticatedExtendedCardNotConfiguredError>();
    EXPECT_EQ(deserialized.code, -32007);
    EXPECT_EQ(deserialized.message.value(), "Authenticated Extended Card is not configured");
}

// ===========================================================================
// Security Scheme 序列化测试
// ===========================================================================

TEST_F(TypesSerializationTest, APIKeySecuritySchemeSerialization)
{
    A2A::APIKeySecurityScheme scheme;
    scheme.in_ = "header";
    scheme.name = "X-API-Key";
    scheme.type = "apiKey";
    scheme.description = "API Key authentication";

    json j = scheme;

    EXPECT_EQ(std::string(j["in"]), "header");
    EXPECT_EQ(std::string(j["name"]), "X-API-Key");
    EXPECT_EQ(std::string(j["type"]), "apiKey");
    EXPECT_EQ(std::string(j["description"]), "API Key authentication");

    A2A::APIKeySecurityScheme deserialized = j.get<A2A::APIKeySecurityScheme>();
    EXPECT_EQ(deserialized.in_, "header");
    EXPECT_EQ(deserialized.name, "X-API-Key");
    ASSERT_TRUE(deserialized.description.has_value());
    EXPECT_EQ(deserialized.description.value(), "API Key authentication");
}

TEST_F(TypesSerializationTest, HTTPAuthSecuritySchemeSerialization)
{
    A2A::HTTPAuthSecurityScheme scheme;
    scheme.scheme = "bearer";
    scheme.type = "http";
    scheme.description = "Bearer token authentication";

    json j = scheme;

    EXPECT_EQ(std::string(j["scheme"]), "bearer");
    EXPECT_EQ(std::string(j["type"]), "http");
    EXPECT_EQ(std::string(j["description"]), "Bearer token authentication");

    A2A::HTTPAuthSecurityScheme deserialized = j.get<A2A::HTTPAuthSecurityScheme>();
    EXPECT_EQ(deserialized.scheme, "bearer");
    ASSERT_TRUE(deserialized.description.has_value());
    EXPECT_EQ(deserialized.description.value(), "Bearer token authentication");
}

TEST_F(TypesSerializationTest, ImplicitOAuthFlowSerialization)
{
    A2A::ImplicitOAuthFlow flow;
    flow.authorizationUrl = "https://auth.example.com";
    flow.refreshUrl = "https://refresh.example.com";
    flow.scopes = {{"read", "Read access"}, {"write", "Write access"}};

    json j = flow;

    EXPECT_EQ(std::string(j["authorizationUrl"]), "https://auth.example.com");
    EXPECT_EQ(std::string(j["refreshUrl"]), "https://refresh.example.com");
    EXPECT_EQ(std::string(j["scopes"]["read"]), "Read access");
    EXPECT_EQ(std::string(j["scopes"]["write"]), "Write access");

    A2A::ImplicitOAuthFlow deserialized = j.get<A2A::ImplicitOAuthFlow>();
    EXPECT_EQ(deserialized.authorizationUrl, "https://auth.example.com");
    ASSERT_TRUE(deserialized.refreshUrl.has_value());
    EXPECT_EQ(deserialized.refreshUrl.value(), "https://refresh.example.com");
    EXPECT_EQ(deserialized.scopes.size(), 2);
}

TEST_F(TypesSerializationTest, AuthorizationCodeOAuthFlowSerialization)
{
    A2A::AuthorizationCodeOAuthFlow flow;
    flow.authorizationUrl = "https://auth.example.com";
    flow.tokenUrl = "https://token.example.com";
    flow.refreshUrl = "https://refresh.example.com";
    flow.scopes = {{"read", "Read access"}};

    json j = flow;

    EXPECT_EQ(std::string(j["authorizationUrl"]), "https://auth.example.com");
    EXPECT_EQ(std::string(j["tokenUrl"]), "https://token.example.com");
    EXPECT_EQ(std::string(j["refreshUrl"]), "https://refresh.example.com");
    EXPECT_EQ(std::string(j["scopes"]["read"]), "Read access");

    A2A::AuthorizationCodeOAuthFlow deserialized = j.get<A2A::AuthorizationCodeOAuthFlow>();
    EXPECT_EQ(deserialized.authorizationUrl, "https://auth.example.com");
    EXPECT_EQ(deserialized.tokenUrl, "https://token.example.com");
    EXPECT_EQ(deserialized.scopes.size(), 1);
}

TEST_F(TypesSerializationTest, PasswordOAuthFlowSerialization)
{
    A2A::PasswordOAuthFlow flow;
    flow.tokenUrl = "https://token.example.com";
    flow.refreshUrl = "https://refresh.example.com";
    flow.scopes = {{"read", "Read access"}};

    json j = flow;

    EXPECT_EQ(std::string(j["tokenUrl"]), "https://token.example.com");
    EXPECT_EQ(std::string(j["refreshUrl"]), "https://refresh.example.com");
    EXPECT_EQ(std::string(j["scopes"]["read"]), "Read access");

    A2A::PasswordOAuthFlow deserialized = j.get<A2A::PasswordOAuthFlow>();
    EXPECT_EQ(deserialized.tokenUrl, "https://token.example.com");
    EXPECT_EQ(deserialized.scopes.size(), 1);
}

TEST_F(TypesSerializationTest, ClientCredentialsOAuthFlowSerialization)
{
    A2A::ClientCredentialsOAuthFlow flow;
    flow.tokenUrl = "https://token.example.com";
    flow.refreshUrl = "https://refresh.example.com";
    flow.scopes = {{"read", "Read access"}};

    json j = flow;

    EXPECT_EQ(std::string(j["tokenUrl"]), "https://token.example.com");
    EXPECT_EQ(std::string(j["refreshUrl"]), "https://refresh.example.com");
    EXPECT_EQ(std::string(j["scopes"]["read"]), "Read access");

    A2A::ClientCredentialsOAuthFlow deserialized = j.get<A2A::ClientCredentialsOAuthFlow>();
    EXPECT_EQ(deserialized.tokenUrl, "https://token.example.com");
    EXPECT_EQ(deserialized.scopes.size(), 1);
}

TEST_F(TypesSerializationTest, OAuthFlowsSerialization)
{
    A2A::OAuthFlows flows;

    A2A::ImplicitOAuthFlow implicit;
    implicit.authorizationUrl = "https://auth.example.com";
    flows.implicit = implicit;

    A2A::AuthorizationCodeOAuthFlow authCode;
    authCode.authorizationUrl = "https://auth.example.com";
    authCode.tokenUrl = "https://token.example.com";
    flows.authorizationCode = authCode;

    json j = flows;

    EXPECT_TRUE(j.contains("implicit"));
    EXPECT_TRUE(j.contains("authorizationCode"));
    EXPECT_EQ(std::string(j["implicit"]["authorizationUrl"]), "https://auth.example.com");
    EXPECT_EQ(std::string(j["authorizationCode"]["tokenUrl"]), "https://token.example.com");

    A2A::OAuthFlows deserialized = j.get<A2A::OAuthFlows>();
    ASSERT_TRUE(deserialized.implicit.has_value());
    ASSERT_TRUE(deserialized.authorizationCode.has_value());
    EXPECT_EQ(deserialized.implicit.value().authorizationUrl, "https://auth.example.com");
}

TEST_F(TypesSerializationTest, OAuth2SecuritySchemeSerialization)
{
    A2A::OAuth2SecurityScheme scheme;
    scheme.type = "oauth2";
    scheme.description = "OAuth2 authentication";
    scheme.oauth2MetadataUrl = "https://metadata.example.com";

    A2A::OAuthFlows flows;
    A2A::ImplicitOAuthFlow implicit;
    implicit.authorizationUrl = "https://auth.example.com";
    flows.implicit = implicit;
    scheme.flows = flows;

    json j = scheme;

    EXPECT_EQ(std::string(j["type"]), "oauth2");
    EXPECT_EQ(std::string(j["description"]), "OAuth2 authentication");
    EXPECT_EQ(std::string(j["oauth2MetadataUrl"]), "https://metadata.example.com");
    EXPECT_TRUE(j.contains("flows"));

    A2A::OAuth2SecurityScheme deserialized = j.get<A2A::OAuth2SecurityScheme>();
    EXPECT_EQ(deserialized.type, "oauth2");
    ASSERT_TRUE(deserialized.description.has_value());
    EXPECT_EQ(deserialized.description.value(), "OAuth2 authentication");
    EXPECT_TRUE(deserialized.flows.implicit.has_value());
}

TEST_F(TypesSerializationTest, OpenIdConnectSecuritySchemeSerialization)
{
    A2A::OpenIdConnectSecurityScheme scheme;
    scheme.type = "openIdConnect";
    scheme.openIdConnectUrl = "https://auth.example.com/.well-known/openid-configuration";
    scheme.description = "OpenID Connect authentication";

    json j = scheme;

    EXPECT_EQ(std::string(j["type"]), "openIdConnect");
    EXPECT_EQ(std::string(j["openIdConnectUrl"]), "https://auth.example.com/.well-known/openid-configuration");
    EXPECT_EQ(std::string(j["description"]), "OpenID Connect authentication");

    A2A::OpenIdConnectSecurityScheme deserialized = j.get<A2A::OpenIdConnectSecurityScheme>();
    EXPECT_EQ(deserialized.openIdConnectUrl, "https://auth.example.com/.well-known/openid-configuration");
    ASSERT_TRUE(deserialized.description.has_value());
    EXPECT_EQ(deserialized.description.value(), "OpenID Connect authentication");
}

TEST_F(TypesSerializationTest, MutualTLSSecuritySchemeSerialization)
{
    A2A::MutualTLSSecurityScheme scheme;
    scheme.type = "mutualTLS";
    scheme.description = "mTLS authentication";

    json j = scheme;

    EXPECT_EQ(std::string(j["type"]), "mutualTLS");
    EXPECT_EQ(std::string(j["description"]), "mTLS authentication");

    A2A::MutualTLSSecurityScheme deserialized = j.get<A2A::MutualTLSSecurityScheme>();
    ASSERT_TRUE(deserialized.description.has_value());
    EXPECT_EQ(deserialized.description.value(), "mTLS authentication");
}

TEST_F(TypesSerializationTest, SecuritySchemeVariantSerialization)
{
    A2A::APIKeySecurityScheme apiKey;
    apiKey.in_ = "header";
    apiKey.name = "X-API-Key";

    A2A::SecurityScheme scheme1;
    scheme1.v = apiKey;

    json j1 = scheme1;
    EXPECT_EQ(std::string(j1["type"]), "apiKey");
    EXPECT_EQ(std::string(j1["in"]), "header");
    EXPECT_EQ(std::string(j1["name"]), "X-API-Key");

    A2A::SecurityScheme deserialized1 = j1.get<A2A::SecurityScheme>();
    EXPECT_TRUE(std::holds_alternative<A2A::APIKeySecurityScheme>(deserialized1.v));

    A2A::HTTPAuthSecurityScheme httpAuth;
    httpAuth.scheme = "bearer";

    A2A::SecurityScheme scheme2;
    scheme2.v = httpAuth;

    json j2 = scheme2;
    EXPECT_EQ(std::string(j2["type"]), "http");
    EXPECT_EQ(std::string(j2["scheme"]), "bearer");
}

TEST_F(TypesSerializationTest, SecuritySchemeUnknownType)
{
    json j = {{"type", "unknown"}, {"some", "field"}};
    EXPECT_THROW(j.get<A2A::SecurityScheme>(), std::runtime_error);
}

// ===========================================================================
// Agent 相关序列化测试
// ===========================================================================

TEST_F(TypesSerializationTest, AgentSkillSerialization)
{
    A2A::AgentSkill skill;
    skill.id = "skill-001";
    skill.name = "Weather Lookup";
    skill.description = "Get weather information";
    skill.tags = {"weather", "forecast"};
    skill.examples = std::vector<std::string>{"What's the weather in Beijing?"};
    skill.inputModes = std::vector<std::string>{"text/plain"};
    skill.outputModes = std::vector<std::string>{"text/plain"};
    skill.extension = "custom-extension";

    json j = skill;

    EXPECT_EQ(std::string(j["id"]), "skill-001");
    EXPECT_EQ(std::string(j["name"]), "Weather Lookup");
    EXPECT_EQ(std::string(j["description"]), "Get weather information");
    EXPECT_EQ(std::string(j["tags"][0]), "weather");
    EXPECT_EQ(std::string(j["examples"][0]), "What's the weather in Beijing?");
    EXPECT_EQ(std::string(j["inputModes"][0]), "text/plain");
    EXPECT_EQ(std::string(j["extension"]), "custom-extension");

    A2A::AgentSkill deserialized = j.get<A2A::AgentSkill>();
    EXPECT_EQ(deserialized.id, "skill-001");
    EXPECT_EQ(deserialized.name, "Weather Lookup");
    ASSERT_TRUE(deserialized.examples.has_value());
    EXPECT_EQ(deserialized.examples.value()[0], "What's the weather in Beijing?");
}

TEST_F(TypesSerializationTest, AgentSkillMissingRequiredFields)
{
    json j_no_id = {{"name", "test"}, {"description", "desc"}, {"tags", json::array()}};
    EXPECT_THROW(j_no_id.get<A2A::AgentSkill>(), std::runtime_error);

    json j_empty_id = {{"id", ""}, {"name", "test"}, {"description", "desc"}, {"tags", json::array()}};
    EXPECT_THROW(j_empty_id.get<A2A::AgentSkill>(), std::runtime_error);
}

TEST_F(TypesSerializationTest, AgentExtensionSerialization)
{
    A2A::AgentExtension ext;
    ext.uri = "https://example.com/extension";
    ext.required = true;
    ext.description = "Extension description";
    ext.params = "{\"key\": \"value\"}";

    json j = ext;

    EXPECT_EQ(std::string(j["uri"]), "https://example.com/extension");
    EXPECT_EQ(j["required"], true);
    EXPECT_EQ(std::string(j["description"]), "Extension description");
    EXPECT_EQ(std::string(j["params"]), "{\"key\": \"value\"}");

    A2A::AgentExtension deserialized = j.get<A2A::AgentExtension>();
    EXPECT_EQ(deserialized.uri, "https://example.com/extension");
    ASSERT_TRUE(deserialized.required.has_value());
    EXPECT_TRUE(deserialized.required.value());
    ASSERT_TRUE(deserialized.description.has_value());
    EXPECT_EQ(deserialized.description.value(), "Extension description");
}

TEST_F(TypesSerializationTest, AgentCapabilitiesSerialization)
{
    A2A::AgentCapabilities caps;
    caps.streaming = true;
    caps.pushNotifications = false;
    caps.extendedAgentCard = true;
    caps.extension = "custom";

    A2A::AgentExtension ext;
    ext.uri = "https://example.com/ext";
    caps.extensions = std::vector<A2A::AgentExtension>{ext};

    json j = caps;

    EXPECT_EQ(j["streaming"], true);
    EXPECT_EQ(j["pushNotifications"], false);
    EXPECT_EQ(j["extendedAgentCard"], true);
    EXPECT_EQ(std::string(j["extension"]), "custom");
    EXPECT_EQ(std::string(j["extensions"][0]["uri"]), "https://example.com/ext");

    A2A::AgentCapabilities deserialized = j.get<A2A::AgentCapabilities>();
    ASSERT_TRUE(deserialized.streaming.has_value());
    EXPECT_TRUE(deserialized.streaming.value());
    ASSERT_TRUE(deserialized.extensions.has_value());
    EXPECT_EQ(deserialized.extensions.value().size(), 1);
}

TEST_F(TypesSerializationTest, AgentProviderSerialization)
{
    A2A::AgentProvider provider;
    provider.organization = "Test Org";
    provider.url = "https://example.com";

    json j = provider;

    EXPECT_EQ(std::string(j["organization"]), "Test Org");
    EXPECT_EQ(std::string(j["url"]), "https://example.com");

    A2A::AgentProvider deserialized = j.get<A2A::AgentProvider>();
    EXPECT_EQ(deserialized.organization, "Test Org");
    EXPECT_EQ(deserialized.url, "https://example.com");
}

TEST_F(TypesSerializationTest, AgentProviderMissingRequiredFields)
{
    json j_no_org = {{"url", "https://example.com"}};
    EXPECT_THROW(j_no_org.get<A2A::AgentProvider>(), std::runtime_error);
}

// ===========================================================================
// AgentInterface 序列化测试
// ===========================================================================

TEST_F(TypesSerializationTest, AgentInterfaceSerialization)
{
    auto iface = CreateTestAgentInterface();
    iface.tenant = "test-tenant";

    json j = iface;

    EXPECT_EQ(std::string(j["url"]), "https://api.example.com/a2a");
    EXPECT_EQ(std::string(j["protocolBinding"]), "JSONRPC");
    EXPECT_EQ(std::string(j["protocolVersion"]), "2.0");
    EXPECT_EQ(std::string(j["tenant"]), "test-tenant");

    A2A::AgentInterface deserialized = j.get<A2A::AgentInterface>();
    EXPECT_EQ(deserialized.url, "https://api.example.com/a2a");
    EXPECT_EQ(deserialized.protocolBinding, "JSONRPC");
    EXPECT_EQ(deserialized.protocolVersion, "2.0");
    ASSERT_TRUE(deserialized.tenant.has_value());
    EXPECT_EQ(deserialized.tenant.value(), "test-tenant");
}

TEST_F(TypesSerializationTest, AgentInterfaceMinimalSerialization)
{
    auto iface = CreateTestAgentInterface();

    json j = iface;

    EXPECT_EQ(std::string(j["url"]), "https://api.example.com/a2a");
    EXPECT_EQ(std::string(j["protocolBinding"]), "JSONRPC");
    EXPECT_EQ(std::string(j["protocolVersion"]), "2.0");
    EXPECT_FALSE(j.contains("tenant"));

    A2A::AgentInterface deserialized = j.get<A2A::AgentInterface>();
    EXPECT_EQ(deserialized.url, "https://api.example.com/a2a");
    EXPECT_FALSE(deserialized.tenant.has_value());
}

TEST_F(TypesSerializationTest, AgentInterfaceMissingRequiredFields)
{
    json j_no_url = {{"protocolBinding", "JSONRPC"}, {"protocolVersion", "2.0"}};
    EXPECT_THROW(j_no_url.get<A2A::AgentInterface>(), std::runtime_error);

    json j_no_binding = {{"url", "https://example.com"}, {"protocolVersion", "2.0"}};
    EXPECT_THROW(j_no_binding.get<A2A::AgentInterface>(), std::runtime_error);

    json j_no_version = {{"url", "https://example.com"}, {"protocolBinding", "JSONRPC"}};
    EXPECT_THROW(j_no_version.get<A2A::AgentInterface>(), std::runtime_error);
}

// ===========================================================================
// AgentCard 序列化测试
// ===========================================================================

TEST_F(TypesSerializationTest, AgentCardFullSerialization)
{
    A2A::AgentCard card;
    card.name = "Test Agent";
    card.description = "Test agent description";
    card.version = "1.0.0";
    card.documentationUrl = "https://api.example.com/agent";
    card.capabilities = CreateTestAgentCapabilities();
    card.defaultInputModes = {"text/plain", "application/json"};
    card.defaultOutputModes = {"text/plain"};

    A2A::AgentProvider provider;
    provider.organization = "Test Organization";
    provider.url = "https://example.com";
    card.provider = provider;

    card.iconUrl = "https://example.com/icon.png";
    card.documentationUrl = "https://docs.example.com";
    card.category = "test-category";
    card.extension = "custom-extension";

    card.skills.push_back(CreateTestAgentSkill("skill-001"));
    card.skills.push_back(CreateTestAgentSkill("skill-002"));

    card.supportedInterfaces.push_back(CreateTestAgentInterface());

    A2A::AgentInterface wsInterface;
    wsInterface.url = "wss://api.example.com/ws";
    wsInterface.protocolBinding = "WEBSOCKET";
    wsInterface.protocolVersion = "1.0";
    card.supportedInterfaces.push_back(wsInterface);

    std::map<std::string, A2A::SecurityScheme> schemes;
    schemes["apiKey"] = CreateTestAPIKeySecurityScheme();
    card.securitySchemes = schemes;

    card.security = std::vector<std::string>{"apiKey"};

    json j = card;

    EXPECT_EQ(std::string(j["name"]), "Test Agent");
    EXPECT_EQ(std::string(j["description"]), "Test agent description");
    EXPECT_EQ(std::string(j["version"]), "1.0.0");
    EXPECT_TRUE(j.contains("capabilities"));
    EXPECT_EQ(j["defaultInputModes"].size(), 2);
    EXPECT_EQ(j["defaultOutputModes"].size(), 1);
    EXPECT_EQ(j["skills"].size(), 2);
    EXPECT_EQ(j["supportedInterfaces"].size(), 2);

    EXPECT_TRUE(j.contains("provider"));
    EXPECT_EQ(std::string(j["provider"]["organization"]), "Test Organization");
    EXPECT_EQ(std::string(j["iconUrl"]), "https://example.com/icon.png");
    EXPECT_EQ(std::string(j["documentationUrl"]), "https://docs.example.com");
    EXPECT_TRUE(j.contains("securitySchemes"));
    EXPECT_EQ(j["security"].size(), 1);
    EXPECT_EQ(std::string(j["category"]), "test-category");
    EXPECT_EQ(std::string(j["extension"]), "custom-extension");

    A2A::AgentCard deserialized = j.get<A2A::AgentCard>();
    EXPECT_EQ(deserialized.name, "Test Agent");
    EXPECT_EQ(deserialized.version, "1.0.0");
    ASSERT_TRUE(deserialized.provider.has_value());
    EXPECT_EQ(deserialized.provider.value().organization, "Test Organization");
    ASSERT_TRUE(deserialized.securitySchemes.has_value());
    EXPECT_EQ(deserialized.securitySchemes.value().size(), 1);
    EXPECT_EQ(deserialized.skills.size(), 2);
    EXPECT_EQ(deserialized.supportedInterfaces.size(), 2);
}

TEST_F(TypesSerializationTest, AgentCardMinimalSerialization)
{
    A2A::AgentCard card;
    card.name = "Minimal Agent";
    card.description = "Minimal description";
    card.version = "1.0.0";
    card.capabilities = A2A::AgentCapabilities();
    card.defaultInputModes = {"text/plain"};
    card.defaultOutputModes = {"text/plain"};
    card.skills = {};
    card.supportedInterfaces = {};

    json j = card;

    EXPECT_EQ(std::string(j["name"]), "Minimal Agent");
    EXPECT_FALSE(j.contains("provider"));
    EXPECT_FALSE(j.contains("iconUrl"));
    EXPECT_FALSE(j.contains("securitySchemes"));

    A2A::AgentCard deserialized = j.get<A2A::AgentCard>();
    EXPECT_EQ(deserialized.name, "Minimal Agent");
    EXPECT_FALSE(deserialized.provider.has_value());
}

TEST_F(TypesSerializationTest, AgentCardMissingRequiredFields)
{
    json j_no_name = {{"description", "test"}, {"version", "1.0"}};
    EXPECT_THROW(j_no_name.get<A2A::AgentCard>(), std::runtime_error);

    json j_no_version = {{"name", "test"}, {"description", "desc"}};
    EXPECT_THROW(j_no_version.get<A2A::AgentCard>(), std::runtime_error);
}

// ===========================================================================
// SendMessageSuccessResponse 序列化测试
// ===========================================================================

TEST_F(TypesSerializationTest, SendMessageSuccessResponseWithMessage)
{
    A2A::SendMessageSuccessResponse response;
    response.jsonrpc = "2.0";
    response.id = "12345";

    A2A::Message msg;
    msg.messageId = "msg-001";
    msg.role = A2A::Role::AGENT;
    response.result = msg;

    json j = response;

    EXPECT_EQ(std::string(j["jsonrpc"]), "2.0");
    EXPECT_EQ(std::string(j["id"]), "12345");
    EXPECT_TRUE(j["result"].contains("message"));
    EXPECT_TRUE(j["result"]["message"].contains("messageId"));
    EXPECT_EQ(std::string(j["result"]["message"]["messageId"]), "msg-001");

    A2A::SendMessageSuccessResponse deserialized = j.get<A2A::SendMessageSuccessResponse>();
    EXPECT_EQ(deserialized.jsonrpc, "2.0");
    ASSERT_TRUE(deserialized.id.has_value());
    EXPECT_EQ(deserialized.id.value(), "12345");
    EXPECT_TRUE(std::holds_alternative<A2A::Message>(deserialized.result));
    EXPECT_EQ(std::get<A2A::Message>(deserialized.result).messageId, "msg-001");
}

TEST_F(TypesSerializationTest, SendMessageSuccessResponseWithTask)
{
    A2A::SendMessageSuccessResponse response;
    response.jsonrpc = "2.0";

    A2A::Task task;
    task.id = "task-001";
    task.contextId = "ctx-001";
    A2A::TaskStatus status;
    status.state = A2A::TaskState::WORKING;
    task.status = status;
    response.result = task;

    json j = response;

    EXPECT_EQ(std::string(j["jsonrpc"]), "2.0");
    EXPECT_TRUE(j["result"].contains("task"));
    EXPECT_TRUE(j["result"]["task"].contains("id"));
    EXPECT_EQ(std::string(j["result"]["task"]["id"]), "task-001");

    A2A::SendMessageSuccessResponse deserialized = j.get<A2A::SendMessageSuccessResponse>();
    EXPECT_TRUE(std::holds_alternative<A2A::Task>(deserialized.result));
    EXPECT_EQ(std::get<A2A::Task>(deserialized.result).id, "task-001");
}

TEST_F(TypesSerializationTest, SendMessageSuccessResponseInvalidResult)
{
    json j = {
        {"jsonrpc", "2.0"},
        {"result", {{"unknown", "field"}}}
    };
    EXPECT_THROW(j.get<A2A::SendMessageSuccessResponse>(), std::runtime_error);
}

// ===========================================================================
// SendStreamingMessageSuccessResponse 序列化测试
// ===========================================================================

TEST_F(TypesSerializationTest, SendStreamingMessageSuccessResponseWithTask)
{
    A2A::SendStreamingMessageSuccessResponse response;
    response.jsonrpc = "2.0";

    A2A::Task task;
    task.id = "task-001";
    task.contextId = "ctx-001";
    response.result = task;

    json j = response;

    EXPECT_EQ(std::string(j["jsonrpc"]), "2.0");
    EXPECT_TRUE(j["result"].contains("task"));
    EXPECT_TRUE(j["result"]["task"].contains("id"));
    EXPECT_EQ(std::string(j["result"]["task"]["id"]), "task-001");

    A2A::SendStreamingMessageSuccessResponse deserialized = j.get<A2A::SendStreamingMessageSuccessResponse>();
    EXPECT_TRUE(std::holds_alternative<A2A::Task>(deserialized.result));
}

TEST_F(TypesSerializationTest, SendStreamingMessageSuccessResponseWithMessage)
{
    A2A::SendStreamingMessageSuccessResponse response;
    response.jsonrpc = "2.0";

    A2A::Message msg;
    msg.messageId = "msg-001";
    msg.role = A2A::Role::AGENT;
    response.result = msg;

    json j = response;

    EXPECT_EQ(std::string(j["jsonrpc"]), "2.0");
    EXPECT_TRUE(j["result"].contains("message"));
    EXPECT_TRUE(j["result"]["message"].contains("messageId"));

    A2A::SendStreamingMessageSuccessResponse deserialized = j.get<A2A::SendStreamingMessageSuccessResponse>();
    EXPECT_TRUE(std::holds_alternative<A2A::Message>(deserialized.result));
}

TEST_F(TypesSerializationTest, SendStreamingMessageSuccessResponseWithStatusUpdate)
{
    A2A::SendStreamingMessageSuccessResponse response;
    response.jsonrpc = "2.0";

    A2A::TaskStatusUpdateEvent event;
    event.taskId = "task-001";
    event.contextId = "ctx-001";
    A2A::TaskStatus status;
    status.state = A2A::TaskState::WORKING;
    event.status = status;
    response.result = event;

    json j = response;

    EXPECT_EQ(std::string(j["jsonrpc"]), "2.0");
    EXPECT_TRUE(j["result"].contains("statusUpdate"));
    EXPECT_EQ(std::string(j["result"]["statusUpdate"]["taskId"]), "task-001");

    A2A::SendStreamingMessageSuccessResponse deserialized = j.get<A2A::SendStreamingMessageSuccessResponse>();
    EXPECT_TRUE(std::holds_alternative<A2A::TaskStatusUpdateEvent>(deserialized.result));
}

TEST_F(TypesSerializationTest, SendStreamingMessageSuccessResponseWithArtifactUpdate)
{
    A2A::SendStreamingMessageSuccessResponse response;
    response.jsonrpc = "2.0";

    A2A::TaskArtifactUpdateEvent event;
    event.taskId = "task-001";
    event.contextId = "ctx-001";
    A2A::Artifact artifact;
    artifact.artifactId = "art-001";
    event.artifact = artifact;
    response.result = event;

    json j = response;

    EXPECT_EQ(std::string(j["jsonrpc"]), "2.0");
    EXPECT_TRUE(j["result"].contains("artifactUpdate"));
    EXPECT_EQ(std::string(j["result"]["artifactUpdate"]["artifact"]["artifactId"]), "art-001");

    A2A::SendStreamingMessageSuccessResponse deserialized = j.get<A2A::SendStreamingMessageSuccessResponse>();
    EXPECT_TRUE(std::holds_alternative<A2A::TaskArtifactUpdateEvent>(deserialized.result));
}

TEST_F(TypesSerializationTest, SendStreamingMessageSuccessResponseInvalidResult)
{
    json j = {
        {"jsonrpc", "2.0"},
        {"result", {{"unknown", "field"}}}
    };
    EXPECT_THROW(j.get<A2A::SendStreamingMessageSuccessResponse>(), std::runtime_error);
}

// ===========================================================================
// GetTaskSuccessResponse 序列化测试
// ===========================================================================

TEST_F(TypesSerializationTest, GetTaskSuccessResponseSerialization)
{
    A2A::GetTaskSuccessResponse response;
    response.jsonrpc = "2.0";
    response.id = "12345";

    A2A::Task task;
    task.id = "task-001";
    task.contextId = "ctx-001";
    A2A::TaskStatus status;
    status.state = A2A::TaskState::COMPLETED;
    task.status = status;
    response.result = task;

    json j = response;

    EXPECT_EQ(std::string(j["jsonrpc"]), "2.0");
    EXPECT_EQ(std::string(j["id"]), "12345");
    EXPECT_EQ(std::string(j["result"]["id"]), "task-001");

    A2A::GetTaskSuccessResponse deserialized = j.get<A2A::GetTaskSuccessResponse>();
    EXPECT_EQ(deserialized.result.id, "task-001");
    EXPECT_EQ(deserialized.result.status.state, A2A::TaskState::COMPLETED);
}

// ===========================================================================
// CancelTaskSuccessResponse 序列化测试
// ===========================================================================

TEST_F(TypesSerializationTest, CancelTaskSuccessResponseSerialization)
{
    A2A::CancelTaskSuccessResponse response;
    response.jsonrpc = "2.0";

    A2A::Task task;
    task.id = "task-001";
    task.contextId = "ctx-001";
    A2A::TaskStatus status;
    status.state = A2A::TaskState::CANCELED;
    task.status = status;
    response.result = task;

    json j = response;

    EXPECT_EQ(std::string(j["jsonrpc"]), "2.0");
    EXPECT_EQ(std::string(j["result"]["id"]), "task-001");
    EXPECT_EQ(std::string(j["result"]["status"]["state"]), "TASK_STATE_CANCELED");

    A2A::CancelTaskSuccessResponse deserialized = j.get<A2A::CancelTaskSuccessResponse>();
    EXPECT_EQ(deserialized.result.id, "task-001");
    EXPECT_EQ(deserialized.result.status.state, A2A::TaskState::CANCELED);
}

// ===========================================================================
// SetTaskPushNotificationConfigSuccessResponse 序列化测试
// ===========================================================================

TEST_F(TypesSerializationTest, SetTaskPushNotificationConfigSuccessResponseSerialization)
{
    A2A::SetTaskPushNotificationConfigSuccessResponse response;
    response.jsonrpc = "2.0";

    A2A::TaskPushNotificationConfig config;
    config.taskId = "task-001";
    A2A::PushNotificationConfig pushConfig;
    pushConfig.url = "https://example.com/notify";
    config.pushNotificationConfig = pushConfig;
    response.result = config;

    json j = response;

    EXPECT_EQ(std::string(j["jsonrpc"]), "2.0");
    EXPECT_EQ(std::string(j["result"]["taskId"]), "task-001");
    EXPECT_EQ(std::string(j["result"]["pushNotificationConfig"]["url"]), "https://example.com/notify");

    A2A::SetTaskPushNotificationConfigSuccessResponse deserialized =
        j.get<A2A::SetTaskPushNotificationConfigSuccessResponse>();
    EXPECT_EQ(deserialized.result.taskId, "task-001");
    EXPECT_EQ(deserialized.result.pushNotificationConfig.url, "https://example.com/notify");
}

// ===========================================================================
// GetTaskPushNotificationConfigSuccessResponse 序列化测试
// ===========================================================================

TEST_F(TypesSerializationTest, GetTaskPushNotificationConfigSuccessResponseSerialization)
{
    A2A::GetTaskPushNotificationConfigSuccessResponse response;
    response.jsonrpc = "2.0";

    A2A::TaskPushNotificationConfig config;
    config.taskId = "task-001";
    A2A::PushNotificationConfig pushConfig;
    pushConfig.url = "https://example.com/notify";
    config.pushNotificationConfig = pushConfig;
    response.result = config;

    json j = response;

    EXPECT_EQ(std::string(j["jsonrpc"]), "2.0");
    EXPECT_EQ(std::string(j["result"]["taskId"]), "task-001");

    A2A::GetTaskPushNotificationConfigSuccessResponse deserialized =
        j.get<A2A::GetTaskPushNotificationConfigSuccessResponse>();
    EXPECT_EQ(deserialized.result.taskId, "task-001");
}

// ===========================================================================
// ListTaskPushNotificationConfigSuccessResponse 序列化测试
// ===========================================================================

TEST_F(TypesSerializationTest, ListTaskPushNotificationConfigSuccessResponseSerialization)
{
    A2A::ListTaskPushNotificationConfigSuccessResponse response;
    response.jsonrpc = "2.0";

    std::vector<A2A::TaskPushNotificationConfig> configs;

    A2A::TaskPushNotificationConfig config1;
    config1.taskId = "task-001";
    A2A::PushNotificationConfig pushConfig1;
    pushConfig1.url = "https://example.com/notify1";
    config1.pushNotificationConfig = pushConfig1;
    configs.push_back(config1);

    A2A::TaskPushNotificationConfig config2;
    config2.taskId = "task-002";
    A2A::PushNotificationConfig pushConfig2;
    pushConfig2.url = "https://example.com/notify2";
    config2.pushNotificationConfig = pushConfig2;
    configs.push_back(config2);

    response.result = configs;

    json j = response;

    EXPECT_EQ(std::string(j["jsonrpc"]), "2.0");
    EXPECT_EQ(j["result"].size(), 2);
    EXPECT_EQ(std::string(j["result"][0]["taskId"]), "task-001");
    EXPECT_EQ(std::string(j["result"][1]["taskId"]), "task-002");

    A2A::ListTaskPushNotificationConfigSuccessResponse deserialized =
        j.get<A2A::ListTaskPushNotificationConfigSuccessResponse>();
    EXPECT_EQ(deserialized.result.size(), 2);
    EXPECT_EQ(deserialized.result[0].taskId, "task-001");
    EXPECT_EQ(deserialized.result[1].taskId, "task-002");
}

// ===========================================================================
// GetAgentCardSuccessResponse 序列化测试
// ===========================================================================

TEST_F(TypesSerializationTest, GetAgentCardSuccessResponseSerialization)
{
    A2A::GetAgentCardSuccessResponse response;
    response.jsonrpc = "2.0";

    A2A::AgentCard card;
    card.name = "Test Agent";
    card.description = "Test description";
    card.version = "1.0.0";
    card.documentationUrl = "https://api.example.com/agent";
    card.capabilities = A2A::AgentCapabilities();
    card.defaultInputModes = {"text/plain"};
    card.defaultOutputModes = {"text/plain"};
    card.skills = {};
    card.supportedInterfaces = {};

    response.result = card;

    json j = response;

    EXPECT_EQ(std::string(j["jsonrpc"]), "2.0");
    EXPECT_EQ(std::string(j["result"]["name"]), "Test Agent");

    A2A::GetAgentCardSuccessResponse deserialized = j.get<A2A::GetAgentCardSuccessResponse>();
    EXPECT_EQ(deserialized.result.name, "Test Agent");
}

} // namespace A2A::Test