/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef MCP_TYPE_INCLUDE_H_
#define MCP_TYPE_INCLUDE_H_

#include <chrono>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <unordered_map>

namespace Mcp {

using JsonValue = nlohmann::json;

// Constants
constexpr char DEFAULT_SERVER_NAME[] = "MCP Server";
constexpr char DEFAULT_CLIENT_NAME[] = "MCP Client";
constexpr char DEFAULT_VERSION[] = "1.0.0";
constexpr uint32_t DEFAULT_TIMEOUT = 30000; // 30s

struct StdioClientConfig {
    std::string command;
    std::vector<std::string> args;
    std::map<std::string, std::string> env;
};

struct TlsConfig {
    bool enabled{false};

    std::string caFile;
    std::string certFile;
    std::string keyFile;

    std::string serverName;
    bool verifyPeer{true};
};

struct StreamableHttpClientConfig {
    std::string endpoint;
    std::chrono::milliseconds timeout{DEFAULT_TIMEOUT};
    std::chrono::milliseconds sseTimeout{DEFAULT_TIMEOUT};
    std::unordered_map<std::string, std::string> headers;
    TlsConfig tlsConfig;
};

struct StreamableHttpServerConfig {
    std::string endpoint;
    bool isJsonResponseEnabled{false};
    uint32_t ioThreads{1};
    TlsConfig tlsConfig;
};

struct ClientConfig {
    std::string name{DEFAULT_CLIENT_NAME};
    std::string version{DEFAULT_VERSION};
};

struct ServerConfig {
    std::string name = DEFAULT_SERVER_NAME;
    std::string version = DEFAULT_VERSION;
    uint32_t workerThreads{1};
};

struct MCPBaseType {
    virtual ~MCPBaseType() = default;
};

struct Result : public MCPBaseType {
    std::optional<std::unordered_map<std::string, JsonValue>> meta;

    virtual ~Result() = default;
};

struct McpClientCapabilities {
    struct RootsCapability {
        std::optional<bool> listChanged;
    } roots;
    std::optional<bool> sampling;
};

struct ClientInfo {
    std::string name;
    std::string version;
};
struct ServerInfo {
    std::string name;
    std::string version;
};

// RoleType indicates which side authored a prompt message.
// USER: from a human user. ASSISTANT: from an automated assistant.
enum class RoleType { USER = 0, ASSISTANT = 1 };

struct Annotations {
    std::optional<std::vector<RoleType>> audience;
    std::optional<std::string> lastModified;
    std::optional<double> priority;
};

struct TextContent {
    std::string type = "text";
    std::string text;
    std::optional<Annotations> annotations;
};

struct ImageContent {
    std::string type = "image";
    std::string data; // Base64 encoded image data
    std::string mimeType;
    std::optional<Annotations> annotations;
};

struct AudioContent {
    std::string type = "audio";
    std::string data; // Base64 encoded audio data
    std::string mimeType;
    std::optional<Annotations> annotations;
};

struct TextResourceContents {
    std::string uri;
    std::string text;
    std::optional<std::string> mimeType;
};

struct BlobResourceContents {
    std::string uri;
    std::string blob;
    std::optional<std::string> mimeType;
};

using ResourceContents = std::variant<TextResourceContents, BlobResourceContents>;

struct EmbeddedResource {
    std::string type = "resource";
    ResourceContents resource;
    std::optional<Annotations> annotations;
};

struct Icon {
    std::string src;
    std::optional<std::string> mimeType;
    std::optional<std::vector<std::string>> sizes;
    std::optional<std::string> theme; // "light" | "dark"
};

struct ResourceLink {
    std::string type = "resource_link";
    std::string uri;
    std::string name;
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::optional<std::string> mimeType;
    std::optional<std::int64_t> size;
    std::optional<std::vector<Icon>> icons;
    std::optional<Annotations> annotations;
};

struct ToolAnnotations {
    std::optional<std::string> title;
    std::optional<bool> readOnlyHint = std::nullopt;
    std::optional<bool> destructiveHint = std::nullopt;
    std::optional<bool> idempotentHint = std::nullopt;
    std::optional<bool> openWorldHint = std::nullopt;
};

// A type that can hold any of the content types
using ContentType = std::variant<TextContent, ImageContent, AudioContent, ResourceLink, EmbeddedResource>;

struct CallToolResult : public Result {
    std::vector<ContentType> content;
    bool isError = false;
};

using ToolFunc = std::function<CallToolResult(const std::string& name, const JsonValue& arguments,
                                              const std::optional<JsonValue>& ctx)>;

//Struct for list_tool result
struct Tool {
    std::string name;
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::optional<std::string> inputSchema;
    std::optional<std::string> outputSchema;
    std::optional<ToolAnnotations> annotations;
    std::optional<std::vector<Icon>> icons;
};

struct ListToolsResult : public Result {
    std::vector<Tool> tools;
};

// A single message inside a prompt sequence.
// 'role' indicates who authored the message. 'content' can be text, image or embedded resource.
struct PromptMessage {
    RoleType role;
    ContentType content;
};

// Metadata for a single prompt argument.
struct PromptArgument {
    std::string name;
    std::optional<std::string> description;
    std::optional<bool> required;
    std::optional<std::string> title;
};

// Result type for fetching a specific prompt: description and the sequence of messages.
struct GetPromptResult : public Result {
    std::optional<std::string> description;
    std::vector<PromptMessage> messages;
};

// Render function for a prompt definition.
// The function should take the prompt name and optional arguments, then return a GetPromptResult
// whose `messages_` are ready to be used as model context.
using RenderPromptFunc =
    std::function<GetPromptResult(const std::string& name, const std::optional<JsonValue>& argument)>;

// Describes a reusable prompt.
struct PromptInfo {
    std::string name;
    std::optional<std::string> description;
    std::optional<std::string> title;
    std::optional<std::vector<Icon>> icons;
    std::optional<std::vector<PromptArgument>> arguments;
};

struct SamplingCapability {};

struct ElicitationCapability {};

struct RootsCapability {
    bool listChanged = false;
};

struct ClientTasksCapability {};

struct ClientCapabilities {
    std::optional<std::unordered_map<std::string, std::unordered_map<std::string, nlohmann::json>>> experimental;
    std::optional<SamplingCapability> sampling;
    std::optional<ElicitationCapability> elicitation;
    std::optional<RootsCapability> roots;
    std::optional<ClientTasksCapability> tasks;
};

struct Implementation {
    std::string name;
    std::optional<std::string> title;

