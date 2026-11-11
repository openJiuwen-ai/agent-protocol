# Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
"""e2a：jiuwenswarm 与 jiuwenswarm-gateway 两仓共享的协议包。

**只含纯数据结构**（dataclass / enum / 常量字面量），不含行为实现与接口 ABC；
不 import jiuwenswarm / openjiuwen（不得产生反向依赖），零第三方运行时依赖。
接口（AgentServerClient / ThirdAgent / ExtensionRegistry / ExtensionManager /
SshAuthPort / CryptoProvider 等）、扩展基类（BaseExtension 及子类）、E2A 转换与
编解码逻辑，均由各仓实现侧持有一份副本。

导出策略：各模块的公开常量与类型统一在顶层再导出（``from e2a
import <NAME>`` 可用）；例外只有一处——``match_expressions`` 的函数名易与业务
符号重名，经 ``e2a.match_expressions`` 访问。

模块边界：
- ``channel_event``    通道契约（ChannelType 枚举）
- ``cron_models``      cron 共享数据结构（CronTargetChannel / CronTarget / CronJob / CronRunState）
- ``expert_http_routes`` 专家包专用 HTTP 路径表（ExpertHttpRoute）
- ``auth``             SSH 认证结果/条目数据类（SshKeyEntry / SshAuthResult）
- ``audit``            审计打点字段契约（字段名/事件类型/响应码 + AuditEvent）
- ``hooks``            AgentServer/Gateway hook 事件名常量与上下文数据类
- ``identity``         请求身份字段契约 + 归一化/落点纯函数（routing / user_id）
- ``logging_config``   日志级别配置请求体字段契约（logging 段字段名 / op 字面量）
- ``match_expressions`` 实例 Agent 授权表达式校验/求值（纯函数；经
  ``e2a.match_expressions`` 访问，不在顶层汇总导出）
- ``message``          WS/E2A 线上报文与枚举（Message / ReqMethod / EventType / Mode）
- ``mcp_oauth``        MCP OAuth 回调共享契约（McpOAuthError / provider 域名）
- ``package_allowlist`` 专家包允许名单线上契约（元数据键 / 内部头 / 规范化与编解码）
- ``provider_domains`` external provider 能力域名常量
- ``redis_keys``       Gateway Redis key / Pub-Sub 频道模板字面量
- ``session_assets``   会话素材（session.assets.*）数据结构与常量
- ``sdk``              扩展 SDK 共享数据类（ApplicationPluginServices / 贡献类）
- ``template_slots``   企业配置 template_ref 槽位（TemplateRefSlot / normalize_template_ref）
- ``types``            ExtensionConfig / ExtensionMetadata 等通用数据类型
- ``models``           E2A 信封/响应模型（E2AEnvelope / E2AResponse）
- ``constants``        ACP/E2A 线协议常量
- ``agent_models``     AgentRequest / AgentResponse 线协议类型
- ``agent_compat``     AgentRequest 兼容转换
- ``adapters``         ACP/A2A 与 E2A 信封互转
- ``gateway_normalize`` 网关侧 E2A/Agent 归一化纯函数
- ``wire_codec``       线上编解码
- ``runtime_http_routes`` Runtime HTTP 路由表
- ``acp``              ACP JSON-RPC / 会话更新转换
"""

from __future__ import annotations

