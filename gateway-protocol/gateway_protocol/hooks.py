# Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
"""Hook 事件契约：事件名基类、Gateway/AgentServer 事件常量与钩子上下文。

来源：``jiuwenswarm/common/schema/event_base.py``（HookEventBase）、
``jiuwenswarm/extensions/hook_event.py``（事件常量）、
``jiuwenswarm/extensions/hooks_context.py``（上下文 dataclass）。

边界说明：扩展框架通用模块（hook_event/hooks_context 等）实现整体迁 gateway
仓，本模块仅收纳保留侧（AgentServer / 专属扩展）消费所需的事件常量与上下文
类定义；两仓经本包共享同一套事件名与上下文结构。
"""

from __future__ import annotations

from dataclasses import asdict, dataclass, field
from typing import Any

DEFAULT_SCOPE = "_framework"


def build_event_name(scope: str, event_name: str) -> str:
    return f"{scope}:{event_name}"


def parse_event_name(scoped_event: str) -> tuple[str, str]:
    if ":" in scoped_event:
        scope, event_name = scoped_event.split(":", 1)
        return scope, event_name
    return DEFAULT_SCOPE, scoped_event


class HookEventBase:
    """带 scope 的钩子事件名基类（与 openjiuwen 0.1.9 EventBase 行为一致）."""

    scope: str = DEFAULT_SCOPE

    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)
        for attr_name, attr_value in list(cls.__dict__.items()):
            if isinstance(attr_value, str) and ":" in attr_value:
                scope, event_name = parse_event_name(attr_value)
                if scope == DEFAULT_SCOPE and cls.scope != DEFAULT_SCOPE:
                    setattr(cls, attr_name, build_event_name(cls.scope, event_name))

    @classmethod
    def get_event(cls, event_name: str) -> str:
        return build_event_name(cls.scope, event_name)


class GatewayHookEvents(HookEventBase):
    """Gateway 和 AgentServer 交互事件.

    这些事件定义了 Gateway 与 AgentServer 之间的消息传递生命周期。
    """

    scope: str = "gateway"

    GATEWAY_STARTED = HookEventBase.get_event("gateway_started")
    GATEWAY_STOPPED = HookEventBase.get_event("gateway_stopped")
    BEFORE_CHAT_REQUEST = HookEventBase.get_event("before_chat_request")


class AgentServerHookEvents(HookEventBase):
    """AgentServer 事件

    这些事件定义了 AgentServer 的内部事件。
    """

    scope: str = "agent_server"

    AGENT_SERVER_STARTED = HookEventBase.get_event("agent_server_started")
    AGENT_SERVER_STOPPED = HookEventBase.get_event("agent_server_stopped")
    BEFORE_CHAT_REQUEST = HookEventBase.get_event("before_chat_request")
    MEMORY_BEFORE_CHAT = HookEventBase.get_event("memory_before_chat")
    MEMORY_AFTER_CHAT = HookEventBase.get_event("memory_after_chat")
    BEFORE_SYSTEM_PROMPT_BUILD = HookEventBase.get_event("before_system_prompt_build")


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

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass
class GatewayChatHookContext:
    request_id: str
    channel_id: str
    session_id: str | None
    req_method: str | None
    # 扩展可直接原地修改 params，Gateway 会将其继续传给 AgentRequest.params
    params: dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass
class AgentServerChatHookContext:
    request_id: str
    channel_id: str
    session_id: str | None
    req_method: str | None
    # 扩展可直接原地修改 params，AgentServer 后续逻辑会继续使用 request.params
    params: dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass
class SystemPromptHookContext:
    # 扩展可设置此目录，用于覆盖默认的 home_dir
    home_dir: str | None = None
    # 扩展可设置此目录，用于扩展默认的 skill_dir
    skill_dir: str | None = None

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


__all__ = [
    "AgentServerChatHookContext",
    "AgentServerHookEvents",
    "DEFAULT_SCOPE",
    "GatewayChatHookContext",
    "GatewayHookEvents",
    "HookEventBase",
    "MemoryHookContext",
    "SystemPromptHookContext",
    "build_event_name",
    "parse_event_name",
]
