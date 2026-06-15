/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_TYPES
#define A2A_TYPES

#include <nlohmann/json.hpp>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace A2A {
constexpr const char* JSONRPC_TRANSPORT = "JSONRPC";

constexpr const char* JSONRPC_VERSION = "2.0";

enum class Role { AGENT, USER };

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
    A2A_ENCRYPT_ERROR           = -32104,
    A2A_DECRYPT_ERROR           = -32105,
    A2A_INVALID_FORMAT          = -32106,
    A2A_UNAUTHORIZED            = -32107,
    A2A_STATUS_ERROR            = -32108,
    A2A_INVALID_INPUT           = -32109,
    A2A_BAD_ALLOC               = -32110,
    A2A_CONCURRENT_LIMIT        = -32111,

    // Business errors for server
    INVALID_INPUT               = 1022400010,
    BAD_ALLOC                   = 1022420001
};

struct A2AError {
    int code = -1;
    std::optional<std::string> data;
    std::optional<std::string> message;
};

struct ClientCallContext {
    // Arbitrary per-request state
    nlohmann::json state;
    nlohmann::json headers;
};

struct Part {
    // oneof
    std::optional<std::string> text;
    std::optional<std::string> raw;
    std::optional<std::string> url;
    std::optional<std::string> data; // map-like JSON

    std::optional<std::string> metadata;
    std::optional<std::string> filename;
    std::optional<std::string> mediaType;
};

struct Artifact {
    std::string artifactId;
    std::optional<std::string> description;
    std::optional<std::vector<std::string>> extensions;
    std::optional<nlohmann::json> metadata;
    std::optional<std::string> name;
    std::vector<Part> parts;
};

struct Message {
    std::optional<std::string> contextId;
    std::optional<std::vector<std::string>> extensions;
    std::string kind = "message";
    std::string messageId;
    std::optional<std::string> metadata;
    std::vector<Part> parts;
    std::optional<std::vector<std::string>> referenceTaskIds;
    Role role;
    std::optional<std::string> taskId;
};

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
    UNKNOWN,
    UNSPECIFIED = UNKNOWN
};

struct TaskStatus {
    std::optional<Message> message;
    TaskState state;
    std::optional<std::string> timestamp;
};

struct Task {
    std::optional<std::vector<Artifact>> artifacts;
    std::string contextId;
    std::optional<std::vector<Message>> history;
    std::string id;
    std::string kind = "task";
    std::optional<nlohmann::json> metadata;
    TaskStatus status;
};

// Push Notifications and Task params
struct PushNotificationAuthenticationInfo {
    std::optional<std::string> credentials;
    std::vector<std::string> schemes;
};

struct PushNotificationConfig {
    std::optional<PushNotificationAuthenticationInfo> authentication;
    std::optional<std::string> id;
    std::optional<std::string> token;
    std::string url;
};

// Minimal parameter and event types used by helpers
struct MessageSendConfiguration {
    std::optional<std::vector<std::string>> acceptedOutputModes;
    std::optional<int> historyLength;
    std::optional<PushNotificationConfig> pushNotificationConfig;
    std::optional<bool> blocking;
    std::optional<bool> returnImmediately;
};

struct MessageSendParams {
    std::optional<MessageSendConfiguration> configuration;
    Message message;
    std::optional<nlohmann::json> metadata;
};

struct TaskArtifactUpdateEvent {
    std::optional<bool> append;
    Artifact artifact;
    std::string contextId;
    std::string kind = "artifact-update";
    std::optional<bool> lastChunk;
    std::optional<nlohmann::json> metadata;
    std::string taskId;
};

struct TaskStatusUpdateEvent {
    std::string contextId;
    std::optional<bool> final = false;
    std::string kind = "status-update";
    std::optional<nlohmann::json> metadata;
    TaskStatus status;
    std::string taskId;
};

struct TaskIdParams {
    std::string id;
    std::optional<nlohmann::json> metadata;
};

struct TaskQueryParams {
    std::optional<int> historyLength;
    std::string id;
    std::optional<nlohmann::json> metadata;
};

struct TaskPushNotificationConfig {
    PushNotificationConfig pushNotificationConfig;
    std::string taskId;
};

// GetTaskPushNotificationConfig parameters
struct GetTaskPushNotificationConfigParams {
    std::string id;
    std::optional<nlohmann::json> metadata;
    std::optional<std::string> pushNotificationConfigId;
};