from e2a.audit import (
    AUDIT_EVENT_EVENT,
    AUDIT_EVENT_USER_ACTION,
    AUDIT_FIELD_EVT,
    AUDIT_FIELD_MSG,
    AUDIT_FIELD_PROC,
    AUDIT_FIELD_REQUEST_ID,
    AUDIT_FIELD_RSPCD,
    AUDIT_FIELD_SESSION_ID,
    AUDIT_FIELD_SUBMDL,
    AUDIT_FIELD_UA,
    AUDIT_FIELD_UID,
    AUDIT_FIELD_UID_LOWER,
    AUDIT_LEVEL_INFO,
    AUDIT_LEVEL_WARN,
    AUDIT_LIFTED_FIELDS,
    AUDIT_RSPCD_FAILURE,
    AUDIT_RSPCD_SUCCESS,
    AuditEvent,
)
from e2a.auth import SshAuthResult, SshKeyEntry
from e2a.channel_event import ChannelType
from e2a.cron_models import (
    CRON_JOB_DEFAULT_MODE,
    CRON_JOB_DESCRIPTION_MAX_LENGTH,
    CRON_JOB_NAME_MAX_LENGTH,
    DEFAULT_WEB_WORK_MODE,
    CronJob,
    CronRunState,
    CronTarget,
    CronTargetChannel,
)
from e2a.expert_http_routes import (
    EXPERT_TEMPLATE_HTTP_ROUTES,
    ExpertHttpRoute,
    expert_http_routes,
)
from e2a.hooks import (
    AgentServerChatHookContext,
    AgentServerHookEvents,
    GatewayChatHookContext,
    GatewayHookEvents,
    MemoryHookContext,
    SystemPromptHookContext,
)
from e2a.identity import (
    ROUTING_METADATA_KEY,
    TENANT_ROUTING_FIELDS,
    USER_ID_FIELD,
    WEB_IDENTITY_FIELDS,
    WEB_ROUTING_ID_FIELDS,
    apply_routing_metadata,
    normalize_routing_identity,
    web_routing_identity,
)
from e2a.logging_config import (
    LOGGING_CONFIG_SECTION,
    LOGGING_LEVEL_FIELDS,
    LOG_CONFIG_OP_DELETE,
    LOG_CONFIG_OP_FIELD,
)
from e2a.mcp_oauth import MCP_REGISTRY_PROVIDER_DOMAIN, McpOAuthError
from e2a.message import EventType, Message, Mode, ReqMethod
from e2a.package_allowlist import (
    HIDDEN_HUB_ASSET_IDS,
    MAX_PACKAGE_ALLOWLIST_JSON_BYTES,
    PACKAGE_ALLOWLIST_HEADER_NAME,
    PACKAGE_ALLOWLIST_METADATA_KEY,
    PACKAGE_KIND_ALIASES,
    PACKAGE_KIND_DIRS,
    apply_allowlist_to_envelope,
    canonical_kind,
    decode_package_allowlist_header,
    encode_package_allowlist_header,
    ensure_packages_fit,
    normalize_packages,
)
from e2a.provider_domains import (
    PROVIDER_DOMAIN_A2UI,
    PROVIDER_DOMAIN_CRON_STORE_FACTORY,
    PROVIDER_DOMAIN_MCP_CREDENTIAL,
    PROVIDER_DOMAIN_MCP_REGISTRY,
    PROVIDER_DOMAIN_MCP_STATE,
    PROVIDER_DOMAIN_SESSION_METADATA,
    PROVIDER_DOMAIN_TEAM_BINDING_STORE,
    PROVIDER_DOMAIN_TEAM_ENTITY_STORE,
    PROVIDER_DOMAINS,
)
from e2a.redis_keys import (
    GATEWAY_CRON_JOBS_HASH_TEMPLATE,
    GATEWAY_CRON_LIVE_CHANNEL_TEMPLATE,
    GATEWAY_CRON_LIVE_REPLAY_TEMPLATE,
    GATEWAY_CRON_PERMISSION_HOLD_BY_SESSION_TEMPLATE,
    GATEWAY_CRON_PERMISSION_HOLD_TEMPLATE,
    GATEWAY_LEADER_LOCK_TEMPLATE,
    GATEWAY_SESSION_MAP_HASH_TEMPLATE,
    REDIS_KEY_PREFIX_SEPARATOR,
)
from e2a.sdk import (
    ApplicationPluginServices,
    FrontendContribution,
    WebSocketEndpoint,
    WebSocketRouteContribution,
)
from e2a.session_assets import (
    ASSET_ID_PREFIX,
    ASSET_KINDS,
    ASSET_KIND_AUDIO,
    ASSET_KIND_DOCUMENT,
    ASSET_KIND_IMAGE,
    ASSET_KIND_VIDEO,
    ASSET_NAME_MAX_LENGTH,
    ASSET_SOURCES,
    ASSET_SOURCE_GENERATED,
    ASSET_SOURCE_UPLOAD,
    SessionAsset,
)
from e2a.template_slots import (
    DEFAULT_AGENT_LOAD_SLOTS,
    MODEL_SLOT_KEYS,
    SLOT_ENTITY_TABLE,
    TemplateRefSlot,
    normalize_template_ref,
)
from e2a.types import ExtensionConfig, ExtensionMetadata

