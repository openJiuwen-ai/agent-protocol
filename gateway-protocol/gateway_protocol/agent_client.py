# Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
"""AgentServerClient —— 与 AgentServer 通信的客户端契约（南北向接口）。

实现方各自持有：gateway 仓提供 ``WebSocketAgentServerClient``（WebSocket 实现），
AgentOS 部署由路由/扩展层提供自定义 client。跨仓实例经组装期注入传递，
不在 import 期引用任何实现。
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from typing import TYPE_CHECKING, Any

if TYPE_CHECKING:
    # 仅供类型注解使用（object 前向声明避免协议包依赖实现侧类型）。
    # E2AEnvelope / AgentResponse / AgentResponseChunk 的正式定义暂在两侧
    # common 副本中（E2A 协议已确认不进本包），此处以 Any 传导。
    from typing import AsyncIterator


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
        envelope: Any,
        *,
        timeout: float | None = None,
    ) -> Any:
        """发送 E2A 信封，等待完整响应.

        Args:
            envelope: E2A 信封（具体类型由实现侧 common 副本定义，跨仓以 Any 传导）.
            timeout: 等待响应的上限（秒）。``None`` 时使用客户端默认值；
                调用方可传入更大的值以覆盖默认上限（例如 cron 任务的
                ``timeout_seconds``），使任务自身的超时真正生效.
        """
        ...

    @abstractmethod
    def send_request_stream(self, envelope: Any) -> "AsyncIterator[Any]":
        """发送 E2A 信封，流式接收响应."""
        ...