// List&Delete push notification configs
struct ListTaskPushNotificationConfigParams {
    std::string id;
    std::optional<nlohmann::json> metadata;
};

struct DeleteTaskPushNotificationConfigParams {
    std::string id;
    std::optional<nlohmann::json> metadata;
    std::string pushNotificationConfigId;
};

// Error models (A2A + JSON-RPC)
struct InvalidRequestError {
    int code = -32600;
    std::optional<nlohmann::json> data;
    std::optional<std::string> message = std::string("Invalid request");
};

struct ServerError {
    int code = -32000;
    std::optional<nlohmann::json> data;
    std::optional<std::string> message = std::string("Server error");
};

struct InvalidParamsError {
    int code = -32602;
    std::optional<nlohmann::json> data;
    std::optional<std::string> message = std::string("Invalid parameters");
};

struct InvalidAgentIdError {
    int code = -32300;
    std::optional<nlohmann::json> data;
    std::optional<std::string> message = std::string("Invalid Agent ID");
};

struct AddExternalMemoryError {
    int code = -32301;
    std::optional<nlohmann::json> data;
    std::optional<std::string> message = std::string("Add external memory failure");
};

struct JSONParseError {
    int code = -32700;
    std::optional<nlohmann::json> data;
    std::optional<std::string> message = std::string("Invalid JSON payload");
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

// Security Schemes
struct APIKeySecurityScheme {
    std::optional<std::string> description;
    std::string in_; // cookie|header|query
    std::string name;
    std::string type = "apiKey";
};

struct HTTPAuthSecurityScheme {
    std::optional<std::string> description;
    std::string scheme;
    std::string type = "http";
};

struct ImplicitOAuthFlow {
    std::string authorizationUrl;
    std::optional<std::string> refreshUrl;
    std::map<std::string, std::string> scopes;
};

struct AuthorizationCodeOAuthFlow {
    std::string authorizationUrl;
    std::optional<std::string> refreshUrl;
    std::map<std::string, std::string> scopes;
    std::string tokenUrl;
};

struct PasswordOAuthFlow {
    std::optional<std::string> refreshUrl;
    std::map<std::string, std::string> scopes;
    std::string tokenUrl;
};

struct ClientCredentialsOAuthFlow {
    std::optional<std::string> refreshUrl;
    std::map<std::string, std::string> scopes;
    std::string tokenUrl;
};

struct OAuthFlows {
    std::optional<AuthorizationCodeOAuthFlow> authorizationCode;
    std::optional<ClientCredentialsOAuthFlow> clientCredentials;
    std::optional<ImplicitOAuthFlow> implicit;
    std::optional<PasswordOAuthFlow> password;
};

struct OAuth2SecurityScheme {
    std::optional<std::string> description;
    OAuthFlows flows;
    std::optional<std::string> oauth2MetadataUrl;
    std::string type = "oauth2";
};

struct OpenIdConnectSecurityScheme {
    std::optional<std::string> description;
    std::string openIdConnectUrl;
    std::string type = "openIdConnect";
};

struct MutualTLSSecurityScheme {
    std::optional<std::string> description;
    std::string type = "mutualTLS";
};

struct SecurityScheme {
    std::variant<APIKeySecurityScheme, HTTPAuthSecurityScheme, OAuth2SecurityScheme,
        OpenIdConnectSecurityScheme, MutualTLSSecurityScheme> v;
};

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

// AgentExtension mirrors the Python model used in extensions/common.py utilities
struct AgentExtension {
    std::string uri;
    std::optional<bool> required;
    std::optional<std::string> description;
    std::optional<std::string> params;
};

struct AgentCapabilities {
    std::optional<bool> streaming;
    std::optional<bool> pushNotifications;
    std::optional<bool> extendedAgentCard;
    std::optional<std::string> extension;
    std::optional<std::vector<AgentExtension>> extensions;
};

struct AgentProvider {
    std::string organization;
    std::string url;
};

struct AgentInterface {
    std::string url;
    std::string protocolBinding;
    std::string protocolVersion;
    std::optional<std::string> tenant;
};

struct SecurityRequirement {
    std::unordered_map<std::string, std::vector<std::string>> schemes;
};

struct AgentCardSignature {
    // Required. The protected JWS header. This is always a base64url-encoded JSON object.
    // Note: Trailing underscore because 'protected' is a C++ keyword.
    std::string protected_;

    // Required. The computed signature, base64url-encoded.
    std::string signature;

    // The unprotected JWS header values
    std::optional<std::string> header;  // json like
};

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
