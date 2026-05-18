/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_ERROR
#define A2A_ERROR

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace A2A {
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
} // namespace A2A

#endif