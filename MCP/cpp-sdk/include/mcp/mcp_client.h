/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef MCP_CLIENT_INCLUDE_H_
#define MCP_CLIENT_INCLUDE_H_

#include <future>
#include <optional>
#include <string>

#include "mcp_auth.h"
#include "mcp_type.h"

namespace Mcp {

class McpClient {
public:
    virtual ~McpClient() = default;

    /**
     * @brief Initialize the client and perform the MCP initialization handshake.
     *
     * Establishes a connection and performs the initial handshake with the MCP server.
     *
     * @throw std::runtime_error If the client is already initialized.
     * @throw std::runtime_error If the underlying transport or session cannot be created.
     */
    virtual std::future<std::shared_ptr<InitializeResult>> Initialize() = 0;

    /**
     * @brief List all available tools on the server.
     *
     * Retrieves a list of tools that are registered and available for use on the server.
     *
     * @throw std::runtime_error If the client is not initialized.
     */
    virtual std::future<std::shared_ptr<ListToolsResult>> ListTools() = 0;
    
    /** Progress callback for long-running operations: (progress, total?, message?). */
    using ProgressCallback =
        std::function<void(double progress, std::optional<double> total, const std::optional<std::string>& message)>;

    /**
     * @brief Call a tool by name with optional arguments, timeout and progress callback.
     *
     * This method invokes a tool registered on the server, passing the specified arguments.
     * The call is asynchronous and returns a future to the result.
     * When progressCallback is provided, the request includes a progressToken in params._meta
     * (MCP progress tracking); the server may send notifications/progress and the callback is invoked.
     *
     * @param name The name of the tool to call.
     * @param arguments Optional arguments to pass to the tool (as a JSON value). Defaults to nullopt.
     * @param timeout Timeout in milliseconds for the tool call. If 0, uses the default timeout.
     * @param progressCallback Optional callback for progress notifications (notifications/progress).
     *        Defaults to nullopt.
     * @return A future to a shared pointer of CallToolResult containing the tool's response.
     *
     * @throw std::runtime_error If the client is not initialized or the call fails to start.
     */
    virtual std::future<std::shared_ptr<CallToolResult>> CallTool(
        const std::string& name, const std::optional<JsonValue>& arguments = std::nullopt, int timeout = 0,
        std::optional<ProgressCallback> progressCallback = std::nullopt) = 0;
    
    /**
     * @brief List all available resources on the server.
     *
     * Retrieves a list of resources that are available for access on the server.
     *
     * @throw std::runtime_error If the client is not initialized.
     */
    virtual std::future<std::shared_ptr<ListResourcesResult>> ListResources() = 0;

    /**
     * @brief Read the content of a resource by URI.
     *
     * Fetches the content of the specified resource from the server.
     *
     * @param uri The URI of the resource to read.
     * @throw std::runtime_error If the client is not initialized.
     */
    virtual std::future<std::shared_ptr<ReadResourceResult>> ReadResource(const std::string& uri) = 0;
    
    /**
     * @brief Subscribe to updates for a resource by URI.
     *
     * Registers for notifications when the specified resource changes on the server.
     *
     * @param uri The URI of the resource to subscribe to.
     * @throw std::runtime_error If the client is not initialized.
     */
    virtual std::future<std::shared_ptr<EmptyResult>> SubscribeResource(const std::string& uri) = 0;
    
    /**
     * @brief Unsubscribe from updates for a resource by URI.
     *
     * Cancels notifications for changes to the specified resource on the server.
     *
     * @param uri The URI of the resource to unsubscribe from.
     * @throw std::runtime_error If the client is not initialized.
     */
    virtual std::future<std::shared_ptr<EmptyResult>> UnsubscribeResource(const std::string& uri) = 0;
    
    /**
     * @brief List all available resource templates on the server.
     *
     * Retrieves a list of resource templates that can be used to create or access resources.
     *
     * @throw std::runtime_error If the client is not initialized.
     */
    virtual std::future<std::shared_ptr<ListResourceTemplatesResult>> ListResourcesTemplates() = 0;

    /**
     * @brief List all available prompts on the server.
     *
     * Retrieves a list of prompt templates that are available for use on the server.
     *
     * @throw std::runtime_error If the client is not initialized.
     */
    virtual std::future<std::shared_ptr<ListPromptsResult>> ListPrompts() = 0;

    /**
     * @brief Get a prompt by name with optional arguments.
     *
     * Retrieves the specified prompt template from the server, optionally providing arguments for rendering.
     *
     * @param name The name of the prompt to retrieve.
     * @param arguments Optional arguments for rendering the prompt.
     * @throw std::runtime_error If the client is not initialized.
     */
    virtual std::future<std::shared_ptr<GetPromptResult>> GetPrompt(
        const std::string& name, const std::optional<JsonValue>& arguments = std::nullopt) = 0;

    /**
     * @brief Notify the server that the list of root resources has changed.
     *
     * Sends a notification to the server indicating that the set of root resources has been updated.
     *
     * @throw std::runtime_error If the client is not initialized.
     */
    virtual void SendRootsListChanged() = 0;

