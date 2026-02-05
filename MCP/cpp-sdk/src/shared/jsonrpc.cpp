/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "jsonrpc.h"

#include <stdexcept>
#include <utility>
#include <variant>

#include "common_type.h"
#include "mcp_type.h"

namespace nlohmann {

static inline std::optional<Mcp::MetaMap> ParseMetaStringMap(const json& j)
{
    if (!j.is_object()) {
        return std::nullopt;
    }

    Mcp::MetaMap out;
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (it.value().is_string()) {
            out.emplace(it.key(), it.value().get<std::string>());
        } else {
            // Keep deserialization tolerant: stringify any non-string values.
            out.emplace(it.key(), it.value().dump());
        }
    }
    return out;
}

// ---- Client capability details ----
template <>
struct adl_serializer<Mcp::SamplingCapability> {
    static void to_json(json& j, const Mcp::SamplingCapability& c)
    {
        j = json::object();
        if (c.context) {
            j["context"] = json::object();
        }
        if (c.tools) {
            j["tools"] = json::object();
        }
    }

    static void from_json(const json& j, Mcp::SamplingCapability& c)
    {
        c.context = j.contains("context") && j.at("context").is_object();
        c.tools = j.contains("tools") && j.at("tools").is_object();
    }
};

template <>
struct adl_serializer<Mcp::ClientTasksCapability> {
    static void to_json(json& j, const Mcp::ClientTasksCapability& c)
    {
        j = json::object();

        if (c.list) {
            j["list"] = json::object();
        }
        if (c.cancel) {
            j["cancel"] = json::object();
        }

        if (c.samplingCreateMessage || c.elicitationCreate) {
            j["requests"] = json::object();
        }
        if (c.samplingCreateMessage) {
            j["requests"]["sampling"] = json::object();
            j["requests"]["sampling"]["createMessage"] = json::object();
        }
        if (c.elicitationCreate) {
            j["requests"]["elicitation"] = json::object();
            j["requests"]["elicitation"]["create"] = json::object();
        }
    }

    static void from_json(const json& j, Mcp::ClientTasksCapability& c)
    {
        c = Mcp::ClientTasksCapability{};
        if (!j.is_object()) {
            return;
        }

        c.list = j.contains("list") && j.at("list").is_object();
        c.cancel = j.contains("cancel") && j.at("cancel").is_object();

        if (j.contains("requests") && j.at("requests").is_object()) {
            const auto& req = j.at("requests");
            if (req.contains("sampling") && req.at("sampling").is_object()) {
                const auto& samplingReq = req.at("sampling");
                c.samplingCreateMessage = samplingReq.contains("createMessage") &&
                    samplingReq.at("createMessage").is_object();
            }
            if (req.contains("elicitation") && req.at("elicitation").is_object()) {
                const auto& elicReq = req.at("elicitation");
                c.elicitationCreate = elicReq.contains("create") && elicReq.at("create").is_object();
            }
        }
    }
};

template <>
struct adl_serializer<Mcp::ClientCapabilities> {
    static void to_json(json& j, const Mcp::ClientCapabilities& caps)
    {
        j = json::object();

        if (caps.sampling.has_value()) {
            j["sampling"] = caps.sampling.value();
        }
        if (caps.elicitation.has_value()) {
            j["elicitation"] = json::object();
        }
        if (caps.tasks.has_value()) {
            j["tasks"] = caps.tasks.value();
        }
        if (caps.roots.has_value()) {
            j["roots"] = json::object();
            if (caps.roots->listChanged) {
                j["roots"]["listChanged"] = true;
            }
        }
    }

    static void from_json(const json& j, Mcp::ClientCapabilities& caps)
    {
        // Default: no capabilities.
        caps = Mcp::ClientCapabilities{};
        if (!j.is_object()) {
            return;
        }

        if (j.contains("sampling") && j.at("sampling").is_object()) {
            caps.sampling = j.at("sampling").get<Mcp::SamplingCapability>();
        } else {
            caps.sampling.reset();
        }

        if (j.contains("elicitation") && j.at("elicitation").is_object()) {
            caps.elicitation = Mcp::ElicitationCapability{};
        } else {
            caps.elicitation.reset();
        }

        if (j.contains("tasks") && j.at("tasks").is_object()) {
            caps.tasks = j.at("tasks").get<Mcp::ClientTasksCapability>();
        } else {
            caps.tasks.reset();
        }

        if (j.contains("roots") && j.at("roots").is_object()) {
            Mcp::RootsCapability roots;
            roots.listChanged = j.at("roots").value("listChanged", false);
            caps.roots = roots;
        } else {
            caps.roots.reset();
        }
    }
};

// ListTools params/request serializers
template <>
struct adl_serializer<Mcp::ListToolsParams> {
    static void to_json(json& j, const Mcp::ListToolsParams& p)
    {
        if (p.cursor.has_value()) {
            j["cursor"] = p.cursor.value();
        }
    }

    static void from_json(const json& j, Mcp::ListToolsParams& p)
    {
        if (j.contains("cursor")) {
            p.cursor = j.at("cursor").get<std::string>();
        } else {
            p.cursor.reset();
        }
    }
};

template <>
struct adl_serializer<Mcp::ListToolsRequest> {
    static void to_json(json& j, const Mcp::ListToolsRequest& req)
    {
        if (req.params_) {
            auto p = static_cast<const Mcp::ListToolsParams*>(req.params_.get());
            j["params"] = *p;
        }
    }

    static void from_json(const json& j, Mcp::ListToolsRequest& req)
    {
        j.at("method").get_to(req.method_);
        if (j.contains("params")) {
            Mcp::ListToolsParams p;
            j.at("params").get_to(p);
            req.params_ = std::make_unique<Mcp::ListToolsParams>(std::move(p));
        }
    }
};

template <>
struct adl_serializer<Mcp::InitializeRequestParams> {
    static void to_json(json& j, const Mcp::InitializeRequestParams& p)
    {
        j["protocolVersion"] = p.protocolVersion_;
        j["capabilities"] = p.capabilities_;
        j["clientInfo"] = json::object();
        j["clientInfo"]["name"] = p.clientInfo_.name;
        j["clientInfo"]["version"] = p.clientInfo_.version;
    }

