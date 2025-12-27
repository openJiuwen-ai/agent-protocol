/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_ERRORS
#define A2A_ERRORS

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace a2a {
enum class HttpStatusCode : std::uint16_t {
    // 2xx Success
    OK                        = 200,
    Created                   = 201,
    Accepted                  = 202,
    NoContent                 = 204,
    // 3xx Redirection
    MovedPermanently          = 301,
    Found                     = 302,
    NotModified               = 304,
    // 4xx Client Errors
    BadRequest                = 400,
    Unauthorized              = 401,
    PaymentRequired           = 402,
    Forbidden                 = 403,
    NotFound                  = 404,
    MethodNotAllowed          = 405,
    RequestTimeout            = 408,
    Conflict                  = 409,
    Gone                      = 410,
    PayloadTooLarge           = 413,
    URITooLong                = 414,
    UnsupportedMediaType      = 415,
    TooManyRequests           = 429,
    // 5xx Server Errors
    InternalServerError       = 500,
    NotImplemented            = 501,
    BadGateway                = 502,
    ServiceUnavailable        = 503,
    GatewayTimeout            = 504,
    HTTPVersionNotSupported   = 505
};

enum class JSONRPCErrorCode : int {
    // --- Standard JSON-RPC 2.0 Errors (RFC 7196) ---
    ParseError = -32700,
    InvalidRequest = -32600,
    MethodNotFound = -32601,
    InvalidParams  = -32602,
    InternalError  = -32603,

    // --- Implementation-defined Server Errors (custom, in [-32099, -32000]) ---
    //     Note: JSON-RPC spec reserves [-32099, -32000] for implementation-defined errors
    TaskNotFound                        = -32001,
    TaskNotCancelable                   = -32002,
    PushNotificationNotSupported        = -32003,
    UnsupportedOperation                = -32004,
    ContentTypeNotSupported             = -32005,
    InvalidAgentResponse                = -32006,
    AuthenticatedExtendedCardNotConfigured = -32007,
};

// Generic unsupported operation
struct UnsupportedOperationError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Base exception for server-side errors (parity with Python A2AServerError)
struct A2AServerError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Method not implemented on server handler (parity with Python MethodNotImplementedError)
struct MethodNotImplementedError : public A2AServerError {
    explicit MethodNotImplementedError(const std::string& message = "This method is not implemented by the server")
        : A2AServerError("Not Implemented operation Error: " + message)
    {
    }
};

// Generic server-side error wrapper
struct ServerError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

// HTTP error with status code
struct A2AClientHTTPError : public std::runtime_error {
    int statusCode;
    explicit A2AClientHTTPError(int code, const std::string& message)
        : std::runtime_error("HTTP Error " + std::to_string(code) + ": " + message), statusCode(code)
    {
    }
};

// JSON parse/validation error
struct A2AClientJSONError : public std::runtime_error {
    explicit A2AClientJSONError(const std::string& message) : std::runtime_error("JSON Error: " + message)
    {
    }
};

// Timeout error
struct A2AClientTimeoutError : public std::runtime_error {
    explicit A2AClientTimeoutError(const std::string& message) : std::runtime_error("Timeout Error: " + message)
    {
    }
};

// JSON-RPC Error wrapper
struct A2AClientJSONRPCError : public std::runtime_error {
    nlohmann::json error;
    explicit A2AClientJSONRPCError(const nlohmann::json& err)
        : std::runtime_error("JSON-RPC Error: " + err.dump()), error(err)
    {
    }
};

} // namespace a2a

#endif
