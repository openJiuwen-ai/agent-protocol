/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef MCP_SERVER_INCLUDE_H_
#define MCP_SERVER_INCLUDE_H_

#include <cstdint>
#include <future>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "mcp_type.h"

namespace Mcp {

// Use plain string for structured tool output (JSON text)
using ToolReturn = std::variant<CallToolResult, std::string>;

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

// Function type for completion handler
using CompleteFunc = std::function<CompleteResult(const CompleteReference& ref,
    const CompletionArgument& argument, const std::optional<CompletionContext>& ctx)>;

class McpServerSession {
public:
    virtual ~McpServerSession() = default;

    // Notify the client that the server's tool list/prompt list/resource list has changed.
    virtual void SendToolListChangedNotification() = 0;
    virtual void SendPromptListChangedNotification() = 0;
    virtual void SendResourceListChangedNotification() = 0;

    // Request the client's roots list via `roots/list`.
    virtual std::future<std::shared_ptr<ListRootsResult>> ListRoots() = 0;

    // Capabilities advertised by the connected client during `initialize`.
    virtual ClientCapabilities GetClientCapabilities() const = 0;

    virtual void SendProgressNotification(ProgressToken progressToken, double progress,
                                          std::optional<double> total = std::nullopt,
                                          const std::optional<std::string>& message = std::nullopt) = 0;
    // Server -> Client sampling request.
    // Requires the connected client to advertise support for `sampling/createMessage`.
    virtual std::future<std::shared_ptr<CreateMessageResult>> SamplingCreateMessage(
        const CreateMessageParams& params) = 0;
};

// Unified callback function type for sending responses from user threads
using ResponseCallback = std::function<void(const Result& result)>;

struct ServerContext {
    std::shared_ptr<McpServerSession> session;
    ResponseCallback responseCallback;
    // Meta data from client request (e.g., progressToken for MCP progress)
    std::optional<RequestParamsMeta> meta;
};

// ToolFunc wraps sync and async tool callbacks (both receive ServerContext).
// Sync:  f(ServerContext, name, args_str) -> ToolReturn
// Async: f(ServerContext, name, args_str) -> void  (response via ctx.responseCallback)
class ToolFunc {
public:
    using SyncFn = std::function<ToolReturn(const ServerContext& ctx, const std::string& name,
                                            const std::string& arguments)>;
    using AsyncFn = std::function<void(const ServerContext& ctx, const std::string& name,
                                       const std::string& arguments)>;

    ToolFunc() : isAsync_(false) {}
    ToolFunc(std::nullptr_t) noexcept : isAsync_(false) {}
    ~ToolFunc() = default;

    template <typename F,
              typename = std::enable_if_t<
                  !std::is_same_v<std::decay_t<F>, ToolFunc> &&
                  (std::is_invocable_r_v<ToolReturn, F, const ServerContext&, const std::string&,
                                         const std::string&> ||
                   std::is_invocable_v<F, const ServerContext&, const std::string&, const std::string&>)>>
    ToolFunc(F&& fn)
    {
        if constexpr (std::is_invocable_r_v<ToolReturn, F, const ServerContext&, const std::string&,
                                            const std::string&>) {
            syncFn_ = std::forward<F>(fn);
            isAsync_ = false;
        } else if constexpr (std::is_invocable_v<F, const ServerContext&, const std::string&,
                                                   const std::string&>) {
            asyncFn_ = std::forward<F>(fn);
            isAsync_ = true;
        }
    }

    // Returns nullopt for async functions (caller should not send response).
    std::optional<ToolReturn> operator()(const ServerContext& ctx, const std::string& name,
                                         const std::string& arguments) const
    {
        if (isAsync_ && asyncFn_) {
            asyncFn_(ctx, name, arguments);
            return std::nullopt;
        }
        if (syncFn_) {
            return syncFn_(ctx, name, arguments);
        }
        throw std::runtime_error("ToolFunc not initialized");
    }

    explicit operator bool() const noexcept
    {
        return static_cast<bool>(syncFn_) || static_cast<bool>(asyncFn_);
    }

    bool IsAsync() const noexcept { return isAsync_; }

    bool operator==(std::nullptr_t) const noexcept { return !static_cast<bool>(*this); }
    bool operator!=(std::nullptr_t) const noexcept { return static_cast<bool>(*this); }

private:
    SyncFn syncFn_;
    AsyncFn asyncFn_;
    bool isAsync_;
};

// RenderPromptFunc wraps sync and async prompt-render callbacks.
// Sync:  f(ServerContext, name, optional<string> arg) -> GetPromptResult
// Async: f(ServerContext, name, optional<string> arg) -> void
class RenderPromptFunc {
public:
    using SyncFn = std::function<GetPromptResult(const ServerContext& ctx, const std::string& name,
                                                 const std::optional<std::string>& argument)>;
    using AsyncFn = std::function<void(const ServerContext& ctx, const std::string& name,
                                       const std::optional<std::string>& argument)>;

