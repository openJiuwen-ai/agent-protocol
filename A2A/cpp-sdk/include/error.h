/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_ERROR
#define A2A_ERROR

#include <exception>
#include <stdexcept>
#include <string>

#include "types.h"

namespace A2A {

/**
 * @brief Base exception for A2A client-side errors.
 * @note errorCode 使用 A2AErrorCode 或 HTTP 状态码（传输层错误）。
 */
struct A2AClientException : public std::runtime_error {
    /** @brief Wire / SDK error code (A2AErrorCode or HTTP status). */
    int errorCode;
    /** @brief Human-readable error message. */
    std::string message;

    /**
     * @brief Construct a client exception.
     * @param[in] code Error code.
     * @param[in] msg  Error message.
     */
    A2AClientException(int code, std::string msg);

    /**
     * @brief Create an exception_ptr wrapping A2AClientException.
     * @param[in] code    Error code.
     * @param[in] message Error message.
     * @return std::exception_ptr suitable for std::future::set_exception.
     */
    static std::exception_ptr Make(int code, const std::string& message);

    /**
     * @brief Parse an A2AError from a caught exception.
     * @param[in]  e   Exception to inspect.
     * @param[out] out Populated A2AError on success.
     * @return true if @p e is an A2AClientException.
     */
    static bool TryParse(const std::exception& e, A2AError& out);
};

/**
 * @brief Base exception for A2A server-side errors.
 * @note statusCode 映射到 JSON-RPC / A2A wire error code (A2AErrorCode)。
 */
struct A2AServerError : public std::runtime_error {
    /** @brief JSON-RPC / A2A error code returned to the client. */
    int statusCode;

    /**
     * @brief Construct with internal-error code (-32603).
     * @param[in] message Error description.
     */
    explicit A2AServerError(const std::string& message)
        : std::runtime_error(message),
          statusCode(static_cast<int>(A2AErrorCode::JSONRPC_INTERNAL_ERROR))
    {
    }

    /**
     * @brief Construct with an explicit error code.
     * @param[in] message Error description.
     * @param[in] code    A2AErrorCode value.
     */
    explicit A2AServerError(const std::string& message, int code)
        : std::runtime_error(message), statusCode(code)
    {
    }
};

/**
 * @brief Thrown when the server does not implement the requested JSON-RPC method.
 * @note 对应 JSON-RPC -32601 Method not found。
 */
struct MethodNotImplementedError : public A2AServerError {
    /**
     * @brief Construct a method-not-implemented error.
     * @param[in] message Optional detail appended to the error text.
     */
    explicit MethodNotImplementedError(const std::string& message = "This method is not implemented by the server")
        : A2AServerError("Not Implemented operation Error: " + message,
            static_cast<int>(A2AErrorCode::JSONRPC_METHOD_NOT_FOUND))
    {
    }
};

/**
 * @brief Client exception for non-2xx HTTP responses.
 */
struct A2AClientHTTPError : public A2AClientException {
    /** @brief HTTP status code from the transport layer. */
    int statusCode;

    /**
     * @brief Construct an HTTP transport error.
     * @param[in] httpStatusCode HTTP status code.
     * @param[in] message        Error description.
     */
    explicit A2AClientHTTPError(int httpStatusCode, const std::string& message);
};

/**
 * @brief Client exception for JSON-RPC or A2A protocol errors in a response body.
 */
struct A2AClientJSONError : public A2AClientException {
    /**
     * @brief Construct a JSON / protocol error.
     * @param[in] code    A2AErrorCode value from the response.
     * @param[in] message Error description.
     */
    explicit A2AClientJSONError(int code, const std::string& message);
};

/**
 * @brief Client exception for request timeouts.
 */
struct A2AClientTimeoutError : public A2AClientException {
    /**
     * @brief Construct a timeout error.
     * @param[in] message Error description.
     */
    explicit A2AClientTimeoutError(const std::string& message);
};

} // namespace A2A

#endif
