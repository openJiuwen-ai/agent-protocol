/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_TYPES
#define A2A_TYPES

#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <map>
#include <unordered_map>
#include <functional>

namespace A2A {

/**
 * @brief A2A protocol type definitions (messages, tasks, agent card, errors).
 * @note A2A 协议公共数据类型，与 JSON-RPC wire format 对应。
 */

/** @brief Default transport label for JSON-RPC over HTTP. */
constexpr const char* JSONRPC_TRANSPORT = "JSONRPC";

/** @brief JSON-RPC protocol version string. */
constexpr const char* JSONRPC_VERSION = "2.0";

/**
 * @brief Role of a message sender in a conversation.
 */
enum class Role {
    AGENT,
    USER,
    UNSPECIFIED
};

/**
 * @brief A2A and JSON-RPC error codes.
 * @note 包含 RFC 4627 JSON-RPC 标准码与 A2A 扩展码。
 */
enum class A2AErrorCode : int {
    A2A_SUCCESS                 = 0,

    // Standard JSON-RPC 2.0 Errors (RFC 7196)
    JSONRPC_PARSE_ERROR         = -32700,
    JSONRPC_INVALID_REQUEST     = -32600,
    JSONRPC_METHOD_NOT_FOUND    = -32601,
    JSONRPC_INVALID_PARAMS      = -32602,
    JSONRPC_INTERNAL_ERROR      = -32603,

    // Implementation-defined Server Errors (in [-32099, -32000])
    TASK_NOT_FOUND                              = -32001,
    TASK_NOT_CANCELABLE                         = -32002,
    PUSH_NOTIFICATION_NOT_SUPPORTED             = -32003,
    UNSUPPORTED_OPERATION                       = -32004,
    CONTENT_TYPE_NOT_SUPPORTED                  = -32005,
    INVALID_AGENT_RESPONSE                      = -32006,
    AUTHENTICATED_EXTENDED_CARD_NOT_CONFIGURED  = -32007,
    EXTENSION_SUPPORT_REQUIRED_ERROR            = -32008,
    VERSION_NOT_SUPPORTED_ERROR                 = -32009,

    // A2A Client Errors (in [-32199, -32100])
    A2A_REQUEST_TIMEOUT         = -32101,
    A2A_TRANSPORT_EXCEPTION     = -32102,
    A2A_INVALID_TRANSPORT       = -32103,
    A2A_INVALID_FORMAT          = -32106,
    A2A_UNAUTHORIZED            = -32107,
    A2A_STATUS_ERROR            = -32108,
    A2A_INVALID_INPUT           = -32109,
    A2A_BAD_ALLOC               = -32110,
    A2A_CONCURRENT_LIMIT        = -32111
};

/** @brief Callback that sends a raw string payload. */
using A2ASendCb = std::function<int(const std::string&)>;

/** @brief Callback that receives a raw string payload. */
using A2ARawDataCb = std::function<void(const std::string&)>;

/**
 * @brief Per-request client-side call context.
 */
struct ClientCallContext {
    /** @brief Arbitrary per-request state. */
    std::string state;
    /** @brief Serialized request headers. */
    std::string headers;
};

/**
 * @brief A single content part within a message or artifact.
 * @note 支持 text / raw / url / structured data 四种载荷（oneof）。
 */
struct Part {
    std::optional<std::string> text;
    std::optional<std::string> raw;
    std::optional<std::string> url;
    std::optional<std::variant<std::string, int, double, bool>> data;

