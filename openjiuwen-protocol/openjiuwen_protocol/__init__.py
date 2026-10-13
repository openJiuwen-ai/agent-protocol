# Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
"""openjiuwen-protocol：jiuwenswarm 与 jiuwenswarm-gateway 两仓共享的协议包。

**只含纯数据结构**（dataclass / enum / 常量字面量），不含行为实现与接口 ABC；
不 import jiuwenswarm / openjiuwen（不得产生反向依赖），零第三方运行时依赖。
接口（AgentServerClient / ThirdAgent / ExtensionRegistry / ExtensionManager /
SshAuthPort / CryptoProvider 等）、扩展基类（BaseExtension 及子类）、E2A 转换与
编解码逻辑，均由各仓实现侧持有一份副本。

模块边界：
- ``channel_event``    通道契约（ChannelType 枚举）
- ``cron_models``      cron 共享数据结构（CronTargetChannel / CronTarget / CronJob / CronRunState）
- ``auth``             SSH 认证结果/条目数据类（SshKeyEntry / SshAuthResult）
- ``hooks``            AgentServer/Gateway hook 事件名常量与上下文数据类
- ``sdk``              扩展 SDK 共享数据类（ApplicationPluginServices / 贡献类）
- ``types``            ExtensionConfig / ExtensionMetadata 等通用数据类型
- ``e2a``              E2A 协议数据结构（信封/响应/线协议模型/常量；经
  ``openjiuwen_protocol.e2a`` 子包命名空间访问，不在顶层汇总导出）
"""

from __future__ import annotations

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
from openjiuwen_protocol.hooks import (
    AgentServerChatHookContext,
    AgentServerHookEvents,
    GatewayChatHookContext,
    GatewayHookEvents,
    MemoryHookContext,
    SystemPromptHookContext,
)
from openjiuwen_protocol.sdk import (
    ApplicationPluginServices,
    FrontendContribution,
    WebSocketEndpoint,
    WebSocketRouteContribution,
)
from openjiuwen_protocol.types import ExtensionConfig, ExtensionMetadata

__version__ = "0.1.0"

__all__ = [
    "AgentServerChatHookContext",
    "AgentServerHookEvents",
    "ApplicationPluginServices",
    "CRON_JOB_DEFAULT_MODE",
    "CRON_JOB_DESCRIPTION_MAX_LENGTH",
    "CRON_JOB_NAME_MAX_LENGTH",
    "ChannelType",
    "CronJob",
    "CronRunState",
    "CronTarget",
    "CronTargetChannel",
    "DEFAULT_WEB_WORK_MODE",
    "ExtensionConfig",
    "ExtensionMetadata",
    "FrontendContribution",
    "GatewayChatHookContext",
    "GatewayHookEvents",
    "MemoryHookContext",
    "SshAuthResult",
    "SshKeyEntry",
    "SystemPromptHookContext",
    "WebSocketEndpoint",
    "WebSocketRouteContribution",
    "__version__",
]