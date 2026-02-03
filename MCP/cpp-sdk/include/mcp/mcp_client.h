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
     * @throw Mcp::MCPError If the server responds with a JSON-RPC error (surfaced when calling future.get()).
     */
    virtual std::future<std::shared_ptr<InitializeResult>> Initialize() = 0;

    /**
     * @brief List available tools starting from the specified cursor.
     *
     * Retrieves a list (page) of tools. When cursor is std::nullopt, listing
     * starts from the beginning. The returned result may contain nextCursor for
     * subsequent pages.
     *
     * @param cursor Optional cursor indicating the starting position. Defaults to std::nullopt.
     * @throw std::runtime_error If the client is not initialized.
     * @throw Mcp::MCPError If the server responds with a JSON-RPC error (surfaced when calling future.get()).
     */
    virtual std::future<std::shared_ptr<ListToolsResult>> ListTools(
        const std::optional<std::string>& cursor = std::nullopt) = 0;
    
    /**
     * @brief Call a tool by name with optional arguments and timeout.
     *
     * This method invokes a tool registered on the server, passing the specified arguments.
     * The call is asynchronous and returns a future to the result.
     *
     * @param name The name of the tool to call.
     * @param arguments Optional arguments to pass to the tool (as a JSON string). Defaults to nullopt.
     * @param timeout Timeout in milliseconds for the tool call. If 0, uses the default timeout.
     * @return A future to a shared pointer of CallToolResult containing the tool's response.
     *
     * @throw std::runtime_error If the client is not initialized or the call fails to start.
     * @throw Mcp::MCPError If the server responds with a JSON-RPC error (surfaced when calling future.get()).
     */
    virtual std::future<std::shared_ptr<CallToolResult>> CallTool(
        const std::string& name, const std::optional<std::string>& arguments = std::nullopt, int timeout = 0) = 0;
    
    /**
     * @brief List available resources starting from the specified cursor.
     *
     * Retrieves a list (page) of resources. When cursor is std::nullopt,
     * listing starts from the beginning. The returned result may contain
     * nextCursor for subsequent pages.
     *
     * @param cursor Optional cursor indicating the starting position. Defaults to std::nullopt.
     * @throw std::runtime_error If the client is not initialized.
     * @throw Mcp::MCPError If the server responds with a JSON-RPC error (surfaced when calling future.get()).
     */
    virtual std::future<std::shared_ptr<ListResourcesResult>> ListResources(
        const std::optional<std::string>& cursor = std::nullopt) = 0;

    /**
     * @brief Read the content of a resource by URI.
     *
     * Fetches the content of the specified resource from the server.
     *
     * @param uri The URI of the resource to read.
     * @throw std::runtime_error If the client is not initialized.
     * @throw Mcp::MCPError If the server responds with a JSON-RPC error (surfaced when calling future.get()).
     */
    virtual std::future<std::shared_ptr<ReadResourceResult>> ReadResource(const std::string& uri) = 0;
    
    /**
     * @brief Subscribe to updates for a resource by URI.
     *
     * Registers for notifications when the specified resource changes on the server.
     *
     * @param uri The URI of the resource to subscribe to.
     * @throw std::runtime_error If the client is not initialized.
     * @throw Mcp::MCPError If the server responds with a JSON-RPC error (surfaced when calling future.get()).
     */
    virtual std::future<std::shared_ptr<EmptyResult>> SubscribeResource(const std::string& uri) = 0;
    
    /**
     * @brief Unsubscribe from updates for a resource by URI.
     *
     * Cancels notifications for changes to the specified resource on the server.
     *
     * @param uri The URI of the resource to unsubscribe from.
     * @throw std::runtime_error If the client is not initialized.
     * @throw Mcp::MCPError If the server responds with a JSON-RPC error (surfaced when calling future.get()).
     */
    virtual std::future<std::shared_ptr<EmptyResult>> UnsubscribeResource(const std::string& uri) = 0;
    
    /**
     * @brief List all available resource templates on the server.
     *
     * Retrieves a list of resource templates that can be used to create or access resources.
     *
     * @throw std::runtime_error If the client is not initialized.
     * @throw Mcp::MCPError If the server responds with a JSON-RPC error (surfaced when calling future.get()).
     */
    virtual std::future<std::shared_ptr<ListResourceTemplatesResult>> ListResourcesTemplates() = 0;

    /**
     * @brief List all available prompts on the server.
     *
     * Retrieves a list of prompt templates that are available for use on the server.
     *
     * @throw std::runtime_error If the client is not initialized.
     * @throw Mcp::MCPError If the server responds with a JSON-RPC error (surfaced when calling future.get()).
     */
    virtual std::future<std::shared_ptr<ListPromptsResult>> ListPrompts() = 0;

    /**
     * @brief Get a prompt by name with optional arguments.
     *
     * Retrieves the specified prompt template from the server, optionally providing arguments for rendering.
     *
     * @param name The name of the prompt to retrieve.
     * @param arguments Optional arguments for rendering the prompt (as a JSON string).
     * @throw std::runtime_error If the client is not initialized.
     * @throw Mcp::MCPError If the server responds with a JSON-RPC error (surfaced when calling future.get()).
     */
    virtual std::future<std::shared_ptr<GetPromptResult>> GetPrompt(
        const std::string& name, const std::optional<std::string>& arguments = std::nullopt) = 0;

    /**
     * @brief Send a ping to the server to check connectivity.
     *
     * Sends a ping message and waits for the response to verify the client/server link.
     * Returns an empty result object on success.
     *
     * @return A future to a shared pointer of EmptyResult indicating the ping outcome.
     * @throw std::runtime_error If the client is not initialized.
     */
    virtual std::future<std::shared_ptr<EmptyResult>> SendPing() = 0;

    /**
     * @brief Notify the server that the list of root resources has changed.
     *
     * Sends a notification to the server indicating that the set of root resources has been updated.
     *
     * @throw std::runtime_error If the client is not initialized.
     */
    virtual void SendRootsListChanged() = 0;

    /**
     * @brief Set the capabilities supported by the client.
     *
     * Informs the server of the features and capabilities supported by this client instance.
     *
     * @param caps The capabilities supported by the client.
     * @throw std::runtime_error If the client is not initialized.
     */
    virtual void SetClientCapabilities(const McpClientCapabilities& caps) = 0;
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
     * @brief Send a progress notification to the server.
     *
     * Notifies the server of the current progress of a long-running operation.
     *
     * @param progressToken Token identifying the progress operation.
     * @param progress The current progress value.
     * @param total The total value for completion.
     * @param message An optional message describing the progress.
     * @throw std::runtime_error If the client is not initialized.
     */
    virtual std::future<void> SendProgressNotification(std::string progressToken, float progress, float total,
                                                       std::string message) = 0;

    /**
     * @brief Complete an operation of the specified type on a resource.
     *
     * Requests the server to complete a specific operation on a given resource, with optional extra parameters.
     *
     * @param type The type of operation to complete.
     * @param uri The URI of the resource.
     * @param extras Additional parameters for the operation.
     * @throw std::runtime_error If the client is not initialized.
     */
    virtual std::future<Result> Complete(std::string type, std::string uri,
                                         std::unordered_map<std::string, std::string> extras) = 0;
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
