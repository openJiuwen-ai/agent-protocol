/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef MCP_SERVER_INCLUDE_H_
#define MCP_SERVER_INCLUDE_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "mcp_type.h"

namespace Mcp {

// Use plain string for structured tool output (JSON text)
using ToolReturn = std::variant<CallToolResult, std::string>;
using ToolFunc = std::function<ToolReturn(const std::string& name, const JsonValue& arguments,
                                          const std::optional<JsonValue>& ctx)>;

/**
 * Optional parameters for AddTool method.
 */
struct AddToolOptionalParams {
    std::optional<std::reference_wrapper<const std::string>> title = std::nullopt;
    std::optional<std::reference_wrapper<const std::string>> description = std::nullopt;
    std::optional<std::reference_wrapper<const std::string>> inputSchema = std::nullopt;
    std::optional<std::reference_wrapper<const std::string>> outputSchema = std::nullopt;
    bool structuredOutput = false;
    std::optional<std::reference_wrapper<const ToolAnnotations>> annotations = std::nullopt;
    std::optional<std::reference_wrapper<const std::vector<Icon>>> icons = std::nullopt;
};

/**
 * Optional parameters for AddPrompt method.
 */
struct AddPromptOptionalParams {
    std::optional<std::reference_wrapper<const std::string>> description = std::nullopt;
    std::optional<std::reference_wrapper<const std::string>> title = std::nullopt;
    std::optional<std::reference_wrapper<const std::vector<Icon>>> icons = std::nullopt;
    std::optional<std::reference_wrapper<const std::vector<PromptArgument>>> arguments = std::nullopt;
};

/**
 * Optional parameters for AddResource method.
 */
struct AddResourceOptionalParams {
    std::optional<std::reference_wrapper<const std::string>> title = std::nullopt;
    std::optional<std::reference_wrapper<const std::string>> description = std::nullopt;
    std::optional<std::reference_wrapper<const std::string>> mimeType = std::nullopt;
    std::optional<std::int64_t> size = std::nullopt;
    std::optional<std::reference_wrapper<const std::vector<Icon>>> icons = std::nullopt;
    std::optional<std::reference_wrapper<const Annotations>> annotations = std::nullopt;
};

/**
 * Optional parameters for AddResourceTemplate method.
 */
struct AddResourceTemplateOptionalParams {
    std::optional<std::reference_wrapper<const std::string>> title = std::nullopt;
    std::optional<std::reference_wrapper<const std::string>> description = std::nullopt;
    std::optional<std::reference_wrapper<const std::string>> mimeType = std::nullopt;
    std::optional<std::reference_wrapper<const std::vector<Icon>>> icons = std::nullopt;
    std::optional<std::reference_wrapper<const Annotations>> annotations = std::nullopt;
};

/**
 * Abstract base class for MCP server implementations.
 *
 * This interface defines the core functionality that all MCP servers must provide.
 * Implementations should handle the complete server lifecycle including
 * initialization, request processing, and graceful shutdown.
 */
class McpServer {
public:
    /**
     * Virtual destructor to ensure proper cleanup of derived classes.
     */
    virtual ~McpServer() = default;

    /**
     * Start the MCP server and begin accepting connections.
     *
     * This method should initialize all server components and begin
     * listening for incoming connections. It should return true only
     * if the server started successfully and is ready to handle requests.
     *
     * @return true if the server started successfully, false otherwise
     */
    virtual bool Run() = 0;

    /**
     * Stop the MCP server gracefully.
     *
     * This method should stop accepting new connections, finish
     * processing any in-flight requests, and clean up all resources.
     */
    virtual void Stop() = 0;

    /**
     * Check if the server is currently running.
     *
     * @return true if the server is running and accepting connections, false otherwise
     */
    virtual bool IsRunning() const = 0;

    /**
     * Add a tool to the server.
     *
     * @param name Tool name, must be unique (required).
     * @param fn Tool function object (required).
     * @param params Optional parameters for the tool.
     */
    virtual void AddTool(const std::string& name, ToolFunc fn,
        AddToolOptionalParams params = {}) = 0;

    /**
     * Remove a tool from the server.
     *
     * @param name Name of the tool to remove
     */
    virtual void RemoveTool(const std::string& name) = 0;

    /**
     * Add a prompt template/handler to the server.
     *
     * The server will expose it via MCP methods: `prompts/list` and `prompts/get`.
     */
    virtual void AddPrompt(const std::string& name, RenderPromptFunc handler,
        AddPromptOptionalParams params = {}) = 0;

    /**
     * Remove a prompt by name.
     */
    virtual void RemovePrompt(const std::string& name) = 0;

    /**
     * Add a resource to the server.
     * 
     * @param resource Resource infomation including URI, name, and read function
     * @param readFunc Function to read the resource content
     */
    virtual void AddResource(const std::string& uri, const std::string& name, ReadResourceFunc readFunc,
        AddResourceOptionalParams params = {}) = 0;

    /**
     * Remove a resource by name.
     * 
     * @param uri URI of the resource to remove
     */
    virtual void RemoveResource(const std::string& uri) = 0;

    /**
     * Add a resource template to the server.
     * 
     * @param resourceTemplate Resource template information
     */
    virtual void AddResourceTemplate(const std::string& uriTemplate, const std::string& name,
        AddResourceTemplateOptionalParams params = {}) = 0;

    /**
     * Remove a resource template by URI template.
     * 
     * @param uriTemplate URI template of the resource template to remove
     */
    virtual void RemoveResourceTemplate(const std::string& uriTemplate) = 0;
};

/**
 * Factory class for creating MCP server instances.
 *
 * This factory provides a clean interface for creating server instances
 * without exposing the implementation details. It abstracts the
 * concrete server implementation from the client code.
 */
class McpServerFactory {
public:
    /**
     * @brief Create a new MCP server instance using the streamable HTTP transport.
     *
     * @param config The main server configuration containing all required settings.
     * @param transportConfig The configuration for the streamable HTTP transport layer.
     * @return A unique pointer to the created MCP server instance.
     *
     * @throw std::invalid_argument If either @p config or @p transportConfig is invalid.
     * @throw std::runtime_error If the server instance cannot be created or initialized.
     */
    static std::unique_ptr<McpServer> CreateStreamableHttpServer(const ServerConfig& config,
        const StreamableHttpServerConfig& transportConfig);

    /**
     * @brief Create a new MCP server instance using standard input/output (stdio) for communication.
     *
     * This function creates and initializes an MCP server that communicates via stdio.
     * The server will be configured according to the provided server settings.
     *
     * @param config The main server configuration containing all required settings.
     * @return A unique pointer to the created MCP server instance.
     *
     * @throw std::invalid_argument If @p config is invalid.
     * @throw std::runtime_error If the server instance cannot be created or initialized.
     */
    static std::unique_ptr<McpServer> CreateStdioServer(const ServerConfig& config);
};

} // namespace Mcp

#endif // MCP_SERVER_INCLUDE_H_