from e2a.acp import (
    AcpSessionUpdateState,
    build_acp_final_text_update,
    build_acp_initialize_result,
    build_acp_prompt_result,
    build_acp_session_update,
    build_acp_usage_update,
)
from e2a.adapters import (
    e2a_response_to_a2a_stream_payload,
    e2a_response_to_acp_jsonrpc_response,
    envelope_from_a2a_send_message,
    envelope_from_acp_jsonrpc,
    envelope_to_acp_jsonrpc_call,
)
from e2a.agent_compat import e2a_to_agent_request
from e2a.agent_models import (
    AgentRef,
    AgentRequest,
    AgentResponse,
    AgentResponseChunk,
    PermissionContext,
)
from e2a.constants import (
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
from e2a.gateway_normalize import (
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
from e2a.models import (
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
from e2a.wire_codec import (
    encode_agent_chunk_for_wire,
    encode_agent_response_for_wire,
    encode_json_parse_error_wire,
    is_e2a_response_wire_dict,
    parse_agent_server_wire_chunk,
    parse_agent_server_wire_unary,
)

__version__ = "0.1.0"

__all__ = [
    "ACP_AGENT_TO_CLIENT_METHODS",
    "ACP_CLIENT_TO_AGENT_METHODS",
    "ACP_NOTIFICATION_NAMES",
    "ACP_SESSION_UPDATE_KINDS",
    "ASSET_ID_PREFIX",
    "ASSET_KINDS",
    "ASSET_KIND_AUDIO",
    "ASSET_KIND_DOCUMENT",
    "ASSET_KIND_IMAGE",
    "ASSET_KIND_VIDEO",
    "ASSET_NAME_MAX_LENGTH",
    "ASSET_SOURCES",
    "ASSET_SOURCE_GENERATED",
    "ASSET_SOURCE_UPLOAD",
    "AUDIT_EVENT_EVENT",
    "AUDIT_EVENT_USER_ACTION",
    "AUDIT_FIELD_EVT",
    "AUDIT_FIELD_MSG",
    "AUDIT_FIELD_PROC",
    "AUDIT_FIELD_REQUEST_ID",
    "AUDIT_FIELD_RSPCD",
    "AUDIT_FIELD_SESSION_ID",
    "AUDIT_FIELD_SUBMDL",
    "AUDIT_FIELD_UA",
    "AUDIT_FIELD_UID",
    "AUDIT_FIELD_UID_LOWER",
    "AUDIT_LEVEL_INFO",
    "AUDIT_LEVEL_WARN",
    "AUDIT_LIFTED_FIELDS",
    "AUDIT_RSPCD_FAILURE",
    "AUDIT_RSPCD_SUCCESS",
    "AcpSessionUpdateState",
    "AgentRef",
    "AgentRequest",
    "AgentResponse",
    "AgentResponseChunk",
    "AgentServerChatHookContext",
    "AgentServerHookEvents",
    "ApplicationPluginServices",
    "AuditEvent",
    "CRON_JOB_DEFAULT_MODE",
    "CRON_JOB_DESCRIPTION_MAX_LENGTH",
    "CRON_JOB_NAME_MAX_LENGTH",
    "ChannelType",
    "CronJob",
    "CronRunState",
    "CronTarget",
    "CronTargetChannel",
    "DEFAULT_AGENT_LOAD_SLOTS",
    "DEFAULT_WEB_WORK_MODE",
    "E2AAuth",
    "E2AEnvelope",
    "E2AFileRef",
    "E2AProvenance",
    "E2AResponse",
    "E2A_A2A_STREAM_BRANCHES",
    "E2A_FALLBACK_FAILED_KEY",
    "E2A_INTERNAL_CONTEXT_KEY",
    "E2A_LEGACY_AGENT_REQUEST_KEY",
    "E2A_PROTOCOL_VERSION",
    "E2A_RESPONSE_KINDS",
    "E2A_RESPONSE_KIND_A2A_MESSAGE",
    "E2A_RESPONSE_KIND_A2A_STREAM_EVENT",
    "E2A_RESPONSE_KIND_A2A_TASK",
    "E2A_RESPONSE_KIND_ACP_JSONRPC_ERROR",
    "E2A_RESPONSE_KIND_ACP_PROMPT_RESULT",
    "E2A_RESPONSE_KIND_ACP_SESSION_UPDATE",
    "E2A_RESPONSE_KIND_E2A_CHUNK",
    "E2A_RESPONSE_KIND_E2A_COMPLETE",
    "E2A_RESPONSE_KIND_E2A_ERROR",
    "E2A_RESPONSE_KIND_EXT",
    "E2A_RESPONSE_STATUS_FAILED",
    "E2A_RESPONSE_STATUS_IN_PROGRESS",
    "E2A_RESPONSE_STATUS_SUCCEEDED",
    "E2A_SOURCE_PROTOCOL_A2A",
    "E2A_SOURCE_PROTOCOL_ACP",
    "E2A_SOURCE_PROTOCOL_E2A",
    "E2A_WIRE_LEGACY_AGENT_CHUNK_KEY",
    "E2A_WIRE_LEGACY_AGENT_RESPONSE_KEY",
    "E2A_WIRE_SERVER_PUSH_KEY",
    "EXPERT_TEMPLATE_HTTP_ROUTES",
    "EventType",
    "ExpertHttpRoute",
    "ExtensionConfig",
    "ExtensionMetadata",
    "FrontendContribution",
    "GATEWAY_CRON_JOBS_HASH_TEMPLATE",
    "GATEWAY_CRON_LIVE_CHANNEL_TEMPLATE",
    "GATEWAY_CRON_LIVE_REPLAY_TEMPLATE",
    "GATEWAY_CRON_PERMISSION_HOLD_BY_SESSION_TEMPLATE",
    "GATEWAY_CRON_PERMISSION_HOLD_TEMPLATE",
    "GATEWAY_LEADER_LOCK_TEMPLATE",
    "GATEWAY_SESSION_MAP_HASH_TEMPLATE",
    "GatewayChatHookContext",
    "GatewayHookEvents",
    "HIDDEN_HUB_ASSET_IDS",
    "IdentityOrigin",
    "LOGGING_CONFIG_SECTION",
    "LOGGING_LEVEL_FIELDS",
    "LOG_CONFIG_OP_DELETE",
    "LOG_CONFIG_OP_FIELD",
    "MAX_LEGACY_AGENT_REQUEST_JSON_BYTES",
    "MAX_PACKAGE_ALLOWLIST_JSON_BYTES",
    "MCP_REGISTRY_PROVIDER_DOMAIN",
    "MODEL_SLOT_KEYS",
    "McpOAuthError",
    "MemoryHookContext",
    "Message",
    "Mode",
    "PACKAGE_ALLOWLIST_HEADER_NAME",
    "PACKAGE_ALLOWLIST_METADATA_KEY",
    "PACKAGE_KIND_ALIASES",
    "PACKAGE_KIND_DIRS",
    "PROVIDER_DOMAINS",
    "PROVIDER_DOMAIN_A2UI",
    "PROVIDER_DOMAIN_CRON_STORE_FACTORY",
    "PROVIDER_DOMAIN_MCP_CREDENTIAL",
    "PROVIDER_DOMAIN_MCP_REGISTRY",
    "PROVIDER_DOMAIN_MCP_STATE",
    "PROVIDER_DOMAIN_SESSION_METADATA",
    "PROVIDER_DOMAIN_TEAM_BINDING_STORE",
    "PROVIDER_DOMAIN_TEAM_ENTITY_STORE",
    "PermissionContext",
    "REDIS_KEY_PREFIX_SEPARATOR",
    "ROUTING_METADATA_KEY",
    "ReqMethod",
    "SLOT_ENTITY_TABLE",
    "SessionAsset",
    "SshAuthResult",
    "SshKeyEntry",
    "SystemPromptHookContext",
    "TENANT_ROUTING_FIELDS",
    "TemplateRefSlot",
    "USER_ID_FIELD",
    "WEB_IDENTITY_FIELDS",
    "WEB_ROUTING_ID_FIELDS",
    "WebSocketEndpoint",
    "WebSocketRouteContribution",
    "__version__",
    "apply_allowlist_to_envelope",
    "apply_routing_metadata",
    "build_acp_final_text_update",
    "build_acp_initialize_result",
    "build_acp_prompt_result",
    "build_acp_session_update",
    "build_acp_usage_update",
    "build_fallback_e2a",
    "canonical_kind",
    "channel_context_for_channel_reply",
    "decode_package_allowlist_header",
    "e2a_from_agent_fields",
    "e2a_response_from_agent_chunk",
    "e2a_response_from_agent_response",
    "e2a_response_to_a2a_stream_payload",
    "e2a_response_to_acp_jsonrpc_response",
    "e2a_response_to_agent_chunk",
    "e2a_response_to_agent_response",
    "e2a_to_agent_request",
    "encode_agent_chunk_for_wire",
    "encode_agent_response_for_wire",
    "encode_json_parse_error_wire",
    "encode_package_allowlist_header",
    "ensure_packages_fit",
    "envelope_from_a2a_send_message",
    "envelope_from_acp_jsonrpc",
    "envelope_to_acp_jsonrpc_call",
    "expert_http_routes",
    "is_e2a_response_wire_dict",
    "merge_params_to_acp_prompt",
    "message_to_e2a",
    "message_to_e2a_or_fallback",
    "message_to_legacy_agent_dict",
    "normalize_packages",
    "normalize_routing_identity",
    "normalize_template_ref",
    "parse_agent_server_wire_chunk",
    "parse_agent_server_wire_unary",
    "utc_now_iso",
    "web_routing_identity",
]