    RenderPromptFunc() : isAsync_(false) {}
    RenderPromptFunc(std::nullptr_t) noexcept : isAsync_(false) {}
    ~RenderPromptFunc() = default;

    template <typename F,
              typename = std::enable_if_t<
                  !std::is_same_v<std::decay_t<F>, RenderPromptFunc> &&
                  (std::is_invocable_r_v<GetPromptResult, F, const ServerContext&, const std::string&,
                                         const std::optional<std::string>&> ||
                   std::is_invocable_v<F, const ServerContext&, const std::string&,
                                       const std::optional<std::string>&>)>>
    RenderPromptFunc(F&& fn)
    {
        if constexpr (std::is_invocable_r_v<GetPromptResult, F, const ServerContext&, const std::string&,
                                            const std::optional<std::string>&>) {
            syncFn_ = std::forward<F>(fn);
            isAsync_ = false;
        } else if constexpr (std::is_invocable_v<F, const ServerContext&, const std::string&,
                                                   const std::optional<std::string>&>) {
            asyncFn_ = std::forward<F>(fn);
            isAsync_ = true;
        }
    }

    std::optional<GetPromptResult> operator()(const ServerContext& ctx, const std::string& name,
                               const std::optional<std::string>& argument) const
    {
        if (isAsync_ && asyncFn_) {
            asyncFn_(ctx, name, argument);
            return std::nullopt;
        }
        if (syncFn_) {
            return syncFn_(ctx, name, argument);
        }
        throw std::runtime_error("RenderPromptFunc not initialized");
    }

    explicit operator bool() const noexcept
    {
        return static_cast<bool>(syncFn_) || static_cast<bool>(asyncFn_);
    }

    bool IsAsync() const noexcept { return isAsync_; }

    bool operator==(std::nullptr_t) const noexcept { return !static_cast<bool>(*this); }
    bool operator!=(std::nullptr_t) const noexcept { return static_cast<bool>(*this); }

private:
    SyncFn syncFn_;
    AsyncFn asyncFn_;
    bool isAsync_;
};

// ReadResourceFunc wraps sync and async resource-read callbacks.
// Sync:  f(ServerContext, uri) -> ReadResourceResult
// Async: f(ServerContext, uri) -> void
class ReadResourceFunc {
public:
    using SyncFn = std::function<ReadResourceResult(const ServerContext& ctx, const std::string& uri)>;
    using AsyncFn = std::function<void(const ServerContext& ctx, const std::string& uri)>;

    ReadResourceFunc() : isAsync_(false) {}
    ReadResourceFunc(std::nullptr_t) noexcept : isAsync_(false) {}
    ~ReadResourceFunc() = default;

    template <typename F,
              typename = std::enable_if_t<
                  !std::is_same_v<std::decay_t<F>, ReadResourceFunc> &&
                  (std::is_invocable_r_v<ReadResourceResult, F, const ServerContext&, const std::string&> ||
                   std::is_invocable_v<F, const ServerContext&, const std::string&>)>>
    ReadResourceFunc(F&& fn)
    {
        if constexpr (std::is_invocable_r_v<ReadResourceResult, F, const ServerContext&,
                                            const std::string&>) {
            syncFn_ = std::forward<F>(fn);
            isAsync_ = false;
        } else if constexpr (std::is_invocable_v<F, const ServerContext&, const std::string&>) {
            asyncFn_ = std::forward<F>(fn);
            isAsync_ = true;
        }
    }

    std::optional<ReadResourceResult> operator()(const ServerContext& ctx, const std::string& uri) const
    {
        if (isAsync_ && asyncFn_) {
            asyncFn_(ctx, uri);
            return std::nullopt;
        }
        if (syncFn_) {
            return syncFn_(ctx, uri);
        }
        throw std::runtime_error("ReadResourceFunc not initialized");
    }

    explicit operator bool() const noexcept
    {
        return static_cast<bool>(syncFn_) || static_cast<bool>(asyncFn_);
    }

    bool IsAsync() const noexcept { return isAsync_; }

    bool operator==(std::nullptr_t) const noexcept { return !static_cast<bool>(*this); }
    bool operator!=(std::nullptr_t) const noexcept { return static_cast<bool>(*this); }

private:
    SyncFn syncFn_;
    AsyncFn asyncFn_;
    bool isAsync_;
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

    virtual void RegisterSetLoggingLevelHandler(std::function<void(const std::string& level)> h) = 0;

    /**
     * Add a completion handler to the server.
     *
     * @param handler Completion handler function that processes complete requests
     */
    virtual void AddCompletion(CompleteFunc handler) = 0;
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
