/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_TYPES
#define A2A_TYPES

#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace a2a {
// Role
enum class Role { AGENT, USER };

inline void to_json(nlohmann::json& j, const Role& r)
{
    j = (r == Role::AGENT) ? "agent" : "user";
}

inline void from_json(const nlohmann::json& j, Role& r)
{
    const auto s = j.get<std::string>();
    if (s == "agent") {
        r = Role::AGENT;
    } else {
        r = Role::USER;
    }
}

struct ClientCallContext {
    // Arbitrary per-request state
    std::unordered_map<std::string, nlohmann::json> state;
    std::unordered_map<std::string, nlohmann::json> headers;
};

// FileWithBytes / FileWithUri
struct FileWithBytes {
    std::string bytes; // base64
    std::optional<std::string> mimeType;
    std::optional<std::string> name;
};

struct FileWithUri {
    std::optional<std::string> mimeType;
    std::optional<std::string> name;
    std::string uri;
};

inline void to_json(nlohmann::json& j, const FileWithBytes& f)
{
    j = nlohmann::json{{"bytes", f.bytes}};
    if (f.mimeType) {
        j["mimeType"] = *f.mimeType;
    }

    if (f.name) {
        j["name"] = *f.name;
    }
}

inline void from_json(const nlohmann::json& j, FileWithBytes& f)
{
    j.at("bytes").get_to(f.bytes);
    if (j.contains("mimeType")) {
        f.mimeType = j.at("mimeType").get<std::string>();
    }

    if (j.contains("name")) {
        f.name = j.at("name").get<std::string>();
    }
}

inline void to_json(nlohmann::json& j, const FileWithUri& f)
{
    j = nlohmann::json{};
    if (f.mimeType) {
        j["mimeType"] = *f.mimeType;
    }

    if (f.name) {
        j["name"] = *f.name;
    }

    j["uri"] = f.uri;
}

inline void from_json(const nlohmann::json& j, FileWithUri& f)
{
    if (j.contains("mimeType")) {
        f.mimeType = j.at("mimeType").get<std::string>();
    }

    if (j.contains("name")) {
        f.name = j.at("name").get<std::string>();
    }

    j.at("uri").get_to(f.uri);
}

// Parts
struct TextPart {
    std::string kind = "text";
    std::optional<nlohmann::json> metadata;
    std::string text;
};

struct DataPart {
    std::string kind = "data";
    std::optional<nlohmann::json> metadata;
    nlohmann::json data; // map-like JSON
};

struct FilePart {
    std::string kind = "file";
    std::optional<nlohmann::json> metadata;
    std::variant<FileWithBytes, FileWithUri> file;
};

using Part = std::variant<TextPart, DataPart, FilePart>;

// JSON for parts
inline void to_json(nlohmann::json& j, const TextPart& p)
{
    j = nlohmann::json{{"kind", p.kind}, {"text", p.text}};

    if (p.metadata) {
        j["metadata"] = *p.metadata;
    }
}

inline void from_json(const nlohmann::json& j, TextPart& p)
{
    if (j.contains("kind")) {
        p.kind = j.at("kind").get<std::string>();
    }

    if (j.contains("metadata")) {
        p.metadata = j.at("metadata");
    }

    j.at("text").get_to(p.text);
}

inline void to_json(nlohmann::json& j, const DataPart& p)
{
    j = nlohmann::json{{"kind", p.kind}, {"data", p.data}};

    if (p.metadata) {
        j["metadata"] = *p.metadata;
    }
}

inline void from_json(const nlohmann::json& j, DataPart& p)
{
    if (j.contains("kind")) {
        p.kind = j.at("kind").get<std::string>();
    }

    if (j.contains("metadata")) {
        p.metadata = j.at("metadata");
    }

    j.at("data").get_to(p.data);
}

inline void to_json(nlohmann::json& j, const FilePart& p)
{
    j = nlohmann::json{{"kind", p.kind}};

    if (p.metadata) {
        j["metadata"] = *p.metadata;
    }

    // file is a union, emit discriminated by presence of field
    std::visit(
        [&](auto&& f) {
            nlohmann::json jf;
            to_json(jf, f);
            j["file"] = jf;
        },
        p.file);
}

inline void from_json(const nlohmann::json& j, FilePart& p)
{
    if (j.contains("kind")) {
        p.kind = j.at("kind").get<std::string>();
    }

    if (j.contains("metadata")) {
        p.metadata = j.at("metadata");
    }

    const auto& jf = j.at("file");
    if (jf.contains("bytes")) {
        FileWithBytes fb;
        from_json(jf, fb);
        p.file = fb;
    } else {
        FileWithUri fu;
        from_json(jf, fu);
        p.file = fu;
    }
}

inline void to_json(nlohmann::json& j, const Part& p)
{
    std::visit([&](auto&& part) { to_json(j, part); }, p);
}

inline void from_json(const nlohmann::json& j, Part& p)
{
    const auto kind = j.value<std::string>("kind", "");
    if (kind == "text") {
        TextPart t;
        from_json(j, t);
        p = t;
        return;
    }

    if (kind == "data") {
        DataPart d;
        from_json(j, d);
        p = d;
        return;
    }

    if (kind == "file") {
        FilePart f;
        from_json(j, f);
        p = f;
        return;
    }

    throw std::runtime_error("UNKNOWN part kind: " + kind);
}

// Artifact
struct Artifact {
    std::string artifactId;
    std::optional<std::string> description;
    std::optional<std::vector<std::string>> extensions;
    std::optional<nlohmann::json> metadata;
    std::optional<std::string> name;
    std::vector<Part> parts;
};

inline void to_json(nlohmann::json& j, const Artifact& a)
{
    j = nlohmann::json{{"artifactId", a.artifactId}, {"parts", nlohmann::json::array()}};

    if (a.description) {
        j["description"] = *a.description;
    }

    if (a.extensions) {
        j["extensions"] = *a.extensions;
    }

    if (a.metadata) {
        j["metadata"] = *a.metadata;
    }

    if (a.name) {
        j["name"] = *a.name;
    }

    for (const auto& p : a.parts) {
        nlohmann::json jp;
        to_json(jp, p);
        j["parts"].push_back(jp);
    }
}

inline void from_json(const nlohmann::json& j, Artifact& a)
{
    j.at("artifactId").get_to(a.artifactId);
    if (j.contains("description")) {
        a.description = j.at("description").get<std::string>();
    }

    if (j.contains("extensions")) {
        a.extensions = j.at("extensions").get<std::vector<std::string>>();
    }

    if (j.contains("metadata")) {
        a.metadata = j.at("metadata");
    }

    if (j.contains("name")) {
        a.name = j.at("name").get<std::string>();
    }

    a.parts.clear();
    for (const auto& jp : j.at("parts")) {
        Part p;
        from_json(jp, p);
        a.parts.push_back(std::move(p));
    }
}

