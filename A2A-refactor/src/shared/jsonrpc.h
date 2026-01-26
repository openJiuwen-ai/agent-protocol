/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_TYPES_SERIALIZATION_H
#define A2A_TYPES_SERIALIZATION_H

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "types.h"

namespace nlohmann {
    // Role serializers
    template<>
    struct adl_serializer<A2A::Role> {
        static void to_json(nlohmann::json& j, const A2A::Role& r)
        {
            j = (r == A2A::Role::AGENT) ? "agent" : "user";
        }

        static void from_json(const nlohmann::json& j, A2A::Role& r)
        {
            const auto s = j.get<std::string>();
            if (s == "agent") {
                r = A2A::Role::AGENT;
            } else if (s == "user") {
                r = A2A::Role::USER;
            } else {
                throw std::runtime_error("Invalid Role value: " + s);
            }
        }
    };

    // FileWithBytes serializers
    template<>
    struct adl_serializer<A2A::FileWithBytes> {
        static void to_json(nlohmann::json& j, const A2A::FileWithBytes& f)
        {
            j = nlohmann::json{{"bytes", f.bytes}};
            if (f.mimeType) {
                j["mimeType"] = *f.mimeType;
            }

            if (f.name) {
                j["name"] = *f.name;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::FileWithBytes& f)
        {
            if (!j.contains("bytes")) {
                throw std::runtime_error("FileWithBytes must contain 'bytes' field");
            }
            j.at("bytes").get_to(f.bytes);

            if (j.contains("mimeType")) {
                f.mimeType = j.at("mimeType").get<std::string>();
            }

            if (j.contains("name")) {
                f.name = j.at("name").get<std::string>();
            }
        }
    };

    // FileWithUri serializers
    template<>
    struct adl_serializer<A2A::FileWithUri> {
        static void to_json(nlohmann::json& j, const A2A::FileWithUri& f)
        {
            j = nlohmann::json{{"uri", f.uri}};
            if (f.mimeType) {
                j["mimeType"] = *f.mimeType;
            }

            if (f.name) {
                j["name"] = *f.name;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::FileWithUri& f)
        {
            if (!j.contains("uri")) {
                throw std::runtime_error("FileWithUri must contain 'uri' field");
            }
            j.at("uri").get_to(f.uri);

            if (j.contains("mimeType")) {
                f.mimeType = j.at("mimeType").get<std::string>();
            }

            if (j.contains("name")) {
                f.name = j.at("name").get<std::string>();
            }
        }
    };

    // TextPart serializers
    template<>
    struct adl_serializer<A2A::TextPart> {
        static void to_json(nlohmann::json& j, const A2A::TextPart& p)
        {
            if (p.kind != "text") {
                throw std::runtime_error("TextPart.kind must be 'text'");
            }
            j = nlohmann::json{{"kind", p.kind}, {"text", p.text}};
            if (p.metadata) {
                j["metadata"] = *p.metadata;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::TextPart& p)
        {
            if (!j.contains("text")) {
                throw std::runtime_error("TextPart must contain 'text' field");
            }
            j.at("text").get_to(p.text);

            if (j.contains("kind")) {
                p.kind = j.at("kind").get<std::string>();
                if (p.kind != "text") {
                    throw std::runtime_error("TextPart.kind must be 'text'");
                }
            }

            if (j.contains("metadata")) {
                p.metadata = j.at("metadata");
            }
        }
    };

    // DataPart serializers
    template<>
    struct adl_serializer<A2A::DataPart> {
        static void to_json(nlohmann::json& j, const A2A::DataPart& p)
        {
            if (p.kind != "data") {
                throw std::runtime_error("DataPart.kind must be 'data'");
            }
            j = nlohmann::json{{"kind", p.kind}, {"data", p.data}};

            if (p.metadata) {
                j["metadata"] = *p.metadata;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::DataPart& p)
        {
            if (!j.contains("data")) {
                throw std::runtime_error("DataPart must contain 'data' field");
            }
            j.at("data").get_to(p.data);

            if (j.contains("kind")) {
                p.kind = j.at("kind").get<std::string>();
                if (p.kind != "data") {
                    throw std::runtime_error("DataPart.kind must be 'data'");
                }
            }

            if (j.contains("metadata")) {
                p.metadata = j.at("metadata");
            }
        }
    };

    // FilePart serializers
    template<>
    struct adl_serializer<A2A::FilePart> {
        static void to_json(nlohmann::json& j, const A2A::FilePart& p)
        {
            if (p.kind != "file") {
                throw std::runtime_error("FilePart.kind must be 'file'");
            }
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

        static void from_json(const nlohmann::json& j, A2A::FilePart& p)
        {
            if (j.contains("kind")) {
                p.kind = j.at("kind").get<std::string>();
                if (p.kind != "file") {
                    throw std::runtime_error("FilePart.kind must be 'file'");
                }
            }

            if (j.contains("metadata")) {
                p.metadata = j.at("metadata");
            }

            if (!j.contains("file")) {
                throw std::runtime_error("FilePart must contain 'file' field");
            }
            const auto& jf = j.at("file");
            if (jf.contains("bytes")) {
                FileWithBytes fb;
                from_json(jf, fb);
                p.file = fb;
            } else if (jf.contains("uri")) {
                FileWithUri fu;
                from_json(jf, fu);
                p.file = fu;
            } else {
                throw std::runtime_error("File must contain either 'bytes' or 'uri' field");
            }
        }
    };

    // Part serializers
    template<>
    struct adl_serializer<A2A::Part> {
        static void to_json(nlohmann::json& j, const A2A::Part& p)
        {
            std::visit([&](auto&& part) { to_json(j, part); }, p);
        }

        static void from_json(const nlohmann::json& j, A2A::Part& p)
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
    };

    // Artifact serializers
    template<>
    struct adl_serializer<A2A::Artifact> {
        static void to_json(nlohmann::json& j, const A2A::Artifact& a)
        {
            if (a.artifactId.empty()) {
                throw std::runtime_error("Artifact.artifactId cannot be empty");
            }

            nlohmann::json parts_array = nlohmann::json::array();
            for (const auto& p : a.parts) {
                nlohmann::json jp;
                to_json(jp, p);
                parts_array.push_back(jp);
            }

            j = nlohmann::json{{"artifactId", a.artifactId}, {"parts", parts_array}};

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
        }

        static void from_json(const nlohmann::json& j, A2A::Artifact& a)
        {
            if (!j.contains("artifactId") || !j.contains("parts")) {
                throw std::runtime_error("Artifact missing required fields");
            }

            j.at("artifactId").get_to(a.artifactId);
            if (a.artifactId.empty()) {
                throw std::runtime_error("Artifact.artifactId cannot be empty");
            }

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
    };

