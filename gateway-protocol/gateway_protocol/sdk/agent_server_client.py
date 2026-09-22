# Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
"""AgentServerClient 扩展入口契约."""

from __future__ import annotations

from abc import abstractmethod

from gateway_protocol.agent_client import AgentServerClient
from gateway_protocol.sdk.base import BaseExtension


class AgentServerClientExtension(BaseExtension):
    """扩展入口：持有真正的 `AgentServerClient` 实现，通过 `get_client()` 暴露。"""

    @abstractmethod
    def get_client(self) -> AgentServerClient:
        """返回与 AgentServer 通信使用的客户端实例。"""
        ...

    async def shutdown(self) -> None:
        """扩展关闭"""
        pass


__all__ = ["AgentServerClientExtension"]