// Message
struct Message {
    std::optional<std::string> contextId;
    std::optional<std::vector<std::string>> extensions;
    std::string kind = "message";
    std::string messageId;
    std::optional<nlohmann::json> metadata;
    std::vector<Part> parts;
    std::optional<std::vector<std::string>> referenceTaskIds;
    Role role;
    std::optional<std::string> taskId;
};

inline void to_json(nlohmann::json& j, const Message& m)
{
    j = nlohmann::json{{"kind", m.kind},
                       {"messageId", m.messageId},
                       {"parts", nlohmann::json::array()},
                       {"role", m.role == Role::AGENT ? "agent" : "user"}};

    if (m.contextId) {
        j["contextId"] = *m.contextId;
    }

    if (m.extensions) {
        j["extensions"] = *m.extensions;
    }

    if (m.metadata) {
        j["metadata"] = *m.metadata;
    }

    if (m.referenceTaskIds) {
        j["referenceTaskIds"] = *m.referenceTaskIds;
    }

    if (m.taskId) {
        j["taskId"] = *m.taskId;
    }

    for (const auto& p : m.parts) {
        nlohmann::json jp;
        to_json(jp, p);
        j["parts"].push_back(jp);
    }
}

inline void from_json(const nlohmann::json& j, Message& m)
{
    if (j.contains("contextId")) {
        m.contextId = j.at("contextId").get<std::string>();
    }

    if (j.contains("extensions")) {
        m.extensions = j.at("extensions").get<std::vector<std::string>>();
    }

    if (j.contains("kind")) {
        m.kind = j.at("kind").get<std::string>();
    }

    j.at("messageId").get_to(m.messageId);
    if (j.contains("metadata")) {
        m.metadata = j.at("metadata");
    }

    m.parts.clear();
    for (const auto& jp : j.at("parts")) {
        Part p;
        from_json(jp, p);
        m.parts.push_back(std::move(p));
    }

    if (j.contains("referenceTaskIds")) {
        m.referenceTaskIds = j.at("referenceTaskIds").get<std::vector<std::string>>();
    }

    if (j.contains("role")) {
        from_json(j.at("role"), m.role);
    } else {
        m.role = Role::USER;
    }

    if (j.contains("taskId")) {
        m.taskId = j.at("taskId").get<std::string>();
    }
}

// Task state/status minimal subset for helper functions
enum class TaskState {
    SUBMITTED,
    WORKING,
    INPUT_REQUIRED,
    COMPLETED,
    CANCELED,
    FAILED,
    REJECTED,
    AUTH_REQUIRED,
    UNKNOWN
};

struct TaskStatus {
    std::optional<Message> message;
    TaskState state;
    std::optional<std::string> timestamp;
};

inline void to_json(nlohmann::json& j, const TaskStatus& s)
{
    auto stateToStr = [](TaskState st) {
        switch (st) {
            case TaskState::SUBMITTED:
                return "submitted";
            case TaskState::WORKING:
                return "working";
            case TaskState::INPUT_REQUIRED:
                return "input-required";
            case TaskState::COMPLETED:
                return "completed";
            case TaskState::CANCELED:
                return "canceled";
            case TaskState::FAILED:
                return "failed";
            case TaskState::REJECTED:
                return "rejected";
            case TaskState::AUTH_REQUIRED:
                return "auth-required";
            default:
                return "unknown";
        }
    };

    j = nlohmann::json{{"state", stateToStr(s.state)}};

    if (s.message) {
        j["message"] = *s.message;
    }

    if (s.timestamp) {
        j["timestamp"] = *s.timestamp;
    }
}

inline void from_json(const nlohmann::json& j, TaskStatus& s)
{
    auto str = j.value<std::string>("state", "unknown");
    if (str == "submitted") {
        s.state = TaskState::SUBMITTED;
    } else if (str == "working") {
        s.state = TaskState::WORKING;
    } else if (str == "input-required") {
        s.state = TaskState::INPUT_REQUIRED;
    } else if (str == "completed") {
        s.state = TaskState::COMPLETED;
    } else if (str == "canceled") {
        s.state = TaskState::CANCELED;
    } else if (str == "failed") {
        s.state = TaskState::FAILED;
    } else if (str == "rejected") {
        s.state = TaskState::REJECTED;
    } else if (str == "auth-required") {
        s.state = TaskState::AUTH_REQUIRED;
    } else {
        s.state = TaskState::UNKNOWN;
    }

    if (j.contains("message")) {
        s.message = j.at("message").get<Message>();
    }

    if (j.contains("timestamp")) {
        s.timestamp = j.at("timestamp").get<std::string>();
    }
}

struct Task {
    std::optional<std::vector<Artifact>> artifacts;
    std::string contextId;
    std::optional<std::vector<Message>> history;
    std::string id;
    std::string kind = "task";
    std::optional<nlohmann::json> metadata;
    TaskStatus status;
};

inline void to_json(nlohmann::json& j, const Task& t)
{
    j = nlohmann::json{{"contextId", t.contextId}, {"id", t.id}, {"kind", t.kind}, {"status", t.status}};

    if (t.artifacts) {
        j["artifacts"] = *t.artifacts;
    }

    if (t.history) {
        j["history"] = *t.history;
    }

    if (t.metadata) {
        j["metadata"] = *t.metadata;
    }
}

inline void from_json(const nlohmann::json& j, Task& t)
{
    j.at("contextId").get_to(t.contextId);
    j.at("id").get_to(t.id);

    if (j.contains("kind")) {
        t.kind = j.at("kind").get<std::string>();
    }

    if (j.contains("artifacts")) {
        t.artifacts = j.at("artifacts").get<std::vector<Artifact>>();
    }

    if (j.contains("history")) {
        t.history = j.at("history").get<std::vector<Message>>();
    }

    if (j.contains("metadata")) {
        t.metadata = j.at("metadata");
    }

    t.status = j.at("status").get<TaskStatus>();
}

// JSON-RPC error and responses (subset)
struct JSONRPCError {
    int code;
    std::optional<nlohmann::json> data;
    std::string message;
};

inline void to_json(nlohmann::json& j, const JSONRPCError& e)
{
    j = nlohmann::json{{"code", e.code}, {"message", e.message}};

    if (e.data) {
        j["data"] = *e.data;
    }
}

inline void from_json(const nlohmann::json& j, JSONRPCError& e)
{
    j.at("code").get_to(e.code);
    j.at("message").get_to(e.message);
    if (j.contains("data")) {
        e.data = j.at("data");
    }
}

struct JSONRPCErrorResponse {
    nlohmann::json id;
    std::string jsonrpc = "2.0";
    JSONRPCError error;
};

inline void to_json(nlohmann::json& j, const JSONRPCErrorResponse& r)
{
    j = nlohmann::json{{"jsonrpc", r.jsonrpc}, {"id", r.id}, {"error", r.error}};
}