    // Message serializers
    template<>
    struct adl_serializer<A2A::Message> {
        static void to_json(nlohmann::json& j, const A2A::Message& m)
        {
            if (m.kind != "message") {
                throw std::runtime_error("Message.kind must be 'message'");
            }
            if (m.messageId.empty()) {
                throw std::runtime_error("Message.messageId cannot be empty");
            }

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

        static void from_json(const nlohmann::json& j, A2A::Message& m)
        {
            if (!j.contains("messageId") || !j.contains("parts") || !j.contains("role")) {
                throw std::runtime_error("Message missing required fields");
            }

            j.at("messageId").get_to(m.messageId);
            if (m.messageId.empty()) {
                throw std::runtime_error("Message.messageId cannot be empty");
            }

            if (j.contains("kind")) {
                m.kind = j.at("kind").get<std::string>();
                if (m.kind != "message") {
                    throw std::runtime_error("Message.kind must be 'message', got: " + m.kind);
                }
            }

            if (j.contains("contextId")) {
                m.contextId = j.at("contextId").get<std::string>();
            }

            if (j.contains("extensions")) {
                m.extensions = j.at("extensions").get<std::vector<std::string>>();
            }

            if (j.contains("metadata")) {
                m.metadata = j.at("metadata");
            }

            if (j.contains("taskId")) {
                m.taskId = j.at("taskId").get<std::string>();
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
                m.role = A2A::Role::USER;
            }
        }
    };

