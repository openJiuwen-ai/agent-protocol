/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_ERRORS
#define A2A_ERRORS

namespace A2A {
enum class JSONRPCErrorCode : int {
    // --- Standard JSON-RPC 2.0 Errors (RFC 7196) ---
    PARSE_ERROR      = -32700,
    INVALID_REQUEST  = -32600,
    METHOD_NOT_FOUND = -32601,
    INVALID_PARAMS   = -32602,
    INTERNAL_ERROR   = -32603,

    // --- Implementation-defined Server Errors (custom, in [-32099, -32000]) ---
    //     Note: JSON-RPC spec reserves [-32099, -32000] for implementation-defined errors
    TASK_NOT_FOUND                             = -32001,
    TASK_NOT_CANCELABLE                        = -32002,
    PUSH_NOTIFICATION_NOT_SUPPORTED            = -32003,
    UNSUPPORTED_OPERATION                      = -32004,
    CONTENT_TYPE_NOT_SUPPORTED                 = -32005,
    INVALID_AGENT_RESPONSE                     = -32006,
    AUTHENTICATED_EXTENDED_CARD_NOT_CONFIGURED = -32007,
};
} // namespace A2A

#endif