inline void from_json(const nlohmann::json& j, JSONRPCErrorResponse& r)
{
    r.id = j.at("id");
    if (j.contains("jsonrpc")) {
        r.jsonrpc = j.at("jsonrpc").get<std::string>();
    }

    r.error = j.at("error").get<JSONRPCError>();
}

// Minimal parameter and event types used by helpers
struct MessageSendParams {
    std::optional<nlohmann::json> configuration;
    Message message;
    std::optional<nlohmann::json> metadata;
};

inline void to_json(nlohmann::json& j, const MessageSendParams& p)
{
    j = nlohmann::json{{"message", p.message}};

    if (p.configuration) {
        j["configuration"] = *p.configuration;
    }

    if (p.metadata) {
        j["metadata"] = *p.metadata;
    }
}

inline void from_json(const nlohmann::json& j, MessageSendParams& p)
{
    p.message = j.at("message").get<Message>();
    if (j.contains("configuration")) {
        p.configuration = j.at("configuration");
    }

    if (j.contains("metadata")) {
        p.metadata = j.at("metadata");
    }
}

struct TaskArtifactUpdateEvent {
    std::optional<bool> append;
    Artifact artifact;
    std::string contextId;
    std::string kind = "artifact-update";
    std::optional<bool> lastChunk;
    std::optional<nlohmann::json> metadata;
    std::string taskId;
};

inline void to_json(nlohmann::json& j, const TaskArtifactUpdateEvent& e)
{
    j = nlohmann::json{{"artifact", e.artifact}, {"contextId", e.contextId}, {"kind", e.kind}, {"taskId", e.taskId}};

    if (e.append) {
        j["append"] = *e.append;
    }

    if (e.lastChunk) {
        j["lastChunk"] = *e.lastChunk;
    }

    if (e.metadata) {
        j["metadata"] = *e.metadata;
    }
}

inline void from_json(const nlohmann::json& j, TaskArtifactUpdateEvent& e)
{
    if (j.contains("append")) {
        e.append = j.at("append").get<bool>();
    }

    e.artifact = j.at("artifact").get<Artifact>();
    j.at("contextId").get_to(e.contextId);
    if (j.contains("kind")) {
        e.kind = j.at("kind").get<std::string>();
    }

    if (j.contains("lastChunk")) {
        e.lastChunk = j.at("lastChunk").get<bool>();
    }

    if (j.contains("metadata")) {
        e.metadata = j.at("metadata");
    }

    j.at("taskId").get_to(e.taskId);
}

struct TaskStatusUpdateEvent {
    std::string contextId;
    bool final;
    std::string kind = "status-update";
    std::optional<nlohmann::json> metadata;
    TaskStatus status;
    std::string taskId;
};

inline void to_json(nlohmann::json& j, const TaskStatusUpdateEvent& e)
{
    j = nlohmann::json{
        {"contextId", e.contextId}, {"final", e.final}, {"kind", e.kind}, {"status", e.status}, {"taskId", e.taskId}};

    if (e.metadata) {
        j["metadata"] = *e.metadata;
    }
}

inline void from_json(const nlohmann::json& j, TaskStatusUpdateEvent& e)
{
    j.at("contextId").get_to(e.contextId);
    j.at("final").get_to(e.final);
    if (j.contains("kind")) {
        e.kind = j.at("kind").get<std::string>();
    }

    if (j.contains("metadata")) {
        e.metadata = j.at("metadata");
    }

    e.status = j.at("status").get<TaskStatus>();
    j.at("taskId").get_to(e.taskId);
}

// ---- Push Notifications and Task params ----
struct PushNotificationAuthenticationInfo {
    std::optional<std::string> credentials;
    std::vector<std::string> schemes;
};

inline void to_json(nlohmann::json& j, const PushNotificationAuthenticationInfo& i)
{
    j = {{"schemes", i.schemes}};
    if (i.credentials) {
        j["credentials"] = *i.credentials;
    }
}

inline void from_json(const nlohmann::json& j, PushNotificationAuthenticationInfo& i)
{
    if (j.contains("credentials")) {
        i.credentials = j.at("credentials").get<std::string>();
    }

    j.at("schemes").get_to(i.schemes);
}

struct PushNotificationConfig {
    std::optional<PushNotificationAuthenticationInfo> authentication;
    std::optional<std::string> id;
    std::optional<std::string> token;
    std::string url;
};

inline void to_json(nlohmann::json& j, const PushNotificationConfig& c)
{
    j = {{"url", c.url}};
    if (c.authentication) {
        j["authentication"] = *c.authentication;
    }

    if (c.id) {
        j["id"] = *c.id;
    }

    if (c.token) {
        j["token"] = *c.token;
    }
}

inline void from_json(const nlohmann::json& j, PushNotificationConfig& c)
{
    if (j.contains("authentication")) {
        c.authentication = j.at("authentication").get<PushNotificationAuthenticationInfo>();
    }

    if (j.contains("id")) {
        c.id = j.at("id").get<std::string>();
    }

    if (j.contains("token")) {
        c.token = j.at("token").get<std::string>();
    }

    j.at("url").get_to(c.url);
}

struct TaskIdParams {
    std::string id;
    std::optional<nlohmann::json> metadata;
};

inline void to_json(nlohmann::json& j, const TaskIdParams& p)
{
    j = {{"id", p.id}};
    if (p.metadata) {
        j["metadata"] = *p.metadata;
    }
}

inline void from_json(const nlohmann::json& j, TaskIdParams& p)
{
    j.at("id").get_to(p.id);
    if (j.contains("metadata")) {
        p.metadata = j.at("metadata");
    }
}

struct TaskQueryParams {
    std::optional<int> historyLength;
    std::string id;
    std::optional<nlohmann::json> metadata;
};

inline void to_json(nlohmann::json& j, const TaskQueryParams& p)
{
    j = {{"id", p.id}};
    if (p.historyLength) {
        j["historyLength"] = *p.historyLength;
    }

    if (p.metadata) {
        j["metadata"] = *p.metadata;
    }
}

inline void from_json(const nlohmann::json& j, TaskQueryParams& p)
{
    if (j.contains("historyLength")) {
        p.historyLength = j.at("historyLength").get<int>();
    }

    j.at("id").get_to(p.id);
    if (j.contains("metadata")) {
        p.metadata = j.at("metadata");
    }
}

struct TaskPushNotificationConfig {
    PushNotificationConfig pushNotificationConfig;
    std::string taskId;
};

inline void to_json(nlohmann::json& j, const TaskPushNotificationConfig& c)
{
    j = {{"pushNotificationConfig", c.pushNotificationConfig}, {"taskId", c.taskId}};
}

inline void from_json(const nlohmann::json& j, TaskPushNotificationConfig& c)
{
    c.pushNotificationConfig = j.at("pushNotificationConfig").get<PushNotificationConfig>();
    j.at("taskId").get_to(c.taskId);
}