    static void from_json(const json& j, Mcp::InitializeRequestParams& p)
    {
        if (j.contains("protocolVersion")) {
            p.protocolVersion_ = j.at("protocolVersion").get<std::string>();
        } else {
            p.protocolVersion_ = Mcp::DEFAULT_PROTOCOL_VERSION;
        }

        if (j.contains("capabilities")) {
            j.at("capabilities").get_to(p.capabilities_);
        } else {
            p.capabilities_ = Mcp::ClientCapabilities{};
        }

        if (j.contains("clientInfo") && j.at("clientInfo").is_object()) {
            const auto& ci = j.at("clientInfo");
            p.clientInfo_.name = ci.value("name", std::string{});
            p.clientInfo_.version = ci.value("version", std::string{});
        } else {
            p.clientInfo_.name.clear();
            p.clientInfo_.version.clear();
        }
    }
};

template <>
struct adl_serializer<Mcp::PromptsCapabilities> {
    static void to_json(json& j, const Mcp::PromptsCapabilities& c)
    {
        j = json::object();
        if (c.listChanged.has_value()) {
            j["listChanged"] = c.listChanged.value();
        }
    }

    static void from_json(const json& j, Mcp::PromptsCapabilities& c)
    {
        if (j.contains("listChanged")) {
            c.listChanged = j.at("listChanged").get<bool>();
        } else {
            c.listChanged.reset();
        }
    }
};

template <>
struct adl_serializer<Mcp::ResourcesCapabilities> {
    static void to_json(json& j, const Mcp::ResourcesCapabilities& c)
    {
        j = json::object();
        if (c.subscribe.has_value()) {
            j["subscribe"] = c.subscribe.value();
        }
        if (c.listChanged.has_value()) {
            j["listChanged"] = c.listChanged.value();
        }
    }

    static void from_json(const json& j, Mcp::ResourcesCapabilities& c)
    {
        if (j.contains("subscribe")) {
            c.subscribe = j.at("subscribe").get<bool>();
        } else {
            c.subscribe.reset();
        }

        if (j.contains("listChanged")) {
            c.listChanged = j.at("listChanged").get<bool>();
        } else {
            c.listChanged.reset();
        }
    }
};

template <>
struct adl_serializer<Mcp::ToolsCapabilities> {
    static void to_json(json& j, const Mcp::ToolsCapabilities& c)
    {
        j = json::object();
        if (c.listChanged.has_value()) {
            j["listChanged"] = c.listChanged.value();
        }
    }

    static void from_json(const json& j, Mcp::ToolsCapabilities& c)
    {
        if (j.contains("listChanged")) {
            c.listChanged = j.at("listChanged").get<bool>();
        } else {
            c.listChanged.reset();
        }
    }
};

template <>
struct adl_serializer<Mcp::ServerCapabilities> {
    static void to_json(json& j, const Mcp::ServerCapabilities& c)
    {
        j = json::object();

        if (c.logging.has_value()) {
            j["logging"] = json::object();
        }
        if (c.prompts.has_value()) {
            j["prompts"] = c.prompts.value();
        }
        if (c.resources.has_value()) {
            j["resources"] = c.resources.value();
        }
        if (c.tools.has_value()) {
            j["tools"] = c.tools.value();
        }
    }

    static void from_json(const json& j, Mcp::ServerCapabilities& c)
    {
        c.experimental.reset();

        if (j.contains("logging")) {
            c.logging = Mcp::LoggingCapabilities{};
        } else {
            c.logging.reset();
        }

        if (j.contains("prompts")) {
            c.prompts = j.at("prompts").get<Mcp::PromptsCapabilities>();
        } else {
            c.prompts.reset();
        }

        if (j.contains("resources")) {
            c.resources = j.at("resources").get<Mcp::ResourcesCapabilities>();
        } else {
            c.resources.reset();
        }

        if (j.contains("tools")) {
            c.tools = j.at("tools").get<Mcp::ToolsCapabilities>();
        } else {
            c.tools.reset();
        }
    }
};

