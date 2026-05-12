"""
SDK LLM Strategy - 策路策略基桩

RoutingStrategy: 抽象策略基类
"""
from abc import ABC, abstractmethod
from typing import List, Optional, TYPE_CHECKING
from dataclasses import dataclass

if TYPE_CHECKING:
    from ..core.deployment import Deployment
    from ..core.context import RoutingContext
    from ..core.state import LocalRouterState

class RoutingStrategy(ABC):
    """
    路由策略抽象基类

    每个策略需实现:
    - select_deployment: 选择部署
    - on_success: 成功回调
    - on_failure: 失败回调
    """

    @abstractmethod
    async def select_deployment(
        self,
        deployments: List["Deployment"],
        context: "RoutingContext"
    ) -> Optional["Deployment"]:
        """
        选择一个部署

        Args:
            deployments: 可用部署列表
            context: 路由上下文

        Returns:
            选中的部署，或None表示无可用
        """
        pass

    @abstractmethod
    def on_success(
        self,
        deployment: "Deployment",
        latency: float,
        tokens: int
    ) -> None:
        """
        成功回调 - 策略可更新内部状态

        Args:
            deployment: 成功的部署
            latency: 请求延迟 (秒)
            tokens: completion_tokens
        """
        pass

    @abstractmethod
    def on_failure(
        self,
        deployment: "Deployment",
        error: Exception
    ) -> None:
        """
        失败回调 - 策略可更新内部状态

        Args:
            deployment: 失败的部署
            error: 异常对象
        """
        pass