// GetTaskPushNotificationConfig parameters
struct GetTaskPushNotificationConfigParams {
    std::string id;
    std::optional<nlohmann::json> metadata;
    std::optional<std::string> pushNotificationConfigId;
};

inline void to_json(nlohmann::json& j, const GetTaskPushNotificationConfigParams& p)
{
    j = {{"id", p.id}};
    if (p.metadata) {
        j["metadata"] = *p.metadata;
    }

    if (p.pushNotificationConfigId) {
        j["pushNotificationConfigId"] = *p.pushNotificationConfigId;
    }
}

inline void from_json(const nlohmann::json& j, GetTaskPushNotificationConfigParams& p)
{
    j.at("id").get_to(p.id);
    if (j.contains("metadata")) {
        p.metadata = j.at("metadata");
    }

    if (j.contains("pushNotificationConfigId")) {
        p.pushNotificationConfigId = j.at("pushNotificationConfigId").get<std::string>();
    }
}

// List&  Delete push notification configs
struct ListTaskPushNotificationConfigParams {
    std::string id;
    std::optional<nlohmann::json> metadata;
};

inline void to_json(nlohmann::json& j, const ListTaskPushNotificationConfigParams& p)
{
    j = {{"id", p.id}};
    if (p.metadata) {
        j["metadata"] = *p.metadata;
    }
}

inline void from_json(const nlohmann::json& j, ListTaskPushNotificationConfigParams& p)
{
    j.at("id").get_to(p.id);
    if (j.contains("metadata")) {
        p.metadata = j.at("metadata");
    }
}

struct DeleteTaskPushNotificationConfigParams {
    std::string id;
    std::optional<nlohmann::json> metadata;
    std::string pushNotificationConfigId;
};

inline void to_json(nlohmann::json& j, const DeleteTaskPushNotificationConfigParams& p)
{
    j = {{"id", p.id}, {"pushNotificationConfigId", p.pushNotificationConfigId}};

    if (p.metadata) {
        j["metadata"] = *p.metadata;
    }
}

inline void from_json(const nlohmann::json& j, DeleteTaskPushNotificationConfigParams& p)
{
    j.at("id").get_to(p.id);
    if (j.contains("metadata")) {
        p.metadata = j.at("metadata");
    }

    j.at("pushNotificationConfigId").get_to(p.pushNotificationConfigId);
}

// ---- Requests/Responses (subset, enough to wire server/client) ----
struct SendMessageRequest {
    nlohmann::json id;
    std::string jsonrpc = "2.0";
    std::string method = "message/send";
    MessageSendParams params;
};

inline void to_json(nlohmann::json& j, const SendMessageRequest& r)
{
    j = {{"jsonrpc", r.jsonrpc}, {"id", r.id}, {"method", r.method}, {"params", r.params}};
}

inline void from_json(const nlohmann::json& j, SendMessageRequest& r)
{
    r.id = j.at("id");
    if (j.contains("jsonrpc")) {
        r.jsonrpc = j.at("jsonrpc").get<std::string>();
    }

    if (j.contains("method")) {
        r.method = j.at("method").get<std::string>();
    }

    r.params = j.at("params").get<MessageSendParams>();
}

struct SendMessageSuccessResponse {
    std::optional<nlohmann::json> id;
    std::string jsonrpc = "2.0";
    std::variant<Task, Message> result;
};

inline void to_json(nlohmann::json& j, const SendMessageSuccessResponse& r)
{
    j = {{"jsonrpc", r.jsonrpc},
         {"result", std::holds_alternative<Message>(r.result) ? nlohmann::json(std::get<Message>(r.result))
                                                              : nlohmann::json(std::get<Task>(r.result))}};

    if (r.id) {
        j["id"] = *r.id;
    }
}

inline void from_json(const nlohmann::json& j, SendMessageSuccessResponse& r)
{
    if (j.contains("id")) {
        r.id = j.at("id");
    }

    if (j.contains("jsonrpc")) {
        r.jsonrpc = j.at("jsonrpc").get<std::string>();
    }

    const auto& res = j.at("result");
    if (res.contains("kind") && res.at("kind") == "message") {
        r.result = res.get<Message>();
    } else {
        r.result = res.get<Task>();
    }
}

struct GetTaskRequest {
    nlohmann::json id;
    std::string jsonrpc = "2.0";
    std::string method = "tasks/get";
    TaskIdParams params;
};

inline void to_json(nlohmann::json& j, const GetTaskRequest& r)
{
    j = {{"jsonrpc", r.jsonrpc}, {"id", r.id}, {"method", r.method}, {"params", r.params}};
}

inline void from_json(const nlohmann::json& j, GetTaskRequest& r)
{
    r.id = j.at("id");
    if (j.contains("jsonrpc")) {
        r.jsonrpc = j.at("jsonrpc").get<std::string>();
    }

    if (j.contains("method")) {
        r.method = j.at("method").get<std::string>();
    }

    r.params = j.at("params").get<TaskIdParams>();
}

struct GetTaskSuccessResponse {
    std::optional<nlohmann::json> id;
    std::string jsonrpc = "2.0";
    Task result;
};

inline void to_json(nlohmann::json& j, const GetTaskSuccessResponse& r)
{
    j = {{"jsonrpc", r.jsonrpc}, {"result", r.result}};

    if (r.id) {
        j["id"] = *r.id;
    }
}

inline void from_json(const nlohmann::json& j, GetTaskSuccessResponse& r)
{
    if (j.contains("id")) {
        r.id = j.at("id");
    }

    if (j.contains("jsonrpc")) {
        r.jsonrpc = j.at("jsonrpc").get<std::string>();
    }

    r.result = j.at("result").get<Task>();
}

struct CancelTaskRequest {
    nlohmann::json id;
    std::string jsonrpc = "2.0";
    std::string method = "tasks/cancel";
    TaskIdParams params;
};

inline void to_json(nlohmann::json& j, const CancelTaskRequest& r)
{
    j = {{"jsonrpc", r.jsonrpc}, {"id", r.id}, {"method", r.method}, {"params", r.params}};
}

inline void from_json(const nlohmann::json& j, CancelTaskRequest& r)
{
    r.id = j.at("id");
    if (j.contains("jsonrpc")) {
        r.jsonrpc = j.at("jsonrpc").get<std::string>();
    }

    if (j.contains("method")) {
        r.method = j.at("method").get<std::string>();
    }

    r.params = j.at("params").get<TaskIdParams>();
}

struct CancelTaskSuccessResponse {
    std::optional<nlohmann::json> id;
    std::string jsonrpc = "2.0";
    Task result;
};

inline void to_json(nlohmann::json& j, const CancelTaskSuccessResponse& r)
{
    j = {{"jsonrpc", r.jsonrpc}, {"result", r.result}};

    if (r.id) {
        j["id"] = *r.id;
    }
}