    std::string version;
    std::optional<std::string> websiteUrl;
    std::optional<std::vector<Icon>> icons;
};

struct LoggingCapabilities {};

struct PromptsCapabilities {
    std::optional<bool> listChanged;
};

struct ResourcesCapabilities {
    std::optional<bool> subscribe;
    std::optional<bool> listChanged;
};

struct ToolsCapabilities {
    std::optional<bool> listChanged;
};

struct ServerCapabilities {
    std::optional<std::unordered_map<std::string, JsonValue>> experimental;
    std::optional<LoggingCapabilities> logging;
    std::optional<PromptsCapabilities> prompts;
    std::optional<ResourcesCapabilities> resources;
    std::optional<ToolsCapabilities> tools;
};

struct InitializeResult : public Result {
    std::string protocolVersion;
    ServerCapabilities capabilities;
    Implementation serverInfo;
    std::optional<std::string> instructions;

    InitializeResult(std::string protocolVersion, ServerCapabilities capabilities, Implementation serverInfo,
                     std::optional<std::string> instructions = std::nullopt);
};

// Result type for listing available prompts.
struct ListPromptsResult : public Result {
    std::vector<PromptInfo> prompts;
};

// A response that indicates success but carries no data.
struct EmptyResult : public Result {};

struct ResourceInfo {
    std::string uri;
    std::string name;
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::optional<std::string> mimeType;
    std::optional<std::int64_t> size;
    std::optional<std::vector<Icon>> icons;
    std::optional<Annotations> annotations;
};

struct ResourceTemplate {
    std::string uriTemplate;
    std::string name;
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::optional<std::string> mimeType;
    std::optional<std::vector<Icon>> icons;
    std::optional<Annotations> annotations;
};

// resources/list
struct ListResourcesResult : public Result {
    std::vector<ResourceInfo> resources;
};

// resources/read
struct ReadResourceResult : public Result {
    std::vector<ResourceContents> contents;
};

// Function type for reading a resource
using ReadResourceFunc = std::function<ReadResourceResult(const std::string& uri)>;

// resources/templates/list
struct ListResourceTemplatesResult : public Result {
    std::vector<ResourceTemplate> resourceTemplates;
};

} // namespace Mcp

#endif // MCP_TYPE_INCLUDE_H_
