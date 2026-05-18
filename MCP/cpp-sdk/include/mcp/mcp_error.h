/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef MCP_ERROR_INCLUDE_H_
#define MCP_ERROR_INCLUDE_H_

#include <optional>
#include <stdexcept>
#include <string>

#include "mcp_type.h"

namespace Mcp {

// JSON-RPC 2.0 error codes
// See: https://www.jsonrpc.org/specification#error_object
enum class JsonRpcErrorCode : int {
    PARSE_ERROR = -32700,
    INVALID_REQUEST = -32600,
    METHOD_NOT_FOUND = -32601,
    INVALID_PARAMS = -32602,
    INTERNAL_ERROR = -32603,
    SERVER_ERROR = -32000 // -32000 to -32099 reserved for server errors
};

/**
 * @brief Exception raised when an error arrives over an MCP/JSON-RPC connection.
 *
 * This mirrors the JSON-RPC error object shape:
 * `{ "code": int, "message": string, "data"?: any }`
 *
 * Notes:
 * - `what()` (from std::runtime_error) is initialized from the `message`.
 * - The original fields are also stored and can be retrieved via accessors.
 */
class MCPError : public std::runtime_error {
public:
    /**
     * @brief Construct an MCPError from an ErrorResult.
     *
     * This is a convenience overload for code paths that already have an
     * ErrorResult instance (e.g., from JSON-RPC decoding).
     */
    explicit MCPError(const ErrorResult& error);

    ~MCPError() override;

    /**
     * @brief Get the JSON-RPC error code.
     */
    int code() const noexcept;

    /**
     * @brief Convert @ref code() to a known JsonRpcErrorCode enum value.
     * @return A JsonRpcErrorCode when the code matches a known value; otherwise std::nullopt.
     */
    std::optional<JsonRpcErrorCode> codeEnum() const noexcept;

    /**
     * @brief Get the JSON-RPC error message.
     */
    const std::string& message() const noexcept;

    /**
     * @brief Get the optional JSON-RPC error data.
     */
    const std::optional<std::string>& data() const noexcept;

private:
    ErrorResult error;
};

} // namespace Mcp

#endif // MCP_ERROR_INCLUDE_H_
