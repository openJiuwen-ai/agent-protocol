/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "jsonrpc.h"

#include <cstdio>
#include <stdexcept>
#include <utility>
#include <variant>
#include <nlohmann/json-schema.hpp>

#include "common_type.h"
#include "mcp_error.h"
#include "mcp_log.h"
#include "mcp_type.h"

namespace nlohmann {

// JSON serialization specialization for RequestId
template <>
struct adl_serializer<Mcp::RequestId> {
    static void to_json(json& j, const Mcp::RequestId& id)
    {
        std::visit([&j](const auto& value) { j = value; }, id);
    }

    static void from_json(const json& j, Mcp::RequestId& id)
    {
        if (j.is_string()) {
            id = j.get<std::string>();
        } else if (j.is_number_integer()) {
            id = j.get<int64_t>();
        } else {
            id = int64_t(0);
        }
    }
};

// Convert a JSON value map into a stringified map to match public string meta.
static inline std::unordered_map<std::string, std::string> JsonMapToStringMap
(const std::unordered_map<std::string, json>& src)
{
    std::unordered_map<std::string, std::string> out;
    out.reserve(src.size());
    for (const auto& [key, value] : src) {
        out[key] = value.dump();
    }
    return out;
}
// ListToolsRequest -> {cursor?}
template <>
struct adl_serializer<Mcp::ListToolsRequest> {
    static void to_json(json& j, const Mcp::ListToolsRequest& req)
    {
        if (req.params_) {
            auto p = static_cast<const Mcp::RequestParams*>(req.params_.get());
            if (p->cursor.has_value()) {
                j["cursor"] = p->cursor.value();
            }
        }
    }

    static void from_json(const json& j, Mcp::ListToolsRequest& req)
    {
        if (j.contains("method")) {
            j.at("method").get_to(req.method_);
        }
        if (j.contains("params")) {
            const auto& paramsJson = j.at("params");
            if (paramsJson.contains("cursor")) {
                auto p = std::make_unique<Mcp::RequestParams>();
                p->cursor = paramsJson.at("cursor").get<std::string>();
                req.params_ = std::move(p);
            }
        }
    }
};

template <>
struct adl_serializer<Mcp::InitializeRequestParams> {
    static void to_json(json& j, const Mcp::InitializeRequestParams& p)
    {
        j["protocolVersion"] = p.protocolVersion_;
        j["capabilities"] = json::object();
        j["clientInfo"] = {
            {"name", p.clientInfo_.name},
            {"version", p.clientInfo_.version},
        };
    }

    static void from_json(const json& j, Mcp::InitializeRequestParams& p)
    {
        if (j.contains("protocolVersion")) {
            p.protocolVersion_ = j.at("protocolVersion").get<std::string>();
        } else {
            p.protocolVersion_ = Mcp::DEFAULT_PROTOCOL_VERSION;
        }
        p.capabilities_ = Mcp::ClientCapabilities{};
        if (j.contains("clientInfo")) {
            const auto& ci = j.at("clientInfo");
            p.clientInfo_.name = ci.value("name", std::string{});
            p.clientInfo_.version = ci.value("version", std::string{});
        }
    }
};

template <>
struct adl_serializer<Mcp::InitializeResult> {
    static void to_json(json& j, const Mcp::InitializeResult& r)
    {
        j["protocolVersion"] = r.protocolVersion;
        j["capabilities"] = json::object();
        j["serverInfo"] = {
            {"name", r.serverInfo.name},
            {"version", r.serverInfo.version},
        };
        if (r.instructions.has_value()) {
            j["instructions"] = r.instructions.value();
        }
        if (r.meta.has_value()) {
            j["_meta"] = r.meta.value();
        }
    }

    static void from_json(const json& j, Mcp::InitializeResult& r)
    {
        if (j.contains("protocolVersion")) {
            r.protocolVersion = j.at("protocolVersion").get<std::string>();
        } else {
            r.protocolVersion = Mcp::DEFAULT_PROTOCOL_VERSION;
        }
        r.capabilities = Mcp::ServerCapabilities{};
        if (j.contains("serverInfo")) {
            const auto& si = j.at("serverInfo");
            r.serverInfo.name = si.value("name", std::string{});
            r.serverInfo.version = si.value("version", std::string{});
        }
        if (j.contains("instructions")) {
            r.instructions = j.at("instructions").get<std::string>();
        }
        if (j.contains("_meta")) {
            auto metaJson = j.at("_meta").get<std::unordered_map<std::string, json>>();
            r.meta = JsonMapToStringMap(metaJson);
        }
    }
};

// ---- Prompt-related helper types and serializers ----
// RoleType mapping helpers
static inline std::string McpRoleTypeToString(Mcp::RoleType role)
{
    switch (role) {
        case Mcp::RoleType::USER:
            return "user";
        case Mcp::RoleType::ASSISTANT:
            return "assistant";
    }
    return "user";
}

static inline Mcp::RoleType McpRoleTypeFromString(const std::string& s)
{
    if (s == "user") {
        return Mcp::RoleType::USER;
    }
    if (s == "assistant") {
        return Mcp::RoleType::ASSISTANT;
    }
    return Mcp::RoleType::USER;
}

// Icon
template <>
struct adl_serializer<Mcp::Icon> {
    static void to_json(json& j, const Mcp::Icon& i)
    {
        j["src"] = i.src;
        if (i.mimeType.has_value()) {
            j["mimeType"] = i.mimeType.value();
        }
        if (i.sizes.has_value()) {
            j["sizes"] = i.sizes.value();
        }
        if (i.theme.has_value()) {
            j["theme"] = i.theme.value();
        }
    }

    static void from_json(const json& j, Mcp::Icon& i)
    {
        j.at("src").get_to(i.src);
        if (j.contains("mimeType")) {
            i.mimeType = j.at("mimeType").get<std::string>();
        }
        if (j.contains("sizes")) {
            i.sizes = j.at("sizes").get<std::vector<std::string>>();
        }
        if (j.contains("theme")) {
            i.theme = j.at("theme").get<std::string>();
        }
    }
};

// Annotations
template <>
struct adl_serializer<Mcp::Annotations> {
    static void to_json(json& j, const Mcp::Annotations& a)
    {
        if (a.audience.has_value()) {
            j["audience"] = json::array();
            for (const auto& r : a.audience.value()) {
                j["audience"].push_back(McpRoleTypeToString(r));
            }
        }
        if (a.lastModified.has_value()) {
            j["lastModified"] = a.lastModified.value();
        }
        if (a.priority.has_value()) {
            j["priority"] = a.priority.value();
        }
    }

    static void from_json(const json& j, Mcp::Annotations& a)
    {
        if (j.contains("audience") && j.at("audience").is_array()) {
            std::vector<Mcp::RoleType> roles;
            for (const auto& v : j.at("audience")) {
                roles.push_back(McpRoleTypeFromString(v.get<std::string>()));
            }
            a.audience = std::move(roles);
        } else {
            a.audience = std::nullopt;
        }
        if (j.contains("lastModified")) {
            a.lastModified = j.at("lastModified").get<std::string>();
        } else {
            a.lastModified = std::nullopt;
        }
        if (j.contains("priority")) {
            a.priority = j.at("priority").get<double>();
        } else {
            a.priority = std::nullopt;
        }
    }
};

// ResourceInfo
template <>
struct adl_serializer<Mcp::ResourceInfo> {
    static void to_json(json& j, const Mcp::ResourceInfo& r)
    {
        j["uri"] = r.uri;
        j["name"] = r.name;
        if (r.title.has_value()) {
            j["title"] = r.title.value();
        }
        if (r.description.has_value()) {
            j["description"] = r.description.value();
        }
        if (r.mimeType.has_value()) {
            j["mimeType"] = r.mimeType.value();
        }
        if (r.size.has_value()) {
            j["size"] = r.size.value();
        }
        if (r.icons.has_value()) {
            j["icons"] = r.icons.value();
        }
        if (r.annotations.has_value()) {
            j["annotations"] = r.annotations.value();
        }
    }