inline void from_json(const nlohmann::json& j, CancelTaskSuccessResponse& r)
{
    if (j.contains("id")) {
        r.id = j.at("id");
    }

    if (j.contains("jsonrpc")) {
        r.jsonrpc = j.at("jsonrpc").get<std::string>();
    }

    r.result = j.at("result").get<Task>();
}

// ---- Error models (A2A + JSON-RPC) ----
struct JSONParseError {
    int code = -32700;
    std::optional<nlohmann::json> data;
    std::optional<std::string> message = std::string("Invalid JSON payload");
};

struct InvalidRequestError {
    int code = -32600;
    std::optional<nlohmann::json> data;
    std::optional<std::string> message = std::string("Request payload validation error");
};

struct InvalidParamsError {
    int code = -32602;
    std::optional<nlohmann::json> data;
    std::optional<std::string> message = std::string("Invalid parameters");
};

struct InternalError {
    int code = -32603;
    std::optional<nlohmann::json> data;
    std::optional<std::string> message = std::string("Internal error");
};

struct MethodNotFoundError {
    int code = -32601;
    std::optional<nlohmann::json> data;
    std::optional<std::string> message = std::string("Method not found");
};

struct TaskNotFoundError {
    int code = -32001;
    std::optional<nlohmann::json> data;
    std::optional<std::string> message = std::string("Task not found");
};

struct TaskNotCancelableError {
    int code = -32002;
    std::optional<nlohmann::json> data;
    std::optional<std::string> message = std::string("Task cannot be canceled");
};

struct PushNotificationNotSupportedError {
    int code = -32003;
    std::optional<nlohmann::json> data;
    std::optional<std::string> message = std::string("Push Notification is not supported");
};

struct UnsupportedOperationErrorModel {
    int code = -32004;
    std::optional<nlohmann::json> data;
    std::optional<std::string> message = std::string("This operation is not supported");
};

struct ContentTypeNotSupportedError {
    int code = -32005;
    std::optional<nlohmann::json> data;
    std::optional<std::string> message = std::string("Incompatible content types");
};

struct InvalidAgentResponseError {
    int code = -32006;
    std::optional<nlohmann::json> data;
    std::optional<std::string> message = std::string("Invalid agent response");
};

struct AuthenticatedExtendedCardNotConfiguredError {
    int code = -32007;
    std::optional<nlohmann::json> data;
    std::optional<std::string> message = std::string("Authenticated Extended Card is not configured");
};

inline void to_json(nlohmann::json& j, const JSONParseError& e)
{
    j = {{"code", e.code}, {"message", e.message.value_or("")}};

    if (e.data) {
        j["data"] = *e.data;
    }
}

inline void from_json(const nlohmann::json& j, JSONParseError& e)
{
    if (j.contains("data")) {
        e.data = j.at("data");
    }

    if (j.contains("message")) {
        e.message = j.at("message").get<std::string>();
    }
}

inline void to_json(nlohmann::json& j, const InvalidRequestError& e)
{
    j = {{"code", e.code}, {"message", e.message.value_or("")}};

    if (e.data) {
        j["data"] = *e.data;
    }
}

inline void from_json(const nlohmann::json& j, InvalidRequestError& e)
{
    if (j.contains("data")) {
        e.data = j.at("data");
    }

    if (j.contains("message")) {
        e.message = j.at("message").get<std::string>();
    }
}

inline void to_json(nlohmann::json& j, const InvalidParamsError& e)
{
    j = {{"code", e.code}, {"message", e.message.value_or("")}};

    if (e.data) {
        j["data"] = *e.data;
    }
}

inline void from_json(const nlohmann::json& j, InvalidParamsError& e)
{
    if (j.contains("data")) {
        e.data = j.at("data");
    }

    if (j.contains("message")) {
        e.message = j.at("message").get<std::string>();
    }
}

inline void to_json(nlohmann::json& j, const InternalError& e)
{
    j = {{"code", e.code}, {"message", e.message.value_or("")}};

    if (e.data) {
        j["data"] = *e.data;
    }
}

inline void from_json(const nlohmann::json& j, InternalError& e)
{
    if (j.contains("data")) {
        e.data = j.at("data");
    }

    if (j.contains("message")) {
        e.message = j.at("message").get<std::string>();
    }
}

inline void to_json(nlohmann::json& j, const MethodNotFoundError& e)
{
    j = {{"code", e.code}, {"message", e.message.value_or("")}};

    if (e.data) {
        j["data"] = *e.data;
    }
}

inline void from_json(const nlohmann::json& j, MethodNotFoundError& e)
{
    if (j.contains("data")) {
        e.data = j.at("data");
    }

    if (j.contains("message")) {
        e.message = j.at("message").get<std::string>();
    }
}

inline void to_json(nlohmann::json& j, const TaskNotFoundError& e)
{
    j = {{"code", e.code}, {"message", e.message.value_or("")}};

    if (e.data) {
        j["data"] = *e.data;
    }
}

inline void from_json(const nlohmann::json& j, TaskNotFoundError& e)
{
    if (j.contains("data")) {
        e.data = j.at("data");
    }

    if (j.contains("message")) {
        e.message = j.at("message").get<std::string>();
    }
}

inline void to_json(nlohmann::json& j, const TaskNotCancelableError& e)
{
    j = {{"code", e.code}, {"message", e.message.value_or("")}};

    if (e.data) {
        j["data"] = *e.data;
    }
}

inline void from_json(const nlohmann::json& j, TaskNotCancelableError& e)
{
    if (j.contains("data")) {
        e.data = j.at("data");
    }

    if (j.contains("message")) {
        e.message = j.at("message").get<std::string>();
    }
}

inline void to_json(nlohmann::json& j, const PushNotificationNotSupportedError& e)
{
    j = {{"code", e.code}, {"message", e.message.value_or("")}};

    if (e.data) {
        j["data"] = *e.data;
    }
}

inline void from_json(const nlohmann::json& j, PushNotificationNotSupportedError& e)
{
    if (j.contains("data")) {
        e.data = j.at("data");
    }

    if (j.contains("message")) {
        e.message = j.at("message").get<std::string>();
    }
}

inline void to_json(nlohmann::json& j, const UnsupportedOperationErrorModel& e)
{
    j = {{"code", e.code}, {"message", e.message.value_or("")}};

    if (e.data) {
        j["data"] = *e.data;
    }
}

inline void from_json(const nlohmann::json& j, UnsupportedOperationErrorModel& e)
{
    if (j.contains("data")) {
        e.data = j.at("data");
    }

    if (j.contains("message")) {
        e.message = j.at("message").get<std::string>();
    }
}

inline void to_json(nlohmann::json& j, const ContentTypeNotSupportedError& e)
{
    j = {{"code", e.code}, {"message", e.message.value_or("")}};

    if (e.data) {
        j["data"] = *e.data;
    }
}

