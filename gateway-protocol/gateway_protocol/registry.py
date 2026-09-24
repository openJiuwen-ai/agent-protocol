# Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
"""ExtensionRegistry / ExtensionManager 接口（契约）。

两仓各自实现注册/加载逻辑，仅共用本接口定义；实例经组装期注入跨仓传递
（jiuwenswarm 在 ``load_all_extensions`` 完成后将已初始化实例注入 gateway）。
来源：``jiuwenswarm/extensions/{registry,manager}.py`` 的公共调用面。
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from typing import Any, Callable

from gateway_protocol.types import ExtensionConfig


class ExtensionRegistry(ABC):
    """扩展注册表接口（单例语义由各仓实现提供）."""

    @classmethod
    @abstractmethod
    def get_instance(cls) -> "ExtensionRegistry":
        """返回已初始化的注册表实例（未初始化时由实现方报错）."""
        ...

    @classmethod
    @abstractmethod
    def create_instance(cls, *args: Any, **kwargs: Any) -> "ExtensionRegistry":
        """创建并持有单例实例."""
        ...

    @classmethod
    @abstractmethod
    def reset_instance(cls) -> None:
        """重置单例（测试/重启用）."""
        ...

    # ---- 注册面 ----
    @abstractmethod
    def register_agent_server_client(self, extension: Any) -> None:
        ...

    @abstractmethod
    def register_crypto_utility(self, extension: Any) -> None:
        ...

    @abstractmethod
    def register_third_agent(self, extension: Any) -> None:
        ...

    @abstractmethod
    def register_application_plugin(self, extension: Any) -> None:
        ...

    # ---- 查询面 ----
    @abstractmethod
    def get_application_plugins(self) -> tuple[Any, ...]:
        ...

    @abstractmethod
    def get_application_plugin(self, plugin_id: str) -> Any | None:
        ...

    @abstractmethod
    def bind_application_plugins(
        self,
        channel: Any,
        *,
        agent_client: Any = None,
        media_attachment_normalizer: Callable[..., None] | None = None,
    ) -> None:
        ...

    @abstractmethod
    def get_agent_server_client_extension(self) -> Any | None:
        ...

    @abstractmethod
    def get_agent_server_client(self) -> Any | None:
        ...

    @abstractmethod
    def get_crypto_utility_extension(self) -> Any | None:
        ...

    @abstractmethod
    def get_crypto_provider(self) -> Any | None:
        ...

    @abstractmethod
    def get_third_agent_extension(self) -> Any | None:
        ...

    @abstractmethod
    def get_third_agent(self) -> Any | None:
        ...

    # ---- 事件总线透传 ----
    @abstractmethod
    def register(
        self,
        event: str,
        handler: Callable,
        priority: int = 100,
        **kwargs: Any,
    ) -> None:
        ...

    @abstractmethod
    def unregister(self, event: str, handler: Callable | None = None) -> None:
        ...

    @abstractmethod
    async def trigger(
        self, event: str, context: Any | None = None, **kwargs: Any
    ) -> None:
        ...

    @property
    @abstractmethod
    def config(self) -> ExtensionConfig:
        ...


class ExtensionManager(ABC):
    """扩展加载/生命周期管理接口."""

    @abstractmethod
    async def load_all_extensions(
        self,
        *,
        include_transport_extensions: bool = True,
    ) -> None:
        """发现并加载全部扩展（Runtime 直连场景传 False 跳过 transport 扩展）."""
        ...

    @abstractmethod
    async def shutdown_all_extensions(self) -> None:
        ...

    @abstractmethod
    def list_extensions(self) -> list[dict]:
        ...


__all__ = ["ExtensionManager", "ExtensionRegistry"]
