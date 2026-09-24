# Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
"""E2A（Everything-to-Agent）：统一信封；ACP / A2A 等经转换进入 E2A，并由 provenance 记录出处。

来源：``jiuwenswarm/common/e2a``（E2A wire 契约自 jiuwenswarm 仓迁入本包，
jiuwenswarm 侧 ``common/e2a`` 现为指向本包的转发别名（过渡形态））；
协议说明文档在 jiuwenswarm 仓 ``docs/{zh,en}/E2A-protocol.md``。

与原仓差异（协议包零反向依赖所需）：
- ``agent_compat.e2a_to_agent_request`` 未随迁（依赖 jiuwenswarm 侧
  ``schema.agent.AgentRequest``/``ReqMethod`` 实例构造），留在 jiuwenswarm
  仓 ``common/e2a``（该处保留实现而非别名）。
- ``AgentResponse`` / ``AgentResponseChunk`` 线协议类型定义在
  ``gateway_protocol.e2a.agent_models``（原 jiuwenswarm ``schema.agent`` 的
  线协议部分；jiuwenswarm 侧 ``schema/agent.py`` 为其转发别名）。
- ``acp.protocol.build_acp_initialize_result`` 的 agent 版本号改由调用方
  ``agent_version=`` 注入；``acp.session_updates`` 的事件分发改按事件名
  字符串（枚举取 ``.value``），不再依赖 jiuwenswarm ``EventType``。
- ``adapters.build_acp_tool_response_message`` 增设 ``message_factory=``
  注入（未注入时返回等价 dict），不再 import jiuwenswarm ``Message``。
"""

from gateway_protocol.e2a.acp import (
    AcpSessionUpdateState,
    build_acp_final_text_update,
    build_acp_initialize_result,
    build_acp_prompt_result,
    build_acp_session_update,
    build_acp_usage_update,
)
from gateway_protocol.e2a.adapters import (
    e2a_response_to_a2a_stream_payload,
    e2a_response_to_acp_jsonrpc_response,
    envelope_from_a2a_send_message,
    envelope_from_acp_jsonrpc,
    envelope_to_acp_jsonrpc_call,
)
from gateway_protocol.e2a.agent_models import (
    AgentRequest,
    AgentResponse,
    AgentResponseChunk,
    PermissionContext,
)
from gateway_protocol.e2a.constants import (
    ACP_AGENT_TO_CLIENT_METHODS,
    ACP_CLIENT_TO_AGENT_METHODS,
    ACP_NOTIFICATION_NAMES,
    ACP_SESSION_UPDATE_KINDS,
    E2A_A2A_STREAM_BRANCHES,
    E2A_RESPONSE_KINDS,
    E2A_WIRE_LEGACY_AGENT_CHUNK_KEY,
    E2A_WIRE_LEGACY_AGENT_RESPONSE_KEY,
    E2A_WIRE_SERVER_PUSH_KEY,
    E2A_RESPONSE_KIND_ACP_JSONRPC_ERROR,
    E2A_RESPONSE_KIND_ACP_PROMPT_RESULT,
    E2A_RESPONSE_KIND_ACP_SESSION_UPDATE,
    E2A_RESPONSE_KIND_A2A_MESSAGE,
    E2A_RESPONSE_KIND_A2A_STREAM_EVENT,
    E2A_RESPONSE_KIND_A2A_TASK,
    E2A_RESPONSE_KIND_E2A_CHUNK,
    E2A_RESPONSE_KIND_E2A_COMPLETE,
    E2A_RESPONSE_KIND_E2A_ERROR,
    E2A_RESPONSE_KIND_EXT,
    E2A_RESPONSE_STATUS_FAILED,
    E2A_RESPONSE_STATUS_IN_PROGRESS,
    E2A_RESPONSE_STATUS_SUCCEEDED,
    E2A_SOURCE_PROTOCOL_A2A,
    E2A_SOURCE_PROTOCOL_ACP,
    E2A_SOURCE_PROTOCOL_E2A,
)
from gateway_protocol.e2a.gateway_normalize import (
    E2A_FALLBACK_FAILED_KEY,
    E2A_INTERNAL_CONTEXT_KEY,
    E2A_LEGACY_AGENT_REQUEST_KEY,
    MAX_LEGACY_AGENT_REQUEST_JSON_BYTES,
    build_fallback_e2a,
    channel_context_for_channel_reply,
    e2a_from_agent_fields,
    e2a_response_from_agent_chunk,
    e2a_response_from_agent_response,
    e2a_response_to_agent_chunk,
    e2a_response_to_agent_response,
    message_to_e2a,
    message_to_e2a_or_fallback,
    message_to_legacy_agent_dict,
)
from gateway_protocol.e2a.models import (
    E2A_PROTOCOL_VERSION,
    E2AAuth,
    E2AEnvelope,
    E2AFileRef,
    E2AProvenance,
    E2AResponse,
    IdentityOrigin,
    merge_params_to_acp_prompt,
    utc_now_iso,
)
from gateway_protocol.e2a.wire_codec import (
    encode_agent_chunk_for_wire,
    encode_agent_response_for_wire,
    encode_json_parse_error_wire,
    is_e2a_response_wire_dict,
    parse_agent_server_wire_chunk,
    parse_agent_server_wire_unary,
)