inline void from_json(const nlohmann::json& j, ContentTypeNotSupportedError& e)
{
    if (j.contains("data")) {
        e.data = j.at("data");
    }

    if (j.contains("message")) {
        e.message = j.at("message").get<std::string>();
    }
}

inline void to_json(nlohmann::json& j, const InvalidAgentResponseError& e)
{
    j = {{"code", e.code}, {"message", e.message.value_or("")}};

    if (e.data) {
        j["data"] = *e.data;
    }
}

inline void from_json(const nlohmann::json& j, InvalidAgentResponseError& e)
{
    if (j.contains("data")) {
        e.data = j.at("data");
    }

    if (j.contains("message")) {
        e.message = j.at("message").get<std::string>();
    }
}

inline void to_json(nlohmann::json& j, const AuthenticatedExtendedCardNotConfiguredError& e)
{
    j = {{"code", e.code}, {"message", e.message.value_or("")}};

    if (e.data) {
        j["data"] = *e.data;
    }
}

inline void from_json(const nlohmann::json& j, AuthenticatedExtendedCardNotConfiguredError& e)
{
    if (j.contains("data")) {
        e.data = j.at("data");
    }

    if (j.contains("message")) {
        e.message = j.at("message").get<std::string>();
    }
}

// ---- Security Schemes ----
struct APIKeySecurityScheme {
    std::optional<std::string> description;
    std::string in_; // cookie|header|query
    std::string name;
    std::string type = "apiKey";
};

inline void to_json(nlohmann::json& j, const APIKeySecurityScheme& s)
{
    j = nlohmann::json{{"in", s.in_}, {"name", s.name}, {"type", s.type}};

    if (s.description) {
        j["description"] = *s.description;
    }
}

inline void from_json(const nlohmann::json& j, APIKeySecurityScheme& s)
{
    if (j.contains("description")) {
        s.description = j.at("description").get<std::string>();
    }

    j.at("in").get_to(s.in_);
    j.at("name").get_to(s.name);
    if (j.contains("type")) {
        s.type = j.at("type").get<std::string>();
    }
}

struct HTTPAuthSecurityScheme {
    std::optional<std::string> description;
    std::string scheme;
    std::string type = "http";
};

inline void to_json(nlohmann::json& j, const HTTPAuthSecurityScheme& s)
{
    j = {{"scheme", s.scheme}, {"type", s.type}};

    if (s.description) {
        j["description"] = *s.description;
    }
}

inline void from_json(const nlohmann::json& j, HTTPAuthSecurityScheme& s)
{
    if (j.contains("description")) {
        s.description = j.at("description").get<std::string>();
    }

    j.at("scheme").get_to(s.scheme);
    if (j.contains("type")) {
        s.type = j.at("type").get<std::string>();
    }
}

struct ImplicitOAuthFlow {
    std::string authorizationUrl;
    std::optional<std::string> refreshUrl;
    std::map<std::string, std::string> scopes;
};

inline void to_json(nlohmann::json& j, const ImplicitOAuthFlow& f)
{
    j = {{"authorizationUrl", f.authorizationUrl}, {"scopes", f.scopes}};

    if (f.refreshUrl) {
        j["refreshUrl"] = *f.refreshUrl;
    }
}

inline void from_json(const nlohmann::json& j, ImplicitOAuthFlow& f)
{
    j.at("authorizationUrl").get_to(f.authorizationUrl);
    if (j.contains("refreshUrl")) {
        f.refreshUrl = j.at("refreshUrl").get<std::string>();
    }

    j.at("scopes").get_to(f.scopes);
}

struct AuthorizationCodeOAuthFlow {
    std::string authorizationUrl;
    std::optional<std::string> refreshUrl;
    std::map<std::string, std::string> scopes;
    std::string tokenUrl;
};

inline void to_json(nlohmann::json& j, const AuthorizationCodeOAuthFlow& f)
{
    j = {{"authorizationUrl", f.authorizationUrl}, {"scopes", f.scopes}, {"tokenUrl", f.tokenUrl}};

    if (f.refreshUrl) {
        j["refreshUrl"] = *f.refreshUrl;
    }
}

inline void from_json(const nlohmann::json& j, AuthorizationCodeOAuthFlow& f)
{
    j.at("authorizationUrl").get_to(f.authorizationUrl);
    if (j.contains("refreshUrl")) {
        f.refreshUrl = j.at("refreshUrl").get<std::string>();
    }

    j.at("scopes").get_to(f.scopes);
    j.at("tokenUrl").get_to(f.tokenUrl);
}

struct PasswordOAuthFlow {
    std::optional<std::string> refreshUrl;
    std::map<std::string, std::string> scopes;
    std::string tokenUrl;
};

inline void to_json(nlohmann::json& j, const PasswordOAuthFlow& f)
{
    j = {{"scopes", f.scopes}, {"tokenUrl", f.tokenUrl}};

    if (f.refreshUrl) {
        j["refreshUrl"] = *f.refreshUrl;
    }
}

inline void from_json(const nlohmann::json& j, PasswordOAuthFlow& f)
{
    if (j.contains("refreshUrl")) {
        f.refreshUrl = j.at("refreshUrl").get<std::string>();
    }

    j.at("scopes").get_to(f.scopes);
    j.at("tokenUrl").get_to(f.tokenUrl);
}

struct ClientCredentialsOAuthFlow {
    std::optional<std::string> refreshUrl;
    std::map<std::string, std::string> scopes;
    std::string tokenUrl;
};

inline void to_json(nlohmann::json& j, const ClientCredentialsOAuthFlow& f)
{
    j = {{"scopes", f.scopes}, {"tokenUrl", f.tokenUrl}};

    if (f.refreshUrl) {
        j["refreshUrl"] = *f.refreshUrl;
    }
}

inline void from_json(const nlohmann::json& j, ClientCredentialsOAuthFlow& f)
{
    if (j.contains("refreshUrl")) {
        f.refreshUrl = j.at("refreshUrl").get<std::string>();
    }

    j.at("scopes").get_to(f.scopes);
    j.at("tokenUrl").get_to(f.tokenUrl);
}

struct OAuthFlows {
    std::optional<AuthorizationCodeOAuthFlow> authorizationCode;
    std::optional<ClientCredentialsOAuthFlow> clientCredentials;
    std::optional<ImplicitOAuthFlow> implicit;
    std::optional<PasswordOAuthFlow> password;
};

inline void to_json(nlohmann::json& j, const OAuthFlows& f)
{
    j = nlohmann::json{};
    if (f.authorizationCode) {
        j["authorizationCode"] = *f.authorizationCode;
    }

    if (f.clientCredentials) {
        j["clientCredentials"] = *f.clientCredentials;
    }

    if (f.implicit) {
        j["implicit"] = *f.implicit;
    }

    if (f.password) {
        j["password"] = *f.password;
    }
}

