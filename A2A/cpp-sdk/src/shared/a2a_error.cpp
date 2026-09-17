/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <nlohmann/json.hpp>

#include "error.h"
#include "http_common.h"
#include "jsonrpc.h"

namespace A2A {

namespace {

std::string FormatClientErrorWhat(int code, const std::string& message)
{
    A2AError error;
    error.code = code;
    error.message = message;
    return nlohmann::json(error).dump();
}

bool IsHttpStatusCode(int code)
{
    return code >= 100 && code <= 599;
}

bool IsJsonRelatedErrorCode(int code)
{
    return code == static_cast<int>(A2AErrorCode::JSONRPC_PARSE_ERROR) ||
        code == static_cast<int>(A2AErrorCode::A2A_INVALID_FORMAT) ||
        code == Http::HTTP_PARSE_ERROR;
}

} // namespace

A2AClientException::A2AClientException(int code, std::string msg)
    : std::runtime_error(FormatClientErrorWhat(code, msg)), errorCode(code), message(std::move(msg))
{
}

std::exception_ptr A2AClientException::Make(int code, const std::string& message)
{
    if (IsHttpStatusCode(code)) {
        return std::make_exception_ptr(A2AClientHTTPError(code, message));
    }
    if (IsJsonRelatedErrorCode(code)) {
        return std::make_exception_ptr(A2AClientJSONError(code, message));
    }
    if (code == static_cast<int>(A2AErrorCode::A2A_REQUEST_TIMEOUT)) {
        return std::make_exception_ptr(A2AClientTimeoutError(message));
    }
    return std::make_exception_ptr(A2AClientException(code, message));
}

bool A2AClientException::TryParse(const std::exception& e, A2AError& out)
{
    if (const auto* clientError = dynamic_cast<const A2AClientException*>(&e)) {
        out.code = clientError->errorCode;
        out.message = clientError->message;
        return true;
    }

    try {
        out = nlohmann::json::parse(e.what()).get<A2AError>();
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

A2AClientHTTPError::A2AClientHTTPError(int httpStatusCode, const std::string& message)
    : A2AClientException(httpStatusCode, message), statusCode(httpStatusCode)
{
}

A2AClientJSONError::A2AClientJSONError(int code, const std::string& message)
    : A2AClientException(code, message)
{
}

A2AClientTimeoutError::A2AClientTimeoutError(const std::string& message)
    : A2AClientException(static_cast<int>(A2AErrorCode::A2A_REQUEST_TIMEOUT), message)
{
}

} // namespace A2A
