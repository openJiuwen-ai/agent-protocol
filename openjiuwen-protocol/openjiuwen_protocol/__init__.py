# Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
"""openjiuwen-protocol：jiuwenswarm 与 jiuwenswarm-gateway 两仓共享的协议包。

**只含纯数据结构**（dataclass / enum / 常量字面量），不含行为实现与接口 ABC；
不 import jiuwenswarm / openjiuwen（不得产生反向依赖），零第三方运行时依赖。
接口（AgentServerClient / ThirdAgent / ExtensionRegistry / ExtensionManager /
SshAuthPort / CryptoProvider 等）、扩展基类（BaseExtension 及子类）、E2A 转换与
编解码逻辑，均由各仓实现侧持有一份副本。

导出策略：各模块的公开常量与类型统一在顶层再导出（``from openjiuwen_protocol
import <NAME>`` 可用）；例外只有两处——``match_expressions`` 的函数名易与业务
符号重名，经 ``openjiuwen_protocol.match_expressions`` 访问；``e2a`` 子包按
命名空间访问，不在顶层汇总。

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
  ``openjiuwen_protocol.match_expressions`` 访问，不在顶层汇总导出）
- ``message``          WS/E2A 线上报文与枚举（Message / ReqMethod / EventType / Mode）
- ``mcp_oauth``        MCP OAuth 回调共享契约（McpOAuthError / provider 域名）
- ``package_allowlist`` 专家包允许名单线上契约（元数据键 / 内部头 / 规范化与编解码）
- ``provider_domains`` external provider 能力域名常量
- ``redis_keys``       Gateway Redis key / Pub-Sub 频道模板字面量
- ``session_assets``   会话素材（session.assets.*）数据结构与常量
- ``sdk``              扩展 SDK 共享数据类（ApplicationPluginServices / 贡献类）
- ``template_slots``   企业配置 template_ref 槽位（TemplateRefSlot / normalize_template_ref）
- ``types``            ExtensionConfig / ExtensionMetadata 等通用数据类型
- ``e2a``              E2A 协议数据结构（信封/响应/线协议模型/常量；经
  ``openjiuwen_protocol.e2a`` 子包命名空间访问，不在顶层汇总导出）
"""

from __future__ import annotations

from openjiuwen_protocol.audit import (
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
from openjiuwen_protocol.auth import SshAuthResult, SshKeyEntry
from openjiuwen_protocol.channel_event import ChannelType
from openjiuwen_protocol.cron_models import (
    CRON_JOB_DEFAULT_MODE,
    CRON_JOB_DESCRIPTION_MAX_LENGTH,
    CRON_JOB_NAME_MAX_LENGTH,
    DEFAULT_WEB_WORK_MODE,
    CronJob,
    CronRunState,
    CronTarget,
    CronTargetChannel,
)
from openjiuwen_protocol.expert_http_routes import (
    EXPERT_TEMPLATE_HTTP_ROUTES,
    ExpertHttpRoute,
    expert_http_routes,
)
from openjiuwen_protocol.hooks import (
    AgentServerChatHookContext,
    AgentServerHookEvents,
    GatewayChatHookContext,
    GatewayHookEvents,
    MemoryHookContext,
    SystemPromptHookContext,
)
from openjiuwen_protocol.identity import (
    ROUTING_METADATA_KEY,
    TENANT_ROUTING_FIELDS,
    USER_ID_FIELD,
    WEB_IDENTITY_FIELDS,
    WEB_ROUTING_ID_FIELDS,
    apply_routing_metadata,
    normalize_routing_identity,
    web_routing_identity,
)
from openjiuwen_protocol.logging_config import (
    LOGGING_CONFIG_SECTION,
    LOGGING_LEVEL_FIELDS,
    LOG_CONFIG_OP_DELETE,
    LOG_CONFIG_OP_FIELD,
)
from openjiuwen_protocol.mcp_oauth import MCP_REGISTRY_PROVIDER_DOMAIN, McpOAuthError
from openjiuwen_protocol.message import EventType, Message, Mode, ReqMethod
from openjiuwen_protocol.package_allowlist import (
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
from openjiuwen_protocol.provider_domains import (
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
from openjiuwen_protocol.redis_keys import (
    GATEWAY_CRON_JOBS_HASH_TEMPLATE,
    GATEWAY_CRON_LIVE_CHANNEL_TEMPLATE,
    GATEWAY_CRON_LIVE_REPLAY_TEMPLATE,
    GATEWAY_CRON_PERMISSION_HOLD_BY_SESSION_TEMPLATE,
    GATEWAY_CRON_PERMISSION_HOLD_TEMPLATE,
    GATEWAY_LEADER_LOCK_TEMPLATE,
    GATEWAY_SESSION_MAP_HASH_TEMPLATE,
    REDIS_KEY_PREFIX_SEPARATOR,
)
from openjiuwen_protocol.sdk import (
    ApplicationPluginServices,
    FrontendContribution,
    WebSocketEndpoint,
    WebSocketRouteContribution,
)
from openjiuwen_protocol.session_assets import (
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
from openjiuwen_protocol.template_slots import (
    DEFAULT_AGENT_LOAD_SLOTS,
    MODEL_SLOT_KEYS,
    SLOT_ENTITY_TABLE,
    TemplateRefSlot,
    normalize_template_ref,
)
from openjiuwen_protocol.types import ExtensionConfig, ExtensionMetadata

__version__ = "0.1.0"

__all__ = [
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
    "EXPERT_TEMPLATE_HTTP_ROUTES",
    "ExpertHttpRoute",
    "ExtensionConfig",
    "ExtensionMetadata",
    "EventType",
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
    "LOGGING_CONFIG_SECTION",
    "LOGGING_LEVEL_FIELDS",
    "LOG_CONFIG_OP_DELETE",
    "LOG_CONFIG_OP_FIELD",
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
    "canonical_kind",
    "decode_package_allowlist_header",
    "encode_package_allowlist_header",
    "ensure_packages_fit",
    "expert_http_routes",
    "normalize_packages",
    "normalize_routing_identity",
    "normalize_template_ref",
    "web_routing_identity",
]