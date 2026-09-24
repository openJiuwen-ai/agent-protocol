# Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
"""AgentServerClient —— 与 AgentServer 通信的客户端契约（南北向接口）。

实现方各自持有：gateway 仓提供 ``WebSocketAgentServerClient``（WebSocket 实现），
AgentOS 部署由路由/扩展层提供自定义 client。跨仓实例经组装期注入传递，
不在 import 期引用任何实现。

信封/响应类型自 ``gateway_protocol.e2a``（E2A 契约以本包为
source of truth），签名与 jiuwenswarm ``common/client/agent_client.py`` 保持
一致；common 侧现为转发别名，两路径为同一对象。
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from typing import TYPE_CHECKING, Any

if TYPE_CHECKING:
    from typing import AsyncIterator

    from gateway_protocol.e2a.agent_models import AgentResponse, AgentResponseChunk
    from gateway_protocol.e2a.models import E2AEnvelope


class AgentServerClient(ABC):
    """AgentServer 客户端接口."""

    @abstractmethod
    async def connect(self, uri: str) -> None:
        """建立与 AgentServer 的连接."""
        ...

    @abstractmethod
    async def disconnect(self) -> None:
        """断开连接."""
        ...

    @abstractmethod
    def set_or_update_server_config(
        self,
        *,
        config: dict[str, Any],
        env: dict[str, str] | None = None,
    ) -> None:
        """缓存或更新服务端配置快照，供自定义 client 后续使用."""
        ...

    @abstractmethod
    async def send_request(
        self,
        envelope: "E2AEnvelope",
        *,
        timeout: float | None = None,
    ) -> "AgentResponse":
        """发送 E2A 信封，等待完整响应.

        Args:
            envelope: E2A 信封.
            timeout: 等待响应的上限（秒）。``None`` 时使用客户端默认值；
                调用方可传入更大的值以覆盖默认上限（例如 cron 任务的
                ``timeout_seconds``），使任务自身的超时真正生效，而非被内层
                默认值提前截断.
        """
        ...

    @abstractmethod
    def send_request_stream(
        self, envelope: "E2AEnvelope"
    ) -> "AsyncIterator[AgentResponseChunk]":
        """发送 E2A 信封，流式接收响应."""
        ...