inline void from_json(const nlohmann::json& j, OAuthFlows& f)
{
    if (j.contains("authorizationCode")) {
        f.authorizationCode = j.at("authorizationCode").get<AuthorizationCodeOAuthFlow>();
    }

    if (j.contains("clientCredentials")) {
        f.clientCredentials = j.at("clientCredentials").get<ClientCredentialsOAuthFlow>();
    }

    if (j.contains("implicit")) {
        f.implicit = j.at("implicit").get<ImplicitOAuthFlow>();
    }

    if (j.contains("password")) {
        f.password = j.at("password").get<PasswordOAuthFlow>();
    }
}

struct OAuth2SecurityScheme {
    std::optional<std::string> description;
    OAuthFlows flows;
    std::optional<std::string> oauth2MetadataUrl;
    std::string type = "oauth2";
};

inline void to_json(nlohmann::json& j, const OAuth2SecurityScheme& s)
{
    j = {{"flows", s.flows}, {"type", s.type}};

    if (s.description) {
        j["description"] = *s.description;
    }

    if (s.oauth2MetadataUrl) {
        j["oauth2MetadataUrl"] = *s.oauth2MetadataUrl;
    }
}

inline void from_json(const nlohmann::json& j, OAuth2SecurityScheme& s)
{
    if (j.contains("description")) {
        s.description = j.at("description").get<std::string>();
    }

    s.flows = j.at("flows").get<OAuthFlows>();
    if (j.contains("oauth2MetadataUrl")) {
        s.oauth2MetadataUrl = j.at("oauth2MetadataUrl").get<std::string>();
    }

    if (j.contains("type")) {
        s.type = j.at("type").get<std::string>();
    }
}

struct OpenIdConnectSecurityScheme {
    std::optional<std::string> description;
    std::string openIdConnectUrl;
    std::string type = "openIdConnect";
};

inline void to_json(nlohmann::json& j, const OpenIdConnectSecurityScheme& s)
{
    j = {{"openIdConnectUrl", s.openIdConnectUrl}, {"type", s.type}};

    if (s.description) {
        j["description"] = *s.description;
    }
}

inline void from_json(const nlohmann::json& j, OpenIdConnectSecurityScheme& s)
{
    if (j.contains("description")) {
        s.description = j.at("description").get<std::string>();
    }

    j.at("openIdConnectUrl").get_to(s.openIdConnectUrl);
    if (j.contains("type")) {
        s.type = j.at("type").get<std::string>();
    }
}

struct MutualTLSSecurityScheme {
    std::optional<std::string> description;
    std::string type = "mutualTLS";
};

inline void to_json(nlohmann::json& j, const MutualTLSSecurityScheme& s)
{
    j = {{"type", s.type}};
    if (s.description) {
        j["description"] = *s.description;
    }
}

inline void from_json(const nlohmann::json& j, MutualTLSSecurityScheme& s)
{
    if (j.contains("description")) {
        s.description = j.at("description").get<std::string>();
    }

    if (j.contains("type")) {
        s.type = j.at("type").get<std::string>();
    }
}

struct SecurityScheme {
    std::variant<APIKeySecurityScheme, HTTPAuthSecurityScheme, OAuth2SecurityScheme, OpenIdConnectSecurityScheme,
                 MutualTLSSecurityScheme>
        v;
};

inline void to_json(nlohmann::json& j, const SecurityScheme& ss)
{
    std::visit(
        [&](auto&& s) {
            nlohmann::json js;
            to_json(js, s);
            j = js;
        },
        ss.v);
}

inline void from_json(const nlohmann::json& j, SecurityScheme& ss)
{
    auto t = j.value<std::string>("type", "");
    if (t == "apiKey") {
        APIKeySecurityScheme s;
        from_json(j, s);
        ss.v = s;
    } else if (t == "http") {
        HTTPAuthSecurityScheme s;
        from_json(j, s);
        ss.v = s;
    } else if (t == "oauth2") {
        OAuth2SecurityScheme s;
        from_json(j, s);
        ss.v = s;
    } else if (t == "openIdConnect") {
        OpenIdConnectSecurityScheme s;
        from_json(j, s);
        ss.v = s;
    } else if (t == "mutualTLS") {
        MutualTLSSecurityScheme s;
        from_json(j, s);
        ss.v = s;
    } else {
        throw std::runtime_error("UNKNOWN security scheme type");
    }
}

struct AgentSkill {
    std::string description;
    std::optional<std::vector<std::string>> examples;
    std::string id;
    std::optional<std::vector<std::string>> inputModes;
    std::string name;
    std::optional<std::vector<std::string>> outputModes;
    std::optional<std::vector<nlohmann::json>> security;
    std::vector<std::string> tags;
};

inline void to_json(nlohmann::json& j, const AgentSkill& s)
{
    j = nlohmann::json{{"description", s.description}, {"id", s.id}, {"name", s.name}, {"tags", s.tags}};

    if (s.examples) {
        j["examples"] = *s.examples;
    }

    if (s.inputModes) {
        j["inputModes"] = *s.inputModes;
    }

    if (s.outputModes) {
        j["outputModes"] = *s.outputModes;
    }

    if (s.security) {
        j["security"] = *s.security;
    }
}

inline void from_json(const nlohmann::json& j, AgentSkill& s)
{
    j.at("description").get_to(s.description);
    if (j.contains("examples")) {
        s.examples = j.at("examples").get<std::vector<std::string>>();
    }

    j.at("id").get_to(s.id);
    if (j.contains("inputModes")) {
        s.inputModes = j.at("inputModes").get<std::vector<std::string>>();
    }

    j.at("name").get_to(s.name);
    if (j.contains("outputModes")) {
        s.outputModes = j.at("outputModes").get<std::vector<std::string>>();
    }

    if (j.contains("security")) {
        s.security = j.at("security").get<std::vector<nlohmann::json>>();
    }

    j.at("tags").get_to(s.tags);
}

// AgentExtension mirrors the Python model used in extensions/common.py utilities
struct AgentExtension {
    std::optional<std::string> description;
    std::optional<nlohmann::json> params;
    std::optional<bool> required;
    std::string uri;
};

inline void to_json(nlohmann::json& j, const AgentExtension& e)
{
    j = nlohmann::json{{"uri", e.uri}};
    if (e.description) {
        j["description"] = *e.description;
    }

    if (e.params) {
        j["params"] = *e.params;
    }

    if (e.required) {
        j["required"] = *e.required;
    }
}

inline void from_json(const nlohmann::json& j, AgentExtension& e)
{
    if (j.contains("description")) {
        e.description = j.at("description").get<std::string>();
    }

    if (j.contains("params")) {
        e.params = j.at("params");
    }

    if (j.contains("required")) {
        e.required = j.at("required").get<bool>();
    }

    j.at("uri").get_to(e.uri);
}