__all__ = [
    "ACP_AGENT_TO_CLIENT_METHODS",
    "ACP_CLIENT_TO_AGENT_METHODS",
    "ACP_NOTIFICATION_NAMES",
    "ACP_SESSION_UPDATE_KINDS",
    "AgentRequest",
    "AgentResponse",
    "AgentResponseChunk",
    "AcpSessionUpdateState",
    "E2A_A2A_STREAM_BRANCHES",
    "E2A_PROTOCOL_VERSION",
    "E2A_RESPONSE_KINDS",
    "E2A_RESPONSE_KIND_ACP_JSONRPC_ERROR",
    "E2A_RESPONSE_KIND_ACP_PROMPT_RESULT",
    "E2A_RESPONSE_KIND_ACP_SESSION_UPDATE",
    "E2A_RESPONSE_KIND_A2A_MESSAGE",
    "E2A_RESPONSE_KIND_A2A_STREAM_EVENT",
    "E2A_RESPONSE_KIND_A2A_TASK",
    "E2A_RESPONSE_KIND_E2A_CHUNK",
    "E2A_RESPONSE_KIND_E2A_COMPLETE",
    "E2A_RESPONSE_KIND_E2A_ERROR",
    "E2A_RESPONSE_KIND_EXT",
    "E2A_WIRE_LEGACY_AGENT_CHUNK_KEY",
    "E2A_WIRE_LEGACY_AGENT_RESPONSE_KEY",
    "E2A_WIRE_SERVER_PUSH_KEY",
    "E2A_RESPONSE_STATUS_FAILED",
    "E2A_RESPONSE_STATUS_IN_PROGRESS",
    "E2A_RESPONSE_STATUS_SUCCEEDED",
    "E2A_SOURCE_PROTOCOL_A2A",
    "E2A_SOURCE_PROTOCOL_ACP",
    "E2A_SOURCE_PROTOCOL_E2A",
    "E2AAuth",
    "E2AEnvelope",
    "E2AFileRef",
    "E2AProvenance",
    "E2AResponse",
    "IdentityOrigin",
    "PermissionContext",
    "e2a_response_to_a2a_stream_payload",
    "e2a_response_to_acp_jsonrpc_response",
    "envelope_from_a2a_send_message",
    "envelope_from_acp_jsonrpc",
    "envelope_to_acp_jsonrpc_call",
    "merge_params_to_acp_prompt",
    "utc_now_iso",
    "E2A_FALLBACK_FAILED_KEY",
    "E2A_INTERNAL_CONTEXT_KEY",
    "E2A_LEGACY_AGENT_REQUEST_KEY",
    "MAX_LEGACY_AGENT_REQUEST_JSON_BYTES",
    "build_acp_final_text_update",
    "build_acp_initialize_result",
    "build_acp_prompt_result",
    "build_acp_session_update",
    "build_acp_usage_update",
    "build_fallback_e2a",
    "channel_context_for_channel_reply",
    "e2a_from_agent_fields",
    "e2a_response_from_agent_chunk",
    "e2a_response_from_agent_response",
    "e2a_response_to_agent_chunk",
    "e2a_response_to_agent_response",
    "encode_agent_chunk_for_wire",
    "encode_agent_response_for_wire",
    "encode_json_parse_error_wire",
    "is_e2a_response_wire_dict",
    "parse_agent_server_wire_chunk",
    "parse_agent_server_wire_unary",
    "message_to_e2a",
    "message_to_e2a_or_fallback",
    "message_to_legacy_agent_dict",
]
