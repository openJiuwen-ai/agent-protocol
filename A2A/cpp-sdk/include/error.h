/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_ERROR
#define A2A_ERROR

#include <stdexcept>
#include <string>

namespace A2A {
constexpr int INTERNAL_ERROR_CODE = -32603;
// Base exception for server-side errors (parity with Python A2AServerError)
struct A2AServerError : public std::runtime_error {
    int statusCode;
    explicit A2AServerError(const std::string& message)
        : std::runtime_error(message), statusCode(INTERNAL_ERROR_CODE)
    {
    }

    explicit A2AServerError(const std::string& message, int code)
        : std::runtime_error(message), statusCode(code)
    {
    }
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
} // namespace A2A

#endif