    static void from_json(const json& j, Mcp::ResourceInfo& r)
    {
        j.at("uri").get_to(r.uri);
        j.at("name").get_to(r.name);
        if (j.contains("title")) {
            r.title = j.at("title").get<std::string>();
        } else {
            r.title = std::nullopt;
        }
        if (j.contains("description")) {
            r.description = j.at("description").get<std::string>();
        } else {
            r.description = std::nullopt;
        }
        if (j.contains("mimeType")) {
            r.mimeType = j.at("mimeType").get<std::string>();
        } else {
            r.mimeType = std::nullopt;
        }
        if (j.contains("size")) {
            r.size = j.at("size").get<std::int64_t>();
        } else {
            r.size = std::nullopt;
        }
        if (j.contains("icons")) {
            r.icons = j.at("icons").get<std::vector<Mcp::Icon>>();
        } else {
            r.icons = std::nullopt;
        }
        if (j.contains("annotations")) {
            r.annotations = j.at("annotations").get<Mcp::Annotations>();
        } else {
            r.annotations = std::nullopt;
        }
    }
};

// ResourceLink
template <>
struct adl_serializer<Mcp::ResourceLink> {
    static void to_json(json& j, const Mcp::ResourceLink& l)
    {
        j["type"] = l.type;
        j["uri"] = l.uri;
        j["name"] = l.name;
        if (l.title.has_value()) {
            j["title"] = l.title.value();
        }
        if (l.description.has_value()) {
            j["description"] = l.description.value();
        }
        if (l.mimeType.has_value()) {
            j["mimeType"] = l.mimeType.value();
        }
        if (l.size.has_value()) {
            j["size"] = l.size.value();
        }
        if (l.icons.has_value()) {
            j["icons"] = l.icons.value();
        }
        if (l.annotations.has_value()) {
            j["annotations"] = l.annotations.value();
        }
    }

    static void from_json(const json& j, Mcp::ResourceLink& l)
    {
        l.type = j.value("type", std::string("resource_link"));
        j.at("uri").get_to(l.uri);
        j.at("name").get_to(l.name);
        if (j.contains("title")) {
            l.title = j.at("title").get<std::string>();
        } else {
            l.title = std::nullopt;
        }
        if (j.contains("description")) {
            l.description = j.at("description").get<std::string>();
        } else {
            l.description = std::nullopt;
        }
        if (j.contains("mimeType")) {
            l.mimeType = j.at("mimeType").get<std::string>();
        } else {
            l.mimeType = std::nullopt;
        }
        if (j.contains("size")) {
            l.size = j.at("size").get<std::int64_t>();
        } else {
            l.size = std::nullopt;
        }
        if (j.contains("icons")) {
            l.icons = j.at("icons").get<std::vector<Mcp::Icon>>();
        } else {
            l.icons = std::nullopt;
        }
        if (j.contains("annotations")) {
            l.annotations = j.at("annotations").get<Mcp::Annotations>();
        } else {
            l.annotations = std::nullopt;
        }
    }
};

// TextContent
template <>
struct adl_serializer<Mcp::TextContent> {
    static void to_json(json& j, const Mcp::TextContent& c)
    {
        j["type"] = c.type;
        j["text"] = c.text;
        if (c.annotations.has_value()) {
            j["annotations"] = c.annotations.value();
        }
    }

    static void from_json(const json& j, Mcp::TextContent& c)
    {
        c.type = j.value("type", std::string("text"));
        c.text = j.value("text", std::string());
        if (j.contains("annotations")) {
            c.annotations = j.at("annotations").get<Mcp::Annotations>();
        } else {
            c.annotations = std::nullopt;
        }
    }
};

// ImageContent
template <>
struct adl_serializer<Mcp::ImageContent> {
    static void to_json(json& j, const Mcp::ImageContent& c)
    {
        j["type"] = c.type;
        j["data"] = c.data;
        j["mimeType"] = c.mimeType;
        if (c.annotations.has_value()) {
            j["annotations"] = c.annotations.value();
        }
    }

    static void from_json(const json& j, Mcp::ImageContent& c)
    {
        c.type = j.value("type", std::string("image"));
        c.data = j.value("data", std::string());
        j.at("mimeType").get_to(c.mimeType);
        if (j.contains("annotations")) {
            c.annotations = j.at("annotations").get<Mcp::Annotations>();
        } else {
            c.annotations = std::nullopt;
        }
    }
};

// AudioContent
template <>
struct adl_serializer<Mcp::AudioContent> {
    static void to_json(json& j, const Mcp::AudioContent& c)
    {
        j["type"] = c.type;
        j["data"] = c.data;
        j["mimeType"] = c.mimeType;
        if (c.annotations.has_value()) {
            j["annotations"] = c.annotations.value();
        }
    }

    static void from_json(const json& j, Mcp::AudioContent& c)
    {
        c.type = j.value("type", std::string("audio"));
        c.data = j.value("data", std::string());
        j.at("mimeType").get_to(c.mimeType);
        if (j.contains("annotations")) {
            c.annotations = j.at("annotations").get<Mcp::Annotations>();
        } else {
            c.annotations = std::nullopt;
        }
    }
};

// TextResourceContents
template <>
struct adl_serializer<Mcp::TextResourceContents> {
    static void to_json(json& j, const Mcp::TextResourceContents& c)
    {
        j["uri"] = c.uri;
        j["text"] = c.text;
        if (c.mimeType.has_value()) {
            j["mimeType"] = c.mimeType.value();
        }
    }

    static void from_json(const json& j, Mcp::TextResourceContents& c)
    {
        j.at("uri").get_to(c.uri);
        c.text = j.value("text", std::string());
        if (j.contains("mimeType")) {
            c.mimeType = j.at("mimeType").get<std::string>();
        } else {
            c.mimeType = std::nullopt;
        }
    }
};

// BlobResourceContents
template <>
struct adl_serializer<Mcp::BlobResourceContents> {
    static void to_json(json& j, const Mcp::BlobResourceContents& c)
    {
        j["uri"] = c.uri;
        j["blob"] = c.blob;
        if (c.mimeType.has_value()) {
            j["mimeType"] = c.mimeType.value();
        }
    }

    static void from_json(const json& j, Mcp::BlobResourceContents& c)
    {
        j.at("uri").get_to(c.uri);
        c.blob = j.value("blob", std::string());
        if (j.contains("mimeType")) {
            c.mimeType = j.at("mimeType").get<std::string>();
        } else {
            c.mimeType = std::nullopt;
        }
    }
};

// ResourceContents (variant)
template <>
struct adl_serializer<Mcp::ResourceContents> {
    static void to_json(json& j, const Mcp::ResourceContents& v)
    {
        std::visit([&](auto&& arg) { j = json(arg); }, v);
    }

