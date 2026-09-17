/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "mcp_error.h"

#include <utility>

namespace Mcp {

MCPError::MCPError(const ErrorResult& errorResult)
    : std::runtime_error(errorResult.message), error(errorResult)
{
}
MCPError::~MCPError() = default;

int MCPError::code() const noexcept
{
    return error.code;
}

std::optional<JsonRpcErrorCode> MCPError::codeEnum() const noexcept
{
    switch (error.code) {
        case static_cast<int>(JsonRpcErrorCode::PARSE_ERROR):
            return JsonRpcErrorCode::PARSE_ERROR;
        case static_cast<int>(JsonRpcErrorCode::INVALID_REQUEST):
            return JsonRpcErrorCode::INVALID_REQUEST;
        case static_cast<int>(JsonRpcErrorCode::METHOD_NOT_FOUND):
            return JsonRpcErrorCode::METHOD_NOT_FOUND;
        case static_cast<int>(JsonRpcErrorCode::INVALID_PARAMS):
            return JsonRpcErrorCode::INVALID_PARAMS;
        case static_cast<int>(JsonRpcErrorCode::INTERNAL_ERROR):
            return JsonRpcErrorCode::INTERNAL_ERROR;
        case static_cast<int>(JsonRpcErrorCode::SERVER_ERROR):
            return JsonRpcErrorCode::SERVER_ERROR;
        default:
            return std::nullopt;
    }
}

const std::string& MCPError::message() const noexcept
{
    return error.message;
}

const std::optional<std::string>& MCPError::data() const noexcept
{
    return error.data;
}

} // namespace Mcp