template <>
struct adl_serializer<Mcp::InitializeResult> {
    static void to_json(json& j, const Mcp::InitializeResult& r)
    {
        j["protocolVersion"] = r.protocolVersion;
        j["capabilities"] = r.capabilities;
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
        if (j.contains("capabilities")) {
            j.at("capabilities").get_to(r.capabilities);
        } else {
            r.capabilities = Mcp::ServerCapabilities{};
        }
        if (j.contains("serverInfo")) {
            const auto& si = j.at("serverInfo");
            r.serverInfo.name = si.value("name", std::string{});
            r.serverInfo.version = si.value("version", std::string{});
        }
        if (j.contains("instructions")) {
            r.instructions = j.at("instructions").get<std::string>();
        }
        if (j.contains("_meta") && j.at("_meta").is_object()) {
            r.meta = ParseMetaStringMap(j.at("_meta"));
        } else {
            r.meta = std::nullopt;
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

// Root
template <>
struct adl_serializer<Mcp::Root> {
    static void to_json(json& j, const Mcp::Root& r)
    {
        j = json::object();
        j["uri"] = r.uri;
        if (r.name.has_value()) {
            j["name"] = r.name.value();
        }
    }

    static void from_json(const json& j, Mcp::Root& r)
    {
        j.at("uri").get_to(r.uri);
        if (j.contains("name")) {
            r.name = j.at("name").get<std::string>();
        } else {
            r.name = std::nullopt;
        }
    }
};

// ListRootsResult
template <>
struct adl_serializer<Mcp::ListRootsResult> {
    static void to_json(json& j, const Mcp::ListRootsResult& res)
    {
        j = json::object();
        j["roots"] = res.roots;
        if (res.meta.has_value()) {
            j["_meta"] = res.meta.value();
        }
    }

    static void from_json(const json& j, Mcp::ListRootsResult& res)
    {
        res.roots.clear();
        if (j.contains("roots") && j.at("roots").is_array()) {
            res.roots = j.at("roots").get<std::vector<Mcp::Root>>();
        }
        if (j.contains("_meta") && j.at("_meta").is_object()) {
            res.meta = ParseMetaStringMap(j.at("_meta"));
        } else {
            res.meta = std::nullopt;
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

// ---- Sampling message content ----
template <>
struct adl_serializer<Mcp::ToolUseContent> {
    static void to_json(json& j, const Mcp::ToolUseContent& c)
    {
        j = json::object();
        j["type"] = c.type;
        j["id"] = c.id;
        j["name"] = c.name;
        j["input"] = c.input;
        if (c.meta.has_value()) {
            j["_meta"] = c.meta.value();
        }
    }

    static void from_json(const json& j, Mcp::ToolUseContent& c)
    {
        c.type = j.value("type", std::string("tool_use"));
        c.id = j.value("id", std::string());
        c.name = j.value("name", std::string());
        c.input.clear();
        if (j.contains("input") && j.at("input").is_object()) {
            const auto parsed = ParseMetaStringMap(j.at("input"));
            if (parsed.has_value()) {
                c.input = parsed.value();
            }
        }
        if (j.contains("_meta") && j.at("_meta").is_object()) {
            c.meta = ParseMetaStringMap(j.at("_meta"));
        } else {
            c.meta = std::nullopt;
        }
    }
};

template <>
struct adl_serializer<Mcp::ToolResultContent> {
    static void to_json(json& j, const Mcp::ToolResultContent& c)
    {
        j = json::object();
        j["type"] = c.type;
        j["toolUseId"] = c.toolUseId;
        j["content"] = c.content;
        if (c.structuredContent.has_value()) {
            j["structuredContent"] = c.structuredContent.value();
        }
        if (c.isError.has_value()) {
            j["isError"] = c.isError.value();
        }
        if (c.meta.has_value()) {
            j["_meta"] = c.meta.value();
        }
    }

    static void from_json(const json& j, Mcp::ToolResultContent& c)
    {
        c.type = j.value("type", std::string("tool_result"));
        c.toolUseId = j.value("toolUseId", std::string());
        if (j.contains("content")) {
            j.at("content").get_to(c.content);
        } else {
            c.content.clear();
        }
        if (j.contains("structuredContent") && j.at("structuredContent").is_object()) {
            c.structuredContent = ParseMetaStringMap(j.at("structuredContent"));
        } else {
            c.structuredContent = std::nullopt;
        }
        if (j.contains("isError")) {
            c.isError = j.at("isError").get<bool>();
        } else {
            c.isError = std::nullopt;
        }
        if (j.contains("_meta") && j.at("_meta").is_object()) {
            c.meta = ParseMetaStringMap(j.at("_meta"));
        } else {
            c.meta = std::nullopt;
        }
    }
};

template <>
struct adl_serializer<Mcp::SamplingMessageContentBlock> {
    static void to_json(json& j, const Mcp::SamplingMessageContentBlock& v)
    {
        std::visit([&](auto&& arg) { j = json(arg); }, v);
    }

    static void from_json(const json& j, Mcp::SamplingMessageContentBlock& v)
    {
        const auto type = j.value("type", std::string());
        if (type == "text") {
            v = j.get<Mcp::TextContent>();
        } else if (type == "image") {
            v = j.get<Mcp::ImageContent>();
        } else if (type == "audio") {
            v = j.get<Mcp::AudioContent>();
        } else if (type == "tool_use") {
            v = j.get<Mcp::ToolUseContent>();
        } else if (type == "tool_result") {
            v = j.get<Mcp::ToolResultContent>();
        } else {
            Mcp::TextContent c;
            c.text = j.dump();
            v = c;
        }
    }
};

template <>
struct adl_serializer<Mcp::SamplingMessage> {
    static void to_json(json& j, const Mcp::SamplingMessage& m)
    {
        j = json::object();
        j["role"] = McpRoleTypeToString(m.role);

        if (std::holds_alternative<Mcp::SamplingMessageContentBlock>(m.content)) {
            j["content"] = std::get<Mcp::SamplingMessageContentBlock>(m.content);
        } else {
            j["content"] = std::get<std::vector<Mcp::SamplingMessageContentBlock>>(m.content);
        }

        if (m.meta.has_value()) {
            j["_meta"] = m.meta.value();
        }
    }

    static void from_json(const json& j, Mcp::SamplingMessage& m)
    {
        const auto roleStr = j.value("role", std::string("user"));
        m.role = McpRoleTypeFromString(roleStr);

        if (j.contains("content")) {
            const auto& c = j.at("content");
            if (c.is_array()) {
                m.content = c.get<std::vector<Mcp::SamplingMessageContentBlock>>();
            } else if (c.is_object()) {
                m.content = c.get<Mcp::SamplingMessageContentBlock>();
            } else {
                m.content = std::vector<Mcp::SamplingMessageContentBlock>{};
            }
        } else {
            m.content = std::vector<Mcp::SamplingMessageContentBlock>{};
        }

        if (j.contains("_meta") && j.at("_meta").is_object()) {
            m.meta = ParseMetaStringMap(j.at("_meta"));
        } else {
            m.meta = std::nullopt;
        }
    }
};

template <>
struct adl_serializer<Mcp::ModelHint> {
    static void to_json(json& j, const Mcp::ModelHint& h)
    {
        j = json::object();
        if (h.name.has_value()) {
            j["name"] = h.name.value();
        }
    }

    static void from_json(const json& j, Mcp::ModelHint& h)
    {
        if (j.contains("name")) {
            h.name = j.at("name").get<std::string>();
        } else {
            h.name = std::nullopt;
        }
    }
};

template <>
struct adl_serializer<Mcp::ModelPreferences> {
    static void to_json(json& j, const Mcp::ModelPreferences& p)
    {
        j = json::object();
        if (p.hints.has_value()) {
            j["hints"] = p.hints.value();
        }
        if (p.costPriority.has_value()) {
            j["costPriority"] = p.costPriority.value();
        }
        if (p.speedPriority.has_value()) {
            j["speedPriority"] = p.speedPriority.value();
        }
        if (p.intelligencePriority.has_value()) {
            j["intelligencePriority"] = p.intelligencePriority.value();
        }
    }

    static void from_json(const json& j, Mcp::ModelPreferences& p)
    {
        if (j.contains("hints") && j.at("hints").is_array()) {
            p.hints = j.at("hints").get<std::vector<Mcp::ModelHint>>();
        } else {
            p.hints = std::nullopt;
        }

        if (j.contains("costPriority")) {
            p.costPriority = j.at("costPriority").get<double>();
        } else {
            p.costPriority = std::nullopt;
        }

        if (j.contains("speedPriority")) {
            p.speedPriority = j.at("speedPriority").get<double>();
        } else {
            p.speedPriority = std::nullopt;
        }

        if (j.contains("intelligencePriority")) {
            p.intelligencePriority = j.at("intelligencePriority").get<double>();
        } else {
            p.intelligencePriority = std::nullopt;
        }
    }
};

template <>
struct adl_serializer<Mcp::ToolChoice> {
    static void to_json(json& j, const Mcp::ToolChoice& c)
    {
        j = json::object();
        if (c.mode.has_value()) {
            j["mode"] = c.mode.value();
        }
    }

    static void from_json(const json& j, Mcp::ToolChoice& c)
    {
        if (j.contains("mode")) {
            c.mode = j.at("mode").get<std::string>();
        } else {
            c.mode = std::nullopt;
        }
    }
};

template <>
struct adl_serializer<Mcp::CreateMessageResult> {
    static void to_json(json& j, const Mcp::CreateMessageResult& r)
    {
        j = json::object();
        j["model"] = r.model;
        j["role"] = McpRoleTypeToString(r.role);

        if (std::holds_alternative<Mcp::SamplingMessageContentBlock>(r.content)) {
            j["content"] = std::get<Mcp::SamplingMessageContentBlock>(r.content);
        } else {
            j["content"] = std::get<std::vector<Mcp::SamplingMessageContentBlock>>(r.content);
        }

        if (r.stopReason.has_value()) {
            j["stopReason"] = r.stopReason.value();
        }
        if (r.meta.has_value()) {
            j["_meta"] = r.meta.value();
        }
    }

    static void from_json(const json& j, Mcp::CreateMessageResult& r)
    {
        r.model = j.value("model", std::string());
        r.role = McpRoleTypeFromString(j.value("role", std::string("assistant")));

        if (j.contains("content")) {
            const auto& c = j.at("content");
            if (c.is_array()) {
                r.content = c.get<std::vector<Mcp::SamplingMessageContentBlock>>();
            } else if (c.is_object()) {
                r.content = c.get<Mcp::SamplingMessageContentBlock>();
            } else {
                r.content = std::vector<Mcp::SamplingMessageContentBlock>{};
            }
        } else {
            r.content = std::vector<Mcp::SamplingMessageContentBlock>{};
        }

        if (j.contains("stopReason")) {
            r.stopReason = j.at("stopReason").get<std::string>();
        } else {
            r.stopReason = std::nullopt;
        }

        if (j.contains("_meta") && j.at("_meta").is_object()) {
            r.meta = ParseMetaStringMap(j.at("_meta"));
        } else {
            r.meta = std::nullopt;
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
    }

    static void from_json(const json& j, Mcp::ListResourcesResult& r)
    {
        if (j.contains("resources")) {
            j.at("resources").get_to(r.resources);
        } else {
            r.resources.clear();
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

// Tool
template <>
struct adl_serializer<Mcp::Tool> {
    static void to_json(json& j, const Mcp::Tool& t)
    {
        j["name"] = t.name;
        j["description"] = t.description;
        j["inputSchema"] = t.inputSchema;
    }

    static void from_json(const json& j, Mcp::Tool& t)
    {
        j.at("name").get_to(t.name);
        t.description = j.value("description", std::string());
        if (j.contains("inputSchema")) {
            t.inputSchema = j.at("inputSchema");
        } else {
            t.inputSchema = json::object();
        }
    }
};

// sampling/createMessage request
template <>
struct adl_serializer<Mcp::CreateMessageRequest> {
    static void to_json(json& j, const Mcp::CreateMessageRequest& req)
    {
        if (!req.params_) {
            return;
        }

        const auto* p = static_cast<const Mcp::CreateMessageRequestParams*>(req.params_.get());
        j["messages"] = p->messages;
        j["maxTokens"] = p->maxTokens;

        if (p->modelPreferences.has_value()) {
            j["modelPreferences"] = p->modelPreferences.value();
        }
        if (p->systemPrompt.has_value()) {
            j["systemPrompt"] = p->systemPrompt.value();
        }
        if (p->includeContext.has_value()) {
            j["includeContext"] = p->includeContext.value();
        }
        if (p->temperature.has_value()) {
            j["temperature"] = p->temperature.value();
        }
        if (p->stopSequences.has_value()) {
            j["stopSequences"] = p->stopSequences.value();
        }
        if (p->metadata.has_value()) {
            j["metadata"] = p->metadata.value();
        }
        if (p->tools.has_value()) {
            j["tools"] = p->tools.value();
        }
        if (p->toolChoice.has_value()) {
            j["toolChoice"] = p->toolChoice.value();
        }
    }

    static void from_json(const json& j, Mcp::CreateMessageRequest& req)
    {
        j.at("method").get_to(req.method_);

        if (!j.contains("params") || !j.at("params").is_object()) {
            return;
        }

        const auto& paramsJson = j.at("params");
        auto p = std::make_unique<Mcp::CreateMessageRequestParams>();
        if (paramsJson.contains("messages")) {
            p->messages = paramsJson.at("messages").get<std::vector<Mcp::SamplingMessage>>();
        }
        p->maxTokens = paramsJson.value("maxTokens", 0);

        if (paramsJson.contains("modelPreferences") && paramsJson.at("modelPreferences").is_object()) {
            p->modelPreferences = paramsJson.at("modelPreferences").get<Mcp::ModelPreferences>();
        }
        if (paramsJson.contains("systemPrompt")) {
            p->systemPrompt = paramsJson.at("systemPrompt").get<std::string>();
        }
        if (paramsJson.contains("includeContext")) {
            p->includeContext = paramsJson.at("includeContext").get<std::string>();
        }
        if (paramsJson.contains("temperature")) {
            p->temperature = paramsJson.at("temperature").get<double>();
        }
        if (paramsJson.contains("stopSequences") && paramsJson.at("stopSequences").is_array()) {
            p->stopSequences = paramsJson.at("stopSequences").get<std::vector<std::string>>();
        }
        if (paramsJson.contains("metadata") && paramsJson.at("metadata").is_object()) {
            p->metadata = ParseMetaStringMap(paramsJson.at("metadata"));
        } else {
            p->metadata = std::nullopt;
        }
        if (paramsJson.contains("tools") && paramsJson.at("tools").is_array()) {
            p->tools = paramsJson.at("tools").get<std::vector<Mcp::Tool>>();
        }
        if (paramsJson.contains("toolChoice") && paramsJson.at("toolChoice").is_object()) {
            p->toolChoice = paramsJson.at("toolChoice").get<Mcp::ToolChoice>();
        }

        req.params_ = std::move(p);
    }
};

// SetLoggingLevelRequest -> {"method":"logging/Set:Level", "params":{level}}
template <>
struct adl_serializer<Mcp::SetLoggingLevelRequest> {
    static void to_json(json& j, const Mcp::SetLoggingLevelRequest& req)
    {
        if (req.params_) {
            auto p = static_cast<const Mcp::SetLoggingLevelParams*>(req.params_.get());
            j["level"] = p->level;
        }
    }

    static void from_json(const json& j, Mcp::SetLoggingLevelRequest& req)
    {
        j.at("method").get_to(req.method_);
        if (j.contains("params")) {
            const auto& paramsJson = j.at("params");
            std::string level;
            paramsJson.at("level").get_to(level);

            req.params_ = std::make_unique<Mcp::SetLoggingLevelParams>(std::move(level));
        }
    }
};

// CallToolResult
template <>
struct adl_serializer<Mcp::CallToolResult> {
    static void to_json(json& j, const Mcp::CallToolResult& r)
    {
        j["content"] = r.content;
        j["isError"] = r.isError;
    }

    static void from_json(const json& j, Mcp::CallToolResult& r)
    {
        if (j.contains("content")) {
            j.at("content").get_to(r.content);
        } else {
            r.content.clear();
        }
        r.isError = j.value("isError", false);
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
    }

    static void from_json(const json& j, Mcp::ListToolsResult& r)
    {
        if (j.contains("tools")) {
            j.at("tools").get_to(r.tools);
        } else {
            r.tools.clear();
        }
        if (j.contains("_meta") && j.at("_meta").is_object()) {
            r.meta = ParseMetaStringMap(j.at("_meta"));
        } else {
            r.meta = std::nullopt;
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
        if (j.contains("_meta") && j.at("_meta").is_object()) {
            r.meta = ParseMetaStringMap(j.at("_meta"));
        } else {
            r.meta = std::nullopt;
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

// ListResourcesRequest -> {"method":"resources/list", "params":{}}
template <>
struct adl_serializer<Mcp::ListResourcesRequest> {
    static void to_json(json& j, const Mcp::ListResourcesRequest& req)
    {
    }

    static void from_json(const json& j, Mcp::ListResourcesRequest& req)
    {
        j.at("method").get_to(req.method_);
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

// ListRootsRequest -> {"method":"roots/list", "params":{}}
// (This codebase currently serializes empty params as JSON null via an empty to_json,
// consistent with other no-param list requests like prompts/list/resources/list.)
template <>
struct adl_serializer<Mcp::ListRootsRequest> {
    static void to_json(json& j, const Mcp::ListRootsRequest&)
    {
    }

    static void from_json(const json& j, Mcp::ListRootsRequest& req)
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
        if (j.contains("_meta") && j.at("_meta").is_object()) {
            r.meta = ParseMetaStringMap(j.at("_meta"));
        } else {
            r.meta = std::nullopt;
        }
    }
};

// CancelledNotification
template <>
struct adl_serializer<Mcp::CancelledNotificationParams> {
    static void to_json(json &j, const Mcp::CancelledNotificationParams &p)
    {
        j["requestId"] = p.requestId;
        j["reason"] = p.reason;
    }

    static void from_json(const json &j, Mcp::CancelledNotificationParams &p)
    {
        p.requestId = j.at("requestId").get<int64_t>();
        p.reason = j.value("reason", std::string{});
    }
};

template <>
struct adl_serializer<Mcp::CancelledNotification> {
    static void to_json(json &j, const Mcp::CancelledNotification &notif)
    {
        j = json::object();
        if (notif.params_) {
            auto p = static_cast<const Mcp::CancelledNotificationParams *>(notif.params_.get());
            if (p != nullptr) {
                j = *p;
            }
        }
    }

    static void from_json(const json &j, Mcp::CancelledNotification &notif)
    {
        if (j.contains("requestId")) {
            Mcp::CancelledNotificationParams p(0, std::string{});
            j.get_to(p);
            notif.params_ = std::make_unique<Mcp::CancelledNotificationParams>(std::move(p));
        } else {
            notif.params_.reset();
        }
    }
};

// Notification-related serializers
template <>
struct adl_serializer<Mcp::ResourceUpdatedNotificationParams> {
    static void to_json(json &j, const Mcp::ResourceUpdatedNotificationParams &p)
    {
        j["uri"] = p.uri;
    }

    static void from_json(const json &j, Mcp::ResourceUpdatedNotificationParams &p)
    {
        p.uri = j.value("uri", std::string{});
    }
};

template <>
struct adl_serializer<Mcp::ResourceUpdatedNotification> {
    static void to_json(json &j, const Mcp::ResourceUpdatedNotification &notif)
    {
        j = json::object();
        if (notif.params_) {
            auto p = static_cast<const Mcp::ResourceUpdatedNotificationParams *>(notif.params_.get());
            if (p != nullptr) {
                j = *p;
            }
        }
    }

    static void from_json(const json &j, Mcp::ResourceUpdatedNotification &notif)
    {
        if (j.contains("uri")) {
            Mcp::ResourceUpdatedNotificationParams p(std::string{});
            j.get_to(p);
            notif.params_ = std::make_unique<Mcp::ResourceUpdatedNotificationParams>(std::move(p));
        } else {
            notif.params_.reset();
        }
    }
};

template <>
struct adl_serializer<Mcp::ToolListChangedNotification> {
    static void to_json(json &j, const Mcp::ToolListChangedNotification &)
    {
        j = json::object();
    }

    static void from_json(const json &, Mcp::ToolListChangedNotification &) {}
};

template <>
struct adl_serializer<Mcp::PromptListChangedNotification> {
    static void to_json(json &j, const Mcp::PromptListChangedNotification &)
    {
        j = json::object();
    }

    static void from_json(const json &, Mcp::PromptListChangedNotification &) {}
};

template <>
struct adl_serializer<Mcp::ResourceListChangedNotification> {
    static void to_json(json &j, const Mcp::ResourceListChangedNotification &)
    {
        j = json::object();
    }

    static void from_json(const json &, Mcp::ResourceListChangedNotification &) {}
};

// ResourceTemplateReference
template <>
struct adl_serializer<Mcp::ResourceTemplateReference> {
    static void to_json(json& j, const Mcp::ResourceTemplateReference& ref)
    {
        j["type"] = ref.type;
        j["uri"] = ref.uri;
    }

    static void from_json(const json& j, Mcp::ResourceTemplateReference& ref)
    {
        ref.type = j.value("type", std::string("ref/resource"));
        j.at("uri").get_to(ref.uri);
    }
};

// PromptReference
template <>
struct adl_serializer<Mcp::PromptReference> {
    static void to_json(json& j, const Mcp::PromptReference& ref)
    {
        j["type"] = ref.type;
        j["name"] = ref.name;
    }

    static void from_json(const json& j, Mcp::PromptReference& ref)
    {
        ref.type = j.value("type", std::string("ref/prompt"));
        j.at("name").get_to(ref.name);
    }
};

// CompleteReference (variant)
template <>
struct adl_serializer<Mcp::CompleteReference> {
    static void to_json(json& j, const Mcp::CompleteReference& v)
    {
        std::visit([&](auto&& arg) { j = json(arg); }, v);
    }

    static void from_json(const json& j, Mcp::CompleteReference& v)
    {
        const auto type = j.value("type", std::string());
        if (type == "ref/resource") {
            v = j.get<Mcp::ResourceTemplateReference>();
        } else if (type == "ref/prompt") {
            v = j.get<Mcp::PromptReference>();
        }
    }
};

// CompletionArgument
template <>
struct adl_serializer<Mcp::CompletionArgument> {
    static void to_json(json& j, const Mcp::CompletionArgument& arg)
    {
        j["name"] = arg.name;
        j["value"] = arg.value;
    }

    static void from_json(const json& j, Mcp::CompletionArgument& arg)
    {
        j.at("name").get_to(arg.name);
        j.at("value").get_to(arg.value);
    }
};

// CompletionContext
template <>
struct adl_serializer<Mcp::CompletionContext> {
    static void to_json(json& j, const Mcp::CompletionContext& ctx)
    {
        if (ctx.arguments.has_value()) {
            json args = json::object();
            for (const auto& [key, value] : ctx.arguments.value()) {
                args[key] = value;
            }
            j["arguments"] = args;
        }
    }

    static void from_json(const json& j, Mcp::CompletionContext& ctx)
    {
        if (j.contains("arguments") && j.at("arguments").is_object()) {
            std::unordered_map<std::string, std::string> args;
            const auto& argsJson = j.at("arguments");
            for (auto it = argsJson.begin(); it != argsJson.end(); ++it) {
                if (it->is_string()) {
                    args[it.key()] = it->get<std::string>();
                }
            }
            ctx.arguments = std::move(args);
        } else {
            ctx.arguments = std::nullopt;
        }
    }
};

// Completion
template <>
struct adl_serializer<Mcp::Completion> {
    static void to_json(json& j, const Mcp::Completion& c)
    {
        j["values"] = c.values;
        if (c.total.has_value()) {
            j["total"] = c.total.value();
        }
        if (c.hasMore.has_value()) {
            j["hasMore"] = c.hasMore.value();
        }
    }

    static void from_json(const json& j, Mcp::Completion& c)
    {
        j.at("values").get_to(c.values);
        if (j.contains("total")) {
            c.total = j.at("total").get<int64_t>();
        }
        if (j.contains("hasMore")) {
            c.hasMore = j.at("hasMore").get<bool>();
        }
    }
};

// CompleteResult
template <>
struct adl_serializer<Mcp::CompleteResult> {
    static void to_json(json& j, const Mcp::CompleteResult& r)
    {
        j["completion"] = r.completion;
        if (r.meta.has_value()) {
            j["_meta"] = r.meta.value();
        }
    }

    static void from_json(const json& j, Mcp::CompleteResult& r)
    {
        j.at("completion").get_to(r.completion);
        if (j.contains("_meta") && j.at("_meta").is_object()) {
            r.meta = ParseMetaStringMap(j.at("_meta"));
        } else {
            r.meta = std::nullopt;
        }
    }
};

// CompleteRequest -> {"method":"completion/complete", "params":{ref, argument, context?}}
template <>
struct adl_serializer<Mcp::CompleteRequest> {
    static void to_json(json& j, const Mcp::CompleteRequest& req)
    {
        if (req.params_) {
            auto p = static_cast<const Mcp::CompleteRequestParams*>(req.params_.get());
            j["ref"] = p->ref;
            j["argument"] = p->argument;
            if (p->context.has_value()) {
                j["context"] = p->context.value();
            }
        }
    }

    static void from_json(const json& j, Mcp::CompleteRequest& req)
    {
        j.at("method").get_to(req.method_);
        if (j.contains("params")) {
            const auto& paramsJson = j.at("params");
            auto ref = paramsJson.at("ref").get<Mcp::CompleteReference>();
            auto argument = paramsJson.at("argument").get<Mcp::CompletionArgument>();
            std::optional<Mcp::CompletionContext> context;
            if (paramsJson.contains("context")) {
                context = paramsJson.at("context").get<Mcp::CompletionContext>();
            }
            req.params_ = std::make_unique<Mcp::CompleteRequestParams>(std::move(ref), std::move(argument),
                std::move(context));
        }
    }
};

} // namespace nlohmann

namespace Mcp {

using nlohmann::json;

JSONRPCNotification::JSONRPCNotification()
{
    jsonrpc_ = JSONRPC_VERSION;
}

JSONRPCRequest::JSONRPCRequest()
{
    jsonrpc_ = JSONRPC_VERSION;
    id_ = 0;
}

JSONRPCResponse::JSONRPCResponse()
{
    jsonrpc_ = JSONRPC_VERSION;
    id_ = 0;
}

InitializeRequest::InitializeRequest()
{
    method_ = "initialize";
    params_ = std::make_unique<InitializeRequestParams>(Mcp::DEFAULT_PROTOCOL_VERSION, ClientCapabilities(),
                                                        Implementation());
}

InitializeRequest::InitializeRequest(const std::string& clientName, const std::string& clientVersion,
                                     ClientCapabilities capabilities)
{
    method_ = "initialize";
    auto params = std::make_unique<InitializeRequestParams>(Mcp::DEFAULT_PROTOCOL_VERSION, std::move(capabilities),
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

ResourceUpdatedNotification::ResourceUpdatedNotification()
{
    method_ = "notifications/resources/updated";
}

CancelledNotification::CancelledNotification()
{
    method_ = "notifications/cancelled";
}

ToolListChangedNotification::ToolListChangedNotification()
{
    method_ = "notifications/tools/list_changed";
}

PromptListChangedNotification::PromptListChangedNotification()
{
    method_ = "notifications/prompts/list_changed";
}

ResourceListChangedNotification::ResourceListChangedNotification()
{
    method_ = "notifications/resources/list_changed";
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

SetLoggingLevelRequest::SetLoggingLevelRequest()
{
    method_ = "logging/setLevel";
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

CompleteRequest::CompleteRequest()
{
    method_ = "completion/complete";
}

ListRootsRequest::ListRootsRequest()
{
    method_ = "roots/list";
}

CreateMessageRequest::CreateMessageRequest()
{
    method_ = "sampling/createMessage";
}

std::string JSONRPCRequest::Serialize(const std::string& method) const
{
    json j;
    j["jsonrpc"] = jsonrpc_;
    j["id"] = id_;
    // Use provided method if non-empty, otherwise fallback to stored method_
    j["method"] = method.empty() ? method_ : method;
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
    } else if (request_ != nullptr && request_->method_ == "logging/setLevel") {
        const auto* levelReq = dynamic_cast<const SetLoggingLevelRequest*>(request_.get());
        if (levelReq != nullptr) {
            j["params"] = *levelReq;
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
    } else if (request_ != nullptr && request_->method_ == "completion/complete") {
        const auto* completeReq = dynamic_cast<const CompleteRequest*>(request_.get());
        if (completeReq != nullptr) {
            j["params"] = *completeReq;
        }
    } else if (request_ != nullptr && request_->method_ == "roots/list") {
        const auto* listReq = dynamic_cast<const ListRootsRequest*>(request_.get());
        if (listReq != nullptr) {
            j["params"] = *listReq;
        }
    } else if (request_ != nullptr && request_->method_ == "sampling/createMessage") {
        const auto* sampleReq = dynamic_cast<const CreateMessageRequest*>(request_.get());
        if (sampleReq != nullptr) {
            j["params"] = *sampleReq;
        }
    }

    return j.dump();
}

int JSONRPCRequest::Deserialize(const std::string& jsonStr, const std::string& method)
{
    auto j = json::parse(jsonStr);
    jsonrpc_ = j.value("jsonrpc", "");
    if (j.contains("id")) {
        id_ = j.at("id").get<int64_t>();
    }
    method_ = method;

    if (method_ == "initialize") {
        auto initReq = std::make_unique<InitializeRequest>();
        initReq->method_ = method_;
        auto p = std::make_unique<InitializeRequestParams>(
            Mcp::DEFAULT_PROTOCOL_VERSION, Mcp::ClientCapabilities{}, Mcp::Implementation{});
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
    } else if (method_ == "logging/setLevel") {
        SetLoggingLevelRequest levelReq = j.get<SetLoggingLevelRequest>();
        auto levelPtr = std::make_unique<SetLoggingLevelRequest>(std::move(levelReq));
        request_ = std::move(levelPtr);
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
    } else if (method_ == "completion/complete") {
        CompleteRequest completeReq = j.get<CompleteRequest>();
        auto reqPtr = std::make_unique<CompleteRequest>(std::move(completeReq));
        request_ = std::move(reqPtr);
    } else if (method_ == "roots/list") {
        ListRootsRequest listReq = j.get<ListRootsRequest>();
        auto reqPtr = std::make_unique<ListRootsRequest>(std::move(listReq));
        request_ = std::move(reqPtr);
    } else if (method_ == "sampling/createMessage") {
        CreateMessageRequest sampleReq = j.get<CreateMessageRequest>();
        auto reqPtr = std::make_unique<CreateMessageRequest>(std::move(sampleReq));
        request_ = std::move(reqPtr);
    }

    return 0;
}

std::string JSONRPCResponse::Serialize(const std::string& method) const
{
    json j;
    j["jsonrpc"] = jsonrpc_;
    j["id"] = id_;

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
        } else if (method == "roots/list") {
            auto* listRootsResult = dynamic_cast<const ListRootsResult*>(result_.get());
            if (listRootsResult == nullptr) {
                throw std::runtime_error("Failed to cast result to ListRootsResult");
            }
            j["result"] = *listRootsResult;
        } else if (method == "sampling/createMessage") {
            auto* sampleResult = dynamic_cast<const CreateMessageResult*>(result_.get());
            if (sampleResult == nullptr) {
                throw std::runtime_error("Failed to cast result to CreateMessageResult");
            }
            j["result"] = *sampleResult;
        } else if (method == "resources/subscribe" || method == "resources/unsubscribe") {
            auto* emptyResult = dynamic_cast<const EmptyResult*>(result_.get());
            if (emptyResult == nullptr) {
                throw std::runtime_error("Failed to cast result to EmptyResult");
            }
            j["result"] = json::object();
        } else if (method == "logging/setLevel") {
            auto* emptyResult = dynamic_cast<const EmptyResult*>(result_.get());
            if (emptyResult == nullptr) {
                throw std::runtime_error("Failed to cast result to EmptyResult");
            }
            j["result"] = json::object();
        } else if (method == "completion/complete") {
            auto* completeResult = dynamic_cast<const CompleteResult*>(result_.get());
            if (completeResult == nullptr) {
                throw std::runtime_error("Failed to cast result to CompleteResult");
            }
            j["result"] = *completeResult;
        }
    }

    return j.dump();
}

int JSONRPCResponse::Deserialize(const std::string& jsonStr, const std::string& method)
{
    auto j = json::parse(jsonStr);
    jsonrpc_ = j.value("jsonrpc", "");
    if (j.contains("id")) {
        id_ = j.at("id").get<int64_t>();
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
    } else if (method == "roots/list") {
        auto p = std::make_shared<ListRootsResult>();
        if (j.contains("result")) {
            j.at("result").get_to(*p);
        }
        result_ = std::move(p);
    } else if (method == "sampling/createMessage") {
        auto p = std::make_shared<CreateMessageResult>();
        if (j.contains("result")) {
            j.at("result").get_to(*p);
        }
        result_ = std::move(p);
    } else if (method == "resources/subscribe" || method == "resources/unsubscribe") {
        result_ = std::make_shared<EmptyResult>();
    } else if (method == "logging/setLevel") {
        result_ = std::make_shared<EmptyResult>();
    } else if (method == "completion/complete") {
        auto p = std::make_shared<CompleteResult>();
        if (j.contains("result")) {
            j.at("result").get_to(*p);
        }
        result_ = std::move(p);
    }

    return 0;
}

std::string JSONRPCNotification::Serialize(const std::string& method) const
{
    json j;
    j["jsonrpc"] = jsonrpc_;
    j["method"] = method_;
    if (method_ == "notifications/initialized") {
        j["params"] = json::object();
    } else if (notification_ != nullptr && method_ == "notifications/resources/updated") {
        const auto* notif = dynamic_cast<const ResourceUpdatedNotification*>(notification_.get());
        if (notif != nullptr) {
            j["params"] = *notif;
        }
    } else if (method_ == "notifications/tools/list_changed") {
        j["params"] = json::object();
    } else if (method_ == "notifications/prompts/list_changed") {
        j["params"] = json::object();
    } else if (method_ == "notifications/resources/list_changed") {
        j["params"] = json::object();
    } else if (notification_ != nullptr && method_ == "notifications/cancelled") {
        const auto* notif = dynamic_cast<const CancelledNotification*>(notification_.get());
        if (notif != nullptr) {
            j["params"] = *notif;
        }
    }

    return j.dump();
}

int JSONRPCNotification::Deserialize(const std::string& jsonStr, const std::string& method)
{
    auto j = json::parse(jsonStr);
    jsonrpc_ = j.value("jsonrpc", "");
    method_ = method;

    if (method_ == "notifications/initialized") {
        auto notif = std::make_unique<InitializedNotification>();
        notif->method_ = method_;
        notification_ = std::move(notif);
    } else if (method_ == "notifications/resources/updated") {
        auto notif = std::make_unique<ResourceUpdatedNotification>();
        notif->method_ = method_;
        if (j.contains("params")) {
            j.at("params").get_to(*notif);
        }
        notification_ = std::move(notif);
    } else if (method_ == "notifications/tools/list_changed") {
        auto notif = std::make_unique<ToolListChangedNotification>();
        notif->method_ = method_;
        notification_ = std::move(notif);
    } else if (method_ == "notifications/prompts/list_changed") {
        auto notif = std::make_unique<PromptListChangedNotification>();
        notif->method_ = method_;
        notification_ = std::move(notif);
    } else if (method_ == "notifications/resources/list_changed") {
        auto notif = std::make_unique<ResourceListChangedNotification>();
        notif->method_ = method_;
        notification_ = std::move(notif);
    } else if (method_ == "notifications/cancelled") {
        auto notif = std::make_unique<CancelledNotification>();
        notif->method_ = method_;
        if (j.contains("params")) {
            j.at("params").get_to(*notif);
        }
        notification_ = std::move(notif);
    }

    return 0;
}

std::string JSONRPCError::Serialize() const
{
    json j;
    j["jsonrpc"] = jsonrpc_;
    j["id"] = id_;
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
        id_ = j.at("id").get<int64_t>();
    }

    const auto& errorObj = j.at("error");
    code_ = errorObj.value("code", -1);
    message_ = errorObj.value("message", "Internal error");
    if (errorObj.contains("data")) {
        data_ = errorObj["data"];
    }

    return 0;
}

JSONRPCMessage DeserializeJSONRPCMessage(const std::string& jsonStr, const std::string& method)
{
    auto j = json::parse(jsonStr);

    bool hasId = j.contains("id");
    bool hasMethod = j.contains("method");
    bool hasCode = j.contains("code");

    if (hasId && hasMethod) {
        JSONRPCRequest request;
        std::string reqMethod = j.at("method").get<std::string>();
        request.Deserialize(jsonStr, reqMethod);
        return std::move(request);
    }

    if (hasId && !hasMethod) {
        JSONRPCResponse response;
        response.Deserialize(jsonStr, method);
        return std::move(response);
    }

    if (!hasId && hasMethod) {
        JSONRPCNotification notification;
        std::string notificationMethod = j.at("method").get<std::string>();
        notification.Deserialize(jsonStr, notificationMethod);
        return std::move(notification);
    }

    if (hasId && hasCode) {
        JSONRPCError error;
        error.Deserialize(jsonStr);
        return std::move(error);
    }

    throw std::runtime_error("Invalid JSON-RPC message: cannot determine type");
}

std::string SerializeJSONRPCMessage(const JSONRPCMessage& message, std::optional<std::string> method)
{
    if (auto* request = std::get_if<JSONRPCRequest>(&message)) {
        return request->Serialize(request->method_);
    }

    if (auto* response = std::get_if<JSONRPCResponse>(&message)) {
        if (!method.has_value()) {
            throw std::runtime_error("Method name is required for response serialization");
        }
        return response->Serialize(method.value());
    }

    if (auto* notification = std::get_if<JSONRPCNotification>(&message)) {
        return notification->Serialize(notification->method_);
    }

    if (auto* error = std::get_if<JSONRPCError>(&message)) {
        return error->Serialize();
    }

    throw std::runtime_error("Unknown JSON-RPC message type");
}

} // namespace Mcp