    static void from_json(const json& j, Mcp::ResourceContents& v)
    {
        if (j.contains("text")) {
            v = j.get<Mcp::TextResourceContents>();
        } else if (j.contains("blob")) {
            v = j.get<Mcp::BlobResourceContents>();
        }
    }
};

// EmbeddedResource
template <>
struct adl_serializer<Mcp::EmbeddedResource> {
    static void to_json(json& j, const Mcp::EmbeddedResource& c)
    {
        j["type"] = c.type;
        j["resource"] = c.resource;

        if (c.annotations.has_value()) {
            j["annotations"] = c.annotations.value();
        }
    }

    static void from_json(const json& j, Mcp::EmbeddedResource& c)
    {
        c.type = j.value("type", std::string("resource"));
        c.resource = j.at("resource").get<Mcp::ResourceContents>();

        if (j.contains("annotations") && !j["annotations"].is_null()) {
            c.annotations = j.at("annotations").get<Mcp::Annotations>();
        } else {
            c.annotations = std::nullopt;
        }
    }
};

// ContentType (variant)
template <>
struct adl_serializer<Mcp::ContentType> {
    static void to_json(json& j, const Mcp::ContentType& v)
    {
        std::visit([&](auto&& arg) { j = json(arg); }, v);
    }

    static void from_json(const json& j, Mcp::ContentType& v)
    {
        const auto type = j.value("type", std::string());
        if (type == "text") {
            Mcp::TextContent c = j.get<Mcp::TextContent>();
            v = c;
        } else if (type == "image") {
            Mcp::ImageContent c = j.get<Mcp::ImageContent>();
            v = c;
        } else if (type == "audio") {
            Mcp::AudioContent c = j.get<Mcp::AudioContent>();
            v = c;
        } else if (type == "resource") {
            Mcp::EmbeddedResource c = j.get<Mcp::EmbeddedResource>();
            v = c;
        } else if (type == "resource_link") {
            Mcp::ResourceLink c = j.get<Mcp::ResourceLink>();
            v = c;
        } else {
            Mcp::TextContent c;
            c.text = j.dump();
            v = c;
        }
    }
};

// ReadResourceResult
template <>
struct adl_serializer<Mcp::ReadResourceResult> {
    static void to_json(json& j, const Mcp::ReadResourceResult& r)
    {
        j["contents"] = r.contents;
    }

    static void from_json(const json& j, Mcp::ReadResourceResult& r)
    {
        if (j.contains("contents")) {
            j.at("contents").get_to(r.contents);
        } else {
            r.contents.clear();
        }
    }
};

// ListResourcesResult
template <>
struct adl_serializer<Mcp::ListResourcesResult> {
    static void to_json(json& j, const Mcp::ListResourcesResult& r)
    {
        j["resources"] = r.resources;
        if (r.meta.has_value()) {
            j["_meta"] = r.meta.value();
        }
        if (r.nextCursor.has_value()) {
            j["nextCursor"] = r.nextCursor.value();
        }
    }

    static void from_json(const json& j, Mcp::ListResourcesResult& r)
    {
        if (j.contains("resources")) {
            j.at("resources").get_to(r.resources);
        } else {
            r.resources.clear();
        }
        if (j.contains("_meta")) {
            auto metaJson = j.at("_meta").get<std::unordered_map<std::string, json>>();
            r.meta = JsonMapToStringMap(metaJson);
        } else {
            r.meta.reset();
        }
        if (j.contains("nextCursor")) {
            r.nextCursor = j.at("nextCursor").get<std::string>();
        } else {
            r.nextCursor.reset();
        }
    }
};

template <>
struct adl_serializer<Mcp::CallToolRequest> {
    static void to_json(json& j, const Mcp::CallToolRequest& req)
    {
        if (req.params_) {
            auto p = static_cast<const Mcp::CallToolParams*>(req.params_.get());
            j["name"] = p->name;
            if (p->arguments.has_value()) {
                j["arguments"] = p->arguments.value();
            }
        }
    }

    static void from_json(const json& j, Mcp::CallToolRequest& req)
    {
        j.at("method").get_to(req.method_);

        if (j.contains("params")) {
            const auto& paramsJson = j.at("params");
            std::optional<Mcp::JsonValue> args;
            if (paramsJson.contains("arguments")) {
                args = paramsJson.at("arguments");
            }

            req.params_ =
                std::make_unique<Mcp::CallToolParams>(paramsJson.at("name").get<std::string>(), std::move(args));
        }
    }
};

// CallToolResult
template <>
struct adl_serializer<Mcp::CallToolResult> {
    static void to_json(json& j, const Mcp::CallToolResult& r)
    {
        j["content"] = r.content;
        if (r.structuredContent.has_value()) {
            // Prefer sending structuredContent as parsed JSON when possible
            try {
                j["structuredContent"] = json::parse(r.structuredContent.value());
            } catch (const json::exception&) {
                // Fallback to string if parsing fails
                j["structuredContent"] = r.structuredContent.value();
            }
        }
        j["isError"] = r.isError;
        if (r.meta.has_value()) {
            j["_meta"] = r.meta.value();
        }
    }

    static void from_json(const json& j, Mcp::CallToolResult& r)
    {
        if (j.contains("content")) {
            const auto& c = j.at("content");
            if (c.is_array()) {
                c.get_to(r.content);
            } else if (c.is_string()) {
                // Compatibility: if server returns plain string, wrap as text content
                Mcp::TextContent tc;
                tc.text = c.get<std::string>();
                r.content.clear();
                r.content.push_back(tc);
            } else {
                r.content.clear();
            }
        } else {
            r.content.clear();
        }
        if (j.contains("structuredContent")) {
            const auto& sc = j.at("structuredContent");
            if (sc.is_object() || sc.is_array()) {
                // Store internally as string to keep type stable
                r.structuredContent = sc.dump();
            } else {
                r.structuredContent = sc.get<std::string>();
            }
        }
        r.isError = j.value("isError", false);
        if (j.contains("_meta")) {
            auto metaJson = j.at("_meta").get<std::unordered_map<std::string, json>>();
            r.meta = JsonMapToStringMap(metaJson);
        }
    }
};

// ToolAnnotations
template <>
struct adl_serializer<Mcp::ToolAnnotations> {
    static void to_json(json& j, const Mcp::ToolAnnotations& ta)
    {
        if (ta.title.has_value()) {
            j["title"] = ta.title.value();
        }
        if (ta.readOnlyHint.has_value()) {
            j["readOnlyHint"] = ta.readOnlyHint.value();
        }
        if (ta.destructiveHint.has_value()) {
            j["destructiveHint"] = ta.destructiveHint.value();
        }
        if (ta.idempotentHint.has_value()) {
            j["idempotentHint"] = ta.idempotentHint.value();
        }
        if (ta.openWorldHint.has_value()) {
            j["openWorldHint"] = ta.openWorldHint.value();
        }
    }