    /**
     * @brief Send a ping to the server to check connectivity.
     *
     * Sends a ping message to the server and waits for a response to verify connectivity.
     *
     * @return A future to an EmptyResult indicating the outcome of the ping.
     * @throw std::runtime_error If the client is not initialized.
     */
    virtual std::future<EmptyResult> SendPing() = 0;

    virtual std::future<std::shared_ptr<EmptyResult>> SetLoggingLevel(const LoggingLevel level) = 0;

    /**
     * @brief Get the capabilities supported by the server.
     *
     * Retrieves the set of features and capabilities supported by the connected server.
     *
     * @return The server's capabilities.
     * @throw std::runtime_error If the client is not initialized.
     */
    virtual ServerCapabilities GetServerCapabilities() = 0;

    // Not yet developed, temporarily commented out
    //virtual ExperimentalClientFeatures Experimental();

    /**
     * @brief Send a progress notification to the server (notifications/progress).
     *
     * Notifies the server of the current progress of a long-running operation.
     *
     * @param progressToken Token identifying the progress operation (string or int64_t per MCP spec).
     * @param progress The current progress value (MUST increase with each notification).
     * @param total Optional total value for completion; omit if unknown.
     * @param message Optional human-readable progress message.
     * @return A future that completes when the notification has been sent.
     * @throw std::runtime_error If the client is not initialized.
     */
    virtual std::future<void> SendProgressNotification(ProgressToken progressToken, double progress,
                                                       std::optional<double> total = std::nullopt,
                                                       std::optional<std::string> message = std::nullopt) = 0;

    /**
     * @brief Request completion options for a prompt or resource template.
     *
     * Sends a completion request to the server for the specified resource or prompt.
     *
     * @param ref Reference to the resource template or prompt.
     * @param argument The completion argument containing name and value.
     * @param context Optional completion context with additional arguments.
     * @return A future to a shared pointer of CompleteResult containing completion options.
     * @throw std::runtime_error If the client is not initialized.
     */
    virtual std::future<std::shared_ptr<CompleteResult>> Complete(
        const CompleteReference& ref, const CompletionArgument& argument,
        const std::optional<CompletionContext>& context = std::nullopt) = 0;

    // Register a callback that will be invoked when the server sends `roots/list`.
    // This must be called before Initialize() to ensure capabilities are advertised correctly.
    virtual void SetListRootsCallback(ListRootsCallback cb) = 0;

    // Register a callback that will be invoked when the server sends `notifications/message`.
    // The default callback is print in MCP_LOG.
    virtual void SetLoggingCallback(LoggingCallback cb) = 0;

    // Register a callback that will be invoked when the server sends `elicitation/create` in form mode.
    virtual void SetElicitCallback(ElicitCallback cb) = 0;
    
    // Register a callback that will be invoked when the server sends `elicitation/create` in url mode.
    virtual void SetElicitUrlCallback(ElicitUrlCallback cb) = 0;

    // Callback for handling `sampling/createMessage` requests sent from a server to the client.
    // - Return a value to accept the sampling request and respond with a CreateMessageResult.
    // - Return std::nullopt to indicate the user rejected the sampling request; the SDK will
    //   reply with a JSON-RPC error code -1 as recommended by the spec.
    using SamplingCreateMessageCallback = std::function<std::optional<CreateMessageResult>(const CreateMessageParams&)>;

    /**
     * @brief Register a callback that will be invoked when the server sends `sampling/createMessage`.
     *
     * This must be called before Initialize() to ensure sampling capabilities are advertised correctly.
     *
     * @param cb The callback to handle incoming sampling requests.
     * @param capability Controls the advertised sampling sub-capabilities:
     *  - capability.tools=true enables tool-enabled sampling requests (sampling.tools)
     *  - capability.context=true enables includeContext values beyond "none" (sampling.context)
     *
     * @throw std::runtime_error If the client is already initialized.
     */
    virtual void SetSamplingCreateMessageCallback(SamplingCreateMessageCallback cb,
        SamplingCapability capability = SamplingCapability{}) = 0;
};

class McpClientFactory {
public:
    /**
     * @brief Create a client using the Streamable HTTP transport.
     *
     * Instantiates an MCP client that communicates with the server using the streamable HTTP transport.
     *
     * @param config The main client configuration.
     * @param transportConfig The configuration for the streamable HTTP transport.
     * @param authProvider Optional authentication provider for securing requests.
     * @return A shared pointer to the created client instance.
     * @throw std::runtime_error If the underlying transport cannot be created or started.
     */
    static std::shared_ptr<McpClient> CreateStreamableHttpClient(const ClientConfig& config,
        const StreamableHttpClientConfig& transportConfig, std::shared_ptr<AuthProvider> authProvider = nullptr);

    /**
     * @brief Create a client using the stdio transport.
     *
     * Instantiates an MCP client that communicates with the server using standard input/output (stdio).
     *
     * @param config The main client configuration.
     * @param transportConfig The configuration for the stdio transport.
     * @return A shared pointer to the created client instance.
     */
    static std::shared_ptr<McpClient> CreateStdioClient(const ClientConfig& config,
        const StdioClientConfig& transportConfig);
};

} // namespace Mcp

#endif // MCP_CLIENT_INCLUDE_H_