    // TaskStatus serializers
    template<>
    struct adl_serializer<A2A::TaskStatus> {
        static void to_json(nlohmann::json& j, const A2A::TaskStatus& s)
        {
            auto stateToStr = [](A2A::TaskState st) {
                switch (st) {
                    case A2A::TaskState::SUBMITTED:
                        return "submitted";
                    case A2A::TaskState::WORKING:
                        return "working";
                    case A2A::TaskState::INPUT_REQUIRED:
                        return "input-required";
                    case A2A::TaskState::COMPLETED:
                        return "completed";
                    case A2A::TaskState::CANCELED:
                        return "canceled";
                    case A2A::TaskState::FAILED:
                        return "failed";
                    case A2A::TaskState::REJECTED:
                        return "rejected";
                    case A2A::TaskState::AUTH_REQUIRED:
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

        static void from_json(const nlohmann::json& j, A2A::TaskStatus& s)
        {
            auto str = j.value<std::string>("state", "unknown");
            if (str == "submitted") {
                s.state = A2A::TaskState::SUBMITTED;
            } else if (str == "working") {
                s.state = A2A::TaskState::WORKING;
            } else if (str == "input-required") {
                s.state = A2A::TaskState::INPUT_REQUIRED;
            } else if (str == "completed") {
                s.state = A2A::TaskState::COMPLETED;
            } else if (str == "canceled") {
                s.state = A2A::TaskState::CANCELED;
            } else if (str == "failed") {
                s.state = A2A::TaskState::FAILED;
            } else if (str == "rejected") {
                s.state = A2A::TaskState::REJECTED;
            } else if (str == "auth-required") {
                s.state = A2A::TaskState::AUTH_REQUIRED;
            } else {
                s.state = A2A::TaskState::UNKNOWN;
            }

            if (j.contains("message")) {
                s.message = j.at("message").get<A2A::Message>();
            }

            if (j.contains("timestamp")) {
                s.timestamp = j.at("timestamp").get<std::string>();
            }
        }
    };

    // Task serializers
    template<>
    struct adl_serializer<A2A::Task> {
        static void to_json(nlohmann::json& j, const A2A::Task& t)
        {
            if (t.id.empty()) {
                throw std::runtime_error("Task.id cannot be empty");
            }
            if (t.kind != "task") {
                throw std::runtime_error("Task.kind must be 'task'");
            }

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

        static void from_json(const nlohmann::json& j, A2A::Task& t)
        {
            if (!j.contains("contextId") || !j.contains("id") || !j.contains("status")) {
                throw std::runtime_error("Task missing required fields");
            }

            j.at("contextId").get_to(t.contextId);

            j.at("id").get_to(t.id);
            if (t.id.empty()) {
                throw std::runtime_error("Task.id cannot be empty");
            }

            if (j.contains("kind")) {
                t.kind = j.at("kind").get<std::string>();
                if (t.kind != "task") {
                    throw std::runtime_error("Task.kind must be 'task', got: " + t.kind);
                }
            }

            if (j.contains("artifacts")) {
                t.artifacts = j.at("artifacts").get<std::vector<A2A::Artifact>>();
            }

            if (j.contains("history")) {
                t.history = j.at("history").get<std::vector<A2A::Message>>();
            }

            if (j.contains("metadata")) {
                t.metadata = j.at("metadata");
            }

            t.status = j.at("status").get<A2A::TaskStatus>();
        }
    };

    // JSONRPCError serializers
    template<>
    struct adl_serializer<A2A::JSONRPCError> {
        static void to_json(nlohmann::json& j, const A2A::JSONRPCError& e)
        {
            j = nlohmann::json{{"code", e.code}, {"message", e.message}};

            if (e.data) {
                j["data"] = *e.data;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::JSONRPCError& e)
        {
            j.at("code").get_to(e.code);
            j.at("message").get_to(e.message);
            if (j.contains("data")) {
                e.data = j.at("data");
            }
        }
    };

    // JSONRPCErrorResponse serializers
    template<>
    struct adl_serializer<A2A::JSONRPCErrorResponse> {
        static void to_json(nlohmann::json& j, const A2A::JSONRPCErrorResponse& r)
        {
            j = nlohmann::json{{"jsonrpc", r.jsonrpc}, {"id", r.id}, {"error", r.error}};
        }

        static void from_json(const nlohmann::json& j, A2A::JSONRPCErrorResponse& r)
        {
            r.id = j.at("id");
            if (j.contains("jsonrpc")) {
                r.jsonrpc = j.at("jsonrpc").get<std::string>();
            }

            r.error = j.at("error").get<A2A::JSONRPCError>();
        }
    };

    // MessageSendConfiguration serializers
    template<>
    struct adl_serializer<A2A::MessageSendConfiguration> {
        static void to_json(nlohmann::json& j, const A2A::MessageSendConfiguration& c)
        {
            j = nlohmann::json{{"acceptedOutputModes", c.acceptedOutputModes}};

            if (c.historyLength) {
                j["historyLength"] = *c.historyLength;
            }

            if (c.pushNotificationConfig) {
                j["pushNotificationConfig"] = *c.pushNotificationConfig;
            }

            if (c.blocking) {
                j["blocking"] = *c.blocking;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::MessageSendConfiguration& c)
        {
            if (!j.contains("acceptedOutputModes")) {
                throw std::runtime_error("MessageSendConfiguration must contain 'acceptedOutputModes' field");
            }

            const auto& modes_json = j.at("acceptedOutputModes");
            if (!modes_json.is_array()) {
                throw std::runtime_error("acceptedOutputModes must be an array");
            }

            c.acceptedOutputModes.clear();
            for (const auto& mode : modes_json) {
                if (!mode.is_string()) {
                    throw std::runtime_error("acceptedOutputModes must contain only strings");
                }
                c.acceptedOutputModes.push_back(mode.get<std::string>());
            }

            if (j.contains("historyLength")) {
                c.historyLength = j.at("historyLength").get<int>();
            }

            if (j.contains("pushNotificationConfig")) {
                c.pushNotificationConfig = j.at("pushNotificationConfig").get<PushNotificationConfig>();
            }

            if (j.contains("blocking")) {
                c.blocking = j.at("blocking").get<bool>();
            }
        }
    };

    // MessageSendParams serializers
    template<>
    struct adl_serializer<A2A::MessageSendParams> {
        static void to_json(nlohmann::json& j, const A2A::MessageSendParams& p)
        {
            j = nlohmann::json{{"message", p.message}};

            if (p.configuration) {
                j["configuration"] = *p.configuration;
            }

            if (p.metadata) {
                j["metadata"] = *p.metadata;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::MessageSendParams& p)
        {
            if (!j.contains("message")) {
                throw std::runtime_error("MessageSendParams must contain 'message' field");
            }
            p.message = j.at("message").get<A2A::Message>();

            if (j.contains("configuration")) {
                MessageSendConfiguration config;
                from_json(j.at("configuration"), config);
                p.configuration = std::move(config);
            }

            if (j.contains("metadata")) {
                p.metadata = j.at("metadata");
            }
        }
    };

    // TaskArtifactUpdateEvent serializers
    template<>
    struct adl_serializer<A2A::TaskArtifactUpdateEvent> {
        static void to_json(nlohmann::json& j, const A2A::TaskArtifactUpdateEvent& e)
        {
            if (e.kind != "artifact-update") {
                throw std::runtime_error("TaskArtifactUpdateEvent.kind must be 'artifact-update'");
            }

            j = nlohmann::json{
                {"artifact", e.artifact},
                {"contextId", e.contextId},
                {"kind", e.kind},
                {"taskId", e.taskId}};

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

        static void from_json(const nlohmann::json& j, A2A::TaskArtifactUpdateEvent& e)
        {
            if (!j.contains("artifact") || !j.contains("contextId") || !j.contains("taskId")) {
                throw std::runtime_error("TaskArtifactUpdateEvent missing required fields");
            }

            e.artifact = j.at("artifact").get<A2A::Artifact>();
            j.at("contextId").get_to(e.contextId);
            j.at("taskId").get_to(e.taskId);
            if (e.taskId.empty()) {
                throw std::runtime_error("TaskArtifactUpdateEvent.taskId cannot be empty");
            }

            if (j.contains("kind")) {
                e.kind = j.at("kind").get<std::string>();
                if (e.kind != "artifact-update") {
                    throw std::runtime_error("kind must be 'artifact-update'");
                }
            }

            if (j.contains("append")) {
                e.append = j.at("append").get<bool>();
            }

            if (j.contains("lastChunk")) {
                e.lastChunk = j.at("lastChunk").get<bool>();
            }

            if (j.contains("metadata")) {
                e.metadata = j.at("metadata");
            }
        }
    };

    // TaskStatusUpdateEvent serializers
    template<>
    struct adl_serializer<A2A::TaskStatusUpdateEvent> {
        static void to_json(nlohmann::json& j, const A2A::TaskStatusUpdateEvent& e)
        {
            if (e.kind != "status-update") {
                throw std::runtime_error("TaskStatusUpdateEvent.kind must be 'status-update'");
            }
            j = nlohmann::json{
            {"contextId", e.contextId}, {"kind", e.kind}, {"status", e.status}, {"taskId", e.taskId}};

            if (e.final) {
                j["final"] = *e.final;
            }

            if (e.metadata) {
                j["metadata"] = *e.metadata;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::TaskStatusUpdateEvent& e)
        {
            if (!j.contains("contextId") || !j.contains("status") || !j.contains("taskId")) {
                throw std::runtime_error("TaskStatusUpdateEvent missing required fields");
            }

            j.at("contextId").get_to(e.contextId);
            j.at("taskId").get_to(e.taskId);
            if (e.taskId.empty()) {
                throw std::runtime_error("TaskStatusUpdateEvent.taskId cannot be empty");
            }

            e.status = j.at("status").get<A2A::TaskStatus>();

            if (j.contains("kind")) {
                e.kind = j.at("kind").get<std::string>();
                if (e.kind != "status-update") {
                    throw std::runtime_error("kind must be 'status-update'");
                }
            }

            if (j.contains("final")) {
                e.final = j.at("final").get<bool>();
            }

            if (j.contains("metadata")) {
                e.metadata = j.at("metadata");
            }
        }
    };

    // PushNotificationAuthenticationInfo serializers
    template<>
    struct adl_serializer<A2A::PushNotificationAuthenticationInfo> {
        static void to_json(nlohmann::json& j, const A2A::PushNotificationAuthenticationInfo& i)
        {
            j = {{"schemes", i.schemes}};
            if (i.credentials) {
                j["credentials"] = *i.credentials;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::PushNotificationAuthenticationInfo& i)
        {
            if (!j.contains("schemes")) {
                throw std::runtime_error("PushNotificationAuthenticationInfo must contain 'schemes' field");
            }

            const auto& schemes_json = j.at("schemes");
            if (!schemes_json.is_array()) {
                throw std::runtime_error("PushNotificationAuthenticationInfo.schemes must be an array");
            }
            i.schemes.clear();
            for (const auto& scheme : schemes_json) {
                if (!scheme.is_string()) {
                    throw std::runtime_error("PushNotificationAuthenticationInfo.schemes must contain only strings");
                }
                i.schemes.push_back(scheme.get<std::string>());
            }

            if (j.contains("credentials")) {
                i.credentials = j.at("credentials").get<std::string>();
            }
        }
    };

    // PushNotificationConfig serializers
    template<>
    struct adl_serializer<A2A::PushNotificationConfig> {
        static void to_json(nlohmann::json& j, const A2A::PushNotificationConfig& c)
        {
            if (c.url.empty()) {
                throw std::runtime_error("PushNotificationConfig.url cannot be empty");
            }
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

        static void from_json(const nlohmann::json& j, A2A::PushNotificationConfig& c)
        {
            if (!j.contains("url")) {
                throw std::runtime_error("PushNotificationConfig must contain 'url' field");
            }
            j.at("url").get_to(c.url);
            if (c.url.empty()) {
                throw std::runtime_error("PushNotificationConfig.url cannot be empty");
            }

            if (j.contains("authentication")) {
                c.authentication = j.at("authentication").get<A2A::PushNotificationAuthenticationInfo>();
            }

            if (j.contains("id")) {
                c.id = j.at("id").get<std::string>();
            }

            if (j.contains("token")) {
                c.token = j.at("token").get<std::string>();
            }
        }
    };

    // TaskIdParams serializers
    template<>
    struct adl_serializer<A2A::TaskIdParams> {
        static void to_json(nlohmann::json& j, const A2A::TaskIdParams& p)
        {
            if (p.id.empty()) {
                throw std::runtime_error("TaskIdParams.id cannot be empty");
            }
            j = {{"id", p.id}};
            if (p.metadata) {
                j["metadata"] = *p.metadata;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::TaskIdParams& p)
        {
            if (!j.contains("id")) {
                throw std::runtime_error("TaskIdParams must contain 'id' field");
            }
            j.at("id").get_to(p.id);
            if (p.id.empty()) {
                throw std::runtime_error("TaskIdParams.id cannot be empty");
            }

            if (j.contains("metadata")) {
                p.metadata = j.at("metadata");
            }
        }
    };

    // TaskQueryParams serializers
    template<>
    struct adl_serializer<A2A::TaskQueryParams> {
        static void to_json(nlohmann::json& j, const A2A::TaskQueryParams& p)
        {
            if (p.id.empty()) {
                throw std::runtime_error("TaskQueryParams.id cannot be empty");
            }
            j = {{"id", p.id}};

            if (p.historyLength) {
                j["historyLength"] = *p.historyLength;
            }

            if (p.metadata) {
                j["metadata"] = *p.metadata;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::TaskQueryParams& p)
        {
            if (!j.contains("id")) {
                throw std::runtime_error("TaskQueryParams must contain 'id' field");
            }
            j.at("id").get_to(p.id);
            if (p.id.empty()) {
                throw std::runtime_error("TaskQueryParams.id cannot be empty");
            }

            if (j.contains("historyLength")) {
                p.historyLength = j.at("historyLength").get<int>();
            }

            if (j.contains("metadata")) {
                p.metadata = j.at("metadata");
            }
        }
    };

    // TaskPushNotificationConfig serializers
    template<>
    struct adl_serializer<A2A::TaskPushNotificationConfig> {
        static void to_json(nlohmann::json& j, const A2A::TaskPushNotificationConfig& c)
        {
            if (c.taskId.empty()) {
                throw std::runtime_error("TaskPushNotificationConfig.taskId cannot be empty");
            }
            j = {{"pushNotificationConfig", c.pushNotificationConfig}, {"taskId", c.taskId}};
        }

        static void from_json(const nlohmann::json& j, A2A::TaskPushNotificationConfig& c)
        {
            if (!j.contains("pushNotificationConfig") || !j.contains("taskId")) {
                throw std::runtime_error("TaskPushNotificationConfig missing required fields");
            }

            c.pushNotificationConfig = j.at("pushNotificationConfig").get<A2A::PushNotificationConfig>();
            j.at("taskId").get_to(c.taskId);
            if (c.taskId.empty()) {
                throw std::runtime_error("TaskPushNotificationConfig.taskId cannot be empty");
            }
        }
    };

    // GetTaskPushNotificationConfigParams serializers
    template<>
    struct adl_serializer<A2A::GetTaskPushNotificationConfigParams> {
        static void to_json(nlohmann::json& j, const A2A::GetTaskPushNotificationConfigParams& p)
        {
            if (p.id.empty()) {
                throw std::runtime_error("GetTaskPushNotificationConfigParams.id cannot be empty");
            }
            j = {{"id", p.id}};

            if (p.metadata) {
                j["metadata"] = *p.metadata;
            }

            if (p.pushNotificationConfigId) {
                j["pushNotificationConfigId"] = *p.pushNotificationConfigId;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::GetTaskPushNotificationConfigParams& p)
        {
            if (!j.contains("id")) {
                throw std::runtime_error("GetTaskPushNotificationConfigParams must contain 'id' field");
            }
            j.at("id").get_to(p.id);
            if (p.id.empty()) {
                throw std::runtime_error("GetTaskPushNotificationConfigParams.id cannot be empty");
            }

            if (j.contains("metadata")) {
                p.metadata = j.at("metadata");
            }

            if (j.contains("pushNotificationConfigId")) {
                p.pushNotificationConfigId = j.at("pushNotificationConfigId").get<std::string>();
            }
        }
    };

    // ListTaskPushNotificationConfigParams serializers
    template<>
    struct adl_serializer<A2A::ListTaskPushNotificationConfigParams> {
        static void to_json(nlohmann::json& j, const A2A::ListTaskPushNotificationConfigParams& p)
        {
            if (p.id.empty()) {
                throw std::runtime_error("ListTaskPushNotificationConfigParams.id cannot be empty");
            }
            j = {{"id", p.id}};

            if (p.metadata) {
                j["metadata"] = *p.metadata;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::ListTaskPushNotificationConfigParams& p)
        {
            if (!j.contains("id")) {
                throw std::runtime_error("ListTaskPushNotificationConfigParams must contain 'id' field");
            }
            j.at("id").get_to(p.id);
            if (p.id.empty()) {
                throw std::runtime_error("ListTaskPushNotificationConfigParams.id cannot be empty");
            }

            if (j.contains("metadata")) {
                p.metadata = j.at("metadata");
            }
        }
    };

    // DeleteTaskPushNotificationConfigParams serializers
    template<>
    struct adl_serializer<A2A::DeleteTaskPushNotificationConfigParams> {
        static void to_json(nlohmann::json& j, const A2A::DeleteTaskPushNotificationConfigParams& p)
        {
            if (p.id.empty()) {
                throw std::runtime_error("DeleteTaskPushNotificationConfigParams.id cannot be empty");
            }
            if (p.pushNotificationConfigId.empty()) {
                throw std::runtime_error(
                    "DeleteTaskPushNotificationConfigParams.pushNotificationConfigId cannot be empty");
            }

            j = {{"id", p.id}, {"pushNotificationConfigId", p.pushNotificationConfigId}};

            if (p.metadata) {
                j["metadata"] = *p.metadata;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::DeleteTaskPushNotificationConfigParams& p)
        {
            if (!j.contains("id") || !j.contains("pushNotificationConfigId")) {
                throw std::runtime_error("DeleteTaskPushNotificationConfigParams missing required fields");
            }
            j.at("id").get_to(p.id);
            j.at("pushNotificationConfigId").get_to(p.pushNotificationConfigId);
            if (p.id.empty()) {
                throw std::runtime_error("DeleteTaskPushNotificationConfigParams.id cannot be empty");
            }
            if (p.pushNotificationConfigId.empty()) {
                throw std::runtime_error(
                    "DeleteTaskPushNotificationConfigParams.pushNotificationConfigId cannot be empty");
            }

            if (j.contains("metadata")) {
                p.metadata = j.at("metadata");
            }
        }
    };

    // SendMessageRequest serializers
    template<>
    struct adl_serializer<A2A::SendMessageRequest> {
        static void to_json(nlohmann::json& j, const A2A::SendMessageRequest& r)
        {
            j = {{"jsonrpc", r.jsonrpc}, {"id", r.id}, {"method", r.method}, {"params", r.params}};
        }

        static void from_json(const nlohmann::json& j, A2A::SendMessageRequest& r)
        {
            if (!j.contains("id") || !j.contains("params")) {
                throw std::runtime_error("SendMessageRequest missing required fields");
            }
            r.id = j.at("id");
            if (j.contains("jsonrpc")) {
                r.jsonrpc = j.at("jsonrpc").get<std::string>();
            } else {
                r.jsonrpc = "2.0";
            }

            if (j.contains("method")) {
                r.method = j.at("method").get<std::string>();
            } else {
                r.method = "message/send";
            }

            r.params = j.at("params").get<A2A::MessageSendParams>();
        }
    };

    // SendMessageSuccessResponse serializers
    template<>
    struct adl_serializer<A2A::SendMessageSuccessResponse> {
        static void to_json(nlohmann::json& j, const A2A::SendMessageSuccessResponse& r)
        {
            j = {{"jsonrpc", r.jsonrpc},
                 {"result", std::holds_alternative<Message>(r.result) ? nlohmann::json(std::get<A2A::Message>(r.result))
                                                                      : nlohmann::json(std::get<A2A::Task>(r.result))}};

            if (r.id) {
                j["id"] = *r.id;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::SendMessageSuccessResponse& r)
        {
            if (j.contains("id")) {
                r.id = j.at("id");
            }

            if (j.contains("jsonrpc")) {
                r.jsonrpc = j.at("jsonrpc").get<std::string>();
            } else {
                r.jsonrpc = "2.0";
            }

            const auto& res = j.at("result");
            if (res.contains("kind") && res.at("kind") == "message") {
                r.result = res.get<A2A::Message>();
            } else {
                r.result = res.get<A2A::Task>();
            }
        }
    };

    // GetTaskRequest serializers
    template<>
    struct adl_serializer<A2A::GetTaskRequest> {
        static void to_json(nlohmann::json& j, const A2A::GetTaskRequest& r)
        {
            j = {{"jsonrpc", r.jsonrpc}, {"id", r.id}, {"method", r.method}, {"params", r.params}};
        }

        static void from_json(const nlohmann::json& j, A2A::GetTaskRequest& r)
        {
            if (!j.contains("id") || !j.contains("params")) {
                throw std::runtime_error("GetTaskRequest missing required fields");
            }

            r.id = j.at("id");
            if (j.contains("jsonrpc")) {
                r.jsonrpc = j.at("jsonrpc").get<std::string>();
            } else {
                r.jsonrpc = "2.0";
            }

            if (j.contains("method")) {
                r.method = j.at("method").get<std::string>();
            } else {
                r.method = "tasks/get";
            }

            r.params = j.at("params").get<A2A::TaskIdParams>();
        }
    };

    // GetTaskSuccessResponse serializers
    template<>
    struct adl_serializer<A2A::GetTaskSuccessResponse> {
        static void to_json(nlohmann::json& j, const A2A::GetTaskSuccessResponse& r)
        {
            j = {{"jsonrpc", r.jsonrpc}, {"result", r.result}};

            if (r.id) {
                j["id"] = *r.id;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::GetTaskSuccessResponse& r)
        {
            if (j.contains("id")) {
                r.id = j.at("id");
            }

            if (j.contains("jsonrpc")) {
                r.jsonrpc = j.at("jsonrpc").get<std::string>();
            } else {
                r.jsonrpc = "2.0";
            }

            r.result = j.at("result").get<A2A::Task>();
        }
    };

    // CancelTaskRequest serializers
    template<>
    struct adl_serializer<A2A::CancelTaskRequest> {
        static void to_json(nlohmann::json& j, const A2A::CancelTaskRequest& r)
        {
            j = {{"jsonrpc", r.jsonrpc}, {"id", r.id}, {"method", r.method}, {"params", r.params}};
        }

        static void from_json(const nlohmann::json& j, A2A::CancelTaskRequest& r)
        {
            if (!j.contains("id") || !j.contains("params")) {
                throw std::runtime_error("CancelTaskRequest missing required fields");
            }
            r.id = j.at("id");
            if (j.contains("jsonrpc")) {
                r.jsonrpc = j.at("jsonrpc").get<std::string>();
            } else {
                r.jsonrpc = "2.0";
            }

            if (j.contains("method")) {
                r.method = j.at("method").get<std::string>();
            } else {
                r.jsonrpc = "tasks/cancel";
            }

            r.params = j.at("params").get<A2A::TaskIdParams>();
        }
    };

    // CancelTaskSuccessResponse serializers
    template<>
    struct adl_serializer<A2A::CancelTaskSuccessResponse> {
        static void to_json(nlohmann::json& j, const A2A::CancelTaskSuccessResponse& r)
        {
            j = {{"jsonrpc", r.jsonrpc}, {"result", r.result}};

            if (r.id) {
                j["id"] = *r.id;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::CancelTaskSuccessResponse& r)
        {
            if (j.contains("id")) {
                r.id = j.at("id");
            }

            if (j.contains("jsonrpc")) {
                r.jsonrpc = j.at("jsonrpc").get<std::string>();
            } else {
                r.jsonrpc = "2.0";
            }

            r.result = j.at("result").get<A2A::Task>();
        }
    };

    // InvalidRequestError serializers
    template<>
    struct adl_serializer<A2A::InvalidRequestError> {
        static void to_json(nlohmann::json& j, const A2A::InvalidRequestError& e)
        {
            j = {{"code", e.code}, {"message", e.message.value_or("")}};

            if (e.data) {
                j["data"] = *e.data;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::InvalidRequestError& e)
        {
            if (j.contains("data")) {
                e.data = j.at("data");
            }

            if (j.contains("message")) {
                e.message = j.at("message").get<std::string>();
            }
        }
    };

    // ServerError serializers
    template<>
    struct adl_serializer<A2A::ServerError> {
        static void to_json(nlohmann::json& j, const A2A::ServerError& e)
        {
            j = {{"code", e.code}, {"message", e.message.value_or("")}};

            if (e.data) {
                j["data"] = *e.data;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::ServerError& e)
        {
            if (j.contains("data")) {
                e.data = j.at("data");
            }

            if (j.contains("message")) {
                e.message = j.at("message").get<std::string>();
            }
        }
    };

    // InvalidParamsError serializers
    template<>
    struct adl_serializer<A2A::InvalidParamsError> {
        static void to_json(nlohmann::json& j, const A2A::InvalidParamsError& e)
        {
            j = {{"code", e.code}, {"message", e.message.value_or("")}};

            if (e.data) {
                j["data"] = *e.data;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::InvalidParamsError& e)
        {
            if (j.contains("data")) {
                e.data = j.at("data");
            }

            if (j.contains("message")) {
                e.message = j.at("message").get<std::string>();
            }
        }
    };

    // InvalidAgentIdError serializers
    template<>
    struct adl_serializer<A2A::InvalidAgentIdError> {
        static void to_json(nlohmann::json& j, const A2A::InvalidAgentIdError& e)
        {
            j = {{"code", e.code}, {"message", e.message.value_or("")}};

            if (e.data) {
                j["data"] = *e.data;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::InvalidAgentIdError& e)
        {
            if (j.contains("data")) {
                e.data = j.at("data");
            }

            if (j.contains("message")) {
                e.message = j.at("message").get<std::string>();
            }
        }
    };

    // AddExternalMemoryError serializers
    template<>
    struct adl_serializer<A2A::AddExternalMemoryError> {
        static void to_json(nlohmann::json& j, const A2A::AddExternalMemoryError& e)
        {
            j = {{"code", e.code}, {"message", e.message.value_or("")}};

            if (e.data) {
                j["data"] = *e.data;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::AddExternalMemoryError& e)
        {
            if (j.contains("data")) {
                e.data = j.at("data");
            }

            if (j.contains("message")) {
                e.message = j.at("message").get<std::string>();
            }
        }
    };

    // JSONParseError serializers
    template<>
    struct adl_serializer<A2A::JSONParseError> {
        static void to_json(nlohmann::json& j, const A2A::JSONParseError& e)
        {
            j = {{"code", e.code}, {"message", e.message.value_or("")}};

            if (e.data) {
                j["data"] = *e.data;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::JSONParseError& e)
        {
            if (j.contains("data")) {
                e.data = j.at("data");
            }

            if (j.contains("message")) {
                e.message = j.at("message").get<std::string>();
            }
        }
    };

    // InternalError serializers
    template<>
    struct adl_serializer<A2A::InternalError> {
        static void to_json(nlohmann::json& j, const A2A::InternalError& e)
        {
            j = {{"code", e.code}, {"message", e.message.value_or("")}};

            if (e.data) {
                j["data"] = *e.data;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::InternalError& e)
        {
            if (j.contains("data")) {
                e.data = j.at("data");
            }

            if (j.contains("message")) {
                e.message = j.at("message").get<std::string>();
            }
        }
    };

    // MethodNotFoundError serializers
    template<>
    struct adl_serializer<A2A::MethodNotFoundError> {
        static void to_json(nlohmann::json& j, const A2A::MethodNotFoundError& e)
        {
            j = {{"code", e.code}, {"message", e.message.value_or("")}};

            if (e.data) {
                j["data"] = *e.data;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::MethodNotFoundError& e)
        {
            if (j.contains("data")) {
                e.data = j.at("data");
            }

            if (j.contains("message")) {
                e.message = j.at("message").get<std::string>();
            }
        }
    };

    // TaskNotFoundError serializers
    template<>
    struct adl_serializer<A2A::TaskNotFoundError> {
        static void to_json(nlohmann::json& j, const A2A::TaskNotFoundError& e)
        {
            j = {{"code", e.code}, {"message", e.message.value_or("")}};

            if (e.data) {
                j["data"] = *e.data;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::TaskNotFoundError& e)
        {
            if (j.contains("data")) {
                e.data = j.at("data");
            }

            if (j.contains("message")) {
                e.message = j.at("message").get<std::string>();
            }
        }
    };

    // TaskNotCancelableError serializers
    template<>
    struct adl_serializer<A2A::TaskNotCancelableError> {
        static void to_json(nlohmann::json& j, const A2A::TaskNotCancelableError& e)
        {
            j = {{"code", e.code}, {"message", e.message.value_or("")}};

            if (e.data) {
                j["data"] = *e.data;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::TaskNotCancelableError& e)
        {
            if (j.contains("data")) {
                e.data = j.at("data");
            }

            if (j.contains("message")) {
                e.message = j.at("message").get<std::string>();
            }
        }
    };

    // PushNotificationNotSupportedError serializers
    template<>
    struct adl_serializer<A2A::PushNotificationNotSupportedError> {
        static void to_json(nlohmann::json& j, const A2A::PushNotificationNotSupportedError& e)
        {
            j = {{"code", e.code}, {"message", e.message.value_or("")}};

            if (e.data) {
                j["data"] = *e.data;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::PushNotificationNotSupportedError& e)
        {
            if (j.contains("data")) {
                e.data = j.at("data");
            }

            if (j.contains("message")) {
                e.message = j.at("message").get<std::string>();
            }
        }
    };

    // UnsupportedOperationErrorModel serializers
    template<>
    struct adl_serializer<A2A::UnsupportedOperationErrorModel> {
        static void to_json(nlohmann::json& j, const A2A::UnsupportedOperationErrorModel& e)
        {
            j = {{"code", e.code}, {"message", e.message.value_or("")}};

            if (e.data) {
                j["data"] = *e.data;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::UnsupportedOperationErrorModel& e)
        {
            if (j.contains("data")) {
                e.data = j.at("data");
            }

            if (j.contains("message")) {
                e.message = j.at("message").get<std::string>();
            }
        }
    };

    // ContentTypeNotSupportedError serializers
    template<>
    struct adl_serializer<A2A::ContentTypeNotSupportedError> {
        static void to_json(nlohmann::json& j, const A2A::ContentTypeNotSupportedError& e)
        {
            j = {{"code", e.code}, {"message", e.message.value_or("")}};

            if (e.data) {
                j["data"] = *e.data;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::ContentTypeNotSupportedError& e)
        {
            if (j.contains("data")) {
                e.data = j.at("data");
            }

            if (j.contains("message")) {
                e.message = j.at("message").get<std::string>();
            }
        }
    };

    // InvalidAgentResponseError serializers
    template<>
    struct adl_serializer<A2A::InvalidAgentResponseError> {
        static void to_json(nlohmann::json& j, const A2A::InvalidAgentResponseError& e)
        {
            j = {{"code", e.code}, {"message", e.message.value_or("")}};

            if (e.data) {
                j["data"] = *e.data;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::InvalidAgentResponseError& e)
        {
            if (j.contains("data")) {
                e.data = j.at("data");
            }

            if (j.contains("message")) {
                e.message = j.at("message").get<std::string>();
            }
        }
    };

    // AuthenticatedExtendedCardNotConfiguredError serializers
    template<>
    struct adl_serializer<A2A::AuthenticatedExtendedCardNotConfiguredError> {
        static void to_json(nlohmann::json& j, const A2A::AuthenticatedExtendedCardNotConfiguredError& e)
        {
            j = {{"code", e.code}, {"message", e.message.value_or("")}};

            if (e.data) {
                j["data"] = *e.data;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::AuthenticatedExtendedCardNotConfiguredError& e)
        {
            if (j.contains("data")) {
                e.data = j.at("data");
            }

            if (j.contains("message")) {
                e.message = j.at("message").get<std::string>();
            }
        }
    };

    // APIKeySecurityScheme serializers
    template<>
    struct adl_serializer<A2A::APIKeySecurityScheme> {
        static void to_json(nlohmann::json& j, const A2A::APIKeySecurityScheme& s)
        {
            j = nlohmann::json{{"in", s.in_}, {"name", s.name}, {"type", s.type}};

            if (s.description) {
                j["description"] = *s.description;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::APIKeySecurityScheme& s)
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
    };

    // HTTPAuthSecurityScheme serializers
    template<>
    struct adl_serializer<A2A::HTTPAuthSecurityScheme> {
        static void to_json(nlohmann::json& j, const A2A::HTTPAuthSecurityScheme& s)
        {
            j = {{"scheme", s.scheme}, {"type", s.type}};

            if (s.description) {
                j["description"] = *s.description;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::HTTPAuthSecurityScheme& s)
        {
            if (j.contains("description")) {
                s.description = j.at("description").get<std::string>();
            }

            j.at("scheme").get_to(s.scheme);
            if (j.contains("type")) {
                s.type = j.at("type").get<std::string>();
            }
        }
    };

    // ImplicitOAuthFlow serializers
    template<>
    struct adl_serializer<A2A::ImplicitOAuthFlow> {
        static void to_json(nlohmann::json& j, const A2A::ImplicitOAuthFlow& f)
        {
            j = {{"authorizationUrl", f.authorizationUrl}, {"scopes", f.scopes}};

            if (f.refreshUrl) {
                j["refreshUrl"] = *f.refreshUrl;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::ImplicitOAuthFlow& f)
        {
            j.at("authorizationUrl").get_to(f.authorizationUrl);
            if (j.contains("refreshUrl")) {
                f.refreshUrl = j.at("refreshUrl").get<std::string>();
            }

            j.at("scopes").get_to(f.scopes);
        }
    };

    // AuthorizationCodeOAuthFlow serializers
    template<>
    struct adl_serializer<A2A::AuthorizationCodeOAuthFlow> {
        static void to_json(nlohmann::json& j, const A2A::AuthorizationCodeOAuthFlow& f)
        {
            j = {{"authorizationUrl", f.authorizationUrl}, {"scopes", f.scopes}, {"tokenUrl", f.tokenUrl}};

            if (f.refreshUrl) {
                j["refreshUrl"] = *f.refreshUrl;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::AuthorizationCodeOAuthFlow& f)
        {
            j.at("authorizationUrl").get_to(f.authorizationUrl);
            if (j.contains("refreshUrl")) {
                f.refreshUrl = j.at("refreshUrl").get<std::string>();
            }

            j.at("scopes").get_to(f.scopes);
            j.at("tokenUrl").get_to(f.tokenUrl);
        }
    };

    // PasswordOAuthFlow serializers
    template<>
    struct adl_serializer<A2A::PasswordOAuthFlow> {
        static void to_json(nlohmann::json& j, const A2A::PasswordOAuthFlow& f)
        {
            j = {{"scopes", f.scopes}, {"tokenUrl", f.tokenUrl}};

            if (f.refreshUrl) {
                j["refreshUrl"] = *f.refreshUrl;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::PasswordOAuthFlow& f)
        {
            if (j.contains("refreshUrl")) {
                f.refreshUrl = j.at("refreshUrl").get<std::string>();
            }

            j.at("scopes").get_to(f.scopes);
            j.at("tokenUrl").get_to(f.tokenUrl);
        }
    };

    // ClientCredentialsOAuthFlow serializers
    template<>
    struct adl_serializer<A2A::ClientCredentialsOAuthFlow> {
        static void to_json(nlohmann::json& j, const A2A::ClientCredentialsOAuthFlow& f)
        {
            j = {{"scopes", f.scopes}, {"tokenUrl", f.tokenUrl}};

            if (f.refreshUrl) {
                j["refreshUrl"] = *f.refreshUrl;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::ClientCredentialsOAuthFlow& f)
        {
            if (j.contains("refreshUrl")) {
                f.refreshUrl = j.at("refreshUrl").get<std::string>();
            }

            j.at("scopes").get_to(f.scopes);
            j.at("tokenUrl").get_to(f.tokenUrl);
        }
    };

    // OAuthFlows serializers
    template<>
    struct adl_serializer<A2A::OAuthFlows> {
        static void to_json(nlohmann::json& j, const A2A::OAuthFlows& f)
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

        static void from_json(const nlohmann::json& j, A2A::OAuthFlows& f)
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
    };

    // OAuth2SecurityScheme serializers
    template<>
    struct adl_serializer<A2A::OAuth2SecurityScheme> {
        static void to_json(nlohmann::json& j, const A2A::OAuth2SecurityScheme& s)
        {
            j = {{"flows", s.flows}, {"type", s.type}};

            if (s.description) {
                j["description"] = *s.description;
            }

            if (s.oauth2MetadataUrl) {
                j["oauth2MetadataUrl"] = *s.oauth2MetadataUrl;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::OAuth2SecurityScheme& s)
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
    };

    // OpenIdConnectSecurityScheme serializers
    template<>
    struct adl_serializer<A2A::OpenIdConnectSecurityScheme> {
        static void to_json(nlohmann::json& j, const A2A::OpenIdConnectSecurityScheme& s)
        {
            j = {{"openIdConnectUrl", s.openIdConnectUrl}, {"type", s.type}};

            if (s.description) {
                j["description"] = *s.description;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::OpenIdConnectSecurityScheme& s)
        {
            if (j.contains("description")) {
                s.description = j.at("description").get<std::string>();
            }

            j.at("openIdConnectUrl").get_to(s.openIdConnectUrl);
            if (j.contains("type")) {
                s.type = j.at("type").get<std::string>();
            }
        }
    };

    // MutualTLSSecurityScheme serializers
    template<>
    struct adl_serializer<A2A::MutualTLSSecurityScheme> {
        static void to_json(nlohmann::json& j, const A2A::MutualTLSSecurityScheme& s)
        {
            j = {{"type", s.type}};
            if (s.description) {
                j["description"] = *s.description;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::MutualTLSSecurityScheme& s)
        {
            if (j.contains("description")) {
                s.description = j.at("description").get<std::string>();
            }

            if (j.contains("type")) {
                s.type = j.at("type").get<std::string>();
            }
        }
    };

    // SecurityScheme serializers
    template<>
    struct adl_serializer<A2A::SecurityScheme> {
        static void to_json(nlohmann::json& j, const A2A::SecurityScheme& ss)
        {
            std::visit(
                [&](auto&& s) {
                    nlohmann::json js;
                    to_json(js, s);
                    j = js;
                },
                ss.v);
        }

        static void from_json(const nlohmann::json& j, A2A::SecurityScheme& ss)
        {
            auto t = j.value<std::string>("type", "");
            if (t == "apiKey") {
                A2A::APIKeySecurityScheme s;
                from_json(j, s);
                ss.v = s;
            } else if (t == "http") {
                A2A::HTTPAuthSecurityScheme s;
                from_json(j, s);
                ss.v = s;
            } else if (t == "oauth2") {
                A2A::OAuth2SecurityScheme s;
                from_json(j, s);
                ss.v = s;
            } else if (t == "openIdConnect") {
                A2A::OpenIdConnectSecurityScheme s;
                from_json(j, s);
                ss.v = s;
            } else if (t == "mutualTLS") {
                A2A::MutualTLSSecurityScheme s;
                from_json(j, s);
                ss.v = s;
            } else {
                throw std::runtime_error("UNKNOWN security scheme type");
            }
        }
    };

    // AgentSkill serializers
    template<>
    struct adl_serializer<A2A::AgentSkill> {
        static void to_json(nlohmann::json& j, const A2A::AgentSkill& s)
        {
            if (s.id.empty()) {
                throw std::runtime_error("AgentSkill.id cannot be empty");
            }
            if (s.name.empty()) {
                throw std::runtime_error("AgentSkill.name cannot be empty");
            }

            j = nlohmann::json{{"id", s.id}, {"name", s.name}, {"description", s.description}, {"tags", s.tags}};

            if (s.examples) {
                j["examples"] = *s.examples;
            }

            if (s.inputModes) {
                j["inputModes"] = *s.inputModes;
            }

            if (s.outputModes) {
                j["outputModes"] = *s.outputModes;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::AgentSkill& s)
        {
            if (!j.contains("id") || !j.contains("name") || !j.contains("description") || !j.contains("tags")) {
                throw std::runtime_error("AgentSkill missing required fields");
            }

            j.at("id").get_to(s.id);
            j.at("name").get_to(s.name);
            j.at("description").get_to(s.description);
            j.at("tags").get_to(s.tags);

            if (s.id.empty()) {
                throw std::runtime_error("AgentSkill.id cannot be empty");
            }
            if (s.name.empty()) {
                throw std::runtime_error("AgentSkill.name cannot be empty");
            }

            if (j.contains("examples")) {
                s.examples = j.at("examples").get<std::vector<std::string>>();
            }

            if (j.contains("inputModes")) {
                s.inputModes = j.at("inputModes").get<std::vector<std::string>>();
            }

            if (j.contains("outputModes")) {
                s.outputModes = j.at("outputModes").get<std::vector<std::string>>();
            }
        }
    };

    // AgentExtension serializers
    template<>
    struct adl_serializer<A2A::AgentExtension> {
        static void to_json(nlohmann::json& j, const A2A::AgentExtension& e)
        {
            if (e.uri.empty()) {
                throw std::runtime_error("AgentExtension.uri cannot be empty");
            }

            j = nlohmann::json{{"uri", e.uri}};
            if (e.required) {
                j["required"] = *e.required;
            }
            if (e.description) {
                j["description"] = *e.description;
            }
            if (e.params) {
                j["params"] = *e.params;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::AgentExtension& e)
        {
            if (!j.contains("uri")) {
                throw std::runtime_error("AgentExtension must contain 'uri' field");
            }
            j.at("uri").get_to(e.uri);
            if (e.uri.empty()) {
                throw std::runtime_error("AgentExtension.uri cannot be empty");
            }

            if (j.contains("required")) {
                e.required = j.at("required").get<bool>();
            }
            if (j.contains("description")) {
                e.description = j.at("description").get<std::string>();
            }
            if (j.contains("params")) {
                e.params = j.at("params");
            }
        }
    };

    // AgentCapabilities serializers
    template<>
    struct adl_serializer<A2A::AgentCapabilities> {
        static void to_json(nlohmann::json& j, const A2A::AgentCapabilities& c)
        {
            j = nlohmann::json{};

            if (c.streaming) {
                j["streaming"] = *c.streaming;
            }

            if (c.pushNotifications) {
                j["pushNotifications"] = *c.pushNotifications;
            }

            if (c.stateTransitionHistory) {
                j["stateTransitionHistory"] = *c.stateTransitionHistory;
            }

            if (c.extensions) {
                j["extensions"] = *c.extensions;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::AgentCapabilities& c)
        {
            if (j.contains("streaming")) {
                c.streaming = j.at("streaming").get<bool>();
            }

            if (j.contains("pushNotifications")) {
                c.pushNotifications = j.at("pushNotifications").get<bool>();
            }

            if (j.contains("stateTransitionHistory")) {
                c.stateTransitionHistory = j.at("stateTransitionHistory").get<bool>();
            }

            if (j.contains("extensions")) {
                c.extensions = j.at("extensions").get<std::vector<AgentExtension>>();
            }
        }
    };

    // AgentProvider serializers
    template<>
    struct adl_serializer<A2A::AgentProvider> {
        static void to_json(nlohmann::json& j, const A2A::AgentProvider& p)
        {
            j = {{"organization", p.organization}, {"url", p.url}};
        }

        static void from_json(const nlohmann::json& j, A2A::AgentProvider& p)
        {
            if (!j.contains("organization") || !j.contains("url")) {
                throw std::runtime_error("AgentProvider missing required fields");
            }
            j.at("organization").get_to(p.organization);
            j.at("url").get_to(p.url);
        }
    };

    // AgentCard serializers
    template<>
    struct adl_serializer<A2A::AgentCard> {
        static void to_json(nlohmann::json& j, const A2A::AgentCard& c)
        {
            j = nlohmann::json{
                {"protocolVersion", c.protocolVersion},
                {"name", c.name},
                {"description", c.description},
                {"url", c.url},
                {"version", c.version},
                {"capabilities", c.capabilities},
                {"defaultInputModes", c.defaultInputModes},
                {"defaultOutputModes", c.defaultOutputModes},
                {"skills", c.skills}
            };

            if (c.provider) {
                j["provider"] = *c.provider;
            }
            if (c.iconUrl) {
                j["iconUrl"] = *c.iconUrl;
            }
            if (c.documentationUrl) {
                j["documentationUrl"] = *c.documentationUrl;
            }
            if (c.securitySchemes) {
                j["securitySchemes"] = *c.securitySchemes;
            }
            if (c.security) {
                j["security"] = *c.security;
            }
            if (c.supportsAuthenticatedExtendedCard) {
                j["supportsAuthenticatedExtendedCard"] = *c.supportsAuthenticatedExtendedCard;
            }
            if (c.preferredTransport) {
                j["preferredTransport"] = *c.preferredTransport;
            }
        }

        static void from_json(const nlohmann::json& j, A2A::AgentCard& c)
        {
            const std::vector<std::string> required_fields = {
                "protocolVersion", "name", "description", "url", "version", "capabilities",
                "defaultInputModes", "defaultOutputModes", "skills"
            };

            for (const auto& field : required_fields) {
                if (!j.contains(field)) {
                    throw std::runtime_error("AgentCard missing required field: " + field);
                }
            }

            j.at("protocolVersion").get_to(c.protocolVersion);
            j.at("name").get_to(c.name);
            j.at("description").get_to(c.description);
            j.at("url").get_to(c.url);
            j.at("version").get_to(c.version);
            j.at("capabilities").get_to(c.capabilities);
            j.at("defaultInputModes").get_to(c.defaultInputModes);
            j.at("defaultOutputModes").get_to(c.defaultOutputModes);
            j.at("skills").get_to(c.skills);

            if (j.contains("provider")) {
                c.provider = j.at("provider").get<AgentProvider>();
            }
            if (j.contains("iconUrl")) {
                c.iconUrl = j.at("iconUrl").get<std::string>();
            }
            if (j.contains("documentationUrl")) {
                c.documentationUrl = j.at("documentationUrl").get<std::string>();
            }
            if (j.contains("securitySchemes")) {
                c.securitySchemes = j.at("securitySchemes").get<std::map<std::string, SecurityScheme>>();
            }
            if (j.contains("security")) {
                c.security = j.at("security").get<std::vector<nlohmann::json>>();
            }
            if (j.contains("supportsAuthenticatedExtendedCard")) {
                c.supportsAuthenticatedExtendedCard = j.at("supportsAuthenticatedExtendedCard").get<bool>();
            }
            if (j.contains("preferredTransport")) {
                c.preferredTransport = j.at("preferredTransport").get<std::string>();
            }
        }
    };
} // namespace nlohmann

#endif // A2A_TYPES_SERIALIZATION_H