    static void from_json(const json& j, Mcp::ToolAnnotations& ta)
    {
        if (j.contains("title")) {
            ta.title = j.at("title").get<std::string>();
        }
        if (j.contains("readOnlyHint")) {
            ta.readOnlyHint = j.at("readOnlyHint").get<bool>();
        }
        if (j.contains("destructiveHint")) {
            ta.destructiveHint = j.at("destructiveHint").get<bool>();
        }
        if (j.contains("idempotentHint")) {
            ta.idempotentHint = j.at("idempotentHint").get<bool>();
        }
        if (j.contains("openWorldHint")) {
            ta.openWorldHint = j.at("openWorldHint").get<bool>();
        }
    }
};

// Tool
template <>
struct adl_serializer<Mcp::Tool> {
    static void to_json(json& j, const Mcp::Tool& t)
    {
        j["name"] = t.name;
        if (t.title.has_value()) {
            j["title"] = t.title.value();
        }
        if (t.description.has_value()) {
            j["description"] = t.description.value();
        }
        if (t.inputSchema.has_value()) {
            j["inputSchema"] = nlohmann::json::parse(t.inputSchema.value());
        }
        if (t.outputSchema.has_value()) {
            j["outputSchema"] = nlohmann::json::parse(t.outputSchema.value());
        }
        if (t.annotations.has_value()) {
            j["annotations"] = t.annotations.value();
        }
        if (t.icons.has_value()) {
            j["icons"] = t.icons.value();
        }
    }

    static void from_json(const json& j, Mcp::Tool& t)
    {
        j.at("name").get_to(t.name);
        if (j.contains("title")) {
            t.title = j.at("title").get<std::string>();
        }
        if (j.contains("description")) {
            t.description = j.at("description").get<std::string>();
        }
        if (j.contains("inputSchema")) {
            t.inputSchema = j.at("inputSchema").dump();
        }
        if (j.contains("outputSchema")) {
            t.outputSchema = j.at("outputSchema").dump();
        }
        if (j.contains("annotations")) {
            t.annotations = j.at("annotations").get<Mcp::ToolAnnotations>();
        }
        if (j.contains("icons")) {
            t.icons = j.at("icons").get<std::vector<Mcp::Icon>>();
        }
    }
};

// ListToolsResult mapping
template <>
struct adl_serializer<Mcp::ListToolsResult> {
    static void to_json(json& j, const Mcp::ListToolsResult& r)
    {
        j["tools"] = r.tools;
        if (r.meta.has_value()) {
            j["_meta"] = r.meta.value();
        }
        if (r.nextCursor.has_value()) {
            j["nextCursor"] = r.nextCursor.value();
        }
    }

    static void from_json(const json& j, Mcp::ListToolsResult& r)
    {
        if (j.contains("tools")) {
            j.at("tools").get_to(r.tools);
        } else {
            r.tools.clear();
        }
        if (j.contains("_meta")) {
            auto metaJson = j.at("_meta").get<std::unordered_map<std::string, json>>();
            r.meta = JsonMapToStringMap(metaJson);
        } else {
            r.meta.reset();
        }
        if (j.contains("nextCursor")) {
            r.nextCursor = j.at("nextCursor").get<std::string>();
        } else {
            r.nextCursor.reset();
        }
    }
};

// PromptArgument
template <>
struct adl_serializer<Mcp::PromptArgument> {
    static void to_json(json& j, const Mcp::PromptArgument& a)
    {
        j["name"] = a.name;
        if (a.description.has_value()) {
            j["description"] = a.description.value();
        }
        if (a.required.has_value()) {
            j["required"] = a.required.value();
        }
        if (a.title.has_value()) {
            j["title"] = a.title.value();
        }
    }

    static void from_json(const json& j, Mcp::PromptArgument& a)
    {
        j.at("name").get_to(a.name);

        if (j.contains("description") && !j.at("description").is_null()) {
            a.description = j.at("description").get<std::string>();
        } else {
            a.description.reset();
        }

        if (j.contains("required") && !j.at("required").is_null()) {
            a.required = j.at("required").get<bool>();
        } else {
            a.required.reset();
        }

        if (j.contains("title") && !j.at("title").is_null()) {
            a.title = j.at("title").get<std::string>();
        } else {
            a.title.reset();
        }
    }
};

// PromptInfo
template <>
struct adl_serializer<Mcp::PromptInfo> {
    static void to_json(json& j, const Mcp::PromptInfo& p)
    {
        j["name"] = p.name;
        if (p.description.has_value()) {
            j["description"] = p.description.value();
        }
        if (p.title.has_value()) {
            j["title"] = p.title.value();
        }
        if (p.icons.has_value()) {
            j["icons"] = p.icons.value();
        }
        if (p.arguments.has_value()) {
            j["arguments"] = p.arguments.value();
        }
    }

    static void from_json(const json& j, Mcp::PromptInfo& p)
    {
        j.at("name").get_to(p.name);

        if (j.contains("description") && !j.at("description").is_null()) {
            p.description = j.at("description").get<std::string>();
        } else {
            p.description.reset();
        }

        if (j.contains("title") && !j.at("title").is_null()) {
            p.title = j.at("title").get<std::string>();
        } else {
            p.title.reset();
        }

        if (j.contains("icons") && !j.at("icons").is_null()) {
            p.icons = j.at("icons").get<std::vector<Mcp::Icon>>();
        } else {
            p.icons.reset();
        }

        if (j.contains("arguments") && !j.at("arguments").is_null()) {
            p.arguments = j.at("arguments").get<std::vector<Mcp::PromptArgument>>();
        } else {
            p.arguments.reset();
        }
    }
};

// PromptMessage
template <>
struct adl_serializer<Mcp::PromptMessage> {
    static void to_json(json& j, const Mcp::PromptMessage& m)
    {
        j["role"] = McpRoleTypeToString(m.role);
        j["content"] = m.content;
    }

    static void from_json(const json& j, Mcp::PromptMessage& m)
    {
        auto roleStr = j.value("role", std::string("user"));
        m.role = McpRoleTypeFromString(roleStr);

        if (j.contains("content")) {
            const auto& arr = j.at("content");
            if (arr.is_array() && !arr.empty()) {
                m.content = arr.front().get<Mcp::ContentType>();
            }
        }
    }
};

// GetPromptResult mapping
template <>
struct adl_serializer<Mcp::GetPromptResult> {
    static void to_json(json& j, const Mcp::GetPromptResult& r)
    {
        if (r.description.has_value()) {
            j["description"] = r.description.value();
        }
        j["messages"] = r.messages;
        if (r.meta.has_value()) {
            j["_meta"] = r.meta.value();
        }
    }

    static void from_json(const json& j, Mcp::GetPromptResult& r)
    {
        if (j.contains("description") && !j.at("description").is_null()) {
            r.description = j.at("description").get<std::string>();
        } else {
            r.description.reset();
        }

        // messages is required by schema
        j.at("messages").get_to(r.messages);
    }
};

// Result mapping
template <>
struct adl_serializer<Mcp::ListPromptsResult> {
    static void to_json(json& j, const Mcp::ListPromptsResult& r)
    {
        j["prompts"] = r.prompts;
        if (r.meta.has_value()) {
            j["_meta"] = r.meta.value();
        }
    }

    static void from_json(const json& j, Mcp::ListPromptsResult& r)
    {
        if (j.contains("prompts")) {
            j.at("prompts").get_to(r.prompts);
        } else {
            r.prompts.clear();
        }
        if (j.contains("_meta")) {
            auto metaJson = j.at("_meta").get<std::unordered_map<std::string, json>>();
            r.meta = JsonMapToStringMap(metaJson);
        }
    }
};

// ListPromptsRequest -> {"method":"prompts/list"}
template <>
struct adl_serializer<Mcp::ListPromptsRequest> {
    static void to_json(json& j, const Mcp::ListPromptsRequest& req)
    {
    }

