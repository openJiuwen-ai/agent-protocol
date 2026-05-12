"""
SDK LLM Strategy - 简单随机

SimpleShuffleStrategy: 按权重随机选择
"""
import random
from typing import List, Optional, TYPE_CHECKING
from .base_strategy import RoutingStrategy

if TYPE_CHECKING:
    from ..core.deployment import Deployment
    from ..core.context import RoutingContext
    from ..core.state import LocalRouterState

class SimpleShuffleStrategy(RoutingStrategy):
    """
    箄单随机策略 - 按权重随机选择

    答法:
    1. 过滤可用部署
    2. 按权重构概率分布
    3. 随机选择
    """

    def __init__(
        self,
        weights: Optional[dict] = None,
        default_weight: float = 1.0
    ):
        self.weights = weights or {}
        self.default_weight = default_weight

    async def select_deployment(
        self,
        deployments: List["Deployment"],
        context: "RoutingContext"
    ) -> Optional["Deployment"]:
        """按权重随机选择"""
        import time
        now = time.time()
        # 过滤可用部署
        available = [d for d in deployments if d.is_available(now)]
        if not available:
            return None
        # 获取权重
        weights = [
            self.weights.get(d.id, self.default_weight)
            for d in available
        ]
        # 归一化
        total = sum(weights)
        if total == 0:
            return random.choice(available)
        prob = [w / total for w in weights]
        # 箄机选择
        r = random.random()
        cumsum = 0.0
        for i, p in enumerate(prob):
            cumsum += p
            if r <= cumsum:
                return available[i]
        return available[-1]

    def on_success(self, deployment: "Deployment", latency: float, tokens: int) -> None:
        """成功回调 - 箄机策略无状态更新"""
        pass

    def on_failure(self, deployment: "Deployment", error: Exception) -> None:
        """失败回调 - 箄机策略无状态更新"""
        pass
