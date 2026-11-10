# Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
"""external provider 能力域名常量（common 依赖注入点，跨进程共享）。

来源：``jiuwenswarm/common/external_providers.py`` 的域清单。域名字符串是
gateway 进程（WS 远端实现）与 AgentServer 进程（本地实现自注册）之间的契约，
两侧必须逐字一致，因此收口到本包；各仓只保留注册/取用行为。
"""

from __future__ import annotations

#: MCP 会话态存储
PROVIDER_DOMAIN_MCP_STATE: str = "mcp_state"
#: MCP 注册表
PROVIDER_DOMAIN_MCP_REGISTRY: str = "mcp_registry"
#: MCP 凭据
PROVIDER_DOMAIN_MCP_CREDENTIAL: str = "mcp_credential"
#: A2UI 集成
PROVIDER_DOMAIN_A2UI: str = "a2ui"
#: 团队绑定存储
PROVIDER_DOMAIN_TEAM_BINDING_STORE: str = "team_binding_store"
#: 团队实体存储
PROVIDER_DOMAIN_TEAM_ENTITY_STORE: str = "team_entity_store"
#: 会话元数据存储
PROVIDER_DOMAIN_SESSION_METADATA: str = "session_metadata"
#: cron store 工厂
PROVIDER_DOMAIN_CRON_STORE_FACTORY: str = "cron_store_factory"

#: 全部合法域名（顺序与 ``external_providers`` 文档清单一致）。
PROVIDER_DOMAINS: tuple[str, ...] = (
    PROVIDER_DOMAIN_MCP_STATE,
    PROVIDER_DOMAIN_MCP_REGISTRY,
    PROVIDER_DOMAIN_MCP_CREDENTIAL,
    PROVIDER_DOMAIN_A2UI,
    PROVIDER_DOMAIN_TEAM_BINDING_STORE,
    PROVIDER_DOMAIN_TEAM_ENTITY_STORE,
    PROVIDER_DOMAIN_SESSION_METADATA,
    PROVIDER_DOMAIN_CRON_STORE_FACTORY,
)

__all__ = [
    "PROVIDER_DOMAINS",
    "PROVIDER_DOMAIN_A2UI",
    "PROVIDER_DOMAIN_CRON_STORE_FACTORY",
    "PROVIDER_DOMAIN_MCP_CREDENTIAL",
    "PROVIDER_DOMAIN_MCP_REGISTRY",
    "PROVIDER_DOMAIN_MCP_STATE",
    "PROVIDER_DOMAIN_SESSION_METADATA",
    "PROVIDER_DOMAIN_TEAM_BINDING_STORE",
    "PROVIDER_DOMAIN_TEAM_ENTITY_STORE",
]