    static void from_json(const json& j, Mcp::ListPromptsRequest& req)
    {
        j.at("method").get_to(req.method_);
    }
};

// GetPromptRequest -> {"method":"prompts/get", "params":{name,arguments?}}
template <>
struct adl_serializer<Mcp::GetPromptRequest> {
    static void to_json(json& j, const Mcp::GetPromptRequest& req)
    {
        if (req.params_) {
            auto p = static_cast<const Mcp::GetPromptParams*>(req.params_.get());
            j["name"] = p->name;
            if (p->arguments.has_value()) {
                j["arguments"] = p->arguments.value();
            }
        }
    }

    static void from_json(const json& j, Mcp::GetPromptRequest& req)
    {
        j.at("method").get_to(req.method_);
        if (j.contains("params")) {
            const auto& paramsJson = j.at("params");
            std::string name;
            paramsJson.at("name").get_to(name);
            std::optional<Mcp::JsonValue> args;
            if (paramsJson.contains("arguments")) {
                args = paramsJson.at("arguments");
            }
            req.params_ = std::make_unique<Mcp::GetPromptParams>(name, std::move(args));
        }
    }
};

// ResourceTemplate
template <>
struct adl_serializer<Mcp::ResourceTemplate> {
    static void to_json(json& j, const Mcp::ResourceTemplate& r)
    {
        j["uriTemplate"] = r.uriTemplate;
        j["name"] = r.name;
        if (r.title.has_value()) {
            j["title"] = r.title.value();
        }
        if (r.description.has_value()) {
            j["description"] = r.description.value();
        }
        if (r.mimeType.has_value()) {
            j["mimeType"] = r.mimeType.value();
        }
        if (r.icons.has_value()) {
            j["icons"] = r.icons.value();
        }
        if (r.annotations.has_value()) {
            j["annotations"] = r.annotations.value();
        }
    }

    static void from_json(const json& j, Mcp::ResourceTemplate& r)
    {
        j.at("uriTemplate").get_to(r.uriTemplate);
        j.at("name").get_to(r.name);
        if (j.contains("title")) {
            r.title = j.at("title").get<std::string>();
        } else {
            r.title = std::nullopt;
        }
        if (j.contains("description")) {
            r.description = j.at("description").get<std::string>();
        } else {
            r.description = std::nullopt;
        }
        if (j.contains("mimeType")) {
            r.mimeType = j.at("mimeType").get<std::string>();
        } else {
            r.mimeType = std::nullopt;
        }
        if (j.contains("icons")) {
            r.icons = j.at("icons").get<std::vector<Mcp::Icon>>();
        } else {
            r.icons = std::nullopt;
        }
        if (j.contains("annotations")) {
            r.annotations = j.at("annotations").get<Mcp::Annotations>();
        } else {
            r.annotations = std::nullopt;
        }
    }
};

// ReadResourceRequest -> {"method":"resources/read", "params":{uri}}
template <>
struct adl_serializer<Mcp::ReadResourceRequest> {
    static void to_json(json& j, const Mcp::ReadResourceRequest& req)
    {
        if (req.params_) {
            auto p = static_cast<const Mcp::ReadResourceRequestParams*>(req.params_.get());
            j["uri"] = p->uri_;
        }
    }

    static void from_json(const json& j, Mcp::ReadResourceRequest& req)
    {
        j.at("method").get_to(req.method_);
        if (j.contains("params")) {
            const auto& paramsJson = j.at("params");
            std::string uri;
            paramsJson.at("uri").get_to(uri);
            req.params_ = std::make_unique<Mcp::ReadResourceRequestParams>(std::move(uri));
        }
    }
};

// SubscribeRequest -> {"method":"resources/subscribe", "params":{uri}}
template <>
struct adl_serializer<Mcp::SubscribeRequest> {
    static void to_json(json& j, const Mcp::SubscribeRequest& req)
    {
        if (req.params_) {
            auto p = static_cast<const Mcp::SubscribeRequestParams*>(req.params_.get());
            j["uri"] = p->uri_;
        }
    }

    static void from_json(const json& j, Mcp::SubscribeRequest& req)
    {
        j.at("method").get_to(req.method_);
        if (j.contains("params")) {
            const auto& paramsJson = j.at("params");
            std::string uri;
            paramsJson.at("uri").get_to(uri);
            req.params_ = std::make_unique<Mcp::SubscribeRequestParams>(std::move(uri));
        }
    }
};

// UnsubscribeRequest -> {"method":"resources/unsubscribe", "params":{uri}}
template <>
struct adl_serializer<Mcp::UnsubscribeRequest> {
    static void to_json(json& j, const Mcp::UnsubscribeRequest& req)
    {
        if (req.params_) {
            auto p = static_cast<const Mcp::UnsubscribeRequestParams*>(req.params_.get());
            j["uri"] = p->uri_;
        }
    }

    static void from_json(const json& j, Mcp::UnsubscribeRequest& req)
    {
        j.at("method").get_to(req.method_);
        if (j.contains("params")) {
            const auto& paramsJson = j.at("params");
            std::string uri;
            paramsJson.at("uri").get_to(uri);
            req.params_ = std::make_unique<Mcp::UnsubscribeRequestParams>(std::move(uri));
        }
    }
};

// ListResourcesRequest -> {cursor?}
template <>
struct adl_serializer<Mcp::ListResourcesRequest> {
    static void to_json(json& j, const Mcp::ListResourcesRequest& req)
    {
        if (req.params_) {
            auto p = static_cast<const Mcp::RequestParams*>(req.params_.get());
            if (p->cursor.has_value()) {
                j["cursor"] = p->cursor.value();
            }
        }
    }

    static void from_json(const json& j, Mcp::ListResourcesRequest& req)
    {
        if (j.contains("method")) {
            j.at("method").get_to(req.method_);
        }
        if (j.contains("params")) {
            const auto& paramsJson = j.at("params");
            if (paramsJson.contains("cursor")) {
                auto p = std::make_unique<Mcp::RequestParams>();
                p->cursor = paramsJson.at("cursor").get<std::string>();
                req.params_ = std::move(p);
            }
        }
    }
};

// ListResourceTemplatesRequest -> {"method":"resources/templates/list", "params":{}}
template <>
struct adl_serializer<Mcp::ListResourceTemplatesRequest> {
    static void to_json(json& j, const Mcp::ListResourceTemplatesRequest& req)
    {
    }

    static void from_json(const json& j, Mcp::ListResourceTemplatesRequest& req)
    {
        j.at("method").get_to(req.method_);
    }
};

// ListResourceTemplatesResult
template <>
struct adl_serializer<Mcp::ListResourceTemplatesResult> {
    static void to_json(json& j, const Mcp::ListResourceTemplatesResult& r)
    {
        j["resourceTemplates"] = r.resourceTemplates;
        if (r.meta.has_value()) {
            j["_meta"] = r.meta.value();
        }
    }

    static void from_json(const json& j, Mcp::ListResourceTemplatesResult& r)
    {
        if (j.contains("resourceTemplates")) {
            j.at("resourceTemplates").get_to(r.resourceTemplates);
        } else {
            r.resourceTemplates.clear();
        }
        if (j.contains("_meta")) {
            auto metaJson = j.at("_meta").get<std::unordered_map<std::string, json>>();
            r.meta = JsonMapToStringMap(metaJson);
        } else {
            r.meta = std::nullopt;
        }
    }
};

} // namespace nlohmann

