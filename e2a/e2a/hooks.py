# Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
"""Hook 事件契约：Gateway/AgentServer 事件名常量与钩子上下文数据类。

来源：``jiuwenswarm/common/schema/event_base.py``（HookEventBase 的事件名约定）、
``jiuwenswarm/extensions/hook_event.py``（事件常量）、
``jiuwenswarm/extensions/hooks_context.py``（上下文 dataclass）。

边界说明：本模块为**纯数据**——事件名为字符串常量、上下文为 dataclass。
原 ``HookEventBase``（scope 拼装 / ``__init_subclass__`` 处理）、
``build_event_name`` / ``parse_event_name`` 等行为，以及上下文的 ``to_dict``，
均属实现逻辑，**不进本包**，由各仓实现侧持有一份副本。
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

DEFAULT_SCOPE = "_framework"


class GatewayHookEvents:
    """Gateway 和 AgentServer 交互事件.

    这些事件定义了 Gateway 与 AgentServer 之间的消息传递生命周期。
    """

    scope: str = "gateway"

    GATEWAY_STARTED = "gateway:gateway_started"
    GATEWAY_STOPPED = "gateway:gateway_stopped"
    BEFORE_CHAT_REQUEST = "gateway:before_chat_request"


class AgentServerHookEvents:
    """AgentServer 事件

    这些事件定义了 AgentServer 的内部事件。
    """

    scope: str = "agent_server"

    AGENT_SERVER_STARTED = "agent_server:agent_server_started"
    AGENT_SERVER_STOPPED = "agent_server:agent_server_stopped"
    BEFORE_CHAT_REQUEST = "agent_server:before_chat_request"
    MEMORY_BEFORE_CHAT = "agent_server:memory_before_chat"
    MEMORY_AFTER_CHAT = "agent_server:memory_after_chat"
    BEFORE_SYSTEM_PROMPT_BUILD = "agent_server:before_system_prompt_build"


# ---------------------------------------------------------------------------
# 钩子上下文（原 extensions/hooks_context.py）
# ---------------------------------------------------------------------------


@dataclass
class MemoryHookContext:
    session_id: str
    request_id: str
    channel_id: str | None
    agent_name: str
    workspace_dir: str
    assistant_message: str | None = None
    # 输入扩展
    extra: dict[str, Any] = field(default_factory=dict)
    # 记忆内容（before_chat 扩展写入，宿主从本字段读取拼接结果）
    memory_blocks: list[str] = field(default_factory=list)
    # 输出扩展
    metadata: dict[str, Any] = field(default_factory=dict)


@dataclass
class GatewayChatHookContext:
    request_id: str
    channel_id: str
    session_id: str | None
    req_method: str | None
    # 扩展可直接原地修改 params，Gateway 会将其继续传给 AgentRequest.params
    params: dict[str, Any] = field(default_factory=dict)


@dataclass
class AgentServerChatHookContext:
    request_id: str
    channel_id: str
    session_id: str | None
    req_method: str | None
    # 扩展可直接原地修改 params，AgentServer 后续逻辑会继续使用 request.params
    params: dict[str, Any] = field(default_factory=dict)


@dataclass
class SystemPromptHookContext:
    # 扩展可设置此目录，用于覆盖默认的 home_dir
    home_dir: str | None = None
    # 扩展可设置此目录，用于扩展默认的 skill_dir
    skill_dir: str | None = None


__all__ = [
    "AgentServerChatHookContext",
    "AgentServerHookEvents",
    "DEFAULT_SCOPE",
    "GatewayChatHookContext",
    "GatewayHookEvents",
    "MemoryHookContext",
    "SystemPromptHookContext",
]