struct AgentCapabilities {
    std::optional<std::vector<AgentExtension>> extensions;
    std::optional<bool> pushNotifications;
    std::optional<bool> stateTransitionHistory;
    std::optional<bool> streaming;
};

inline void to_json(nlohmann::json& j, const AgentCapabilities& c)
{
    j = nlohmann::json{};
    if (c.extensions) {
        j["extensions"] = *c.extensions;
    }

    if (c.pushNotifications) {
        j["pushNotifications"] = *c.pushNotifications;
    }

    if (c.stateTransitionHistory) {
        j["stateTransitionHistory"] = *c.stateTransitionHistory;
    }

    if (c.streaming) {
        j["streaming"] = *c.streaming;
    }
}

inline void from_json(const nlohmann::json& j, AgentCapabilities& c)
{
    if (j.contains("extensions")) {
        c.extensions = j.at("extensions").get<std::vector<AgentExtension>>();
    }

    if (j.contains("pushNotifications")) {
        c.pushNotifications = j.at("pushNotifications").get<bool>();
    }

    if (j.contains("stateTransitionHistory")) {
        c.stateTransitionHistory = j.at("stateTransitionHistory").get<bool>();
    }

    if (j.contains("streaming")) {
        c.streaming = j.at("streaming").get<bool>();
    }
}

struct AgentInterface {
    std::string transport;
    std::string url;
};

inline void to_json(nlohmann::json& j, const AgentInterface& i)
{
    j = {{"transport", i.transport}, {"url", i.url}};
}

inline void from_json(const nlohmann::json& j, AgentInterface& i)
{
    j.at("transport").get_to(i.transport);
    j.at("url").get_to(i.url);
}

struct AgentProvider {
    std::string organization;
    std::string url;
};

inline void to_json(nlohmann::json& j, const AgentProvider& p)
{
    j = {{"organization", p.organization}, {"url", p.url}};
}

inline void from_json(const nlohmann::json& j, AgentProvider& p)
{
    j.at("organization").get_to(p.organization);
    j.at("url").get_to(p.url);
}

struct AgentCardSignature {
    std::optional<nlohmann::json> header;
    std::string protected_;
    std::string signature;
};

inline void to_json(nlohmann::json& j, const AgentCardSignature& s)
{
    j = {{"protected", s.protected_}, {"signature", s.signature}};

    if (s.header) {
        j["header"] = *s.header;
    }
}

inline void from_json(const nlohmann::json& j, AgentCardSignature& s)
{
    if (j.contains("header")) {
        s.header = j.at("header");
    }

    j.at("protected").get_to(s.protected_);
    j.at("signature").get_to(s.signature);
}

struct AgentCard {
    std::optional<std::vector<AgentInterface>> additionalInterfaces;
    AgentCapabilities capabilities;
    std::vector<std::string> defaultInputModes;
    std::vector<std::string> defaultOutputModes;
    std::string description;
    std::optional<std::string> documentationUrl;
    std::optional<std::string> iconUrl;
    std::string name;
    std::optional<std::string> preferredTransport;
    std::optional<std::string> protocolVersion;
    std::optional<AgentProvider> provider;
    std::optional<std::vector<nlohmann::json>> security;
    std::optional<std::map<std::string, SecurityScheme>> securitySchemes;
    std::optional<std::vector<AgentCardSignature>> signatures;
    std::vector<AgentSkill> skills;
    std::optional<bool> supportsAuthenticatedExtendedCard;
    std::string url;
    std::string version;
};

inline void to_json(nlohmann::json& j, const AgentCard& c)
{
    j = nlohmann::json{{"capabilities", c.capabilities},
                       {"defaultInputModes", c.defaultInputModes},
                       {"defaultOutputModes", c.defaultOutputModes},
                       {"description", c.description},
                       {"name", c.name},
                       {"skills", c.skills},
                       {"url", c.url},
                       {"version", c.version}};

    if (c.additionalInterfaces) {
        j["additionalInterfaces"] = *c.additionalInterfaces;
    }

    if (c.documentationUrl) {
        j["documentationUrl"] = *c.documentationUrl;
    }

    if (c.iconUrl) {
        j["iconUrl"] = *c.iconUrl;
    }

    if (c.preferredTransport) {
        j["preferredTransport"] = *c.preferredTransport;
    }

    if (c.protocolVersion) {
        j["protocolVersion"] = *c.protocolVersion;
    }

    if (c.provider) {
        j["provider"] = *c.provider;
    }

    if (c.security) {
        j["security"] = *c.security;
    }

    if (c.securitySchemes) {
        j["securitySchemes"] = *c.securitySchemes;
    }

    if (c.signatures) {
        j["signatures"] = *c.signatures;
    }

    if (c.supportsAuthenticatedExtendedCard) {
        j["supportsAuthenticatedExtendedCard"] = *c.supportsAuthenticatedExtendedCard;
    }
}

inline void from_json(const nlohmann::json& j, AgentCard& c)
{
    if (j.contains("additionalInterfaces")) {
        c.additionalInterfaces = j.at("additionalInterfaces").get<std::vector<AgentInterface>>();
    }

    c.capabilities = j.at("capabilities").get<AgentCapabilities>();
    c.defaultInputModes = j.at("defaultInputModes").get<std::vector<std::string>>();
    c.defaultOutputModes = j.at("defaultOutputModes").get<std::vector<std::string>>();

    j.at("description").get_to(c.description);
    if (j.contains("documentationUrl")) {
        c.documentationUrl = j.at("documentationUrl").get<std::string>();
    }

    if (j.contains("iconUrl")) {
        c.iconUrl = j.at("iconUrl").get<std::string>();
    }

    j.at("name").get_to(c.name);
    if (j.contains("preferredTransport")) {
        c.preferredTransport = j.at("preferredTransport").get<std::string>();
    }

    if (j.contains("protocolVersion")) {
        c.protocolVersion = j.at("protocolVersion").get<std::string>();
    }

    if (j.contains("provider")) {
        c.provider = j.at("provider").get<AgentProvider>();
    }

    if (j.contains("security")) {
        c.security = j.at("security").get<std::vector<nlohmann::json>>();
    }

    if (j.contains("securitySchemes")) {
        c.securitySchemes = j.at("securitySchemes").get<std::map<std::string, SecurityScheme>>();
    }

    if (j.contains("signatures")) {
        c.signatures = j.at("signatures").get<std::vector<AgentCardSignature>>();
    }

    c.skills = j.at("skills").get<std::vector<AgentSkill>>();
    if (j.contains("supportsAuthenticatedExtendedCard")) {
        c.supportsAuthenticatedExtendedCard = j.at("supportsAuthenticatedExtendedCard").get<bool>();
    }

    j.at("url").get_to(c.url);
    j.at("version").get_to(c.version);
}

} // namespace a2a

#endif