namespace Mcp {

using nlohmann::json;

// ==== JSON Schema definitions for MCP JSON-RPC 2.0 envelope ====
// Request: must have jsonrpc="2.0", id (string or integer), method (string)
static const nlohmann::json REQUEST_SCHEMA = {
    {"$schema", "http://json-schema.org/draft-07/schema#"},
    {"type", "object"},
    {"required", {"jsonrpc", "id", "method"}},
    {"properties", {
        {"jsonrpc", {{"type", "string"}, {"const", JSONRPC_VERSION}}},
        {"id", {{"oneOf", {{{"type", "string"}}, {{"type", "integer"}}}}}},
        {"method", {{"type", "string"}}},
        {"params", json::object()}
    }},
    {"additionalProperties", true}
};

// Notification: must have jsonrpc="2.0", method (string), MUST NOT have id
static const nlohmann::json NOTIFICATION_SCHEMA = {
    {"$schema", "http://json-schema.org/draft-07/schema#"},
    {"type", "object"},
    {"required", {"jsonrpc", "method"}},
    {"properties", {
        {"jsonrpc", {{"type", "string"}, {"const", JSONRPC_VERSION}}},
        {"method", {{"type", "string"}}},
        {"params", json::object()}
    }},
    {"not", {{"required", {"id"}}}},
    {"additionalProperties", true}
};

// Response: must have jsonrpc="2.0", id (string or integer)
static const nlohmann::json RESPONSE_SCHEMA = {
    {"$schema", "http://json-schema.org/draft-07/schema#"},
    {"type", "object"},
    {"required", {"jsonrpc", "id"}},
    {"properties", {
        {"jsonrpc", {{"type", "string"}, {"const", JSONRPC_VERSION}}},
        {"id", {{"oneOf", {{{"type", "string"}}, {{"type", "integer"}}}}}},
        {"result", json::object()},
        {"error", json::object()}
    }},
    {"additionalProperties", true}
};

// Error Response: must have jsonrpc="2.0", id (string or integer), error {code(int), message(string)}
static const nlohmann::json ERROR_SCHEMA = {
    {"$schema", "http://json-schema.org/draft-07/schema#"},
    {"type", "object"},
    {"required", {"jsonrpc", "id", "error"}},
    {"properties", {
        {"jsonrpc", {{"type", "string"}, {"const", JSONRPC_VERSION}}},
        {"id", {{"oneOf", {{{"type", "string"}}, {{"type", "integer"}}}}}},
        {"error", {
            {"type", "object"},
            {"required", {"code", "message"}},
            {"properties", {
                {"code", {{"type", "integer"}}},
                {"message", {{"type", "string"}}},
                {"data", json::object()}
            }},
            {"additionalProperties", true}
        }}
    }},
    {"additionalProperties", true}
};

JSONRPCNotification::JSONRPCNotification()
{
    jsonrpc_ = JSONRPC_VERSION;
}

JSONRPCRequest::JSONRPCRequest()
{
    jsonrpc_ = JSONRPC_VERSION;
    id_ = int64_t(0);
}

JSONRPCResponse::JSONRPCResponse()
{
    jsonrpc_ = JSONRPC_VERSION;
    id_ = int64_t(0);
}

InitializeRequest::InitializeRequest()
{
    method_ = "initialize";
    params_ = std::make_unique<InitializeRequestParams>(Mcp::LATEST_PROTOCOL_VERSION, ClientCapabilities(),
                                                        Implementation());
}

InitializeRequest::InitializeRequest(const std::string& clientName, const std::string& clientVersion)
{
    method_ = "initialize";
    auto params = std::make_unique<InitializeRequestParams>(Mcp::LATEST_PROTOCOL_VERSION, ClientCapabilities(),
                                                            Implementation());
    params->clientInfo_.name = clientName.empty() ? Mcp::DEFAULT_CLIENT_NAME : clientName;
    params->clientInfo_.version = clientVersion.empty() ? Mcp::DEFAULT_VERSION : clientVersion;
    params_ = std::move(params);
}

InitializeRequestParams::InitializeRequestParams(std::string protocolVersion, ClientCapabilities capabilities,
                                                 Implementation clientInfo)
{
    protocolVersion_ = protocolVersion;
    capabilities_ = capabilities;
    clientInfo_ = clientInfo;
}

InitializeResult::InitializeResult(std::string protocolVersion, ServerCapabilities capabilities,
                                   Implementation serverInfo, std::optional<std::string> instructions)
{
    this->protocolVersion = std::move(protocolVersion);
    this->capabilities = std::move(capabilities);
    this->serverInfo = std::move(serverInfo);
    this->instructions = std::move(instructions);
}

InitializedNotification::InitializedNotification()
{
    method_ = "notifications/initialized";
    params_ = nullptr;
}

CallToolParams::CallToolParams(const std::string& tool_name, std::optional<JsonValue> args)
{
    name = tool_name;
    arguments = std::move(args);
}

CallToolRequest::CallToolRequest()
{
    method_ = "tools/call";
}

ListToolsRequest::ListToolsRequest()
{
    method_ = "tools/list";
}

ListPromptsRequest::ListPromptsRequest()
{
    method_ = "prompts/list";
}

GetPromptParams::GetPromptParams(const std::string& prompt_name, std::optional<JsonValue> args)
{
    name = prompt_name;
    arguments = std::move(args);
}

GetPromptRequest::GetPromptRequest()
{
    method_ = "prompts/get";
}

ReadResourceRequest::ReadResourceRequest()
{
    method_ = "resources/read";
}

ListResourcesRequest::ListResourcesRequest()
{
    method_ = "resources/list";
}

SubscribeRequest::SubscribeRequest()
{
    method_ = "resources/subscribe";
}

UnsubscribeRequest::UnsubscribeRequest()
{
    method_ = "resources/unsubscribe";
}

ListResourceTemplatesRequest::ListResourceTemplatesRequest()
{
    method_ = "resources/templates/list";
}

PingRequest::PingRequest()
{
    method_ = "ping";
}

std::string JSONRPCRequest::Serialize() const
{
    json j;
    j["jsonrpc"] = jsonrpc_;
    std::visit([&j](const auto& id) { j["id"] = id; }, id_);
    j["method"] = method_;
    if (request_ != nullptr && method_ == "initialize") {
        const auto* params = static_cast<const InitializeRequestParams*>(request_->params_.get());
        if (params != nullptr) {
            j["params"] = *params;
        }
    } else if (request_ != nullptr && request_->method_ == "tools/call") {
        const auto* getReq = dynamic_cast<const CallToolRequest*>(request_.get());
        if (getReq != nullptr) {
            j["params"] = *getReq;
        }
    } else if (request_ != nullptr && request_->method_ == "tools/list") {
        const auto* listReq = dynamic_cast<const ListToolsRequest*>(request_.get());
        if (listReq != nullptr) {
            j["params"] = *listReq;
        }
    } else if (request_ != nullptr && request_->method_ == "prompts/list") {
        const auto* listReq = dynamic_cast<const ListPromptsRequest*>(request_.get());
        if (listReq != nullptr) {
            j["params"] = *listReq;
        }
    } else if (request_ != nullptr && request_->method_ == "prompts/get") {
        const auto* getReq = dynamic_cast<const GetPromptRequest*>(request_.get());
        if (getReq != nullptr) {
            j["params"] = *getReq;
        }
    } else if (request_ != nullptr && request_->method_ == "resources/read") {
        const auto* readReq = dynamic_cast<const ReadResourceRequest*>(request_.get());
        if (readReq != nullptr) {
            j["params"] = *readReq;
        }
    } else if (request_ != nullptr && request_->method_ == "resources/subscribe") {
        const auto* subReq = dynamic_cast<const SubscribeRequest*>(request_.get());
        if (subReq != nullptr) {
            j["params"] = *subReq;
        }
    } else if (request_ != nullptr && request_->method_ == "resources/unsubscribe") {
        const auto* unsubReq = dynamic_cast<const UnsubscribeRequest*>(request_.get());
        if (unsubReq != nullptr) {
            j["params"] = *unsubReq;
        }
    } else if (request_ != nullptr && request_->method_ == "resources/list") {
        const auto* listReq = dynamic_cast<const ListResourcesRequest*>(request_.get());
        if (listReq != nullptr) {
            j["params"] = *listReq;
        }
    } else if (request_ != nullptr && request_->method_ == "resources/templates/list") {
        const auto* listReq = dynamic_cast<const ListResourceTemplatesRequest*>(request_.get());
        if (listReq != nullptr) {
            j["params"] = *listReq;
        }
    } else if (request_ != nullptr && request_->method_ == "ping") {
        // ping has no params
    }

    return j.dump();
}

int JSONRPCRequest::Deserialize(const std::string& jsonStr)
{
    auto j = json::parse(jsonStr);
    jsonrpc_ = j.value("jsonrpc", "");
    if (j.contains("id")) {
        const auto& idValue = j.at("id");
        if (idValue.is_string()) {
            id_ = idValue.get<std::string>();
        } else if (idValue.is_number_integer()) {
            id_ = idValue.get<int64_t>();
        } else {
            id_ = int64_t(0);
        }
    }
    method_ = j.value("method", "");
    request_.reset();

    if (method_ == "initialize") {
        auto initReq = std::make_unique<InitializeRequest>();
        initReq->method_ = method_;
        auto p = std::make_unique<InitializeRequestParams>(Mcp::DEFAULT_PROTOCOL_VERSION, Mcp::ClientCapabilities{},
                                                           Mcp::Implementation{});
        if (j.contains("params")) {
            j.at("params").get_to(*p);
        }
        initReq->params_ = std::move(p);
        request_ = std::move(initReq);
    } else if (method_ == "tools/call") {
        CallToolRequest callReq = j.get<CallToolRequest>();
        auto reqPtr = std::make_unique<CallToolRequest>(std::move(callReq));
        request_ = std::move(reqPtr);
    } else if (method_ == "tools/list") {
        ListToolsRequest listReq = j.get<ListToolsRequest>();
        auto reqPtr = std::make_unique<ListToolsRequest>(std::move(listReq));
        request_ = std::move(reqPtr);
    } else if (method_ == "prompts/list") {
        ListPromptsRequest listReq = j.get<ListPromptsRequest>();
        auto reqPtr = std::make_unique<ListPromptsRequest>(std::move(listReq));
        request_ = std::move(reqPtr);
    } else if (method_ == "prompts/get") {
        GetPromptRequest getReq = j.get<GetPromptRequest>();
        auto reqPtr = std::make_unique<GetPromptRequest>(std::move(getReq));
        request_ = std::move(reqPtr);
    } else if (method_ == "resources/read") {
        ReadResourceRequest readReq = j.get<ReadResourceRequest>();
        auto reqPtr = std::make_unique<ReadResourceRequest>(std::move(readReq));
        request_ = std::move(reqPtr);
    } else if (method_ == "resources/subscribe") {
        SubscribeRequest subReq = j.get<SubscribeRequest>();
        auto reqPtr = std::make_unique<SubscribeRequest>(std::move(subReq));
        request_ = std::move(reqPtr);
    } else if (method_ == "resources/unsubscribe") {
        UnsubscribeRequest unsubReq = j.get<UnsubscribeRequest>();
        auto reqPtr = std::make_unique<UnsubscribeRequest>(std::move(unsubReq));
        request_ = std::move(reqPtr);
    } else if (method_ == "resources/list") {
        ListResourcesRequest listReq = j.get<ListResourcesRequest>();
        auto reqPtr = std::make_unique<ListResourcesRequest>(std::move(listReq));
        request_ = std::move(reqPtr);
    } else if (method_ == "resources/templates/list") {
        ListResourceTemplatesRequest listReq = j.get<ListResourceTemplatesRequest>();
        auto reqPtr = std::make_unique<ListResourceTemplatesRequest>(std::move(listReq));
        request_ = std::move(reqPtr);
    } else if (method_ == "ping") {
        auto reqPtr = std::make_unique<PingRequest>();
        reqPtr->method_ = method_;
        request_ = std::move(reqPtr);
    }

    return 0;
}

std::string JSONRPCResponse::Serialize(const std::string& method) const
{
    json j;
    j["jsonrpc"] = jsonrpc_;
    std::visit([&j](const auto& id) { j["id"] = id; }, id_);

    if (result_ != nullptr) {
        if (method == "initialize") {
            auto* initResult = static_cast<const InitializeResult*>(result_.get());
            if (initResult == nullptr) {
                throw std::runtime_error("Failed to cast result to InitializeResult");
            }
            j["result"] = *initResult;
        } else if (method == "tools/call") {
            auto* callResult = dynamic_cast<const CallToolResult*>(result_.get());
            if (callResult == nullptr) {
                throw std::runtime_error("Failed to cast result to CallToolResult");
            }
            j["result"] = *callResult;
        } else if (method == "tools/list") {
            auto* listResult = dynamic_cast<const ListToolsResult*>(result_.get());
            if (listResult == nullptr) {
                throw std::runtime_error("Failed to cast result to ListToolsResult");
            }
            j["result"] = *listResult;
        } else if (method == "prompts/list") {
            auto* promptsResult = dynamic_cast<const ListPromptsResult*>(result_.get());
            if (promptsResult == nullptr) {
                throw std::runtime_error("Failed to cast result to ListPromptsResult");
            }
            j["result"] = *promptsResult;
        } else if (method == "prompts/get") {
            auto* getResult = dynamic_cast<const GetPromptResult*>(result_.get());
            if (getResult == nullptr) {
                throw std::runtime_error("Failed to cast result to GetPromptResult");
            }
            j["result"] = *getResult;
        } else if (method == "resources/read") {
            auto* readResult = dynamic_cast<const ReadResourceResult*>(result_.get());
            if (readResult == nullptr) {
                throw std::runtime_error("Failed to cast result to ReadResourceResult");
            }
            j["result"] = *readResult;
        } else if (method == "resources/list") {
            auto* listResult = dynamic_cast<const ListResourcesResult*>(result_.get());
            if (listResult == nullptr) {
                throw std::runtime_error("Failed to cast result to ListResourcesResult");
            }
            j["result"] = *listResult;
        } else if (method == "resources/templates/list") {
            auto* listTemplateResult = dynamic_cast<const ListResourceTemplatesResult*>(result_.get());
            if (listTemplateResult == nullptr) {
                throw std::runtime_error("Failed to cast result to ListResourceTemplatesResult");
            }
            j["result"] = *listTemplateResult;
        } else if (method == "resources/subscribe" || method == "resources/unsubscribe") {
            auto* emptyResult = dynamic_cast<const EmptyResult*>(result_.get());
            if (emptyResult == nullptr) {
                throw std::runtime_error("Failed to cast result to EmptyResult");
            }
            j["result"] = json::object();
        } else if (method == "ping") {
            auto* emptyResult = dynamic_cast<const EmptyResult*>(result_.get());
            if (emptyResult == nullptr) {
                throw std::runtime_error("Failed to cast result to EmptyResult");
            }
            j["result"] = json::object();
        }
    }

    return j.dump();
}

int JSONRPCResponse::Deserialize(const std::string& jsonStr, const std::string& method)
{
    auto j = json::parse(jsonStr);
    jsonrpc_ = j.value("jsonrpc", "");
    if (j.contains("id")) {
        const auto& idValue = j.at("id");
        if (idValue.is_string()) {
            id_ = idValue.get<std::string>();
        } else if (idValue.is_number_integer()) {
            id_ = idValue.get<int64_t>();
        } else {
            id_ = int64_t(0);
        }
    }

    // Leave problem of multiple Result types for later
    if (method == "initialize") {
        auto p = std::make_shared<InitializeResult>(Mcp::DEFAULT_PROTOCOL_VERSION, Mcp::ServerCapabilities{},
                                                    Mcp::Implementation{});
        if (j.contains("result")) {
            j.at("result").get_to(*p);
        }
        result_ = std::move(p);
    } else if (method == "tools/call") {
        auto p = std::make_shared<CallToolResult>();
        if (j.contains("result")) {
            j.at("result").get_to(*p);
        }
        result_ = std::move(p);
    } else if (method == "tools/list") {
        auto p = std::make_shared<ListToolsResult>();
        if (j.contains("result")) {
            j.at("result").get_to(*p);
        }
        result_ = std::move(p);
    } else if (method == "prompts/list") {
        auto p = std::make_shared<ListPromptsResult>();
        if (j.contains("result")) {
            j.at("result").get_to(*p);
        }
        result_ = std::move(p);
    } else if (method == "prompts/get") {
        auto p = std::make_shared<GetPromptResult>();
        if (j.contains("result")) {
            j.at("result").get_to(*p);
        }
        result_ = std::move(p);
    } else if (method == "resources/read") {
        auto p = std::make_shared<ReadResourceResult>();
        if (j.contains("result")) {
            j.at("result").get_to(*p);
        }
        result_ = std::move(p);
    } else if (method == "resources/list") {
        auto p = std::make_shared<ListResourcesResult>();
        if (j.contains("result")) {
            j.at("result").get_to(*p);
        }
        result_ = std::move(p);
    } else if (method == "resources/templates/list") {
        auto p = std::make_shared<ListResourceTemplatesResult>();
        if (j.contains("result")) {
            j.at("result").get_to(*p);
        }
        result_ = std::move(p);
    } else if (method == "resources/subscribe" || method == "resources/unsubscribe") {
        result_ = std::make_shared<EmptyResult>();
    } else if (method == "ping") {
        result_ = std::make_shared<EmptyResult>();
    }

    return 0;
}

std::string JSONRPCNotification::Serialize() const
{
    json j;
    j["jsonrpc"] = jsonrpc_;
    j["method"] = method_;
    if (method_ == "notifications/initialized") {
        j["params"] = json::object();
    }

    return j.dump();
}

int JSONRPCNotification::Deserialize(const std::string& jsonStr)
{
    auto j = json::parse(jsonStr);
    jsonrpc_ = j.value("jsonrpc", "");
    method_ = j.value("method", "");
    notification_.reset();

    if (method_ == "notifications/initialized") {
        auto notif = std::make_unique<InitializedNotification>();
        notif->method_ = method_;
        notification_ = std::move(notif);
    }

    return 0;
}

std::string JSONRPCError::Serialize() const
{
    json j;
    j["jsonrpc"] = jsonrpc_;
    std::visit([&j](const auto& id) { j["id"] = id; }, id_);
    j["error"] = {{"code", code_}, {"message", message_}};
    if (data_.has_value()) {
        j["error"]["data"] = data_.value();
    }
    return j.dump();
}

int JSONRPCError::Deserialize(const std::string& jsonStr)
{
    auto j = json::parse(jsonStr);
    jsonrpc_ = j.value("jsonrpc", "");
    if (j.contains("id")) {
        const auto& idValue = j.at("id");
        if (idValue.is_string()) {
            id_ = idValue.get<std::string>();
        } else if (idValue.is_number_integer()) {
            id_ = idValue.get<int64_t>();
        } else {
            id_ = int64_t(0);
        }
    }

    const auto& errorObj = j.at("error");
    code_ = errorObj.value("code", -1);
    message_ = errorObj.value("message", "Internal error");
    if (errorObj.contains("data")) {
        data_ = errorObj["data"];
    }

    return 0;
}

static JSONRPCError CreateInvalidRequestError(const nlohmann::json& j, const std::string& detail = "")
{
    JSONRPCError error;
    error.jsonrpc_ = JSONRPC_VERSION;
    error.code_ = static_cast<int>(JsonRpcErrorCode::INVALID_REQUEST);
    error.message_ = detail.empty() ? "Deserialization Failed" : "Deserialization Failed: " + detail;

    // Try to extract ID from original JSON
    if (j.contains("id")) {
        const auto& idValue = j.at("id");
        if (idValue.is_string()) {
            error.id_ = idValue.get<std::string>();
        } else if (idValue.is_number_integer()) {
            error.id_ = idValue.get<int64_t>();
        } else {
            error.id_ = int64_t(0);
        }
    } else {
        error.id_ = int64_t(0);
    }

    return error;
}

JSONRPCMessage DeserializeJSONRPCMessage(const std::string& jsonStr, const std::string& method)
{
    auto j = json::parse(jsonStr);

    bool hasId = j.contains("id");
    bool hasMethod = j.contains("method");
    bool hasCode = j.contains("error") && j.at("error").is_object() && j.at("error").contains("code");

    try {
        if (hasId && hasMethod) {
            static const nlohmann::json_schema::json_validator req_validator(REQUEST_SCHEMA);
            req_validator.validate(j);
            JSONRPCRequest request;
            request.Deserialize(jsonStr);
            return std::move(request);
        }

        if (hasId && !hasMethod && !hasCode) {
            static const nlohmann::json_schema::json_validator resp_validator(RESPONSE_SCHEMA);
            resp_validator.validate(j);
            JSONRPCResponse response;
            response.Deserialize(jsonStr, method);
            return std::move(response);
        }

        if (!hasId && hasMethod) {
            static const nlohmann::json_schema::json_validator notif_validator(NOTIFICATION_SCHEMA);
            notif_validator.validate(j);
            JSONRPCNotification notification;
            notification.Deserialize(jsonStr);
            return std::move(notification);
        }

        if (hasId && hasCode) {
            static const nlohmann::json_schema::json_validator error_validator(ERROR_SCHEMA);
            error_validator.validate(j);
            JSONRPCError error;
            error.Deserialize(jsonStr);
            return std::move(error);
        }
    } catch (const std::exception& e) {
        return CreateInvalidRequestError(j, e.what());
    }

    return CreateInvalidRequestError(j, "No matching message type");
}

std::string SerializeJSONRPCMessage(const JSONRPCMessage& message, std::optional<std::string> method)
{
    if (auto* request = std::get_if<JSONRPCRequest>(&message)) {
        return request->Serialize();
    }

    if (auto* response = std::get_if<JSONRPCResponse>(&message)) {
        if (!method.has_value()) {
            throw std::runtime_error("Method name is required for response serialization");
        }
        return response->Serialize(method.value());
    }

    if (auto* notification = std::get_if<JSONRPCNotification>(&message)) {
        return notification->Serialize();
    }

    if (auto* error = std::get_if<JSONRPCError>(&message)) {
        return error->Serialize();
    }

    throw std::runtime_error("Unknown JSON-RPC message type");
}

} // namespace Mcp
