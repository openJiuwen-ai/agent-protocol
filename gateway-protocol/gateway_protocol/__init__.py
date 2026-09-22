# Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
"""gateway-protocol：jiuwenswarm 与 jiuwenswarm-gateway 两仓共享的协议包。

只含**契约**（ABC、接口、枚举、dataclass、事件常量、纯静态函数），不含任何
运行时实现；零第三方依赖、不 import jiuwenswarm / openjiuwen（不得产生反向
依赖）。实现（WebSocketAgentServerClient、ChannelManager、ExtensionRegistry/
ExtensionManager 等）由 gateway 仓持有，实例经组装期注入。

模块边界：
- ``agent_client``     AgentServerClient ABC
- ``third_agent``      ThirdAgent ABC（含 UnsupportedThirdAgent）
- ``channel_event``    通道契约（ChannelType 等；ChannelManager 实现留 gateway 仓）
- ``cron_models``      cron 共享模型（CronTargetChannel / CronTarget / CronJob）
- ``auth``             SshAuthPort 等 auth 契约
- ``registry``         ExtensionRegistry / ExtensionManager 接口（两仓各自实现）
- ``hooks``            AgentServer 侧 hook 事件常量与上下文类
- ``sdk``              扩展 SDK 基类与贡献类（BaseExtension、各 Extension 基类）
- ``types``            ExtensionConfig / ExtensionMetadata 等通用数据类型
- ``http_bridge``      AgentServer HTTP 基址解析与上传的纯静态函数
- ``e2a``              （暂缺）E2A 协议定义暂不进包，两侧各留副本
"""

from __future__ import annotations

from gateway_protocol.agent_client import AgentServerClient
from gateway_protocol.channel_event import ChannelType
from gateway_protocol.cron_models import CronJob, CronTarget, CronTargetChannel
from gateway_protocol.hooks import (
    AgentServerChatHookContext,
    AgentServerHookEvents,
    GatewayChatHookContext,
    GatewayHookEvents,
    HookEventBase,
    MemoryHookContext,
    SystemPromptHookContext,
)
from gateway_protocol.registry import ExtensionManager, ExtensionRegistry
from gateway_protocol.sdk import BaseExtension
from gateway_protocol.third_agent import ThirdAgent, UnsupportedThirdAgent
from gateway_protocol.types import ExtensionConfig, ExtensionMetadata

__version__ = "0.1.0"

__all__ = [
    "AgentServerChatHookContext",
    "AgentServerClient",
    "AgentServerHookEvents",
    "BaseExtension",
    "ChannelType",
    "CronJob",
    "CronTarget",
    "CronTargetChannel",
    "ExtensionConfig",
    "ExtensionManager",
    "ExtensionMetadata",
    "ExtensionRegistry",
    "GatewayChatHookContext",
    "GatewayHookEvents",
    "HookEventBase",
    "MemoryHookContext",
    "SystemPromptHookContext",
    "ThirdAgent",
    "UnsupportedThirdAgent",
    "__version__",
]
