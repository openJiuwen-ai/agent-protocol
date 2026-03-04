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
using MetaMap = std::unordered_map<std::string, std::string>;

/** MCP progress token: string or integer per spec. */
using ProgressToken = std::variant<std::string, int64_t>;

/** Optional _meta that may exist on any request params (e.g. progressToken for MCP progress). */
struct RequestParamsMeta {
    std::optional<ProgressToken> progressToken;
};

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
    // Stateless mode: each HTTP request is handled independently.
    bool stateless{false};
    uint32_t ioThreads{1};
    TlsConfig tlsConfig;
};

struct ClientConfig {
    std::string name{DEFAULT_CLIENT_NAME};
    std::string version{DEFAULT_VERSION};
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

struct ServerConfig {
    std::string name = DEFAULT_SERVER_NAME;
    std::string version = DEFAULT_VERSION;
    uint32_t workerThreads{1};
    ServerCapabilities capabilities{};
};

struct MCPBaseType {
    virtual ~MCPBaseType() = default;
};

struct Result : public MCPBaseType {
    std::optional<MetaMap> meta;

    virtual ~Result() = default;
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

// A type that can hold any of the content types
using ContentType = std::variant<TextContent, ImageContent, AudioContent, ResourceLink, EmbeddedResource>;

struct CallToolResult : public Result {
    std::vector<ContentType> content;
    bool isError = false;
};

// Content block representing a model-initiated tool call.
struct ToolUseContent {
    std::string type = "tool_use";
    std::string id;
    std::string name;
    MetaMap input;
    std::optional<MetaMap> meta;
};

// Content block representing the result of a tool call.
struct ToolResultContent {
    std::string type = "tool_result";
    std::string toolUseId;
    std::vector<ContentType> content;
    std::optional<MetaMap> structuredContent;
    std::optional<bool> isError;
    std::optional<MetaMap> meta;
};

using SamplingMessageContentBlock =
    std::variant<TextContent, ImageContent, AudioContent, ToolUseContent, ToolResultContent>;

// A message sent to, or received from, an LLM provider.
// Note: schema allows "content" as a single block or an array; we preserve that.
struct SamplingMessage {
    RoleType role;
    std::variant<SamplingMessageContentBlock, std::vector<SamplingMessageContentBlock>> content;
    std::optional<MetaMap> meta;
};

struct ModelHint {
    std::optional<std::string> name;
};

struct ModelPreferences {
    std::optional<std::vector<ModelHint>> hints;
    std::optional<double> costPriority;
    std::optional<double> speedPriority;
    std::optional<double> intelligencePriority;
};

struct ToolChoice {
    std::optional<std::string> mode; // "none" | "required" | "auto"
};

struct CreateMessageResult : public Result {
    std::string model;
    std::optional<std::string> stopReason;
    RoleType role;
    std::variant<SamplingMessageContentBlock, std::vector<SamplingMessageContentBlock>> content;
};

//Struct for list_tool result
struct Tool {
    std::string name;
    std::string description;
    JsonValue inputSchema;
};

// Parameters for the server-initiated sampling request `sampling/createMessage`.
// This mirrors the MCP Sampling specification (2025-11-25).
struct CreateMessageParams {
    std::vector<SamplingMessage> messages;
    std::optional<ModelPreferences> modelPreferences;
    std::optional<std::string> systemPrompt;
    // "none" | "thisServer" | "allServers" (the latter two are soft-deprecated in the spec).
    std::optional<std::string> includeContext;
    std::optional<double> temperature;
    int64_t maxTokens = 0;
    std::optional<std::vector<std::string>> stopSequences;
    std::optional<MetaMap> metadata;
    std::optional<std::vector<Tool>> tools;
    std::optional<ToolChoice> toolChoice;
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

// Describes a reusable prompt.
struct PromptInfo {
    std::string name;
    std::optional<std::string> description;
    std::optional<std::string> title;
    std::optional<std::vector<Icon>> icons;
    std::optional<std::vector<PromptArgument>> arguments;
};

// Client sampling capability flags.
// In the MCP schema, sampling capability is represented as:
//   "sampling": { "context"?: {}, "tools"?: {} }
struct SamplingCapability {
    bool context = false;
    bool tools = false;
};

struct FormElicitationCapability {
};

struct UrlElicitationCapability {
};

struct ElicitationCapability {
    std::optional<FormElicitationCapability> form;
    std::optional<UrlElicitationCapability> url;
};

struct RootsCapability {
    bool listChanged = false;
};

// Client tasks capability flags.
// The MCP schema has a nested shape, but we keep a flattened representation and
// serialize/deserialize to the nested schema form.
struct ClientTasksCapability {
    bool list = false;
    bool cancel = false;
    bool samplingCreateMessage = false;
    bool elicitationCreate = false;
};

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

// Root entry for roots/list
struct Root {
    std::optional<std::string> name;
    std::string uri;
};

struct ListRootsResult : public Result {
    std::vector<Root> roots;
};

struct ElicitResult : public Result {
    std::string action;
    MetaMap content;
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

// resources/templates/list
struct ListResourceTemplatesResult : public Result {
    std::vector<ResourceTemplate> resourceTemplates;
};

enum class LoggingLevel { Debug = 0, Info, Notice, Warning, Error, Critical, Alert, Emergency };

// Callback indicating the client can serve roots/list.
// If this is not set, the client must not advertise the `roots` capability in initialize.
using ListRootsCallback = std::function<ListRootsResult()>;

// Callback indicating the client can serve elicitation/create.
using ElicitCallback = std::function<ElicitResult(const std::string&, const Mcp::MetaMap&)>;

using ElicitUrlCallback = std::function<ElicitResult(const std::string&, const std::string&, const std::string&)>;

// Register a callback that will be invoked when the server sends `notifications/message`.
// The default callback is print in MCP_LOG.
using LoggingCallback = std::function<void(const std::string& level,
                                                 const std::string& data,
                                                 const std::string& logger)>;

// completion/complete references
struct ResourceTemplateReference {
    std::string type = "ref/resource";
    std::string uri;
};

struct PromptReference {
    std::string type = "ref/prompt";
    std::string name;
};

using CompleteReference = std::variant<ResourceTemplateReference, PromptReference>;

// completion/complete argument and context
struct CompletionArgument {
    std::string name;
    std::string value;
};

struct CompletionContext {
    std::optional<std::unordered_map<std::string, std::string>> arguments;
};

// Completion information
struct Completion {
    std::vector<std::string> values;
    std::optional<int64_t> total;
    std::optional<bool> hasMore;
};

// completion/complete request/response
struct CompleteResult : public Result {
    Completion completion;
};

} // namespace Mcp

#endif // MCP_TYPE_INCLUDE_H_