    std::optional<std::string> metadata;
    std::optional<std::string> filename;
    std::optional<std::string> mediaType;
};

/**
 * @brief Agent-produced artifact attached to a task.
 */
struct Artifact {
    std::string artifactId;
    std::optional<std::string> description;
    std::optional<std::vector<std::string>> extensions;
    std::optional<std::string> metadata;
    std::optional<std::string> name;
    std::vector<Part> parts;
};

/**
 * @brief A conversation message (user or agent).
 */
struct Message {
    std::optional<std::string> contextId;
    std::optional<std::vector<std::string>> extensions;
    std::string messageId;
    std::optional<std::string> metadata;
    std::vector<Part> parts;
    std::optional<std::vector<std::string>> referenceTaskIds;
    Role role;
    std::optional<std::string> taskId;
};

/**
 * @brief Lifecycle state of a task.
 */
enum class TaskState {
    SUBMITTED,
    WORKING,
    INPUT_REQUIRED,
    COMPLETED,
    CANCELED,
    FAILED,
    REJECTED,
    AUTH_REQUIRED,
    UNSPECIFIED
};

/**
 * @brief Current status snapshot of a task.
 */
struct TaskStatus {
    std::optional<Message> message;
    TaskState state;
    std::optional<std::string> timestamp;
};

/**
 * @brief A2A task object with status, history, and artifacts.
 */
struct Task {
    std::optional<std::vector<Artifact>> artifacts;
    std::string contextId;
    std::optional<std::vector<Message>> history;
    std::string id;
    std::optional<std::string> metadata;
    TaskStatus status;
    std::optional<std::string> creatAt; // not implemented
    std::optional<std::string> lastModified;  // not implemented
};

/**
 * @brief Authentication info for push-notification webhooks.
 */
struct PushNotificationAuthenticationInfo {
    std::optional<std::string> credentials;
    std::vector<std::string> schemes;
};

/**
 * @brief Push-notification webhook configuration.
 */
struct PushNotificationConfig {
    std::optional<PushNotificationAuthenticationInfo> authentication;
    std::optional<std::string> configId;
    std::optional<std::string> id;
    std::optional<std::string> token;
    std::string url;
    std::optional<std::string> createdAt; // not implemented
};

/**
 * @brief Client configuration for message/send requests.
 */
struct MessageSendConfiguration {
    std::optional<std::vector<std::string>> acceptedOutputModes;
    std::optional<int> historyLength;
    std::optional<PushNotificationConfig> pushNotificationConfig;
    std::optional<bool> returnImmediately;
};

/**
 * @brief Parameters for message/send and message/stream RPC methods.
 */
struct MessageSendParams {
    std::optional<MessageSendConfiguration> configuration;
    Message message;
    std::optional<std::string> metadata;
};

/**
 * @brief Streaming event: artifact chunk update for a task.
 */
struct TaskArtifactUpdateEvent {
    std::optional<bool> append;
    Artifact artifact;
    std::string contextId;
    std::optional<bool> lastChunk;
    std::optional<std::string> metadata;
    std::string taskId;
    std::optional<int> index; // not implemented
};

/**
 * @brief Streaming event: task status change.
 */
struct TaskStatusUpdateEvent {
    std::string contextId;
    std::optional<std::string> metadata;
    TaskStatus status;
    std::string taskId;
};

/**
 * @brief Parameter object identifying a task by ID.
 */
struct TaskIdParams {
    std::string id;
    std::optional<std::string> metadata;
};

/**
 * @brief Parameter object for tasks/get.
 */
struct TaskQueryParams {
    std::optional<int> historyLength;
    std::string id;
    std::optional<std::string> metadata;
};

/**
 * @brief Push-notification config bound to a specific task.
 */
struct TaskPushNotificationConfig {
    PushNotificationConfig pushNotificationConfig;
    std::string taskId;
};

/**
 * @brief Parameters for GetTaskPushNotificationConfig.
 */
struct GetTaskPushNotificationConfigParams {
    std::string id;
    std::optional<std::string> metadata;
    std::optional<std::string> pushNotificationConfigId;
};

/**
 * @brief Parameters for ListTaskPushNotificationConfigs.
 */
struct ListTaskPushNotificationConfigParams {
    std::string id;
    std::optional<std::string> metadata;
};

/**
 * @brief Parameters for DeleteTaskPushNotificationConfig.
 */
struct DeleteTaskPushNotificationConfigParams {
    std::string id;
    std::optional<std::string> metadata;
    std::string pushNotificationConfigId;
};

/**
 * @brief Wire-format error object (JSON-RPC error or A2A extension).
 */
struct A2AError {
    int code = -1;
    std::optional<std::string> data;
    std::optional<std::string> message;
};

/** @brief JSON-RPC -32600 Invalid Request. */
struct InvalidRequestError : public A2AError {
    InvalidRequestError()
    {
        code = static_cast<int>(A2AErrorCode::JSONRPC_INVALID_REQUEST);
        message = std::string("Request payload validation error");
    }
};

/** @brief JSON-RPC -32603 Internal error (generic server error). */
struct ServerError : public A2AError {
    ServerError()
    {
        code = static_cast<int>(A2AErrorCode::JSONRPC_INTERNAL_ERROR);
        message = std::string("Server error");
    }
};

/** @brief JSON-RPC -32602 Invalid params. */
struct InvalidParamsError : public A2AError {
    InvalidParamsError()
    {
        code = static_cast<int>(A2AErrorCode::JSONRPC_INVALID_PARAMS);
        message = std::string("Invalid parameters");
    }
};

/** @brief JSON-RPC -32700 Parse error. */
struct JSONParseError : public A2AError {
    JSONParseError()
    {
        code = static_cast<int>(A2AErrorCode::JSONRPC_PARSE_ERROR);
        message = std::string("Invalid JSON payload");
    }
};

/** @brief JSON-RPC -32603 Internal error. */
struct InternalError : public A2AError {
    InternalError()
    {
        code = static_cast<int>(A2AErrorCode::JSONRPC_INTERNAL_ERROR);
        message = std::string("Internal error");
    }
};

/** @brief JSON-RPC -32601 Method not found. */
struct MethodNotFoundError : public A2AError {
    MethodNotFoundError()
    {
        code = static_cast<int>(A2AErrorCode::JSONRPC_METHOD_NOT_FOUND);
        message = std::string("Method not found");
    }
};

/** @brief A2A -32001 Task not found. */
struct TaskNotFoundError : public A2AError {
    TaskNotFoundError()
    {
        code = static_cast<int>(A2AErrorCode::TASK_NOT_FOUND);
        message = std::string("Task not found");
    }
};

/** @brief A2A -32002 Task not cancelable. */
struct TaskNotCancelableError : public A2AError {
    TaskNotCancelableError()
    {
        code = static_cast<int>(A2AErrorCode::TASK_NOT_CANCELABLE);
        message = std::string("Task cannot be canceled");
    }
};

/** @brief A2A -32003 Push notification not supported. */
struct PushNotificationNotSupportedError : public A2AError {
    PushNotificationNotSupportedError()
    {
        code = static_cast<int>(A2AErrorCode::PUSH_NOTIFICATION_NOT_SUPPORTED);
        message = std::string("Push Notification is not supported");
    }
};

/** @brief A2A -32004 Unsupported operation. */
struct UnsupportedOperationError : public A2AError {
    UnsupportedOperationError()
    {
        code = static_cast<int>(A2AErrorCode::UNSUPPORTED_OPERATION);
        message = std::string("This operation is not supported");
    }
};

/** @brief A2A -32005 Content type not supported. */
struct ContentTypeNotSupportedError : public A2AError {
    ContentTypeNotSupportedError()
    {
        code = static_cast<int>(A2AErrorCode::CONTENT_TYPE_NOT_SUPPORTED);
        message = std::string("Incompatible content types");
    }
};

/** @brief A2A -32006 Invalid agent response. */
struct InvalidAgentResponseError : public A2AError {
    InvalidAgentResponseError()
    {
        code = static_cast<int>(A2AErrorCode::INVALID_AGENT_RESPONSE);
        message = std::string("Invalid agent response");
    }
};

/** @brief A2A -32007 Authenticated extended card not configured. */
struct AuthenticatedExtendedCardNotConfiguredError : public A2AError {
    AuthenticatedExtendedCardNotConfiguredError()
    {
        code = static_cast<int>(A2AErrorCode::AUTHENTICATED_EXTENDED_CARD_NOT_CONFIGURED);
        message = std::string("Authenticated Extended Card is not configured");
    }
};

/** @brief A2A -32008 Extension support required. */
struct ExtensionSupportRequiredError : public A2AError {
    ExtensionSupportRequiredError()
    {
        code = static_cast<int>(A2AErrorCode::EXTENSION_SUPPORT_REQUIRED_ERROR);
        message = std::string("Extension Support Required");
    }
};

/** @brief A2A -32009 Version not supported. */
struct VersionNotSupportedError : public A2AError {
    VersionNotSupportedError()
    {
        code = static_cast<int>(A2AErrorCode::VERSION_NOT_SUPPORTED_ERROR);
        message = std::string("Version Not Supported");
    }
};

/** @brief OpenAPI API-key security scheme. */
struct APIKeySecurityScheme {
    std::optional<std::string> description;
    std::string in_; // cookie|header|query
    std::string name;
    std::string type = "apiKey";
};

/** @brief OpenAPI HTTP authentication security scheme. */
struct HTTPAuthSecurityScheme {
    std::optional<std::string> description;
    std::string scheme;
    std::string type = "http";
};

/** @brief OAuth2 implicit flow definition. */
struct ImplicitOAuthFlow {
    std::string authorizationUrl;
    std::optional<std::string> refreshUrl;
    std::map<std::string, std::string> scopes;
};

/** @brief OAuth2 authorization-code flow definition. */
struct AuthorizationCodeOAuthFlow {
    std::string authorizationUrl;
    std::optional<std::string> refreshUrl;
    std::map<std::string, std::string> scopes;
    std::string tokenUrl;
};

/** @brief OAuth2 password flow definition. */
struct PasswordOAuthFlow {
    std::optional<std::string> refreshUrl;
    std::map<std::string, std::string> scopes;
    std::string tokenUrl;
};

/** @brief OAuth2 client-credentials flow definition. */
struct ClientCredentialsOAuthFlow {
    std::optional<std::string> refreshUrl;
    std::map<std::string, std::string> scopes;
    std::string tokenUrl;
};

/** @brief Collection of supported OAuth2 flows. */
struct OAuthFlows {
    std::optional<AuthorizationCodeOAuthFlow> authorizationCode;
    std::optional<ClientCredentialsOAuthFlow> clientCredentials;
    std::optional<ImplicitOAuthFlow> implicit;
    std::optional<PasswordOAuthFlow> password;
};

/** @brief OpenAPI OAuth2 security scheme. */
struct OAuth2SecurityScheme {
    std::optional<std::string> description;
    OAuthFlows flows;
    std::optional<std::string> oauth2MetadataUrl;
    std::string type = "oauth2";
};

/** @brief OpenAPI OpenID Connect security scheme. */
struct OpenIdConnectSecurityScheme {
    std::optional<std::string> description;
    std::string openIdConnectUrl;
    std::string type = "openIdConnect";
};

/** @brief Mutual TLS security scheme. */
struct MutualTLSSecurityScheme {
    std::optional<std::string> description;
    std::string type = "mutualTLS";
};

/**
 * @brief Discriminated union of supported security schemes.
 */
struct SecurityScheme {
    std::variant<APIKeySecurityScheme, HTTPAuthSecurityScheme, OAuth2SecurityScheme,
        OpenIdConnectSecurityScheme, MutualTLSSecurityScheme> v;
};

/**
 * @brief A capability advertised by an agent (skill).
 */
struct AgentSkill {
    std::string id;
    std::string name;
    std::string description;
    std::vector<std::string> tags;
    std::optional<std::vector<std::string>> examples;
    std::optional<std::vector<std::string>> inputModes;
    std::optional<std::vector<std::string>> outputModes;
    std::optional<std::string> extension;
};

/**
 * @brief Protocol extension entry in an agent card.
 */
struct AgentExtension {
    std::string uri;
    std::optional<bool> required;
    std::optional<std::string> description;
    std::optional<std::string> params;
};

/**
 * @brief Feature flags advertised in an agent card.
 */
struct AgentCapabilities {
    std::optional<bool> streaming;
    std::optional<bool> pushNotifications;
    std::optional<bool> extendedAgentCard;
    std::optional<std::string> extension;
    std::optional<std::vector<AgentExtension>> extensions;
};

/**
 * @brief Agent provider metadata.
 */
struct AgentProvider {
    std::string organization;
    std::string url;
};

/**
 * @brief A transport endpoint exposed by an agent.
 */
struct AgentInterface {
    std::string url;
    std::string protocolBinding;
    std::string protocolVersion;
    std::optional<std::string> tenant;
};

/**
 * @brief Security requirement referencing named schemes.
 */
struct SecurityRequirement {
    std::unordered_map<std::string, std::vector<std::string>> schemes;
};

/**
 * @brief JWS signature over an agent card.
 */
struct AgentCardSignature {
    /** @brief Base64url-encoded protected JWS header (JSON). */
    std::string protected_;

    /** @brief Base64url-encoded signature bytes. */
    std::string signature;

    /** @brief Optional unprotected JWS header (JSON). */
    std::optional<std::string> header;
};

/**
 * @brief Agent discovery card (/.well-known/agent-card.json).
 * @note 代理发现卡片，描述能力、接口与安全配置。
 */
struct AgentCard {
    std::string name;
    std::string description;
    std::optional<AgentProvider> provider;
    std::optional<std::string> iconUrl;
    std::string version;
    std::optional<std::string> documentationUrl;
    AgentCapabilities capabilities;
    std::optional<std::map<std::string, SecurityScheme>> securitySchemes;
    std::optional<std::vector<std::string>> security;
    std::vector<std::string> defaultInputModes;
    std::vector<std::string> defaultOutputModes;
    std::vector<AgentSkill> skills;
    std::vector<AgentInterface> supportedInterfaces;
    std::optional<std::vector<SecurityRequirement>> securityRequirements;
    std::optional<std::vector<AgentCardSignature>> signatures;

    std::optional<std::string> category;
    std::optional<std::string> extension;
};

} // namespace A2A

#endif
